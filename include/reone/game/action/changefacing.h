/*
 * Copyright (c) 2026 The reone project contributors
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

/**
 * A creature turns at once to face an object or a point, measured across
 * the ground, and is done. A creature standing on the spot keeps its facing.
 */
class ChangeFacingAction : public Action {
public:
    ChangeFacingAction(Game &game, ServicesView &services, std::shared_ptr<Object> target) :
        Action(game, services, ActionType::ChangeFacing),
        _target(std::move(target)) {
        requireRuntimeObject(_target);
    }

    ChangeFacingAction(Game &game, ServicesView &services, glm::vec3 point) :
        Action(game, services, ActionType::ChangeFacing),
        _point(std::move(point)) {
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::ChangeFacing;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    std::optional<SavedActionRecord> saveFacingState() const override;

    /** A saved FaceObject or FacePoint command; none once the object faced is gone. */
    static std::shared_ptr<Action> restore(const SavedActionRecord &record, Game &game);

private:
    std::shared_ptr<Object> _target;
    glm::vec3 _point {0.0f};
};

} // namespace game

} // namespace reone
