/*
 * Copyright (c) 2020-2023 The reone project contributors
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

#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "reone/graphics/texture.h"

#include "../types.h"

namespace reone {

namespace game {

struct Feat {
    /** The prerequisite word that names no feat. */
    static constexpr int kNoPrerequisite = 0xffff;

    std::string name;
    std::string description;
    std::shared_ptr<graphics::Texture> icon;
    /** The feat's name (its feat.2da name string). */
    int nameStrRef {-1};
    int category {0}; /**< talent category */
    /** The uses a day, from 0 to 255; zero for a feat whose uses are not counted. */
    int usesPerDay {0};
    /** The challenge rating that ranks the feat as a talent. */
    int maxCR {0};
    /** The talent inclusion mask. */
    uint32_t exclusion {0};
    /** The character level, from 0 to 255, a character must reach to choose the feat. */
    int minCharLevel {0};
    /** The word of a feat that must be held to choose this one; kNoPrerequisite for none. */
    int preReqFeat1 {kNoPrerequisite};
    /** The word of a second feat that must be held to choose this one; kNoPrerequisite for none. */
    int preReqFeat2 {kNoPrerequisite};
    /**
     * The words of feats of which one must be held to choose this one; none
     * of them is needed when the list is empty.
     */
    std::vector<int> alternativePrerequisites;
    /** The base attack bonus, from 0 to 255, a character must have to choose the feat. */
    int minAttackBonus {0};
    /**
     * The base ability scores, each from 0 to 255, a character must have to
     * choose the feat. The table gives a minimum for Strength, Dexterity,
     * Intelligence and Wisdom only.
     */
    std::map<Ability, int> minAbilityScores;
    /** The skill a character must be able to use to choose the feat; none for no skill. */
    std::optional<SkillType> requiredSkill;
    FeatType successor {FeatType::Invalid};
    uint32_t pips {1}; // 1-3, position in a feat chain
    std::optional<int> spellId; /**< the spell whose effects count as this feat's effects */
    /**
     * TSL companion columns by lower-case companion tag: the character level
     * at which the companion gains the feat, or none for a class feat the
     * companion is never granted. Only marked cells are kept.
     */
    std::unordered_map<std::string, std::optional<int>> companionLevels;
};

} // namespace game

} // namespace reone
