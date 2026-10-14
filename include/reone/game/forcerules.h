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

#include <algorithm>
#include <cstdint>

#include "d20/spell.h"
#include "effect.h"
#include "types.h"

namespace reone {

namespace game {

/**
 * A creature's Force resistance: the strongest increase less the penalty of
 * the decreases, at least zero. Both parts, and their difference, are signed
 * bytes, so a large penalty wraps. Removing an increase takes the last
 * decrease off the new strongest increase while the penalty stays, so that
 * decrease then counts twice.
 */
struct ForceResistanceState {
    int8_t base {0};
    int8_t penalty {0};

    /** The resistance a Force power is tested against. */
    int value() const {
        return std::max(0, static_cast<int>(static_cast<int8_t>(base - penalty)));
    }

    /** Applies an increase of \p amount, kept if it raises the resistance. */
    void applyIncrease(int amount) {
        int candidate = std::min(amount, 128);
        if (candidate > value()) {
            base = static_cast<int8_t>(candidate);
        }
    }

    /** Applies a decrease of \p amount, kept if it is at least the penalty. */
    void applyDecrease(int amount) {
        int candidate = std::min(amount, 128);
        if (penalty <= candidate) {
            penalty = static_cast<int8_t>(candidate);
        }
    }

    /** Recomputes the base from the effects left once \p removed, an increase, goes. */
    template <class Records>
    void removeIncrease(const Records &records, const EffectInstance &removed) {
        int highest = 0;
        int lastPenalty = 0;
        for (const auto &record : records) {
            if (record.id == removed.id) {
                continue;
            }
            if (record.serializedType > SavedEffectTypes::forceResistanceDecrease) {
                break;
            }
            if (record.serializedType == SavedEffectTypes::forceResistanceIncrease) {
                highest = std::max(highest, record.integerParameter(0));
            } else if (record.serializedType == SavedEffectTypes::forceResistanceDecrease) {
                lastPenalty = record.integerParameter(0);
            }
        }
        base = static_cast<int8_t>(highest - lastPenalty);
    }

