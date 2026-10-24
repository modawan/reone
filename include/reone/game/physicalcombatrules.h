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
#include <optional>

#include "reone/game/d20/spell.h"
#include "reone/game/types.h"

namespace reone {

namespace game {

/**
 * Weapon Finesse makes a weapon's attack use Dexterity. In K1 a lightsaber
 * always qualifies; otherwise, and in TSL for a lightsaber without Finesse:
 * Lightsabers, a stun baton, single sword or double-bladed sword qualifies
 * with Finesse: Melee Weapons.
 */
constexpr bool qualifiesForWeaponFinesse(
    bool tsl,
    bool lightsaber,
    WeaponWield weaponWield,
    bool hasFinesseLightsabers,
    bool hasFinesseMeleeWeapons) {

    if (!tsl) {
        if (lightsaber) {
            return true;
        }
        switch (weaponWield) {
        case WeaponWield::StunBaton:
        case WeaponWield::SingleSword:
        case WeaponWield::DoubleBladedSword:
            return hasFinesseMeleeWeapons;
        default:
            return false;
        }
    }
    if (lightsaber && hasFinesseLightsabers) {
        return true;
    }

    // TSL falls through to the ordinary wield-category test when a
    // lightsaber user does not have Finesse: Lightsabers.
    switch (weaponWield) {
    case WeaponWield::StunBaton:
    case WeaponWield::SingleSword:
    case WeaponWield::DoubleBladedSword:
        return hasFinesseMeleeWeapons;
    default:
        return false;
    }
}

/**
 * The attack bonus of a ranged attack within five units of its target: 10 in
 * K1, and in TSL one for each of Close Combat and Improved Close Combat.
 */
constexpr int getCloseProximityRangedAttackBonus(
    bool tsl,
    bool withinFiveUnits,
    bool hasCloseCombat,
    bool hasImprovedCloseCombat) {

    if (!withinFiveUnits) {
        return 0;
    }
    if (!tsl) {
        return 10;
    }
    return static_cast<int>(hasCloseCombat) +
           static_cast<int>(hasImprovedCloseCombat);
}

/**
 * The bonus of a melee attack on a target holding a ranged weapon in its
 * right hand: 10 in K1; in TSL 6, less 2 for each of the target's Close
 * Combat and Improved Close Combat.
 */
constexpr int getMeleeOnRangedAttackBonus(
    bool tsl,
    bool targetHasRangedRightHandWeapon,
    bool targetHasCloseCombat,
    bool targetHasImprovedCloseCombat) {

    if (!targetHasRangedRightHandWeapon) {
        return 0;
    }
    if (!tsl) {
        return 10;
    }
    return 6 -
           2 * (static_cast<int>(targetHasCloseCombat) +
                static_cast<int>(targetHasImprovedCloseCombat));
}

/** In TSL the Juyo form grants a lightsaber in the right hand an extra attack. */
constexpr bool grantsJuyoExtraOnHandAttack(
    bool tsl,
    bool rightHandLightsaber,
    CombatForm currentForm) {

    return tsl &&
           rightHandLightsaber &&
           currentForm == CombatForm::SaberVIIJuyo;
}

/** The die of an ordinary unarmed attack: d4 in TSL; in K1 d2 for a small creature, else d1. */
constexpr int getOrdinaryUnarmedDamageDie(
    bool tsl,
    CreatureSize creatureSize) {

    if (tsl) {
        return 4;
    }
    return creatureSize <= CreatureSize::Small ? 2 : 1;
}

/** In TSL a creature outside the party with an autobalance multiplier set is balanced. */
constexpr bool isAutoBalanceEligible(
    bool tsl,
    bool partyMember,
    int multiplierSet) {

    return tsl && !partyMember && multiplierSet != 0;
}

/**
 * The autobalance bonus to attack, defense or saves: the player's level when
 * the creature spawned, one when it is not known, times \p multiplier.
 */
constexpr int getAutoBalanceLevelBonus(
    int playerLevelAtSpawn,
    float multiplier) {

    float factor = playerLevelAtSpawn == 0 ? 1.0f : static_cast<float>(playerLevelAtSpawn);
    return static_cast<int>(factor * multiplier);
}

/**
 * The autobalance factor of physical base damage: one plus the player's level
 * when the creature spawned, less one, times \p multiplier; one when the
 * level is not known.
 */
constexpr int getAutoBalanceDamageFactor(
    int playerLevelAtSpawn,
    float multiplier) {

    float factor = playerLevelAtSpawn == 0 ? 0.0f : static_cast<float>(playerLevelAtSpawn - 1);
    return 1 + static_cast<int>(factor * multiplier);
}

/**
 * The hit-point factor of a balanced creature: one plus \p multiplier times
 * the levels the player had above the second when the creature spawned,
 * truncated. The product is single-precision; it lands exactly on whole
 * numbers at several levels, so it must not be widened to double.
 */
constexpr int getAutoBalanceVitalityFactor(
    int playerLevelAtSpawn,
    float multiplier) {

    float factor = playerLevelAtSpawn >= 2 ? static_cast<float>(playerLevelAtSpawn - 2) : 0.0f;
    return 1 + static_cast<int>(factor * multiplier);
}

/** A save's base: the classes' saving throw, conditioning and autobalance. */
constexpr int getBaseSavingThrowBonus(int classes, int conditioning, int autoBalance) {
    return classes + conditioning + autoBalance;
}

/**
 * A save before effects: its base, the ability modifier and the creature's
 * own bonus. Effects, the room and Survival are added on top of it.
 */
constexpr int getDerivedSavingThrowBonus(int base, int ability, int utcBonus) {
    return base + ability + utcBonus;
}

/**
 * The TSL Survival save bonus of a wounded creature: 1 below half its
 * vitality, rising by one at each tenth less, to 5 below a tenth. A creature
 * without maximum vitality gains nothing.
 */
inline int getSurvivalSavingThrowBonus(bool tsl, bool survival, int vitality, int maximum) {
    if (!tsl || !survival) {
        return 0;
    }
    if (maximum == 0) {
        return 0;
    }
    // The fraction is compared as it is, not rounded to a whole percentage.
    float percent = static_cast<float>(vitality) / static_cast<float>(maximum) * 100.0f;
    if (percent < 0.0f || percent >= 50.0f) {
        return 0;
    }
    if (percent >= 40.0f) {
        return 1;
    }
    if (percent >= 30.0f) {
        return 2;
    }
    if (percent >= 20.0f) {
        return 3;
    }
    if (percent >= 10.0f) {
        return 4;
    }
    return 5;
}

/**
 * In TSL a light-side creature adds the room's Force rating to its saves and
 * a dark-side one subtracts it.
 */
constexpr int getRoomSavingThrowModifier(bool tsl, int goodEvil, int forceRating) {
    if (!tsl) {
        return 0;
    }
    return goodEvil >= 60 ? forceRating : (goodEvil <= 40 ? -forceRating : 0);
}

/**
 * The net save bonus of effects: increases, with TSL Survival, and decreases
 * are each capped at 20 in K1 and 60 in TSL.
 */
constexpr int getSavingThrowEffectTotal(bool tsl, int increases, int decreases, int survival) {
    const int cap = tsl ? 60 : 20;
    const int positive = increases + (tsl ? survival : 0);
    return std::min(cap, positive) - std::min(cap, decreases);
}

/** A save's value outside a roll: its base plus its effects. */
constexpr int getSavingThrowStat(int base, int effects) {
    return base + effects;
}

/**
 * A rolled save's modifier: its effects, and in TSL the room and Survival,
 * capped at 20 in K1 and 60 in TSL.
 */
constexpr int getSavingThrowModifier(bool tsl, int effects, int room, int survival) {
    int modifier = effects + (tsl ? room + survival : 0);
    int cap = tsl ? 60 : 20;
    return modifier > cap ? cap : modifier;
}

/**
 * The immunity that turns a failed save of \p type into an immune one. A
 * death save is covered by death immunity only against a Force power or a
 * special ability.
 */
inline std::optional<ImmunityType> savingThrowImmunity(SavingThrowType type, int spellUserType) {
    switch (type) {
    case SavingThrowType::MindAffecting:
        return ImmunityType::MindSpells;
    case SavingThrowType::Poison:
        return ImmunityType::Poison;
    case SavingThrowType::Disease:
        return ImmunityType::Disease;
    case SavingThrowType::Fear:
        return ImmunityType::Fear;
    case SavingThrowType::Trap:
        return ImmunityType::Trap;
    case SavingThrowType::Death:
        if (spellUserType == kSpellUserTypeForcePower ||
            spellUserType == kSpellUserTypeSpecialAbility)
            return ImmunityType::Death;
        break;
    default:
        break;
    }
    return std::nullopt;
}

/**
 * A save succeeds when its total meets the difficulty class; a negative
 * difficulty class is never met. A failed save may still be an immune one.
 */
template <typename CheckImmunity>
SavingThrowResult resolveSavingThrow(int total, int dc, CheckImmunity &&checkImmunity) {
    if (dc >= 0 && total >= dc) {
        return SavingThrowResult::Saved;
    }
    return checkImmunity() ? SavingThrowResult::Immune : SavingThrowResult::Failed;
}

/**
 * The TSL damage bonus of the combat damage feats: twice the highest rank
 * held, melee ranks counting only for a melee attack.
 */
constexpr int getCombatDamageFeatBonus(bool tsl, bool ranged, int combatRank, int meleeRank) {
    if (!tsl) {
        return 0;
    }
    int rank = !ranged && meleeRank > combatRank ? meleeRank : combatRank;
    return rank * 2;
}

/** The TSL Precise Shot damage bonus of a ranged attack: 1 at rank 1, then twice the rank less one. */
constexpr int getPreciseShotDamageBonus(bool tsl, bool ranged, int highestRank) {
    if (!tsl || !ranged || highestRank <= 0) {
        return 0;
    }
    return highestRank == 1 ? 1 : 2 * (highestRank - 1);
}

/** In TSL the Makashi form adds 3 damage to a lightsaber in the right hand. */
constexpr int getLightsaberFormDamageBonus(bool tsl, bool rightHandLightsaber, CombatForm form) {
    return tsl && rightHandLightsaber && form == CombatForm::SaberIIMakashi ? 3 : 0;
}

/** The damage rolled for the two unarmed feat families, feats 209-211 and 212-219. */
struct UnarmedFeatDamage {
    int rank209 {0};
    int rank212 {0};
    int total() const { return rank209 + rank212; }
};

/**
 * Roll the d4 dice of the unarmed feat families. They are rolled once per
 * attack, not again for each multiple of a critical hit, and the autobalance
 * damage factor does not scale them.
 */
template <class RollDie>
UnarmedFeatDamage rollUnarmedFeatDamage(int dice209, int dice212, RollDie &&rollDie) {
    UnarmedFeatDamage result;
    for (int i = 0; i < dice209; ++i) {
        result.rank209 += rollDie(4);
    }
    for (int i = 0; i < dice212; ++i) {
        result.rank212 += rollDie(4);
    }
    return result;
}

/** Dual Strike serves a TSL attacker: rank 1 only a party member, higher ranks anyone. */
constexpr bool qualifiesForDualStrike(bool tsl, bool partyMember, bool rank1, bool rank2, bool rank3) {
    return tsl && ((partyMember && rank1) || rank2 || rank3);
}

/** The Dual Strike attack bonus with a matching ally: 2 for each rank held. */
constexpr int getDualStrikeAttackBonus(bool qualifies, bool matchingAlly, bool rank1, bool rank2, bool rank3) {
    return qualifies && matchingAlly ? 2 * (static_cast<int>(rank1) +
                                            static_cast<int>(rank2) + static_cast<int>(rank3))
                                     : 0;
}

/** The damage the resistance feats take off a hit, each term reported on its own. */
struct ResistanceFeatReduction {
    int percentage {0};
    int improvedToughness {0};
    int endurance {0};

