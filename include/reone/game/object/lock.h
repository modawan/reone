/*
 * Copyright (c) 2026 The reone project contributors
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

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "reone/script/types.h"

namespace reone {

namespace resource {

class Gff;

} // namespace resource

namespace game {

class Creature;
class Game;
class Object;

/**
 * How TSL counts the DC to open a lock, as a door or placeable record names
 * it. KotOR always takes the lock's own DC. A kind not named here counts as
 * Standard.
 */
enum class LockDifficulty {
    /** A placeable record that names no kind; it counts as Standard. */
    None = 0,
    /** The standard DC of the game difficulty, plus the lock's modifier. */
    Standard = 1,
    /** As Standard, plus seven more than the PC level the object took. */
    ScaledToPCLevel = 3,
    /** The lock's own DC, shifted for the game difficulty and at least 0. */
    Set = 4
};

/**
 * The lock a door or placeable carries: whether it is locked and can be
 * locked, the key it names and whether only that key opens it, the DCs to
 * open and to lock it, its scripts, and who last locked and unlocked it.
 */
class Lock {
public:
    /**
     * @param difficulty how the DC to open the lock is counted until the record names it
     */
    explicit Lock(LockDifficulty difficulty) :
        _difficulty(difficulty) {
    }

    /**
     * Read the lock's fields from a door or placeable record; TSL also reads
     * how the DC to open it is counted. A placeable record that lacks a lock
     * field reads it as 0 or empty, but keeps the PC level; a TSL door record
     * reads a missing DC to open or PC level as 0; a door record keeps the
     * value of any other field it lacks. The key name is kept as the record
     * spells it.
     *
     * @param placeable whether the record is a placeable's rather than a door's
     */
    void deserialize(const resource::Gff &gff, bool tsl, bool placeable);

    /**
     * The DC to open the lock. KotOR takes the lock's own DC. TSL takes it,
     * shifted for difficulty and at least 0, for a set DC; otherwise 21,
     * shifted for difficulty, plus the lock's modifier and, scaled to the PC
     * level, the level the object took plus 7. The DC counts from 0 to 255;
     * a count past 255 starts again from 0.
     */
    int unlockDC(const Game &game) const;

    /**
     * On a running heartbeat, a TSL lock that has not yet taken the PC level
     * takes the campaign's.
     */
    void takePCLevel(Game &game);

    /** The owner was locked by the locker: the lock remembers who, and its script for that runs. */
    void onLocked(Object &owner, uint32_t lockerId);

    /** Tell the owner that \p unlocker, or no one, unlocked it. */
    void signalUnlocked(Object &owner, const std::shared_ptr<Creature> &unlocker) const;

    /** The owner was unlocked by the unlocker: the lock remembers who, and its script for that runs. */
    void onUnlocked(Object &owner, uint32_t unlockerId);

    /** Whether the lock is locked. */
    bool isLocked() const { return _locked; }
    /** Whether the object can be locked. */
    bool isLockable() const { return _lockable; }
    /** Whether only the lock's key opens it. */
    bool isKeyRequired() const { return _keyRequired; }
    /** Whether opening or locking the object with its key uses the key up. */
    bool isAutoRemoveKey() const { return _autoRemoveKey; }
    /** The tag of the key that opens the lock, as the record spells it; empty for none. */
    const std::string &keyName() const { return _keyName; }
    /** The lock's own DC to open it, as its record keeps it. */
    int openDC() const { return _openDC; }
    /** The DC to lock it. */
    int closeDC() const { return _closeDC; }
    /** How TSL counts the DC to open the lock. */
    LockDifficulty difficulty() const { return _difficulty; }
    /** What TSL adds to the standard DC to open the lock. */
    int difficultyModifier() const { return _difficultyModifier; }
    /** The PC level the object took on its first running heartbeat (TSL); 0 until then. */
    int pcLevelAtSpawn() const { return _pcLevelAtSpawn; }
    /** Who last locked the object, kept from the event that says so while the object is loaded; a save does not keep it. */
    uint32_t lastLockedBy() const { return _lastLockedBy; }
    /** Who last unlocked the object, kept from the event that says so while the object is loaded; a save does not keep it. */
    uint32_t lastUnlockedBy() const { return _lastUnlockedBy; }
    /** The sound the object's look plays when it is found locked; empty for none. */
    const std::string &lockedSound() const { return _lockedSound; }

    /** Lock or unlock the lock, without its scripts or reports. */
    void setLocked(bool locked) { _locked = locked; }
    /** The sound the object's look plays when it is found locked (placeableobjsnds.2da); none when blank. */
    void setLockedSound(std::string sound) { _lockedSound = std::move(sound); }
    /** Set the DC to open the lock and how TSL counts it. */
    void setOpenDC(int dc, LockDifficulty difficulty, int difficultyModifier) {
        _openDC = dc;
        _difficulty = difficulty;
        _difficultyModifier = difficultyModifier;
    }

private:
    // Serializable

    /** Whether the lock is locked. */
    bool _locked {false};
    /** Whether the object can be locked. */
    bool _lockable {false};
    /** Whether only the key opens the lock. */
    bool _keyRequired {false};
    /** Whether the key is used up when it opens or locks the object. */
    bool _autoRemoveKey {false};
    /** The tag of the key, as the record spells it. */
    std::string _keyName;
    /** The lock's own DC to open it. */
    int _openDC {0};
    /** The DC to lock it. */
    int _closeDC {0};
    /** How TSL counts the DC to open the lock. */
    LockDifficulty _difficulty {LockDifficulty::None};
    /** What TSL adds to the standard DC. */
    int _difficultyModifier {0};
    /** The PC level the object took on its first running heartbeat (TSL). */
    int _pcLevelAtSpawn {0};
    /** The script run when the object is locked. */
    std::string _onLock;
    /** The script run when the object is unlocked. */
    std::string _onUnlock;

    // END Serializable

    /** The sound the object's look plays when it is found locked; empty for none. */
    std::string _lockedSound;
    /** Who last locked the object; invalid until an event says so. */
    uint32_t _lastLockedBy {script::kObjectInvalid};
    /** Who last unlocked the object; invalid until an event says so. */
    uint32_t _lastUnlockedBy {script::kObjectInvalid};
};

} // namespace game

} // namespace reone
