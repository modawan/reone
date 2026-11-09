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

/** How far a creature's work at a lock has gone; none of it is saved. */
struct LockWorkState {
    /** It had to walk to the lock since it last turned to it. */
    bool approached {false};
    /** It has reached the lock. */
    bool arrived {false};
    /** Its work clip is queued. */
    bool working {false};
};

/**
 * Lock a door or placeable (39). A creature walks up, turning to the object
 * only when it had to walk, and works at it; it then locks it with the key,
 * or, with no key wanted, by a Security check against the close-lock DC. Any
 * other object is worked at the same way and fails the check.
 */
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

    /**
     * Whether \p actor takes a lock: not a creature that cannot use Security,
     * which is told nothing, nor an actor that cannot be commanded.
     */
    static bool admits(const Object &actor);

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    /** Clearing the work at the lock gives the pose back to the creature's state. */
    bool cancel(std::shared_ptr<Action> self, Object &actor) override;
    std::optional<SavedActionRecord> saveFacingState() const override;

    /** A saved Lock command. */
    static std::shared_ptr<Action> restore(const SavedActionRecord &record, Game &game);

private:
    std::shared_ptr<Object> _target;
    LockWorkState _work;
};

} // namespace game

} // namespace reone
