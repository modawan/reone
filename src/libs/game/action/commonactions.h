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

#pragma once

namespace reone {

namespace game {

class Door;
class Object;
class Party;
class Placeable;

// If the door is locked and requires a named key, look for that key in the
// actor's inventory and then the party player's, unlock the door if found, and
// consume one key when AutoRemoveKey is set. Leaves the door locked otherwise.
void tryUnlockDoorWithKey(Door &door, Object &actor, Party &party);

// Move an actor to a door, unlock and open it. Returns true when this action is
// complete.
bool unlockDoor(Door &door, Object &actor, float distance, float dt);

// Move an actor to a placeable and unlock it. Returns true when this action is
// complete.
bool unlockPlaceable(Placeable &placeable, Object &actor, float distance, float dt);

} // namespace game

} // namespace reone
