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

#include "reone/game/action/movetolocation.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/object/module.h"
#include "reone/game/savedruntime.h"

#include <cmath>

namespace reone {

namespace game {

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
    glm::vec3 destination(_destination->position());

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
        {3, SavedObjectReference {areaId}},
        {3, SavedObjectReference {kSavedRuntimeInvalidObjectId}},
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

} // namespace game

} // namespace reone
