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

#include <memory>
#include <optional>
#include <string>

#include "../object.h"

namespace reone {

namespace resource {

class Gff;
class ITwoDAs;

} // namespace resource

namespace game {

class Creature;
class Game;
class Trigger;

/** A trap type (traps.2da). A blank cell, or a type outside the table, has no value. */
struct TrapTypeRow {
    /** The script that runs when the trap goes off. */
    std::string script;
    /** The model a trap trigger shows. */
    std::string model;
    /** The trap kit a recovered mine gives back. */
    std::string kitResRef;
    std::optional<int> nameStrRef;
    std::optional<int> setDC;
    std::optional<int> detectDCMod;
    std::optional<int> disarmDCMod;
};

/** The blast of a mine set on a locked object, as it goes off. */
struct MineBlast {
    /** The setter's Demolitions rank. */
    int rank {0};
    /** The blast's check: a share of the rank, the mine's bonus and a roll of 20. */
    int total {0};
    /** The lock's DC the check is against. */
    int lockDC {0};
    /** Whether the blast breaks the lock. */
    bool opens {false};
};

/**
 * The trap a door, a placeable or a trap trigger carries: its type and
 * scripts, whether it is armed, can be found and can be disarmed, its DCs and
 * who has found it. A door or placeable can also be armed with a mine set on
 * it, which in TSL stands for a mine trigger in the world and may try to blow
 * the object's lock.
 */
class Trap {
public:
    /** A trap goes off only on a creature that regards it at this standing or below. */
    static constexpr int kHostileStanding = 89;

    /** The object that carries the trap. */
    enum class Carrier {
        Door,
        Placeable,
        Trigger
    };

    explicit Trap(Carrier carrier);

    /**
     * The row of a trap type in traps.2da.
     *
     * @param type the trap type, a row of the table
     */
    static TrapTypeRow typeRow(resource::ITwoDAs &twoDas, int type);

    /** Whether the trap on an object of the faction is hostile to the creature. */
    static bool isHostileTo(const Object &trapped, Faction faction, const Creature &creature);

    /** Tell the player that a trap went off. */
    static void reportTriggered(Game &game);

    /** Read the fields every trapped record shares: type, flags, scripts and the setter's Demolitions skill. */
    void deserialize(const resource::Gff &gff);

    /**
     * Read the armed flag and the DCs a door or placeable record keeps. With
     * keepShiftedDCs, a DC the record lacks becomes the current one as
     * shifted for difficulty.
     */
    void deserializeArming(const resource::Gff &gff, const Game &game, bool keepShiftedDCs);

    /** Take the DCs of the trap's type; a blank cell keeps the current one. */
    void loadTypeDCs(resource::ITwoDAs &twoDas);

    /**
     * The DC to find the trap. In TSL the difficulty shifts it: by -5 on easy
     * and +5 on hard. KotOR reads it as stored.
     */
    int detectDC(const Game &game) const;

    /** The DC to disarm the trap, shifted for difficulty as the detect DC is. */
    int disarmDC(const Game &game) const;

    /**
     * The trap goes off on the caller, unless the caller is immune to traps
     * or, without force, is not hostile to it. An armed trap tells the player
     * when the caller is the controlled creature, runs its script and, when
     * one-shot, is spent. Either way it is lost from sight and its mine goes.
     */
    void spring(Object &owner, Faction faction, const Creature &caller, bool force);

    /** An armed trap goes off on the attacker who strikes it from close by. */
    void springOnStrike(Object &owner, Faction faction, uint32_t attackerId);

    /** An armed trap is disarmed by the caller and its script runs; either way it is lost from sight. */
    void disarm(Object &owner, const Object &caller);

    /** Run the disarm script with the caller as the one who disarmed the trap. */
    void runDisarmScript(Object &owner, const Object &caller);

    /**
     * Arm a mine set on a door or placeable. A TSL mine stands for the linked
     * mine trigger and cannot be disarmed; a KotOR one can. With blast, the
     * mine tries to blow the object's lock a few seconds later.
     */
    void armMine(
        Game &game,
        resource::ITwoDAs &twoDas,
        int trapType,
        int detectDC,
        int disarmDC,
        int ownerDemolitionsSkill,
        const std::shared_ptr<Trigger> &linkedMine,
        const std::shared_ptr<Creature> &setter,
        int blastBonus,
        bool blast);

    /**
     * Make this the trap of a mine trigger: the type's script, the given DCs
     * and the setter's Demolitions skill. The mine can be found and disarmed.
     */
    void setUpMine(resource::ITwoDAs &twoDas, int trapType, int detectDC, int disarmDC, int ownerDemolitionsSkill);

    /** A mine standing for a trapped door or placeable: neither seen nor disarmed on its own. */
    void hide();

    /**
     * Count down to a mine's blast. A blast comes due only on a trap still
     * armed; its check takes the setter's Demolitions rank divided by
     * rankDivisor, and opens the lock when it reaches lockDC.
     */
    std::optional<MineBlast> updateMineBlast(float dt, int rankDivisor, int lockDC);

    /** Report a mine's blast to the player. The mine is spent and its trigger goes. */
    void reportMineBlast(Game &game, const MineBlast &blast);

    bool isArmed() const { return _armed; }
    bool isDetectable() const { return _detectable; }
    bool isDisarmable() const { return _disarmable; }
    bool isOneShot() const { return _oneShot; }
    int type() const { return _type; }
    int ownerDemolitionsSkill() const { return _ownerDemolitionsSkill; }
    const std::string &triggeredScript() const { return _triggeredScript; }
    const std::string &disarmScript() const { return _disarmScript; }
    TrapDetection &detection() { return _detection; }
    const TrapDetection &detection() const { return _detection; }

    void setTriggeredScript(std::string script) { _triggeredScript = std::move(script); }
    void setDisarmDC(int dc) { _disarmDC = dc; }

private:
    /** How a difficulty shift settles a DC. */
    enum class DCRule {
        /** The DC is a whole number: a shifted DC at or below 0 reads as 1. */
        Whole,
        /**
         * The DC is a byte: the shift wraps it around 256, so an easy shift
         * takes a DC under 5 up near 255, and a DC that lands on 0 reads as 1.
         */
        Byte,
        /**
         * The shift is taken in full: a shifted DC at or below 0 reads as 1,
         * and one past 255 wraps, the DC being a byte.
         */
        ClampedByte
    };

    DCRule _detectRule;
    DCRule _disarmRule;

    // Serializable

    int _type {0};
    bool _armed {false};
    bool _detectable;
    bool _disarmable;
    bool _oneShot {true};
    int _detectDC {0};
    int _disarmDC {0};
    int _ownerDemolitionsSkill {0};
    std::string _triggeredScript;
    std::string _disarmScript;

    // END Serializable

    TrapDetection _detection;

    // Mine

    /** The TSL mine trigger that marks a mine set on a door or placeable. */
    RuntimeObjectRef<Trigger> _linkedMine;
    RuntimeObjectRef<Creature> _mineSetter;
    int _mineBlastBonus {0};
    /** Seconds until a set mine's blast, while one is due. */
    std::optional<float> _mineBlastDelay;

    // END Mine

    static int shiftDC(int dc, int shift, DCRule rule);

    int shiftedDC(int dc, DCRule rule, const Game &game) const;
    void removeLinkedMine(Game &game);
};

} // namespace game

} // namespace reone
