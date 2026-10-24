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

/**
 * An endless wander about the spot the walker stood on when it was given the
 * walk: a short walk to a random point near that spot, then a pause, over and
 * over, until the walk is cleared.
 */
class RandomWalkAction : public Action {
public:
    RandomWalkAction(Game &game, ServicesView &services, glm::vec3 home) :
        Action(game, services, ActionType::RandomWalk),
        _home(std::move(home)) {
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::RandomWalk;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    std::optional<SavedActionRecord> saveFacingState() const override;

    /** A saved RandomWalk command; none unless its area is the current one. */
    static std::shared_ptr<Action> restore(const SavedActionRecord &record, Game &game);

    const glm::vec3 &home() const { return _home; }

private:
    glm::vec3 _home;
};

} // namespace game

} // namespace reone
