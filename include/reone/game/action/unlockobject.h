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

#include <optional>

#include "../action.h"

#include "lockobject.h"

namespace reone {

namespace game {

class Item;

/**
 * Unlock a door or placeable (38), from Security or a script. A creature
 * walks up, turns to the object, may say it begins, and works at it. A key it
 * carries, or a lock already open, then opens the object without a check;
 * otherwise a lock that wants no key is tried with Security, plus a spike
 * used on it, against the open-lock DC. Any other object is worked at the
 * same way and fails the check. The roll is reported, and a spike is used up
 * either way.
 */
class UnlockObjectAction : public Action {
public:
    /** How the unlock was queued, which the command it saves as follows until a creature first works on it. */
    enum class Command {
        /** By Security: the object, the item used on the lock and the item property it was used through. */
        Skill,
        /** By a script's door or placeable action: the object alone. */
        ObjectAction,
        /**
         * A creature has walked to, reached or begun work at the lock: the
         * object and the item used on the lock.
         */
        Worked
    };

    UnlockObjectAction(Game &game,
                       ServicesView &services,
                       std::shared_ptr<Object> target,
                       std::shared_ptr<Item> item = nullptr,
                       int propertyIndex = 0,
                       Command command = Command::Skill) :
        Action(game, services, ActionType::OpenLock),
        _target(std::move(target)),
        _item(std::move(item)),
        _propertyIndex(propertyIndex),
        _command(command) {
        requireRuntimeObject(_target);
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::OpenLock;
    }

    /**
     * Whether \p actor takes an unlock: a creature that cannot use Security
     * does not, and the controlled creature is told so even when it cannot be
     * commanded; nor does an actor that cannot be commanded. Any other actor
     * works its own lock.
     */
    static bool admits(const Object &actor);

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    /** Clearing the work at the lock gives the pose back to the creature's state. */
    bool cancel(std::shared_ptr<Action> self, Object &actor) override;
    /**
     * Until a creature first works on the unlock, its command: the object
     * alone for a script's object action, else the object, the item used on
     * the lock and the item property it was used through. Once a creature has
     * worked on the unlock (walking, arriving or working), it saves as the
     * object and the item.
     */
    std::optional<SavedActionRecord> saveFacingState() const override;

    /**
     * A saved unlock: the object alone, queued by a script's door or
     * placeable action; the object and the item used on the lock, an unlock a
     * creature had begun; or those and the item property it was used through.
     */
    static std::shared_ptr<Action> restore(const SavedActionRecord &record, Game &game);

private:
    /** The door or placeable to unlock. */
    std::shared_ptr<Object> _target;
    /** The security spike used on the lock, if any. */
    std::shared_ptr<Item> _item;
    /** The item property the spike was used through. */
    int _propertyIndex;
    /** Whether the unlock came from Security or from a script's door or placeable action, until a creature works on it. */
    Command _command;
    /** The walk up to and the work at the lock. */
    LockWorkState _work;

    /** The bonus of the spike used on the lock; 0 without one. */
    int spikeBonus() const;
    /**
     * The DC the roll is reported against when the object has no lock: in
     * TSL, with a spike, the upgrade type of the spike's first used property,
     * or 6 when it has none; otherwise 1.
     */
    int noLockReportDC() const;
    /**
     * The work at the lock is done: the object's trap, a key, or a Security
     * check opens it or not; an object with no lock fails the check.
     */
    void resolve(Object &actor);
};

} // namespace game

} // namespace reone
