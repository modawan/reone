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

#include <cmath>

#include "reone/game/action/follow.h"
#include "reone/game/action/followleader.h"
#include "reone/game/action/followowner.h"
#include "reone/game/action/jumptolocation.h"
#include "reone/game/action/jumptoobject.h"
#include "reone/game/action/moveawayfromobject.h"
#include "reone/game/action/movetolocation.h"
#include "reone/game/action/movetoobject.h"
#include "reone/game/action/movetopoint.h"
#include "reone/game/action/randomwalk.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/object.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/game/savedruntime.h"

#include "commonactions.h"

namespace reone {

namespace game {

MoveToObjectAction::MoveToObjectAction(
    Game &game, ServicesView &services, std::shared_ptr<Object> moveTo,
    bool run, float range, bool force, float timeout) :
    Action(game, services, ActionType::MoveToObject),
    _moveTo(std::move(moveTo)),
    _run(run),
    _range(range),
    _force(force),
    _timeout(timeout) {
    requireRuntimeObject(_moveTo);
    if (_moveTo) {
        _forcedState.destination = _moveTo->position();
    }
}

MoveToObjectAction::MoveToObjectAction(
    Game &game, ServicesView &services, std::shared_ptr<Object> moveTo,
    bool run, float range, float timeout, ForcedState forcedState) :
    Action(game, services, ActionType::MoveToObject),
    _moveTo(std::move(moveTo)),
    _run(run),
    _range(range),
    _force(true),
    _timeout(timeout),
    _forcedState(std::move(forcedState)) {
    requireRuntimeObject(_moveTo);
}

void MoveToObjectAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    // Conversations owned by doors/placeables can queue creature actions on
    // those objects. Such actors have nothing to move, so discard safely.
    auto creatureActor = _game.getObjectById<Creature>(actor.id());
    if (!creatureActor) {
        complete();
        return;
    }

    if (_force && !_forcedState.active) {
        _forcedState.active = true;
        if (_moveTo) {
            _forcedState.destination = _moveTo->position();
        }
        if (auto module = _game.module(); module && module->area()) {
            _forcedState.areaId = module->area()->id();
        }
        _forcedState.expiryMilliseconds = _game.worldTimeMilliseconds() +
                                          static_cast<uint64_t>(
                                              std::llround(std::max(0.0f, _timeout) * 1000.0f));
    }

    auto dest = _moveTo ? _moveTo->position() : _forcedState.destination;

    if (_force && _forcedState.active) {
        bool expired =
            _game.worldTimeMilliseconds() >= _forcedState.expiryMilliseconds;
        if (expired) {
            actor.setPosition(_forcedState.destination);
            if (auto module = _game.module(); module && module->area() &&
                                              module->area()->id() == _forcedState.areaId) {
                module->area()->landObject(actor);
            }
            complete();
            return;
        }
    }

    bool reached = creatureActor->navigateTo(dest, _run, _range, dt);
    if (reached) {
        complete();
    }
}

std::optional<SavedActionRecord> MoveToObjectAction::saveFacingState() const {
    // Retail ActionId 17 is the ranged move-to-object check. Forced movement
    // carries additional path/timeout semantics which are not this record.
    if (!_moveTo || !std::isfinite(_range)) {
        return std::nullopt;
    }

    SavedActionRecord result = originalSavedAction().value_or(SavedActionRecord {});
    if (_force || _timeout >= 0.0f) {
        if (!_force || !std::isfinite(_timeout) || _timeout < 0.0f) {
            return std::nullopt;
        }
        auto destination = _forcedState.active ? _forcedState.destination : _moveTo->position();
        uint32_t areaId = _forcedState.areaId;
        if (areaId == kSavedRuntimeInvalidObjectId) {
            if (auto module = _game.module(); module && module->area()) {
                areaId = module->area()->id();
            }
        }
        if (areaId == kSavedRuntimeInvalidObjectId) {
            return std::nullopt;
        }
        // Split the absolute deadline into the retail pair at the
        // serialization boundary.
        const uint64_t millisecondsPerDay = _game.millisecondsPerWorldDay();
        const uint32_t expiryDay = _forcedState.active
                                       ? static_cast<uint32_t>(_forcedState.expiryMilliseconds / millisecondsPerDay)
                                       : 0;
        const uint32_t expiryTime = _forcedState.active
                                        ? static_cast<uint32_t>(_forcedState.expiryMilliseconds % millisecondsPerDay)
                                        : 0;
        int32_t flags = (_run ? 1 : 0) | (_forcedState.active ? 0 : 4);
        result.actionId = 1;
        result.declaredParameterCount = 13;
        result.parameters = {
            {2, destination.x},
            {2, destination.y},
            {2, destination.z},
            {3, SavedObjectReference::fromRuntimeId(areaId)},
            {3, SavedObjectReference::fromRuntimeId(_moveTo->id())},
            {1, flags},
            {2, _range},
            {1, int32_t {0}},
            {2, _forcedState.active ? 0.0f : _timeout},
            {2, _forcedState.offset.x},
            {2, _forcedState.offset.y},
            {1, static_cast<int32_t>(expiryDay)},
            {1, static_cast<int32_t>(expiryTime)},
        };
        return result;
    }

    result.actionId = 17;
    result.declaredParameterCount = 5;
    result.parameters = {
        SavedActionParameter {
            static_cast<uint32_t>(SavedActionParameterType::Object),
            SavedObjectReference::fromRuntimeId(_moveTo->id())},
        SavedActionParameter {
            static_cast<uint32_t>(SavedActionParameterType::Integer),
            static_cast<int32_t>(_run ? 1 : 0)},
        SavedActionParameter {
            static_cast<uint32_t>(SavedActionParameterType::Float), _range},
        SavedActionParameter {
            static_cast<uint32_t>(SavedActionParameterType::Float), _range},
        SavedActionParameter {
            static_cast<uint32_t>(SavedActionParameterType::Integer), int32_t {1}},
    };
    return result;
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
        if (auto module = _game.module(); module && module->area()) {
            _forcedState.areaId = module->area()->id();
        }
        _forcedState.expiryMilliseconds = _game.worldTimeMilliseconds() +
                                          static_cast<uint64_t>(
                                              std::llround(std::max(0.0f, _timeout) * 1000.0f));
    }

