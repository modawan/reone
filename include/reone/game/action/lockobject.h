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

class LockObjectAction : public Action {
public:
    LockObjectAction(Game &game, ServicesView &services, std::shared_ptr<Object> target) :
        Action(game, services, ActionType::Lock),
        _target(std::move(target)) {
        requireRuntimeObject(_target);
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::Lock;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    /** Clearing the work at the lock gives the pose back to the creature's state. */
    bool cancel(std::shared_ptr<Action> self, Object &actor) override;
    std::optional<SavedActionRecord> saveFacingState() const override;

    /** A saved Lock command. */
    static std::shared_ptr<Action> restore(const SavedActionRecord &record, Game &game);

private:
    std::shared_ptr<Object> _target;
    bool _working {false};
};

} // namespace game

} // namespace reone