    /** Recomputes the penalty from the effects left once \p removed, a decrease, goes. */
    template <class Records>
    void removeDecrease(const Records &records, const EffectInstance &removed) {
        int highest = 0;
        for (const auto &record : records) {
            if (record.serializedType > SavedEffectTypes::forceResistanceDecrease) {
                break;
            }
            if (record.applicationOrder == removed.applicationOrder) {
                continue;
            }
            if (record.serializedType == SavedEffectTypes::forceResistanceIncrease) {
                highest = std::max(highest, record.integerParameter(0));
            }
        }
        penalty = static_cast<int8_t>(highest);
    }
};

/**
 * Tests a power of user type \p userType against a creature's Force
 * resistance: 1 when the creature resists, 0 when it does not or the power is
 * a special ability, -1 for any other power that is not a Force power.
 */
template <class Roll>
int resolveForceResistance(int userType, int resistance, int casterLevel, Roll &&roll) {
    if (userType == kSpellUserTypeSpecialAbility) {
        return 0;
    }
    if (userType != kSpellUserTypeForcePower) {
        return -1;
    }
    if (resistance <= 0) {
        return 0;
    }
    return resistance > casterLevel + roll() ? 1 : 0;
}

/** Whether a creature of class \p type uses the Force. */
inline bool isForceUsingClass(ClassType type, bool tsl) {
    return (type >= ClassType::JediGuardian && type <= ClassType::JediSentinel) ||
           (tsl && type >= ClassType::JediWeaponMaster && type <= ClassType::SithAssassin);
}

/**
 * The saved vitality or Force points of a creature other than the player
 * character, taken back from its base and ability modifier: the saved value
 * plus the ability term or, when the ability term cannot raise the pool above
 * one point per level, the level less the saved damage.
 */
inline int rebuildSavedPool(int saved, int base, int modifier, int level) {
    if (base + modifier * level > level) {
        return saved + modifier * level;
    }
    return saved < base ? level - (base - saved) : level;
}

/**
 * The Force point maximum of a Jedi: its base and \p bonuses, plus the
 * ability modifiers for each level, and at least one point per level.
 */
inline int forcePointMaximum(int base, int level, int wisdomModifier, int charismaModifier, bool tsl, int bonuses) {
    int modifier = wisdomModifier + (tsl ? 0 : charismaModifier);
    return std::max(level, base + level * modifier + bonuses);
}

/**
 * The ordinary Force points after healing \p amount: the healing is measured
 * against the whole pool, temporary points included, up to the maximum, and
 * written to the ordinary points, leaving the temporary ones as they were. A
 * negative amount can leave the points below zero.
 */
inline int healedForcePointPool(int base, int temporary, int amount, int maximum) {
    return std::min(maximum, base + temporary + amount);
}

/**
 * The ordinary Force points after taking \p amount of damage: the damage is
 * taken from the whole pool and written to the ordinary points, leaving the
 * temporary ones as they were.
 */
inline int damagedForcePointPool(int base, int temporary, int amount) {
    return std::max(0, base + temporary - amount);
}

/** Spends \p amount from \p temporary first, and returns what is left to spend. */
inline int consumeTemporaryResource(int &temporary, int amount) {
    amount = std::max(0, amount);
    if (temporary >= amount) {
        temporary -= amount;
        return 0;
    }
    int remainder = amount - temporary;
    temporary = 0;
    return remainder;
}

/**
 * The Force point cost of a power after alignment: multiplied by the
 * alignment multiplier, which TSL lowers by 0.05 per point of Charisma
 * modifier, down to 1. TSL then adjusts it by the room's Force rating for a
 * light or dark creature, makes it at least 1, and raises it by a fifth in the
 * Force potency and mastery forms.
 */
inline int adjustedForcePointCost(int base, float alignmentMultiplier, bool tsl, int charismaModifier, int alignment, int roomRating, CombatForm form) {
    if (base <= 0) {
        return 0;
    }
    if (tsl && alignmentMultiplier > 1.0f && charismaModifier > 0) {
        alignmentMultiplier = std::max(1.0f, alignmentMultiplier - 0.05f * charismaModifier);
    }
    auto cost = static_cast<int>(static_cast<float>(base) * alignmentMultiplier);
    if (!tsl) {
        return cost;
    }
    int roomAdjustment = roomRating / 20;
    if (alignment >= 60) {
        cost -= roomAdjustment;
    } else if (alignment < 50) {
        cost += roomAdjustment;
    }
    cost = std::max(1, cost);
    if (form == CombatForm::ForceIIPotency || form == CombatForm::ForceIVMastery) {
        cost = static_cast<int>(cost * 1.2f);
    }
    return cost;
}

/** What a Force power costs a creature, in Force points and, under Force Body, vitality. */
struct ForcePointCharge {
    int requiredForce {0};
    int spentForce {0};
    int spentHitPoints {0};
    bool usesHitPoints {false};
};

/**
 * The charge for a power of \p cost. Under Force Body of level 0 to 2, half,
 * two fifths or three tenths of the cost is paid in vitality instead; the
 * creature needs the Force points for the rest. Any other Force Body level
 * pays nothing in vitality but still requires vitality to spare.
 */
inline ForcePointCharge forcePointCharge(int cost, int forceBodyLevel) {
    cost = std::max(0, cost);
    if (forceBodyLevel == -1) {
        return {cost, cost, 0, false};
    }
    int percentage = forceBodyLevel >= 0 && forceBodyLevel <= 2 ? 50 - 10 * forceBodyLevel : 0;
    int share = cost * percentage / 100;
    return {cost - share, share, share, true};
}

/** Whether a creature with \p force Force points and \p hp vitality can pay \p charge. */
inline bool canPayForcePointCharge(const ForcePointCharge &charge, int force, int hp) {
    return force >= charge.requiredForce && (!charge.usesHitPoints || hp > charge.spentHitPoints);
}

/** A creature's Force points and vitality, each with its temporary part. */
struct ForcePointPools {
    int force;
    int temporaryForce;
    int hitPoints;
    int temporaryHitPoints;
};

/**
 * Pays \p charge from \p pools, temporary points first, each pool stopping
 * at zero: the payment never fails. When temporary Force points cover the
 * whole Force share, the vitality share is not paid.
 */
inline void payForcePointCharge(ForcePointPools &pools, const ForcePointCharge &charge) {
    int remainder = consumeTemporaryResource(pools.temporaryForce, charge.spentForce);
    if (remainder > 0) {
        pools.force = std::max(0, pools.force - remainder);
    }
    if (remainder > 0 && charge.spentHitPoints > 0) {
        int hpRemainder = consumeTemporaryResource(pools.temporaryHitPoints, charge.spentHitPoints);
        pools.hitPoints = std::max(0, pools.hitPoints - hpRemainder);
    }
}

} // namespace game

} // namespace reone