    if (_force && _forcedState.active) {
        bool expired =
            _game.worldTimeMilliseconds() >= _forcedState.expiryMilliseconds;
        if (expired) {
            actor.setPosition(destination);
            if (auto module = _game.module(); module && module->area() &&
                                              module->area()->id() == _forcedState.areaId) {
                module->area()->landObject(actor);
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

    uint32_t areaId = _forcedState.areaId;
    if (areaId == kSavedRuntimeInvalidObjectId) {
        if (auto module = _game.module(); module && module->area()) {
            areaId = module->area()->id();
        }
    }
    if (areaId == kSavedRuntimeInvalidObjectId) {
        return std::nullopt;
    }

    // Split the absolute deadline into the retail pair at the serialization
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

    SavedActionRecord result = originalSavedAction().value_or(SavedActionRecord {});
    result.actionId = 1;
    result.declaredParameterCount = 13;
    int32_t flags = (_run ? 1 : 0) |
                    (_force && !_forcedState.active ? 4 : 0);
    result.parameters = {
        {2, _destination->position().x},
        {2, _destination->position().y},
        {2, _destination->position().z},
        {3, SavedObjectReference::fromRuntimeId(areaId)},
        {3, SavedObjectReference::fromRuntimeId(kSavedRuntimeInvalidObjectId)},
        {1, flags},
        {2, 0.0f},
        {1, int32_t {0}},
        {2, _force && !_forcedState.active ? _timeout : 0.0f},
        {2, 0.0f},
        {2, 0.0f},
        {1, static_cast<int32_t>(expiryDay)},
        {1, static_cast<int32_t>(expiryTime)},
    };
    return result;
}

void MoveToPointAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto creatureActor = _game.getObjectById<Creature>(actor.id());
    if (!creatureActor) {
        complete();
        return;
    }

    bool reached = creatureActor->navigateTo(_point, true, 1.0f, dt);
    if (reached) {
        complete();
    }
}

void MoveAwayFromObject::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    // TODO: implement

    complete();
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

    auto creatureActor = _game.getObjectById<Creature>(actor.id());
    if (!creatureActor) {
        complete();
        return;
    }

    glm::vec3 destination {leader->position()};
    float distance2 = creatureActor->getSquareDistanceTo(glm::vec2(destination));
    bool run = distance2 > kDistanceWalk;

    if (creatureActor->navigateTo(destination, run, kDefaultFollowDistance, dt)) {
        complete();
    }
}

std::optional<SavedActionRecord> FollowLeaderAction::saveFacingState() const {
    // Retail AI action 61 is the abstract party-follow command. It deliberately
    // carries no target or formation parameters: execution resolves the current
    // party leader, while Party/FollowInfo owns formation state.
    SavedActionRecord result = originalSavedAction().value_or(SavedActionRecord {});
    result.actionId = 61;
    result.declaredParameterCount = 0;
    result.parameters.clear();
    return result;
}

void FollowOwnerAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    // TODO: implement

    complete();
}

void JumpToLocationAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    jumpToPositionFacing(actor, _location->position(), _location->facing(), _game);
    complete();
}

void JumpToObjectAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    jumpToPositionFacing(actor, _toJumpTo->position(), _toJumpTo->getFacing(), _game);
    complete();
}

void RandomWalkAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    // TODO: implement

    complete();
}

} // namespace game

} // namespace reone
