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

// The flags of a saved move: it runs, and it is a forced move not yet begun.
static constexpr int32_t kRunMoveFlag = 1;
static constexpr int32_t kForcedMoveFlag = 4;

MoveToObjectAction::MoveToObjectAction(
    Game &game, ServicesView &services, std::shared_ptr<Object> moveTo,
    bool run, float range, bool force, float timeout, bool pointPath, std::optional<float> checkRange,
    bool closeToUseRange) :
    Action(game, services, ActionType::MoveToObject),
    _moveTo(std::move(moveTo)),
    _run(run),
    _range(range),
    _force(force),
    _pointPath(pointPath),
    _timeout(timeout),
    _checkRange(checkRange),
    _closeToUseRange(closeToUseRange) {
    requireRuntimeObject(_moveTo);
    if (_moveTo) {
        _forcedState.destination = _moveTo->position();
    }
}

MoveToObjectAction::MoveToObjectAction(
    Game &game, ServicesView &services, std::shared_ptr<Object> moveTo,
    bool run, float range, float timeout, ForcedState forcedState, bool force) :
    Action(game, services, ActionType::MoveToObject),
    _moveTo(std::move(moveTo)),
    _run(run),
    _range(range),
    _force(force),
    _pointPath(true),
    _timeout(timeout),
    _forcedState(std::move(forcedState)) {
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
            _approach = Creature::UseApproach {_moveTo->id(), {_moveTo->position(), _range}};
        }
        _approach->follow(creatureActor->useRange(*_moveTo));
    }
    auto destination = [&]() {
        return toUsePoint ? _approach->use.point : _moveTo->position();
    };
    const float closeTo = closingRange();

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
            actor.setPosition(_forcedState.destination);
            // A move begun in another area does not land in this one.
            if (auto area = _game.module()->area(); area->id() == _forcedState.areaId) {
                area->landObject(actor);
            }
            complete();
            return;
        }
    }

    bool reached = creatureActor->navigateTo(dest, _run, closeTo, dt);
    if (!reached) {
        return;
    }
    if (_checkRange && _moveTo) {
        const float useRange = creatureActor->useRange(*_moveTo).range;
        if (!creatureActor->isInUseRange(*_moveTo, std::max(0.0f, *_checkRange - useRange))) {
            if (!_moveTo->spatialArea() && !isa<Creature>(*_moveTo)) {
                complete();
                return;
            }
            // Short of the check, the mover sets out afresh for the current
            // use point with the move's range.
            if (toUsePoint) {
                _approach = Creature::UseApproach {_moveTo->id(), creatureActor->useRange(*_moveTo)};
                _approach->use.range = _range;
            }
            return;
        }
    }
    complete();
}

float MoveToObjectAction::closingRange() const {
    return _closeToUseRange && _approach ? _approach->use.range : _range;
}