    int total() const { return percentage + improvedToughness + endurance; }
};

/**
 * The TSL Wookiee endurance reduction: all of \p remaining up to
 * \p minimum, plus \p percent of the rest.
 */
constexpr int getEnduranceDamageReduction(int remaining, int minimum, int percent) {
    if (remaining <= minimum) {
        return minimum;
    }
    return minimum + (remaining - minimum) * percent / 100;
}

/**
 * The damage the resistance feats take off a hit of \p damage. K1 takes a
 * flat 2 for Improved Toughness and for the endurance feat. TSL takes a
 * percentage by rank, then a tenth of the rest (at least 2) for Improved
 * Toughness, then the highest Wookiee endurance rank's reduction.
 */
inline ResistanceFeatReduction getResistanceFeatReduction(
    bool tsl, int damage, int percentageRank, bool improvedToughness,
    bool endurance95, bool endurance224, bool endurance225) {
    ResistanceFeatReduction result;
    if (!tsl) {
        result.improvedToughness = improvedToughness ? 2 : 0;
        result.endurance = endurance95 ? 2 : 0;
        return result;
    }
    // Resistance pools are drawn on by the incoming damage before these
    // reductions, which come off before the selected resistance.
    float percent = percentageRank >= 3 ? 0.15f : percentageRank == 2 ? 0.10f
                                              : percentageRank == 1   ? 0.05f
                                                                      : 0.0f;
    result.percentage = static_cast<int>(static_cast<float>(damage) * percent);
    int remaining = damage - result.percentage;
    if (improvedToughness) {
        int tenPercent = static_cast<int>(static_cast<float>(remaining) * 0.10f);
        result.improvedToughness = tenPercent < 2 ? 2 : tenPercent;
        remaining -= result.improvedToughness;
    }
    // Feat IDs 225, 224 and 95 select one endurance rank; they are not additive.
    if (endurance225) {
        result.endurance = getEnduranceDamageReduction(remaining, 8, 10);
    } else if (endurance224) {
        result.endurance = getEnduranceDamageReduction(remaining, 5, 5);
    } else if (endurance95) {
        result.endurance = 2;
    }
    return result;
}

/** The cap of the attack bonus of effects: 20 in K1, 60 in TSL. */
constexpr int getAttackEffectModifierCap(bool tsl) {
    return tsl ? 60 : 20;
}

/** The TSL Targeting attack bonus of a ranged weapon: its highest rank, at most 10. */
constexpr int getTargetingAttackBonus(
    bool tsl,
    bool rangedWeapon,
    int highestRank) {

    if (!tsl || !rangedWeapon || highestRank <= 0) {
        return 0;
    }
    return highestRank > 10 ? 10 : highestRank;
}

/** The TSL Superior Weapon Focus: Lightsaber attack bonus: its highest rank, at most 3. */
constexpr int getSuperiorWeaponFocusLightsaberBonus(
    bool tsl,
    bool lightsaber,
    int highestRank) {

    if (!tsl || !lightsaber || highestRank <= 0) {
        return 0;
    }
    return highestRank > 3 ? 3 : highestRank;
}

/**
 * How much TSL Superior Two-Weapon Fighting reduces the two-weapon penalty:
 * 1 on hand at rank 1 and 2 from rank 2; off hand 1 at rank 3.
 */
constexpr int getSuperiorTwoWeaponPenaltyReduction(
    bool tsl,
    bool offHand,
    int highestRank) {

    if (!tsl || highestRank <= 0) {
        return 0;
    }
    if (offHand) {
        return highestRank >= 3 ? 1 : 0;
    }
    return highestRank >= 2 ? 2 : 1;
}

/**
 * Dueling needs an empty left hand and a single sword or blaster pistol in
 * the right; in TSL an empty right hand qualifies too.
 */
constexpr bool qualifiesForDueling(
    bool tsl,
    bool rightHandEquipped,
    WeaponWield rightHandWield,
    bool leftHandEquipped) {

    if (leftHandEquipped) {
        return false;
    }
    if (!rightHandEquipped) {
        return tsl;
    }
    return rightHandWield == WeaponWield::SingleSword ||
           rightHandWield == WeaponWield::BlasterPistol;
}

/**
 * The armor's maximum Dexterity bonus; in TSL its item properties adjust a
 * limited one.
 */
constexpr int getEffectiveArmorMaxDexterityBonus(
    bool tsl,
    int baseMaximum,
    int itemAdjustment) {

    if (!tsl || baseMaximum < 0) {
        return baseMaximum;
    }
    return baseMaximum + itemAdjustment;
}

/** The TSL Total Defense bonus: 6 with a qualifying class above level 3, else 4. */
constexpr int getTotalDefenseBonus(
    bool tsl,
    bool totalDefense,
    int highestQualifyingClassLevel) {

    if (!tsl || !totalDefense) {
        return 0;
    }
    return highestQualifyingClassLevel > 3 ? 6 : 4;
}

/** The TSL attack bonus of the lightsaber form of a lightsaber in the right hand. */
constexpr int getLightsaberFormAttackBonus(
    bool tsl,
    bool rightHandLightsaber,
    CombatForm currentForm,
    bool targetWieldsLightsaber) {

    if (!tsl || !rightHandLightsaber) {
        return 0;
    }

    switch (currentForm) {
    case CombatForm::SaberIShiiCho:
    case CombatForm::SaberVINiman:
        return 1;
    case CombatForm::SaberIIMakashi:
        return targetWieldsLightsaber ? 3 : 0;
    case CombatForm::SaberVShien:
        return 2;
    default:
        return 0;
    }
}

/**
 * The TSL defense bonus of the lightsaber form of a lightsaber in the right
 * hand, which depends on whether the attacker is the defender's combat target.
 */
constexpr int getLightsaberFormDefenseBonus(
    bool tsl,
    bool rightHandLightsaber,
    CombatForm currentForm,
    bool attackerIsCombatTarget) {

    if (!tsl || !rightHandLightsaber) {
        return 0;
    }

    switch (currentForm) {
    case CombatForm::SaberIShiiCho:
        return attackerIsCombatTarget ? 0 : 3;
    case CombatForm::SaberIIMakashi:
        return 0;
    case CombatForm::SaberIIISoresu:
        return attackerIsCombatTarget ? 2 : 0;
    case CombatForm::SaberIVAtaru:
        return attackerIsCombatTarget ? 3 : -2;
    case CombatForm::SaberVShien:
        return attackerIsCombatTarget ? -5 : 0;
    case CombatForm::SaberVINiman:
        return 1;
    case CombatForm::SaberVIIJuyo:
        return attackerIsCombatTarget ? -2 : -4;
    default:
        return 0;
    }
}

/**
 * The roll that threatens a critical hit. In TSL, with a lightsaber in the
 * right hand, Soresu raises it by one and Ataru lowers it by one, applied to
 * the threshold the weapon, Keen and the feat have already set.
 */
constexpr int getCriticalThreatThreshold(
    bool tsl,
    bool rightHandLightsaber,
    CombatForm currentForm,
    int baseThreshold) {

    if (!tsl || !rightHandLightsaber) {
        return baseThreshold;
    }

    switch (currentForm) {
    case CombatForm::SaberIIISoresu:
        return baseThreshold < 20 ? baseThreshold + 1 : baseThreshold;
    case CombatForm::SaberIVAtaru:
        return baseThreshold > 1 ? baseThreshold - 1 : baseThreshold;
    default:
        return baseThreshold;
    }
}

/** In TSL the Juyo form adds 4 to the confirmation of a lightsaber critical hit. */
constexpr int getCriticalConfirmationBonus(
    bool tsl,
    bool rightHandLightsaber,
    CombatForm currentForm) {

    if (!tsl || !rightHandLightsaber) {
        return 0;
    }
    return currentForm == CombatForm::SaberVIIJuyo ? 4 : 0;
}

/** In TSL the Shien form adds one to a lightsaber's critical multiplier. */
constexpr int getCriticalMultiplierBonus(
    bool tsl,
    bool rightHandLightsaber,
    CombatForm currentForm) {

    if (!tsl || !rightHandLightsaber) {
        return 0;
    }
    return currentForm == CombatForm::SaberVShien ? 1 : 0;
}

constexpr bool isMeleePowerAttackFeat(FeatType feat) {
    switch (feat) {
    case FeatType::PowerAttack:
    case FeatType::ImprovedPowerAttack:
    case FeatType::MasterPowerAttack:
        return true;
    default:
        return false;
    }
}

constexpr bool isMeleeCriticalStrikeFeat(FeatType feat) {
    switch (feat) {
    case FeatType::CriticalStrike:
    case FeatType::ImprovedCriticalStrike:
    case FeatType::MasterCriticalStrike:
        return true;
    default:
        return false;
    }
}

/** The attack roll modifier of a melee feat. */
constexpr int getMeleeSpecialAttackRollBonus(bool tsl, FeatType feat) {
    switch (feat) {
    case FeatType::PowerAttack:
    case FeatType::ImprovedPowerAttack:
    case FeatType::MasterPowerAttack:
        return -3;
    case FeatType::Flurry:
        return -4;
    case FeatType::ImprovedFlurry:
        return -2;
    case FeatType::MasterFlurry:
        return tsl ? 0 : -1;
    // The two higher Force Jump attacks strike more surely.
    case FeatType::ForceJumpAdvanced:
        return 2;
    case FeatType::ForceJumpMastery:
        return 4;
    default:
        return 0;
    }
}

/** The damage bonus of a melee feat. */
constexpr int getMeleeSpecialAttackDamageBonus(bool tsl, FeatType feat) {
    switch (feat) {
    case FeatType::PowerAttack:
        return tsl ? 3 : 5;
    case FeatType::ImprovedPowerAttack:
        return tsl ? 7 : 8;
    case FeatType::MasterPowerAttack:
        return tsl ? 12 : 10;
    // The two higher Force Jump attacks strike harder in both games.
    case FeatType::ForceJumpAdvanced:
        return 2;
    case FeatType::ForceJumpMastery:
        return 4;
    default:
        return 0;
    }
}

constexpr bool isRangedPowerBlastFeat(FeatType feat) {
    return feat == FeatType::PowerBlast || feat == FeatType::ImprovedPowerBlast ||
           feat == FeatType::MasterPowerBlast;
}

constexpr bool isRangedSniperFeat(FeatType feat) {
    return feat == FeatType::SniperShot || feat == FeatType::ImprovedSniperShot ||
           feat == FeatType::MasterSniperShot;
}

/**
 * The attack roll modifier of a ranged feat. In TSL a weapon with the Rapid
 * Shot or Sniper Shot property improves its feat's modifier.
 */
constexpr int getRangedSpecialAttackRollBonus(
    bool tsl, FeatType feat, bool rapidShotBonus, bool sniperBonus) {
    if (isRangedPowerBlastFeat(feat)) {
        return -3;
    }
    if (isRangedSniperFeat(feat)) {
        return tsl && sniperBonus ? 4 : 0;
    }
    switch (feat) {
    case FeatType::RapidShot:
        return tsl && rapidShotBonus ? -3 : -4;
    case FeatType::ImprovedRapidShot:
        return tsl && rapidShotBonus ? -1 : -2;
    case FeatType::MultiShot:
        return tsl && rapidShotBonus ? 0 : -1;
    default:
        return 0;
    }
}

/** The damage bonus of a ranged feat. */
constexpr int getRangedSpecialAttackDamageBonus(bool tsl, FeatType feat) {
    switch (feat) {
    case FeatType::PowerBlast:
        return tsl ? 3 : 5;
    case FeatType::ImprovedPowerBlast:
        return tsl ? 7 : 8;
    case FeatType::MasterPowerBlast:
        return tsl ? 12 : 10;
    default:
        return 0;
    }
}

/** The defense the attacker loses for a few seconds by using a ranged feat. */
constexpr int getRangedSpecialAttackDefensePenalty(FeatType feat) {
    if (isRangedSniperFeat(feat)) {
        return 5;
    }
    switch (feat) {
    case FeatType::RapidShot:
        return 4;
    case FeatType::ImprovedRapidShot:
        return 2;
    case FeatType::MultiShot:
        return 1;
    default:
        return 0;
    }
}

/** The defense the attacker loses for a few seconds by using a melee feat. */
constexpr int getMeleeSpecialAttackDefensePenalty(bool tsl, FeatType feat) {
    switch (feat) {
    case FeatType::CriticalStrike:
    case FeatType::ImprovedCriticalStrike:
    case FeatType::MasterCriticalStrike:
        return 5;
    case FeatType::Flurry:
        return tsl ? 2 : 4;
    case FeatType::ImprovedFlurry:
        return tsl ? 1 : 2;
    case FeatType::MasterFlurry:
        return tsl ? 0 : 1;
    default:
        return 0;
    }
}

/** In TSL Power Attack and Power Blast add one to the critical multiplier. */
constexpr int getPowerAttackCriticalMultiplierBonus(
    bool tsl,
    FeatType feat) {

    return tsl && (isMeleePowerAttackFeat(feat) || isRangedPowerBlastFeat(feat)) ? 1 : 0;
}

/** A TSL Power Attack critical hit may knock its target down. */
constexpr bool shouldAttemptPowerAttackKnockdown(
    bool tsl,
    FeatType feat,
    AttackResultType result) {

    return tsl &&
           isMeleePowerAttackFeat(feat) &&
           result == AttackResultType::CriticalHit;
}

/** A Critical Strike hit may stun its target. */
constexpr bool shouldAttemptCriticalStrikeStun(
    FeatType feat,
    AttackResultType result) {

    return isMeleeCriticalStrikeFeat(feat) &&
           (result == AttackResultType::HitSuccessful ||
            result == AttackResultType::CriticalHit ||
            result == AttackResultType::AutomaticHit);
}

/**
 * The DC of a melee feat's save: the attacker's level plus its Strength
 * modifier for Critical Strike, and in TSL plus twice the modifier for Power
 * Attack. Any other feat has none.
 */
constexpr int getMeleeSpecialAttackSaveDC(
    bool tsl,
    FeatType feat,
    int attackerLevel,
    int strengthModifier) {

    if (isMeleeCriticalStrikeFeat(feat)) {
        return attackerLevel + strengthModifier;
    }
    if (tsl && isMeleePowerAttackFeat(feat)) {
        return attackerLevel + 2 * strengthModifier;
    }
    return 0;
}

} // namespace game

} // namespace reone
