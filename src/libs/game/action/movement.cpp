/*
 * Copyright (c) 2020-2023 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <algorithm>
#include <cmath>

#include "reone/game/action/changefacing.h"
#include "reone/game/action/follow.h"
#include "reone/game/action/followleader.h"
#include "reone/game/action/followowner.h"
#include "reone/game/action/jumptolocation.h"
#include "reone/game/action/jumptoobject.h"
#include "reone/game/action/moveawayfromlocation.h"
#include "reone/game/action/moveawayfromobject.h"
#include "reone/game/action/movetolocation.h"
#include "reone/game/action/movetoobject.h"
#include "reone/game/action/movetopoint.h"
#include "reone/game/action/randomwalk.h"
#include "reone/game/action/wait.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/object.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/game/savedruntime.h"

#include "commonactions.h"

namespace reone {

namespace game {

// A jump lands on the first safe spot within this distance of its destination.
static constexpr float kJumpSearchRadius = 20.0f;
// A forced move that runs out of time puts the mover down on its destination
// only when that spot itself is safe.
static constexpr float kForcedPointJumpRadius = 1.0f;
// A range check in front of an action aimed at a point counts the walker as
// in range up to this much beyond its range.
static constexpr float kRangeCheckSlack = 0.01f;
// A range check's eighth parameter, which it may leave out: its walk may end
// in a clear line.
static constexpr size_t kRangeCheckClearLineParameter = 7;

// Facing along a direction; a direction too short to have one faces along +x.
static float facingAlong(const glm::vec3 &direction) {
    static constexpr float kShortestDirection = 1e-9f;
    if (glm::length(direction) < kShortestDirection) {
        return facingAlong(glm::vec3(1.0f, 0.0f, 0.0f));
    }
    return -std::atan2(direction.x, direction.y);
}

// A jump onto a waypoint takes its facing. A jump to a door lands behind the
// door's closed-state use point nearest its back, twice the jumper's creature
// personal space further back, facing away from the door. A jump onto anything
// else lands on it facing along +x. The trail keeps the facing the jumper had
// before it jumped. Returns whether the jumper found a spot to land on.
static bool jumpToObject(Creature &jumper, Object &target, bool clearLine, Game &game) {
    glm::vec3 destination {target.position()};
    float facing;
    if (target.type() == ObjectType::Waypoint) {
        facing = target.getFacing();
    } else if (auto *door = dyn_cast<Door>(&target)) {
        const float doorFacing = door->getFacing();
        const glm::vec3 doorForward {-std::sin(doorFacing), std::cos(doorFacing), 0.0f};
        const float spacing = 2.0f * jumper.creaturePersonalSpace();
        destination = door->nearestActionPoint(door->position() - doorForward, true) - spacing * doorForward;
        facing = facingAlong(destination - door->position());
    } else {
        facing = facingAlong(glm::vec3(1.0f, 0.0f, 0.0f));
    }
    return jumpToPositionFacing(jumper, destination, facing, jumper.getFacing(), kJumpSearchRadius, clearLine, game);
}

MoveToObjectAction::MoveToObjectAction(
    Game &game, ServicesView &services, std::shared_ptr<Object> moveTo,
    bool run, float range, bool force, float timeout, bool pointPath, std::optional<float> checkRange,
    std::optional<ApproachFlags> approachFlags, std::optional<float> pathOffset) :
    Action(game, services, ActionType::MoveToObject),
    _moveTo(std::move(moveTo)),
    _run(run),
    _range(range),
    _force(force),
    _pointPath(pointPath),
    _timeout(timeout),
    _checkRange(checkRange),
    _approachFlags(approachFlags),
    _pathOffset(pathOffset) {
    requireRuntimeObject(_moveTo);
    if (_moveTo) {
        _forcedState.destination = _moveTo->position();
    }
}

MoveToObjectAction::MoveToObjectAction(
    Game &game, ServicesView &services, std::shared_ptr<Object> moveTo,
    bool run, float range, float timeout, ForcedState forcedState, bool force, SavedMoveCommand::Carried carried) :
    Action(game, services, ActionType::MoveToObject),
    _moveTo(std::move(moveTo)),
    _run(run),
    _range(range),
    _force(force),
    _pointPath(true),
    _timeout(timeout),
    _forcedState(std::move(forcedState)),
    _carried(carried) {
    requireRuntimeObject(_moveTo);
}

// A move order ends a push or leap carrying the creature.
void MoveToObjectAction::onQueued(Object &actor) {
    if (auto *creature = dyn_cast<Creature>(&actor)) {
        creature->endForcedMove();
    }
}

void MoveToObjectAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    // Conversations owned by doors/placeables can queue creature actions on
    // those objects. Such actors have nothing to move, so discard safely.
    auto creatureActor = _game.getObjectById<Creature>(actor.id());
    if (!creatureActor) {
        complete();
        return;
    }
    // Moving to an object releases the mover's orientation lock.
    creatureActor->setOrientationLock(script::kObjectInvalid);

    // A door or placeable is walked to at the mover's use point for it. The
    // move sets out for the object with its range, takes the use point and
    // range the first time either differs, and keeps them from then on.
    const bool toUsePoint = _moveTo &&
                            (_moveTo->type() == ObjectType::Door || _moveTo->type() == ObjectType::Placeable);
    if (toUsePoint) {
        if (!_approach) {
            _approach = Creature::UseApproach {{_moveTo->position(), _range}};
        }
        _approach->follow(creatureActor->useRange(*_moveTo, false, _pathOffset));
    }
    auto destination = [&]() {
        return toUsePoint ? _approach->use.point : _moveTo->position();
    };

    if (_force && !_forcedState.active) {
        _forcedState.active = true;
        if (_moveTo) {
            _forcedState.destination = destination();
        }
        _forcedState.areaId = _game.module()->area()->id();
        _forcedState.expiryMilliseconds = _game.worldTimeMilliseconds() +
                                          static_cast<uint64_t>(
                                              std::llround(std::max(0.0f, _timeout) * 1000.0f));
    }

    auto dest = _moveTo ? destination() : _forcedState.destination;

    if (_force && _forcedState.active) {
        bool expired =
            _game.worldTimeMilliseconds() >= _forcedState.expiryMilliseconds;
        if (expired) {
            // Out of time, the mover jumps to the object as a jump to it does.
            jumpToObject(*creatureActor, *_moveTo, true, _game);
            complete();
            return;
        }
    }

    std::optional<std::reference_wrapper<const Object>> pathTarget;
    if (_moveTo) {
        pathTarget = *_moveTo;
    }
    // A walk to an object ends where the mover stands once it is within its
    // use range of the object, or, for a range check, near enough to it; then
    // any check is taken.
    const bool reached = creatureActor->walkToward(pathTarget, dest, _run, _range, dt, pathTarget,
                                                   _approachFlags && _approachFlags->clearLineWalk != 0, _pathOffset);
    if (!reached) {
        // No path reaches the object (a locked room, off the walkmesh): give
        // up, as a walk to a point does, rather than planning again every
        // frame and holding the creature's queue for good. A forced move
        // ends on its own deadline.
        if (!_force && creatureActor->navigationFailed()) {
            complete();
        }
        return;
    }
    if (_checkRange && _moveTo) {
        const float useRange = creatureActor->useRange(*_moveTo, false, _pathOffset).range;
        if (!creatureActor->isInUseRange(*_moveTo, std::max(0.0f, *_checkRange - useRange), _pathOffset)) {
            if (!_moveTo->spatialArea() && !isa<Creature>(*_moveTo)) {
                complete();
                return;
            }
            // Short of the check, the mover sets out afresh for the current
            // use point with the move's range.
            if (toUsePoint) {
                _approach = Creature::UseApproach {creatureActor->useRange(*_moveTo)};
                _approach->use.range = _range;
            }
            return;
        }
    }
    complete();
}

// A forced walk under way keeps its absolute deadline as the day and time a
// save splits it into.
static void saveExpiry(SavedMoveCommand &command, uint64_t expiryMilliseconds, uint64_t millisecondsPerDay) {
    command.expiryDay = static_cast<uint32_t>(expiryMilliseconds / millisecondsPerDay);
    command.expiryTime = static_cast<uint32_t>(expiryMilliseconds % millisecondsPerDay);
}

// The absolute deadline of a saved forced walk.
static uint64_t restoredExpiry(const SavedMoveCommand &command, uint64_t millisecondsPerDay) {
    return static_cast<uint64_t>(command.expiryDay) * millisecondsPerDay + command.expiryTime;
}

// A move to an object in range always carries a move flag of 1.
static constexpr int32_t kMoveToObjectFlags = 1;
// A MoveToObject command has five parameters; the range check in front of an
// attack, a cast or an item use has two more.
static constexpr size_t kMoveToObjectParameterCount = 5;

std::optional<SavedActionRecord> MoveToObjectAction::saveFacingState() const {
    // A move to an object within a range is saved as a MoveToObject command; a
    // forced move, or one along a point path, as a MoveToPoint command.
    if (!_moveTo || !std::isfinite(_range)) {
        return std::nullopt;
    }

    if (usesPointPath()) {
        if ((_force && (!std::isfinite(_timeout) || _timeout < 0.0f)) ||
            (!_force && !_pointPath && _timeout >= 0.0f)) {
            return std::nullopt;
        }
        SavedMoveCommand command;
        command.destination = _forcedState.active ? _forcedState.destination : _moveTo->position();
        // A move not yet begun is in the current area.
        command.area = SavedObjectReference::fromRuntimeId(
            _forcedState.areaId != kSavedRuntimeInvalidObjectId ? _forcedState.areaId : _game.module()->area()->id());
        command.target = SavedObjectReference::fromRuntimeId(_moveTo->id());
        command.run = _run;
        command.carried = _carried;
        command.range = _range;
        command.forcedPending = _force && !_forcedState.active;
        command.timeout = command.forcedPending ? _timeout : 0.0f;
        if (_force && _forcedState.active) {
            saveExpiry(command, _forcedState.expiryMilliseconds, _game.millisecondsPerWorldDay());
        }
        SavedActionRecord result;
        command.toAction(result);
        return result;
    }

    return savedCommand(*_moveTo, _run, _range, _checkRange.value_or(_range), _approachFlags);
}

SavedActionRecord MoveToObjectAction::savedCommand(
    const Object &target,
    bool run,
    float range,
    float checkRange,
    const std::optional<ApproachFlags> &approachFlags) {

    auto result = SavedActionRecord::command(SavedActionType::MoveToObject, {
                                                                                {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(target.id())},
                                                                                {kSavedIntegerParameter, int32_t {run ? 1 : 0}},
                                                                                {kSavedFloatParameter, range},
                                                                                {kSavedFloatParameter, checkRange},
                                                                                {kSavedIntegerParameter, kMoveToObjectFlags},
                                                                            });
    if (approachFlags) {
        result.parameters.push_back({kSavedIntegerParameter, int32_t {approachFlags->objectsInLine}});
        result.parameters.push_back({kSavedIntegerParameter, int32_t {approachFlags->clearLineWalk}});
        result.declaredParameterCount = static_cast<uint16_t>(result.parameters.size());
    }
    return result;
}

std::shared_ptr<Action> MoveToObjectAction::restore(const SavedActionRecord &record, Game &game) {
    auto target = record.objectParameter(0).boundObject();
    const int32_t run = record.integerParameter(1);
    const float moveRange = record.floatParameter(2);
    const float checkRange = record.floatParameter(3);
    const int32_t moveFlags = record.integerParameter(4);
    if (!target || (run != 0 && run != 1) ||
        !std::isfinite(moveRange) || moveRange < 0.0f ||
        !std::isfinite(checkRange) || checkRange < 0.0f ||
        moveFlags != kMoveToObjectFlags) {
        return nullptr;
    }
    std::optional<ApproachFlags> approachFlags;
    if (record.parameters.size() > kMoveToObjectParameterCount) {
        approachFlags = ApproachFlags {record.integerParameter(5), record.integerParameter(6)};
    }
    return game.newAction<MoveToObjectAction>(std::move(target), run != 0, moveRange, false, -1.0f, false, checkRange, approachFlags);
}

// A move order lets go of the orientation lock and ends a push or leap
// carrying the creature.
void MoveToLocationAction::onQueued(Object &actor) {
    if (auto *creature = dyn_cast<Creature>(&actor)) {
        creature->setOrientationLock(script::kObjectInvalid);
        creature->endForcedMove();
    }
}

void MoveToLocationAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    if (!_destination) {
        complete();
        return;
    }
    auto creatureActor = _game.getObjectById<Creature>(actor.id());
    if (!creatureActor) {
        complete();
        return;
    }
    glm::vec3 destination {_destination->position()};

    // A range check is made once, as the walk sets out.
    if (_checksRange) {
        _checksRange = false;
        const float reach = _closeRange + kRangeCheckSlack;
        if (creatureActor->getSquareDistanceTo(glm::vec2(destination)) <= reach * reach) {
            complete();
            return;
        }
    }

    if (_force && !_forcedState.active) {
        _forcedState.active = true;
        _forcedState.areaId = _game.module()->area()->id();
        _forcedState.expiryMilliseconds = _game.worldTimeMilliseconds() +
                                          static_cast<uint64_t>(
                                              std::llround(std::max(0.0f, _timeout) * 1000.0f));
    }

    if (_force && _forcedState.active) {
        bool expired =
            _game.worldTimeMilliseconds() >= _forcedState.expiryMilliseconds;
        if (expired) {
            // Out of time, the mover is put down on its destination, facing
            // along +x, if that spot is safe and still in the area the move
            // began in; otherwise it stays where it is.
            if (_game.module()->area()->id() == _forcedState.areaId) {
                const float facing = facingAlong(glm::vec3(1.0f, 0.0f, 0.0f));
                jumpToPositionFacing(*creatureActor, destination, facing, facing, kForcedPointJumpRadius, true, _game);
            }
            complete();
            return;
        }
    }

    bool reached = creatureActor->navigateTo(destination, _run, _closeRange, dt, std::nullopt, _straight);
    if (reached) {
        complete();
    }
}

std::optional<SavedActionRecord> MoveToLocationAction::saveFacingState() const {
    if (!_destination || !std::isfinite(_destination->position().x) ||
        !std::isfinite(_destination->position().y) ||
        !std::isfinite(_destination->position().z) ||
        (_force && (!std::isfinite(_timeout) ||
                    (!_forcedState.active && _timeout <= 0.0f))) ||
        (_force && _forcedState.active &&
         _forcedState.expiryMilliseconds == 0)) {
        return std::nullopt;
    }

    // A move not yet begun is in the current area.
    const auto area = SavedObjectReference::fromRuntimeId(
        _forcedState.areaId != kSavedRuntimeInvalidObjectId ? _forcedState.areaId : _game.module()->area()->id());
    if (_checksRange) {
        const glm::vec3 &point = _destination->position();
        return SavedActionRecord::command(SavedActionType::PointRangeCheck, {
                                                                                {kSavedFloatParameter, point.x},
                                                                                {kSavedFloatParameter, point.y},
                                                                                {kSavedFloatParameter, point.z},
                                                                                {kSavedObjectParameter, area},
                                                                                {kSavedObjectParameter, SavedObjectReference()},
                                                                                {kSavedIntegerParameter, int32_t {_run ? 1 : 0}},
                                                                                {kSavedFloatParameter, _closeRange},
                                                                                {kSavedIntegerParameter, int32_t {_carried.clearLine ? 1 : 0}},
                                                                            });
    }

    SavedMoveCommand command;
    command.destination = _destination->position();
    command.area = area;
    command.target = SavedObjectReference::fromRuntimeId(_offsetFromId);
    command.offset = _offset;
    command.run = _run;
    command.straight = _straight;
    command.carried = _carried;
    command.range = _closeRange;
    command.forcedPending = _force && !_forcedState.active;
    command.timeout = command.forcedPending ? _timeout : 0.0f;
    // A zero absolute deadline is rejected above, so an armed forced move
    // never saves as the unarmed (0, 0) day and time.
    if (_force && _forcedState.active) {
        saveExpiry(command, _forcedState.expiryMilliseconds, _game.millisecondsPerWorldDay());
    }
    SavedActionRecord result;
    command.toAction(result);
    return result;
}

std::shared_ptr<Action> MoveToLocationAction::restore(const SavedActionRecord &record, Game &game) {
    const auto command = SavedMoveCommand::fromAction(record);
    if (!std::dynamic_pointer_cast<Area>(command->area.boundObject())) {
        return nullptr;
    }
    const uint64_t expiryMilliseconds = restoredExpiry(*command, game.millisecondsPerWorldDay());
    const bool forced = command->forcedPending || command->forcedActive;
    const float timeout = command->forcedPending ? command->timeout : 0.0f;
    auto target = command->target.boundObject();
    if (!command->target.isInvalid() && !target) {
        return nullptr;
    }
    // A walk to a point reckoned from an object walks to the point itself.
    if (!target || command->offset != glm::vec2(0.0f)) {
        ForcedState state;
        state.areaId = command->area.id;
        state.active = command->forcedActive;
        state.expiryMilliseconds = expiryMilliseconds;
        auto location = std::make_shared<Location>(command->destination, 0.0f);
        auto result = game.newAction<MoveToLocationAction>(std::move(location), command->run, forced, timeout, state,
                                                           command->range, command->straight);
        result->_carried = command->carried;
        if (target) {
            result->_offsetFromId = target->id();
            result->_offset = command->offset;
        }
        return result;
    }
    MoveToObjectAction::ForcedState state;
    state.destination = command->destination;
    state.areaId = command->area.id;
    state.active = command->forcedActive;
    state.expiryMilliseconds = expiryMilliseconds;
    return game.newAction<MoveToObjectAction>(std::move(target), command->run, command->range, timeout, state, forced,
                                              command->carried);
}

std::shared_ptr<Action> MoveToLocationAction::restoreRangeCheck(const SavedActionRecord &record, Game &game) {
    const glm::vec3 point {record.floatParameter(0), record.floatParameter(1), record.floatParameter(2)};
    const float range = record.floatParameter(6);
    if (!std::dynamic_pointer_cast<Area>(record.objectParameter(3).boundObject()) ||
        !record.objectParameter(4).isInvalid() ||
        !std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z) ||
        !std::isfinite(range) || range < 0.0f) {
        return nullptr;
    }
    auto result = game.newAction<MoveToLocationAction>(std::make_shared<Location>(point, 0.0f),
                                                       record.integerParameter(5) != 0, false, -1.0f, range);
    result->_checksRange = true;
    result->_carried.clearLine = record.parameters.size() > kRangeCheckClearLineParameter &&
                                 record.integerParameter(kRangeCheckClearLineParameter) != 0;
    return result;
}

void MoveToPointAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto creatureActor = _game.getObjectById<Creature>(actor.id());
    if (!creatureActor) {
        complete();
        return;
    }

    std::optional<std::reference_wrapper<const Object>> goal;
    if (_goal) {
        goal = *_goal;
    }
    if (creatureActor->walkToward(goal, _point, _run, _range, dt, goal, false)) {
        complete();
    }
}

// The walk is kept as an ordinary walk to its point in the current area,
// ending as near the point as it does.
std::optional<SavedActionRecord> MoveToPointAction::saveFacingState() const {
    SavedMoveCommand command;
    command.destination = _point;
    command.area = SavedObjectReference::fromRuntimeId(_game.module()->area()->id());
    command.run = _run;
    command.range = _range;
    SavedActionRecord result;
    command.toAction(result);
    return result;
}

// A creature moving away pauses this long before each leg, less when the
// player controls it.
static constexpr float kMoveAwayLegPause = 0.3f;
static constexpr float kControlledMoveAwayLegPause = 0.1f;

// Sends the creature on its way to the point that takes it the range away from
// a threat, ahead of the action that sent it: a short pause, then a walk in a
// straight line onto the point. The action runs again once the creature gets
// there.
static void moveAwayFrom(Game &game, const Action &action, Creature &creature, const glm::vec3 &threat, float range, bool run) {
    const glm::vec3 point = game.module()->area()->computeAwayPoint(creature, threat, range);
    auto leg = game.newAction<MoveToLocationAction>(std::make_shared<Location>(point, 0.0f), run, false, -1.0f, 0.0f, true);
    creature.addActionBefore(action, leg);
    const bool controlled = game.party().getLeader().get() == &creature;
    creature.addActionBefore(*leg, game.newAction<WaitAction>(controlled ? kControlledMoveAwayLegPause : kMoveAwayLegPause));
}

void MoveAwayFromObject::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto &creature = static_cast<Creature &>(actor);
    // Every other move away from the same object queued for the creature is
    // cleared with its group.
    creature.cancelActionGroups([this](const Action &other) {
        return &other != this && other.type() == ActionType::MoveAwayFromObject &&
               static_cast<const MoveAwayFromObject &>(other)._fleeFrom == _fleeFrom;
    });
    // It is done when the object has left the area, when it is out of range
    // of it, or when it has set off as often as it may.
    if (!_game.module()->area()->isObjectResident(*_fleeFrom) ||
        creature.getSquareDistanceTo(*_fleeFrom) > _moveAwayRange * _moveAwayRange ||
        _attemptsLeft <= 0) {
        complete();
        return;
    }
    --_attemptsLeft;
    moveAwayFrom(_game, *this, creature, _fleeFrom->position(), _moveAwayRange, _run);
}

void MoveAwayFromLocation::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto &creature = static_cast<Creature &>(actor);
    // A creature out of range of the location is done.
    if (creature.getSquareDistanceTo(_moveAwayFrom->position()) > _moveAwayRange * _moveAwayRange) {
        complete();
        return;
    }
    moveAwayFrom(_game, *this, creature, _moveAwayFrom->position(), _moveAwayRange, _run);
}

std::optional<SavedActionRecord> MoveAwayFromObject::saveFacingState() const {
    return SavedActionRecord::command(SavedActionType::MoveAwayFromObject, {
                                                                               {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(_fleeFrom->id())},
                                                                               {kSavedIntegerParameter, int32_t {_run ? 1 : 0}},
                                                                               {kSavedFloatParameter, _moveAwayRange},
                                                                               {kSavedIntegerParameter, int32_t {_attemptsLeft}},
                                                                           });
}

std::shared_ptr<Action> MoveAwayFromObject::restore(const SavedActionRecord &record, Game &game) {
    auto fleeFrom = record.objectParameter(0).boundObject();
    if (!fleeFrom) {
        return nullptr;
    }
    return game.newAction<MoveAwayFromObject>(std::move(fleeFrom), record.integerParameter(1) != 0,
                                              record.floatParameter(2), record.integerParameter(3));
}

// The record also carries a count of legs, which a move away from a location
// never uses.
static constexpr int32_t kUnusedMoveAwayAttempts = 10;

std::optional<SavedActionRecord> MoveAwayFromLocation::saveFacingState() const {
    const glm::vec3 &position = _moveAwayFrom->position();
    return SavedActionRecord::command(SavedActionType::MoveAwayFromLocation, {
                                                                                 {kSavedFloatParameter, position.x},
                                                                                 {kSavedFloatParameter, position.y},
                                                                                 {kSavedFloatParameter, position.z},
                                                                                 {kSavedIntegerParameter, int32_t {_run ? 1 : 0}},
                                                                                 {kSavedFloatParameter, _moveAwayRange},
                                                                                 {kSavedIntegerParameter, kUnusedMoveAwayAttempts},
                                                                             });
}

std::shared_ptr<Action> MoveAwayFromLocation::restore(const SavedActionRecord &record, Game &game) {
    const glm::vec3 position {record.floatParameter(0), record.floatParameter(1), record.floatParameter(2)};
    return game.newAction<MoveAwayFromLocation>(std::make_shared<Location>(position, 0.0f),
                                                record.integerParameter(3) != 0, record.floatParameter(4));
}

// A follower checks again this long after each walk, a party member sooner.
static constexpr float kFollowPause = 0.25f;
static constexpr float kPartyFollowPause = 0.025f;
// A follower has caught up once its point moves no more than this between
// rounds (squared metres).
static constexpr float kCaughtUp2 = 0.01f;

void FollowAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto creatureActor = _game.getObjectById<Creature>(actor.id());
    if (!creatureActor) {
        complete();
        return;
    }

    // A party member heads for its place by the party's follow anchor, anyone
    // else for its point beside the one it follows, at that one's height. The
    // point last headed for is kept at zero height.
    Party &party = _game.party();
    const bool member = party.isMember(*creatureActor);
    const glm::vec3 target = member ? party.followAnchorPoint(_point) : glm::vec3(_point, _follow->position().z);
    const glm::vec3 moved {target - glm::vec3(_last, 0.0f)};
    const float moved2 = glm::dot(moved, moved);
    bool caughtUp;
    if (_caughtUp) {
        // Caught up, the follower waits until its point moves the follow range.
        const float range = party.followRange();
        if (moved2 < range * range) {
            return;
        }
        caughtUp = false;
    } else {
        caughtUp = moved2 <= kCaughtUp2;
    }

    // The follower walks onto the point, or into use range of the one
    // followed, pauses, and follows again.
    const auto group = creatureActor->actionGroup(*this);
    creatureActor->addActionOnTop(_game.newAction<FollowAction>(_follow, _run, _point, caughtUp, glm::vec2(target)));
    creatureActor->addActionOnTop(_game.newAction<WaitAction>(member ? kPartyFollowPause : kFollowPause), group);
    creatureActor->addActionOnTop(_game.newAction<MoveToPointAction>(target, _run, 0.0f, _follow), group);
    complete();
}

std::optional<SavedActionRecord> FollowAction::saveFacingState() const {
    // The one followed, a run, the point heading for, whether the follower
    // caught up, and the point last headed for.
    return SavedActionRecord::command(SavedActionType::Follow, {
                                                                   {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(_follow->id())},
                                                                   {kSavedIntegerParameter, int32_t {_run ? 1 : 0}},
                                                                   {kSavedFloatParameter, _point.x},
                                                                   {kSavedFloatParameter, _point.y},
                                                                   {kSavedIntegerParameter, int32_t {_caughtUp ? 1 : 0}},
                                                                   {kSavedFloatParameter, _last.x},
                                                                   {kSavedFloatParameter, _last.y},
                                                               });
}

std::shared_ptr<Action> FollowAction::restore(const SavedActionRecord &record, Game &game) {
    auto target = record.objectParameter(0).boundObject();
    if (!target) {
        return nullptr;
    }
    return game.newAction<FollowAction>(std::move(target), record.integerParameter(1) != 0,
                                        glm::vec2(record.floatParameter(2), record.floatParameter(3)),
                                        record.integerParameter(4) != 0,
                                        glm::vec2(record.floatParameter(5), record.floatParameter(6)));
}

void FollowCheckAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    actor.addActionOnTop(_game.newAction<FollowCheckAction>(_follow, _run, _point));
    actor.addActionOnTop(_game.newAction<FollowAction>(_follow, _run, _point, false, glm::vec2(actor.position())));
    complete();
}

std::optional<SavedActionRecord> FollowCheckAction::saveFacingState() const {
    // The one followed, a run and the point heading for.
    return SavedActionRecord::command(SavedActionType::FollowCheck, {
                                                                        {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(_follow->id())},
                                                                        {kSavedIntegerParameter, int32_t {_run ? 1 : 0}},
                                                                        {kSavedFloatParameter, _point.x},
                                                                        {kSavedFloatParameter, _point.y},
                                                                    });
}

std::shared_ptr<Action> FollowCheckAction::restore(const SavedActionRecord &record, Game &game) {
    auto target = record.objectParameter(0).boundObject();
    if (!target) {
        return nullptr;
    }
    return game.newAction<FollowCheckAction>(std::move(target), record.integerParameter(1) != 0,
                                             glm::vec2(record.floatParameter(2), record.floatParameter(3)));
}

static constexpr float kGlanceDistance = 8.0f;

void FollowLeaderAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    // The party has no leader while it is empty: before it is first populated,
    // after a module transition resets it, and once the last member is removed
    // by RemovePartyMember. A FollowLeader action queued on a creature can still
    // be executed in those windows, so there is nothing to follow and the action
    // is dropped instead of dereferencing a null leader.
    auto leader = _game.party().getLeader();
    if (!leader) {
        complete();
        return;
    }

    // Only a party member other than the leader follows; for it the action
    // keeps running once it has caught up.
    auto creatureActor = _game.getObjectById<Creature>(actor.id());
    if (!creatureActor || creatureActor == leader || !_game.party().isMember(*creatureActor)) {
        complete();
        return;
    }

    glm::vec3 destination {leader->position()};
    float distance2 = creatureActor->getSquareDistanceTo(glm::vec2(destination));
    bool run = distance2 > kDistanceWalk;

    const glm::vec2 before {creatureActor->position()};
    creatureActor->navigateTo(destination, run, kDefaultFollowDistance, dt);
    glanceAtLeader(*creatureActor, leader, before != glm::vec2(creatureActor->position()));
}

// A follower on the move faces where it went and stops looking. One standing
// still looks at the leader, and turns toward a leader beside it, outside the
// head arc but not behind; its facing stays, only its model turns.
void FollowLeaderAction::glanceAtLeader(Creature &follower, const std::shared_ptr<Creature> &leader, bool moved) {
    const float facing = follower.getFacing();
    const glm::vec3 forward {-std::sin(facing), std::cos(facing), 0.0f};
    if (moved) {
        _game.party().noteFollowerFacing(follower, forward);
        follower.lookAt(nullptr, kGlanceDistance);
        return;
    }
    const glm::vec3 offset {leader->position() - follower.position()};
    if (glm::length(offset) > 0.0f) {
        const glm::vec3 toLeader {glm::normalize(offset)};
        if (std::abs(glm::dot(forward, toLeader)) < std::cos(glm::radians(follower.headTurnHorizontal()))) {
            _game.party().noteFollowerFacing(follower, toLeader);
        }
    }
    follower.lookAt(leader, kGlanceDistance);
}

std::optional<SavedActionRecord> FollowLeaderAction::saveFacingState() const {
    // The command carries no target or formation: it follows whoever leads
    // the party, in the party's formation.
    return SavedActionRecord::command(SavedActionType::FollowLeader, {});
}

std::shared_ptr<Action> FollowLeaderAction::restore(const SavedActionRecord &record, Game &game) {
    return game.newAction<FollowLeaderAction>();
}

// A puppet keeps near its owner. Within the range, measured across the ground,
// it waits; farther away it runs to the owner, behind whatever else it was
// told to do, and then follows again. The order never ends by itself; a
// creature that is no longer a puppet drops it.
void FollowOwnerAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto &puppet = cast<Creature>(actor);
    // Only a puppet follows its owner, and it gives up when it is not free to
    // move, even where the order can no longer be cleared.
    if (!puppet.isPuppet() || (puppet.effectAIStateMask() & Creature::kAIStateMove) == 0) {
        complete();
        return;
    }
    auto owner = _game.party().puppetOwner(puppet);
    if (!owner || puppet.getSquareDistanceTo(glm::vec2(owner->position())) < _range * _range) {
        return;
    }
    puppet.addAction(_game.newAction<MoveToObjectAction>(owner, true, _range, false, -1.0f, true));
    puppet.addAction(_game.newAction<FollowOwnerAction>(_range));
    complete();
}

std::optional<SavedActionRecord> FollowOwnerAction::saveFacingState() const {
    return SavedActionRecord::command(SavedActionType::FollowOwner, {{kSavedFloatParameter, _range}});
}

std::shared_ptr<Action> FollowOwnerAction::restore(const SavedActionRecord &record, Game &game) {
    return game.newAction<FollowOwnerAction>(record.floatParameter(0));
}

// A jump to a location always lands where the jumper could walk straight to
// it from. Having jumped, the jumper holds a moment before its next action.
void JumpToLocationAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    const float facing = objectFacingFromScript(_location->facing());
    if (jumpToPositionFacing(cast<Creature>(actor), _location->position(), facing, facing, kJumpSearchRadius, true, _game)) {
        actor.addActionBefore(*this, _game.newAction<WaitAction>(WaitAction::kAfterJumpSeconds));
    }
    complete();
}

// A jump is kept as the point in the current area, a straight line to it, the
// search radius and the facing.
std::optional<SavedActionRecord> JumpToLocationAction::saveFacingState() const {
    const glm::vec3 &position = _location->position();
    const glm::vec3 orientation = _location->saveOrientation();
    return SavedActionRecord::command(SavedActionType::JumpToLocation, {
                                                                           {kSavedFloatParameter, position.x},
                                                                           {kSavedFloatParameter, position.y},
                                                                           {kSavedFloatParameter, position.z},
                                                                           {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(_game.module()->area()->id())},
                                                                           {kSavedIntegerParameter, int32_t {1}},
                                                                           {kSavedFloatParameter, kJumpSearchRadius},
                                                                           {kSavedFloatParameter, orientation.x},
                                                                           {kSavedFloatParameter, orientation.y},
                                                                       });
}

std::shared_ptr<Action> JumpToLocationAction::restore(const SavedActionRecord &record, Game &game) {
    if (!std::dynamic_pointer_cast<Area>(record.objectParameter(3).boundObject())) {
        return nullptr;
    }
    const glm::vec3 position {record.floatParameter(0), record.floatParameter(1), record.floatParameter(2)};
    const glm::vec3 orientation {record.floatParameter(6), record.floatParameter(7), 0.0f};
    return game.newAction<JumpToLocationAction>(std::make_shared<Location>(position, orientation));
}

// Having jumped, the jumper holds a moment before its next action.
void JumpToObjectAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    if (jumpToObject(cast<Creature>(actor), *_toJumpTo, _walkStraightLine, _game)) {
        actor.addActionBefore(*this, _game.newAction<WaitAction>(WaitAction::kAfterJumpSeconds));
    }
    complete();
}

std::optional<SavedActionRecord> JumpToObjectAction::saveFacingState() const {
    return SavedActionRecord::command(SavedActionType::JumpToObject, {
                                                                         {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(_toJumpTo->id())},
                                                                         {kSavedIntegerParameter, int32_t {_walkStraightLine ? 1 : 0}},
                                                                     });
}

std::shared_ptr<Action> JumpToObjectAction::restore(const SavedActionRecord &record, Game &game) {
    auto target = record.objectParameter(0).boundObject();
    if (!target) {
        return nullptr;
    }
    return game.newAction<JumpToObjectAction>(std::move(target), record.integerParameter(1) != 0);
}

void ChangeFacingAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto &creature = cast<Creature>(actor);
    if (_target) {
        creature.turnToward(*_target);
    } else {
        creature.turnToward(_point);
    }
    complete();
}

// A turn is kept as the object faced, or as the point faced.
std::optional<SavedActionRecord> ChangeFacingAction::saveFacingState() const {
    return _target ? savedCommand(*_target) : savedCommand(_point);
}

SavedActionRecord ChangeFacingAction::savedCommand(const Object &target) {
    return SavedActionRecord::command(SavedActionType::FaceObject, {
                                                                       {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(target.id())},
                                                                   });
}

SavedActionRecord ChangeFacingAction::savedCommand(const glm::vec3 &point) {
    return SavedActionRecord::command(SavedActionType::FacePoint, {
                                                                      {kSavedFloatParameter, point.x},
                                                                      {kSavedFloatParameter, point.y},
                                                                      {kSavedFloatParameter, point.z},
                                                                  });
}

std::shared_ptr<Action> ChangeFacingAction::restore(const SavedActionRecord &record, Game &game) {
    if (record.type() == SavedActionType::FacePoint) {
        return game.newAction<ChangeFacingAction>(
            glm::vec3 {record.floatParameter(0), record.floatParameter(1), record.floatParameter(2)});
    }
    auto target = record.objectParameter(0).boundObject();
    if (!target) {
        return nullptr;
    }
    return game.newAction<ChangeFacingAction>(std::move(target));
}

// A walker stands this long after each try at a walk, and this long, without
// walking, while its AI runs at the lowest level.
static constexpr float kRandomWalkPause = 3.0f;
static constexpr float kRandomWalkIdlePause = 15.0f;

// Each time the walk comes up it throws away whatever was queued behind it,
// then goes back behind a pause, and, when it finds a point near its home that
// the walker can walk straight to, behind a walk to that point too. It never
// ends by itself.
void RandomWalkAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    // Only a creature walks; any other object drops the walk.
    auto *walker = dyn_cast<Creature>(&actor);
    if (!walker) {
        complete();
        return;
    }
    walker->removeActionsBehind(*this);
    if (walker->aiLevel() <= 0) {
        walker->addActionBefore(*this, _game.newAction<WaitAction>(kRandomWalkIdlePause));
        return;
    }
    auto pause = _game.newAction<WaitAction>(kRandomWalkPause);
    walker->addActionBefore(*this, pause);
    if (auto point = _game.module()->area()->randomWalkPoint(*walker, _home)) {
        walker->addActionBefore(*pause, _game.newAction<MoveToLocationAction>(
                                            std::make_shared<Location>(*point, 0.0f), false, false, -1.0f, 0.0f, true));
    }
}

std::optional<SavedActionRecord> RandomWalkAction::saveFacingState() const {
    return SavedActionRecord::command(SavedActionType::RandomWalk, {
                                                                       {kSavedFloatParameter, _home.x},
                                                                       {kSavedFloatParameter, _home.y},
                                                                       {kSavedFloatParameter, _home.z},
                                                                       {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(_game.module()->area()->id())},
                                                                   });
}

std::shared_ptr<Action> RandomWalkAction::restore(const SavedActionRecord &record, Game &game) {
    if (!std::dynamic_pointer_cast<Area>(record.objectParameter(3).boundObject())) {
        return nullptr;
    }
    return game.newAction<RandomWalkAction>(
        glm::vec3 {record.floatParameter(0), record.floatParameter(1), record.floatParameter(2)});
}

} // namespace game

} // namespace reone
