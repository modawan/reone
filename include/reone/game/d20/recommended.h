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

#include "../types.h"

#include "attributes.h"
#include "feats.h"

namespace reone {

namespace game {

class CreatureClass;
class ISpells;

/**
 * Spends skill points as the class recommends: through the skills the class
 * ranks, in its order, raising each as far as the points and the skill's
 * rank cap allow before going on to the next. A skill the class does not
 * rank receives nothing.
 *
 * @param clazz the class the points are spent in
 * @param allocationLevel the level the ranks are bought for: a class skill's
 *        rank reaches at most this level + 3, a cross-class skill's half that
 * @param attributes the ranks to raise, as the skills screen starts them
 * @param points the points to spend, left with those that could not be spent
 */
void recommendSkills(const CreatureClass &clazz, int allocationLevel, CreatureAttributes &attributes, int &points);

/**
 * Chooses feats as the class recommends: one pick at a time, the feat the
 * choice can take that the class ranks first, the lower feat row breaking a
 * tie. A chosen feat counts as held for the next pick.
 *
 * @param choice the choice as the feats screen opens it; the feats it has
 *        chosen so far count as chosen
 * @param picks the feats to choose
 * @param fillWithUnranked true to fill a pick no ranked feat can take with
 *        the choosable feat of the lowest row, as an automatic level-up must
 * @return the recommended feats, in the order they were chosen; fewer than
 *         the picks when no feat the class ranks, or with \p fillWithUnranked
 *         no feat at all, can be chosen
 */
std::vector<FeatType> recommendFeats(const IFeats &feats, const FeatChoice &choice, int picks, bool fillWithUnranked = false);

/**
 * Chooses powers as recommended for the character's side: one pick at a
 * time, the candidate ranked first for its side, or when no candidate is
 * ranked for its side, the candidate ranked first for the other; the lower
 * power row breaks a tie. A chosen power counts toward the prerequisites of
 * the next pick.
 *
 * @param attributes the character before the level the powers are chosen for
 * @param clazz the class the level is gained in
 * @param picks the powers to choose
 * @param alignment the character's alignment: a dark side character's side
 *        is the dark side, any other's the light side
 * @param fillWithUnranked true to fill a pick no ranked candidate can take
 *        with the candidate of the lowest row, as an automatic level-up must
 * @return the recommended powers, in the order they were chosen; fewer than
 *         the picks when no candidate is left
 */
std::vector<SpellType> recommendPowers(
    const ISpells &spells,
    const CreatureAttributes &attributes,
    const CreatureClass &clazz,
    int picks,
    Alignment alignment,
    bool fillWithUnranked = false);

} // namespace game

} // namespace reone
