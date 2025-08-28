/*
 * Copyright (c) 2025 The reone project contributors
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

#include "reone/game/action.h"
#include "reone/game/object.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"

namespace reone {

namespace game {

bool unlockDoor(Door &door, Object &actor, float distance, float dt) {
    if (actor.type() == ObjectType::Creature) {
        auto &creature = static_cast<Creature &>(actor);
        bool reached = creature.navigateTo(door.position(), true, distance, dt);
        if (!reached) {
            return false;
        }
        creature.face(door);
        creature.playAnimation(AnimationType::LoopingUnlockDoor);
    }

    // FIXME: wait for animation to play

    if (door.isKeyRequired()) {
        // FIXME: run onFailedToOpen?
        return true;
    }

    door.setLocked(false);
    door.open();
    door.onOpen(actor);

    return true;
}

} // namespace game

} // namespace reone
