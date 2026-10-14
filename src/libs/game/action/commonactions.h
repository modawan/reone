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

#include <glm/vec3.hpp>

namespace reone {

namespace game {

class Action;
class Door;
class Game;
class Object;
class Party;
class Placeable;

/**
 * If the door is locked and requires a named key, look for that key in the
 * actor's inventory and then the party player's, unlock the door if found, and
 * consume one key when AutoRemoveKey is set. Leaves the door locked otherwise.
 */
void tryUnlockDoorWithKey(Game &game, Door &door, Object &actor, Party &party);

/**
 * A creature works at a lock from the object's use point: it goes there,
 * turns to the object and works at it for a second and a half, a wait queued
 * before \p action. True once the work is done; \p working records that it
 * has begun. Any other actor works at a lock at once.
 */
bool workAtLock(Action &action, Object &target, Object &actor, bool &working, float dt);

/**
 * Tell a door or placeable that it failed to open for the actor; quiet keeps a
 * locked door from saying so.
 */
void signalFailToOpen(Object &target, Object &actor, bool quiet = false);

/**
 * Unlock and open a locked door: with its key the actor opens it; a lock that
 * wants its key refuses anything else.
 */
void unlockDoor(Door &door, Object &actor);

/** Unlock a locked placeable; a lock that wants its key refuses the actor. */
void unlockPlaceable(Placeable &placeable, Object &actor);

/** Lock a door or placeable that is not locked, when the actor can. */
void lockObject(Object &target, Object &actor);

/**
 * An armed trap on a door or placeable that is hostile to the actor goes off
 * instead of the actor's open, unlock or use. Returns true when it did.
 */
bool springTrapOnUse(Object &target, Object &actor);

/**
 * Set position and facing of an actor, and update area visibility. A jumping
 * leader starts the party's trail over at the destination, facing trailFacing.
 */
void jumpToPositionFacing(Object &actor, const glm::vec3 &position,
                          float facing, float trailFacing, Game &game);

} // namespace game

} // namespace reone
