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

#pragma once

#include "../action.h"

namespace reone {

namespace game {

class MoveToObjectAction : public Action {
public:
    struct ForcedState {
        glm::vec3 destination {0.0f};
        uint32_t areaId {kSavedRuntimeInvalidObjectId};
        glm::vec2 offset {0.0f};
        bool active {false};
        /**
         * Absolute deadline in world milliseconds. The retail record stores a
         * day/time pair; it is composed on restore and split again on save, so
         * the running action never rebuilds a calendar.
         */
        uint64_t expiryMilliseconds {0};
    };

    MoveToObjectAction(Game &game,
                       ServicesView &services,
                       std::shared_ptr<Object> moveTo,
                       bool run,
                       float range,
                       bool force = false,
                       float timeout = -1.0f);

    MoveToObjectAction(Game &game,
                       ServicesView &services,
                       std::shared_ptr<Object> moveTo,
                       bool run,
                       float range,
                       float timeout,
                       ForcedState forcedState);

    static bool classof(Action *from) {
        return from->type() == ActionType::MoveToObject;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;

    std::optional<SavedActionRecord> saveFacingState() const override;

    bool isRun() const { return _run; }
    const std::shared_ptr<Object> &target() const { return _moveTo; }
    float range() const { return _range; }
    bool isForced() const { return _force; }
    float timeout() const { return _timeout; }
    const ForcedState &forcedState() const { return _forcedState; }

private:
    std::shared_ptr<Object> _moveTo;
    bool _run;
    float _range;
    bool _force;
    float _timeout;
    ForcedState _forcedState;
};

} // namespace game

} // namespace reone
