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

#include "attackanimations.h"

#include "reone/game/attack.h"

namespace reone {

namespace game {

namespace {

static constexpr int kNonCinematicVariantCount = 2;

int nonCinematicVariant(int variant) {
    return 1 + std::max(0, variant - 1) % kNonCinematicVariantCount;
}

std::string attackAnimation(
    char prefix,
    CreatureWieldType wield,
    int variant) {

    return str(boost::format("%c%da%d") %
               prefix %
               static_cast<int>(wield) %
               variant);
}

} // namespace

std::string getMeleeAttackAnim(
    CreatureWieldType attackerWield,
    CreatureWieldType targetWield,
    int variant,
    bool duel) {

    if (duel && isMeleeWieldType(targetWield)) {
        return attackAnimation('c', attackerWield, variant);
    }

    variant = nonCinematicVariant(variant);
    return attackAnimation(
        targetWield != CreatureWieldType::None ? 'm' : 'g',
        attackerWield,
        variant);
}

std::string getUnarmedAttackAnim(
    CreatureWieldType attackerWield,
    CreatureWieldType targetWield,
    int variant,
    bool duel) {

    if (attackerWield == CreatureWieldType::HandToHandComplex &&
        duel &&
        targetWield == attackerWield) {
        return attackAnimation('c', attackerWield, variant);
    }

    return attackAnimation(
        'g',
        CreatureWieldType::HandToHand,
        nonCinematicVariant(variant));
}

std::string getStunBatonAttackAnim(int variant) {
    return attackAnimation(
        'g',
        CreatureWieldType::StunBaton,
        nonCinematicVariant(variant));
}

std::string formatPhysicalMeleeAttackAnimation(
    CreatureWieldType wield,
    int variant,
    bool creatureModel,
    bool cinematic) {

    if (creatureModel) {
        if (wield == CreatureWieldType::None) {
            throw std::logic_error("Monster attacks are not supported");
        }
        return attackAnimation('g', CreatureWieldType::None, variant);
    }

    switch (wield) {
    case CreatureWieldType::SingleSword:
    case CreatureWieldType::DoubleBladedSword:
    case CreatureWieldType::DualSwords:
        return attackAnimation(cinematic ? 'c' : 'm', wield, variant);
    case CreatureWieldType::StunBaton:
    case CreatureWieldType::HandToHand:
    case CreatureWieldType::HandToHandComplex:
        return attackAnimation('g', wield, variant);
    case CreatureWieldType::None:
        throw std::logic_error("Monster attacks are not supported");
    default:
        throw std::logic_error("Invalid melee wield type");
    }
}

} // namespace game

} // namespace reone
