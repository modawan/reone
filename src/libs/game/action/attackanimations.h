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

#include "reone/game/types.h"

namespace reone {

namespace game {

// Name of the attack animation for an attacker wielding a melee weapon. Duels
// against another melee weapon use the cinematic variants, attacks on other
// creatures use the monster variants, and everything else - doors, placeables -
// uses the generic variants.
std::string getMeleeAttackAnim(
    CreatureWieldType attackerWield,
    CreatureWieldType targetWield,
    int variant,
    bool duel);

// Name of the attack animation for an unarmed attacker.
std::string getUnarmedAttackAnim(
    CreatureWieldType attackerWield,
    CreatureWieldType targetWield,
    int variant,
    bool duel);

// Name of the attack animation for an attacker wielding a stun baton. The stun
// baton has generic variants only.
std::string getStunBatonAttackAnim(int variant);

// Name selected by the physical-combat runtime after it has resolved the
// animation family and variant.
std::string formatPhysicalMeleeAttackAnimation(
    CreatureWieldType wield,
    int variant,
    bool creatureModel,
    bool cinematic);

} // namespace game

} // namespace reone
