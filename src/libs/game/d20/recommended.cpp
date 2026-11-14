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

#include "reone/game/d20/recommended.h"

#include <algorithm>

#include "reone/game/d20/class.h"
#include "reone/game/d20/spells.h"

namespace reone {

namespace game {

static constexpr SkillType kSkills[] {
    SkillType::ComputerUse,
    SkillType::Demolitions,
    SkillType::Stealth,
    SkillType::Awareness,
    SkillType::Persuade,
    SkillType::Repair,
    SkillType::Security,
    SkillType::TreatInjury};

static constexpr Ability kAbilities[] {
    Ability::Strength,
    Ability::Dexterity,
    Ability::Constitution,
    Ability::Intelligence,
    Ability::Wisdom,
    Ability::Charisma};

void recommendSkills(const CreatureClass &clazz, int allocationLevel, CreatureAttributes &attributes, int &points) {
    // A class whose skills table has no recommendation column ranks no skill,
    // and is recommended to spend nothing: what the original does with such a
    // table is unknown, and spending nothing leaves the points to the player.
    std::vector<std::pair<int, SkillType>> order;
    for (auto skill : kSkills) {
        auto recommendation = clazz.getSkillRecommendation(skill);
        if (recommendation) {
            order.emplace_back(*recommendation, skill);
        }
    }
    std::sort(order.begin(), order.end(), [](const auto &left, const auto &right) {
        if (left.first != right.first) {
            return left.first < right.first;
        }
        return static_cast<int>(left.second) < static_cast<int>(right.second);
    });

    for (const auto &[_, skill] : order) {
        const int maxRank = clazz.maxSkillRank(skill, allocationLevel);
        const int cost = clazz.skillPointCost(skill);
        // A skill that cannot take another rank gives way to the next, which
        // may cost less.
        while (points >= cost && attributes.getSkillRank(skill) < maxRank) {
            attributes.setSkillRank(skill, attributes.getSkillRank(skill) + 1);
            points -= cost;
        }
        if (points == 0) {
            break;
        }
    }
}

std::vector<FeatType> recommendFeats(const IFeats &feats, const FeatChoice &choice, int picks, bool fillWithUnranked) {
    std::vector<FeatType> result;
    std::set<FeatType> chosen(choice.chosen);
    for (int pick = 0; pick < picks; ++pick) {
        FeatChoice next {
            choice.attributes,
            choice.clazz,
            choice.normalPicks,
            choice.bonusPicks,
            choice.effectFeats,
            chosen,
            choice.tag,
            choice.tsl};
        std::optional<std::pair<int, FeatType>> best;
        auto choosable = feats.getChoosableFeats(next);
        for (auto feat : choosable) {
            auto recommendation = choice.clazz.getFeatRecommendation(feat);
            if (!recommendation) {
                continue;
            }
            std::pair<int, FeatType> candidate(*recommendation, feat);
            if (!best ||
                candidate.first < best->first ||
                (candidate.first == best->first && static_cast<int>(feat) < static_cast<int>(best->second))) {
                best = candidate;
            }
        }
        // The choosable feats come in feat order, so the first is the lowest row.
        if (!best && fillWithUnranked && !choosable.empty()) {
            best = std::make_pair(0, choosable.front());
        }
        if (!best) {
            break;
        }
        chosen.insert(best->second);
        result.push_back(best->second);
    }
    return result;
}

std::vector<SpellType> recommendPowers(
    const ISpells &spells,
    const CreatureAttributes &attributes,
    const CreatureClass &clazz,
    int picks,
    Alignment alignment,
    bool fillWithUnranked) {
    const bool darkSide = alignment == Alignment::DarkSide;

    auto sideRecommendation = [&spells](SpellType type, bool dark) -> std::optional<int> {
        auto spell = spells.get(type);
        if (!spell) {
            return std::nullopt;
        }
        return dark ? spell->darkRecommendation : spell->lightRecommendation;
    };
    auto findBest = [&](const std::vector<SpellType> &candidates, bool dark) -> std::optional<SpellType> {
        std::optional<std::pair<int, SpellType>> best;
        for (auto type : candidates) {
            auto recommendation = sideRecommendation(type, dark);
            if (!recommendation) {
                continue;
            }
            if (!best ||
                *recommendation < best->first ||
                (*recommendation == best->first && static_cast<int>(type) < static_cast<int>(best->second))) {
                best = std::make_pair(*recommendation, type);
            }
        }
        return best ? std::optional<SpellType>(best->second) : std::nullopt;
    };

    std::vector<SpellType> result;
    std::set<SpellType> chosen;
    for (int pick = 0; pick < picks; ++pick) {
        auto candidates = spells.getLevelUpCandidates(attributes, clazz, chosen);
        auto power = findBest(candidates, darkSide);
        if (!power) {
            power = findBest(candidates, !darkSide);
        }
        // The candidates come in power row order, so the first is the lowest row.
        if (!power && fillWithUnranked && !candidates.empty()) {
            power = candidates.front();
        }
        if (!power) {
            break;
        }
        chosen.insert(*power);
        result.push_back(*power);
    }
    return result;
}

std::optional<Ability> recommendLevelUpAbility(const CreatureClass &clazz, const CreatureAttributes &attributes, int maxScore) {
    auto primary = clazz.primaryAbility();
    if (primary && attributes.getAbilityScore(*primary) < maxScore) {
        return primary;
    }
    std::optional<Ability> result;
    for (auto ability : kAbilities) {
        if (ability == primary || attributes.getAbilityScore(ability) >= maxScore) {
            continue;
        }
        if (!result || attributes.getAbilityScore(ability) > attributes.getAbilityScore(*result)) {
            result = ability;
        }
    }
    return result;
}

} // namespace game

} // namespace reone
