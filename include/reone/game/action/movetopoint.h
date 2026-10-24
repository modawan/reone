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

class MoveToPointAction : public Action {
public:
    /**
     * @param range how near the point the walk ends; at zero it ends on the
     *        point itself
     */
    MoveToPointAction(Game &game, ServicesView &services, glm::vec3 point, bool run = true, float range = 1.0f) :
        Action(game, services, ActionType::MoveToPoint),
        _point(std::move(point)),
        _run(run),
        _range(range) {
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::MoveToPoint;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    std::optional<SavedActionRecord> saveFacingState() const override;

    const glm::vec3 &point() const { return _point; }

private:
    glm::vec3 _point;
    bool _run {true};
    float _range {1.0f};
};

} // namespace game

} // namespace reone
