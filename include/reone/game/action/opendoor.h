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
#include "../object/door.h"

namespace reone {

namespace game {

class Door;

class OpenDoorAction : public Action {
public:
    OpenDoorAction(Game &game,
                   ServicesView &services,
                   std::shared_ptr<Door> door) :
        Action(game, services, ActionType::OpenDoor),
        _door(std::move(door)) {
        requireRuntimeObject(_door);
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::OpenDoor;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    std::optional<SavedActionRecord> saveFacingState() const override;

    /** A saved OpenDoor command; none once the door is gone. */
    static std::shared_ptr<Action> restore(const SavedActionRecord &record, Game &game);

    const std::shared_ptr<Door> &door() const { return _door; }

private:
    std::shared_ptr<Door> _door;
    /**
     * Whether the creature, having walked up to a locked door it steps right
     * up to, waits before opening it.
     */
    bool _waitAfterApproach {false};
};

} // namespace game

} // namespace reone