std::optional<SavedActionRecord> MoveToObjectAction::saveFacingState() const {
    // A move to an object within a range is saved as a MoveToObject command; a
    // forced move, or one along a point path, as a MoveToPoint command.
    if (!_moveTo || !std::isfinite(_range)) {
        return std::nullopt;
    }

    SavedActionRecord result;
    if (usesPointPath()) {
        if ((_force && (!std::isfinite(_timeout) || _timeout < 0.0f)) ||
            (!_force && !_pointPath && _timeout >= 0.0f)) {
            return std::nullopt;
        }
        auto destination = _forcedState.active ? _forcedState.destination : _moveTo->position();
        // A move not yet begun is in the current area.
        const uint32_t areaId = _forcedState.areaId != kSavedRuntimeInvalidObjectId ? _forcedState.areaId
                                                                                    : _game.module()->area()->id();
        // Split the absolute deadline into the day/time pair at the
        // serialization boundary.
        const uint64_t millisecondsPerDay = _game.millisecondsPerWorldDay();
        const uint32_t expiryDay = _force && _forcedState.active
                                       ? static_cast<uint32_t>(_forcedState.expiryMilliseconds / millisecondsPerDay)
                                       : 0;
        const uint32_t expiryTime = _force && _forcedState.active
                                        ? static_cast<uint32_t>(_forcedState.expiryMilliseconds % millisecondsPerDay)
                                        : 0;
        int32_t flags = (_run ? kRunMoveFlag : 0) | (_force && !_forcedState.active ? kForcedMoveFlag : 0);
        result.actionId = static_cast<uint32_t>(SavedActionType::MoveToPoint);
        result.declaredParameterCount = 13;
        result.parameters = {
            {kSavedFloatParameter, destination.x},
            {kSavedFloatParameter, destination.y},
            {kSavedFloatParameter, destination.z},
            {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(areaId)},
            {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(_moveTo->id())},
            {kSavedIntegerParameter, flags},
            {kSavedFloatParameter, closingRange()},
            {kSavedIntegerParameter, int32_t {0}},
            {kSavedFloatParameter, _force && !_forcedState.active ? _timeout : 0.0f},
            {kSavedFloatParameter, _forcedState.offset.x},
            {kSavedFloatParameter, _forcedState.offset.y},
            {kSavedIntegerParameter, static_cast<int32_t>(expiryDay)},
            {kSavedIntegerParameter, static_cast<int32_t>(expiryTime)},
        };
        return result;
    }

    result.actionId = static_cast<uint32_t>(SavedActionType::MoveToObject);
    result.declaredParameterCount = 5;
    result.parameters = {
        SavedActionParameter {
            kSavedObjectParameter,
            SavedObjectReference::fromRuntimeId(_moveTo->id())},
        SavedActionParameter {
            kSavedIntegerParameter,
            static_cast<int32_t>(_run ? 1 : 0)},
        SavedActionParameter {
            kSavedFloatParameter, _range},
        SavedActionParameter {
            kSavedFloatParameter, _checkRange.value_or(_range)},
        SavedActionParameter {
            kSavedIntegerParameter, int32_t {1}},
    };
    return result;
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
            actor.setPosition(destination);
            // A move begun in another area does not land in this one.
            if (auto area = _game.module()->area(); area->id() == _forcedState.areaId) {
                area->landObject(actor);
            }
            complete();
            return;
        }
    }

    bool reached = creatureActor->navigateTo(destination, _run, 1.0f, dt);
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
    const uint32_t areaId = _forcedState.areaId != kSavedRuntimeInvalidObjectId ? _forcedState.areaId
                                                                                : _game.module()->area()->id();

    // Split the absolute deadline into the day/time pair at the serialization
    // boundary. A zero absolute deadline is rejected above, so an armed forced
    // move never serializes as the unarmed (0, 0) encoding.
    const bool forcedActive = _force && _forcedState.active;
    const uint64_t millisecondsPerDay = _game.millisecondsPerWorldDay();
    const uint32_t expiryDay = forcedActive
                                   ? static_cast<uint32_t>(_forcedState.expiryMilliseconds / millisecondsPerDay)
                                   : 0;
    const uint32_t expiryTime = forcedActive
                                    ? static_cast<uint32_t>(_forcedState.expiryMilliseconds % millisecondsPerDay)
                                    : 0;

    SavedActionRecord result;
    result.actionId = static_cast<uint32_t>(SavedActionType::MoveToPoint);
    result.declaredParameterCount = 13;
    int32_t flags = (_run ? kRunMoveFlag : 0) | (_force && !_forcedState.active ? kForcedMoveFlag : 0);
    result.parameters = {
        {kSavedFloatParameter, _destination->position().x},
        {kSavedFloatParameter, _destination->position().y},
        {kSavedFloatParameter, _destination->position().z},
        {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(areaId)},
        {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(kSavedRuntimeInvalidObjectId)},
        {kSavedIntegerParameter, flags},
        {kSavedFloatParameter, 0.0f},
        {kSavedIntegerParameter, int32_t {0}},
        {kSavedFloatParameter, _force && !_forcedState.active ? _timeout : 0.0f},
        {kSavedFloatParameter, 0.0f},
        {kSavedFloatParameter, 0.0f},
        {kSavedIntegerParameter, static_cast<int32_t>(expiryDay)},
        {kSavedIntegerParameter, static_cast<int32_t>(expiryTime)},
    };
    return result;
}

void MoveToPointAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto creatureActor = _game.getObjectById<Creature>(actor.id());
    if (!creatureActor) {
        complete();
        return;
    }

    bool reached = creatureActor->navigateTo(_point, _run, 1.0f, dt);
    if (reached) {
        complete();
    }
}

// Sends the creature on its way to the point that takes it the range away from
// a threat, ahead of the action that sent it; the action runs again once the
// creature gets there.
static void moveAwayFrom(Game &game, const Action &action, Creature &creature, const glm::vec3 &threat, float range, bool run) {
    const glm::vec3 point = game.module()->area()->computeAwayPoint(creature, threat, range);
    creature.addActionBefore(action, game.newAction<MoveToLocationAction>(std::make_shared<Location>(point, 0.0f), run));
}

void MoveAwayFromObject::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto &creature = static_cast<Creature &>(actor);
    // A creature that is dead, or down at no vitality, gives up; one is done
    // when the object has left the area, when it is out of range of it, or
    // when it has set off as often as it may.
    if (creature.isDead() || creature.isTemporarilyDead() ||
        !_game.module()->area()->isObjectResident(*_fleeFrom) ||
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
    // A creature that is dead, or down at no vitality, gives up; one out of
    // range of the location is done.
    if (creature.isDead() || creature.isTemporarilyDead() ||
        creature.getSquareDistanceTo(_moveAwayFrom->position()) > _moveAwayRange * _moveAwayRange) {
        complete();
        return;
    }
    moveAwayFrom(_game, *this, creature, _moveAwayFrom->position(), _moveAwayRange, _run);
}

void FollowAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto creatureActor = _game.getObjectById<Creature>(actor.id());
    if (!creatureActor) {
        complete();
        return;
    }

    auto dest = _follow->position();
    float distance2 = creatureActor->getSquareDistanceTo(glm::vec2(dest));
    bool run = distance2 > kDistanceWalk * kDistanceWalk;

    if (creatureActor->navigateTo(dest, run, _followDistance, dt)) {
        complete();
    }
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
    SavedActionRecord result;
    result.actionId = static_cast<uint32_t>(SavedActionType::FollowLeader);
    result.declaredParameterCount = 0;
    result.parameters.clear();
    return result;
}

void FollowOwnerAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    // TODO: implement

    complete();
}

void JumpToLocationAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    const float facing = objectFacingFromScript(_location->facing());
    jumpToPositionFacing(actor, _location->position(), facing, facing, _game);
    complete();
}

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
// before it jumped.
void JumpToObjectAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    glm::vec3 destination {_toJumpTo->position()};
    float facing;
    if (_toJumpTo->type() == ObjectType::Waypoint) {
        facing = _toJumpTo->getFacing();
    } else if (auto *door = dyn_cast<Door>(_toJumpTo.get())) {
        const float doorFacing = door->getFacing();
        const glm::vec3 doorForward {-std::sin(doorFacing), std::cos(doorFacing), 0.0f};
        const float spacing = 2.0f * cast<Creature>(actor).creaturePersonalSpace();
        destination = door->nearestActionPoint(door->position() - doorForward, true) - spacing * doorForward;
        facing = facingAlong(destination - door->position());
    } else {
        facing = facingAlong(glm::vec3(1.0f, 0.0f, 0.0f));
    }
    jumpToPositionFacing(actor, destination, facing, actor.getFacing(), _game);
    complete();
}

void RandomWalkAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    // TODO: implement

    complete();
}

} // namespace game

} // namespace reone
