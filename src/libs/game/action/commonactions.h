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

#include <memory>

#include <glm/vec3.hpp>

namespace reone {

namespace game {

class Action;
class Creature;
class Game;
class Item;
class Lock;
class Object;
struct LockWorkState;

/** How a skill use went, as it is reported. */
enum class SkillResult {
    /** A use that failed. */
    Failure,
    /** A use that succeeded. */
    Success,
    /** Mine work whose roll fell more than 10 short of the DC. */
    CriticalFailure,
    /** A take of 20 that fell short. */
    Take20Failure,
    /** Work on the creature's own mine. */
    OwnMine,
    /** A lock that wants its key, or one a take of 20 cannot reach. */
    Hopeless
};

/**
 * A creature reports a skill use to the player it is controlled by: the roll
 * in the log, unless hopeless, and over the target, for some actions and
 * results, the action's name and how it went, for five seconds. The log shows
 * the roll as 0-31, the bonus as -128-127 and the DC as 0-127, a value past
 * its range wrapping around it.
 *
 * @param actionStrRef the name of the action, or 0 for none
 * @param skillNameStrRef the name of the skill used
 */
void reportSkillUse(Game &game, Creature &actor, const std::shared_ptr<Object> &target,
                    int actionStrRef, int skillNameStrRef, int roll, int bonus, int dc, SkillResult result);

/**
 * One of an item the creature used is used up, when its item repository
 * still holds it; the last one is destroyed. Without an item nothing is used
 * up.
 */
void useUpCarriedItem(Game &game, Creature &actor, const std::shared_ptr<Item> &item);

/**
 * A key the lock of \p target names, carried by the creature (in the party's
 * items for a party member) or equipped, locks or unlocks it; an auto-remove
 * key is used up, and the player's creature is told the key was used. The key
 * name must match the key's tag exactly, except that a TSL door's key is
 * found among the carried items by the name in lower case.
 *
 * @return whether a key was used
 */
bool useCarriedKey(Creature &actor, Object &target, Lock &lock, bool wantLocked);

/**
 * A creature works at a lock from the object's use point: it goes there,
 * turns to the object (for a lock only when it had to walk; an unlock may
 * then say it begins), and plays the work clip of a door or other lock,
 * queued before \p action. On every pass a creature out of use range walks
 * back, and turns to the object again once there.
 *
 * @return true once the work is done
 */
bool workAtLock(Action &action, Object &target, Creature &creature, LockWorkState &work, bool unlock, float dt);

/**
 * Clearing the work at a lock gives a creature's pose back to its state once
 * the work has begun (\p working); a one-shot still running plays on.
 */
void resumePoseAfterLockWork(Object &actor, bool working);

/**
 * Tell a door or placeable that it failed to open for the actor; quiet keeps a
 * locked door from saying so.
 */
void signalFailToOpen(Object &target, Object &actor, bool quiet = false);

/**
 * Lock a door or placeable that is not locked, when the actor can; a
 * creature trying any other object fails and reports the roll.
 */
void lockObject(Object &target, Object &actor);

/**
 * An armed trap on a door or placeable that is hostile to the actor goes off
 * instead of the actor's open, unlock or use. Returns true when it did.
 */
bool springTrapOnUse(Object &target, Object &actor);

/**
 * Jump a creature to the first safe spot found within \p radius of a
 * position, turned to \p facing, and update area visibility; \p clearLine
 * makes the spot one the creature could walk straight to the position from. A
 * jumping leader starts the party's trail over at the spot, facing
 * \p trailFacing. With no safe spot the creature stays as it is.
 *
 * @return whether the creature found a spot and jumped to it
 */
bool jumpToPositionFacing(Creature &creature, const glm::vec3 &position, float facing, float trailFacing,
                          float radius, bool clearLine, Game &game);

} // namespace game

} // namespace reone
