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

#include <algorithm>
#include <functional>

#include "../types.h"

#include "savingthrows.h"

namespace reone {

namespace game {

/** The lowest ability score a creature can have. */
constexpr int kMinimumAbilityScore = 3;

/** An ability score from its base, effect bonus and racial adjustment; at least 3. */
constexpr int getAbilityScoreFromParts(int base, int bonus, int racial) {
    return std::max(kMinimumAbilityScore, base + bonus + racial);
}

/** The modifier of an ability score: half its distance from 10, rounded down. */
constexpr int getAbilityModifierFromScore(int score) {
    return score >= 10 ? (score - 10) / 2 : (score - 11) / 2;
}

class CreatureClass;

/**
 * Aggregate for creature role-playing attributes: classes, abilities, skills,
 * feats and Force Powers.
 */
class CreatureAttributes {
public:
    int getDefense() const;

    // Class Levels

    void addClassLevels(CreatureClass *clazz, int levels);

    /**
     * @return class type at the specified position (1-based)
     */
    ClassType getClassByPosition(int position) const;

    /**
     * @return class level at the specified position (1-based)
     */
    int getLevelByPosition(int position) const;

    /**
     * @return the sum of all class levels, aka "hit dice"
     */
    int getAggregateLevel() const;

    /**
     * @return the sum of (level * hitdie) of all classes
     */
    int getAggregateHitDie() const;

    /**
     * Derive maximum vitality from the base maximum and a Constitution
     * modifier. hasVitalityFeat answers for the Toughness, War Veteran and
     * Wookiee endurance feats.
     */
    int getMaxHitPoints(int baseHitPoints, int constitutionModifier,
                        const std::function<bool(FeatType)> &hasVitalityFeat) const;

    /**
     * Derive the player character's maximum vitality from its level history:
     * each level grants the hit die recorded for it (levelHitDie, by 0-based
     * level) plus the Constitution modifier, at least 1, plus Toughness. War
     * Veteran counts once and Wookiee endurance not at all.
     */
    int getLevelHistoryMaxHitPoints(const std::function<int(int)> &levelHitDie, int constitutionModifier,
                                    const std::function<bool(FeatType)> &hasVitalityFeat) const;

    ClassType getEffectiveClass() const;
    int getClassLevel(ClassType clazz) const;
    int getAggregateAttackBonus() const;
    int getAggregateDefenseBonus() const;
    SavingThrows getAggregateSavingThrows() const;
    const std::vector<std::pair<CreatureClass *, int>> &classLevels() const {
        return _classLevels;
    }

    // END Class Levels

    // Abilities

    int getAbilityScore(Ability ability) const;
    int getAbilityModifier(Ability ability) const;

    const std::map<Ability, int> &abilityScores() const { return _abilityScores; }
    int strength() const;
    int dexterity() const;
    int constitution() const;
    int intelligence() const;
    int wisdom() const;
    int charisma() const;

    void setAbilityScore(Ability ability, int score);

    // END Abilities

    // Skills

    bool hasSkill(SkillType skill) const;

    int getSkillRank(SkillType skill) const;

    const std::map<SkillType, int> &skillRanks() const { return _skillRanks; }
    int computerUse() const;
    int demolitions() const;
    int stealth() const;
    int awareness() const;
    int persuade() const;
    int repair() const;
    int security() const;
    int treatInjury() const;

    void setSkillRank(SkillType skill, int rank);

    // END Skills

    // Feats

    bool hasFeat(FeatType type) const { return _feats.count(type) > 0; }

    /** Add a feat; feats keep the order they were first added in. */
    void addFeat(FeatType type);
    /** Remove a feat with its spent uses. */
    void removeFeat(FeatType type);
    const std::set<FeatType> &feats() const { return _feats; }
    /** The feats in the order they were added. */
    const std::vector<FeatType> &featOrder() const { return _featOrder; }
    /** The uses of a feat spent so far; none for a feat never used. */
    int spentFeatUses(FeatType type) const;
    void spendFeatUse(FeatType type);

    // END Feats

    // Force Powers

    bool hasSpell(SpellType type) const { return _spells.count(type) > 0; }

    /** Add a power known through the effective class. */
    void addSpell(SpellType type);
    /** Add a power known through \p owner; each class keeps its powers in the order added. */
    void addSpell(SpellType type, ClassType owner);
    /** Remove a power from every class that knows it. */
    void removeSpell(SpellType type);
    const std::set<SpellType> &spells() const { return _spells; }
    /** The powers known through \p owner, in the order added. */
    const std::vector<SpellType> &spellsForClass(ClassType owner) const;

    // END Force Powers

private:
    std::vector<std::pair<CreatureClass *, int>> _classLevels;
    std::map<Ability, int> _abilityScores;
    std::map<SkillType, int> _skillRanks;
    std::set<FeatType> _feats;
    std::vector<FeatType> _featOrder;
    std::map<FeatType, int> _spentFeatUses;
    std::set<SpellType> _spells;
    std::map<ClassType, std::vector<SpellType>> _classSpells;
};

} // namespace game

} // namespace reone
