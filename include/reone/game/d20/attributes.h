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
#include <optional>
#include <set>
#include <string>

#include "../types.h"

#include "savingthrows.h"
#include "skills.h"

namespace reone {

namespace game {

/**
 * The lowest ability score a creature can have, counting its effects and
 * race (getAbilityScoreFromParts); a base score has no floor.
 */
constexpr int kMinimumAbilityScore = 3;

/** An ability score from its base, effect bonus and racial adjustment; at least 3. */
constexpr int getAbilityScoreFromParts(int base, int bonus, int racial) {
    return std::max(kMinimumAbilityScore, base + bonus + racial);
}

/** Every character level divisible by this raises an ability by one. */
constexpr int kAbilityPointLevelInterval = 4;

/** @return whether reaching the character level raises an ability by one */
constexpr bool gainsAbilityPoint(int level) {
    return level > 0 && level % kAbilityPointLevelInterval == 0;
}

/** The modifier of an ability score: half its distance from 10, rounded down. */
constexpr int getAbilityModifierFromScore(int score) {
    return score >= 10 ? (score - 10) / 2 : (score - 11) / 2;
}

/**
 * The points one more point costs a new character at an ability score: the
 * modifier, 1 to 3, so 1 up to 13, 2 at 14 and 15, 3 from 16.
 */
constexpr int getAbilityPointCostFromScore(int score) {
    return std::clamp(getAbilityModifierFromScore(score), 1, 3);
}

class CreatureClass;
class IFeats;
class ISpells;
struct Skill;

/**
 * Aggregate for creature role-playing attributes: classes, abilities, skills,
 * feats and Force Powers.
 */
class CreatureAttributes {
public:
    // Class Levels

    void addClassLevels(CreatureClass *clazz, int levels);
    /**
     * @return these attributes with the creature's coming level: one more
     *         level in its last class
     */
    CreatureAttributes withComingLevel() const;

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
     * @return the sum of the levels of all Jedi classes
     */
    int getJediLevel() const;

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
    /**
     * @param tsl whether the title is TSL: there a class without levels adds
     *            no defense, while in KotOR it adds its level 0 bonus
     * @return the sum of the classes' defense bonuses at their levels
     */
    int getAggregateDefenseBonus(bool tsl) const;
    SavingThrows getAggregateSavingThrows() const;
    const std::vector<std::pair<CreatureClass *, int>> &classLevels() const {
        return _classLevels;
    }

    // END Class Levels

    // Abilities

    /** @return the base score; 0 when the record gives none */
    int getAbilityScore(Ability ability) const;
    /** @return the modifier of the base score */
    int getAbilityModifier(Ability ability) const;

    /** @return the points one more point of the ability costs a new character, see getAbilityPointCostFromScore */
    int getAbilityPointCost(Ability ability) const;

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

    /**
     * @param effectFeats the feats the creature's effects give it
     * @return whether \p clazz makes the skill a class skill: it lists the
     *         skill or, on TSL, the creature has the skill's class-skill
     *         feat, held or given by an effect
     */
    bool isClassSkillIn(const CreatureClass &clazz, SkillType skill, const std::set<FeatType> &effectFeats, bool tsl) const;

    /**
     * Only the creature's classes with a skills table count (isClassSkillIn).
     *
     * @param effectFeats the feats the creature's effects give it
     * @return whether one of the creature's classes makes the skill a class
     *         skill
     */
    bool isClassSkill(SkillType skill, const std::set<FeatType> &effectFeats, bool tsl) const;

    /** @return points one rank of a usable skill costs: 1 for a class skill, 2 otherwise */
    static int skillRankCost(bool classSkill);

    /**
     * @return the highest rank a skill may reach at the total level: the
     *         level plus 3 for a class skill, half that otherwise
     */
    static int maxSkillRank(bool classSkill, int totalLevel);

    /**
     * A skill is usable when one of the creature's classes can use it
     * (canClassesUseSkill).
     *
     * @param rules the skill's rules
     * @param effectFeats the feats the creature's effects give it
     * @return points one rank of the skill costs (skillRankCost, by
     *         isClassSkill); 0 for a skill the creature cannot use
     */
    int getSkillPointCost(const Skill &rules, SkillType skill, const std::set<FeatType> &effectFeats, bool tsl) const;

    /**
     * @param classSkill whether the skill is a class skill, which sets its
     *        cost and maximum rank
     * @param level the level the maximum rank is measured by (maxSkillRank)
     * @return whether the points cover one more rank of the skill
     *         (skillRankCost) and the skill is below its maximum rank
     */
    bool canRaiseSkillRank(SkillType skill, bool classSkill, int level, int points) const;

    /**
     * Raise the skill one rank, paying its cost (skillRankCost) from points.
     *
     * @param classSkill whether the skill is a class skill, which sets its
     *        cost and maximum rank
     * @param level the level the maximum rank is measured by (maxSkillRank)
     * @return false, changing nothing, when the skill cannot take the rank
     *         (canRaiseSkillRank)
     */
    bool raiseSkillRank(SkillType skill, bool classSkill, int level, int &points);

    /**
     * Raise the skill one rank, paying its cost (getSkillPointCost) from
     * points (raiseSkillRank, by isClassSkill).
     *
     * @return false, changing nothing, when the creature cannot use the
     *         skill, points are short of the cost or the skill is at its
     *         maximum rank
     */
    bool increaseSkillRank(const Skill &rules, SkillType skill, int &points, int totalLevel, const std::set<FeatType> &effectFeats, bool tsl);

    /**
     * Skill points left unspent at the last character creation or level-up.
     * They are added to the next level-up's points.
     */
    int unspentSkillPoints() const { return _unspentSkillPoints; }
    void setUnspentSkillPoints(int points) { _unspentSkillPoints = points; }

    // END Skills

    // Feats

    bool hasFeat(FeatType type) const { return _feats.count(type) > 0; }
    /**
     * @return whether the creature holds the feat or one of its effects gives
     *         it (\p effectFeats); Creature::hasEffectiveFeat asks the same of
     *         a creature and its effects
     */
    bool hasFeat(FeatType type, const std::set<FeatType> &effectFeats) const {
        return hasFeat(type) || effectFeats.count(type) > 0;
    }

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
    int _unspentSkillPoints {0};
    std::set<FeatType> _feats;
    std::vector<FeatType> _featOrder;
    std::map<FeatType, int> _spentFeatUses;
    std::set<SpellType> _spells;
    std::map<ClassType, std::vector<SpellType>> _classSpells;

    /**
     * @param effectFeats the feats the creature's effects give it
     * @return whether the creature has the TSL feat that makes the skill a
     *         class skill, held or given by an effect; Persuade has none
     */
    bool hasClassSkillFeat(SkillType skill, const std::set<FeatType> &effectFeats) const;
};

/**
 * Add the feats the creature's coming level in its last class grants
 * (IFeats::addGrantedFeats, on the creature with that level); the level
 * itself is not added.
 *
 * @param race the creature's race, for the first character level's feats;
 *        none grants no race feats
 * @return the feats the grant lists as granted
 */
std::vector<FeatType> addLevelGrantedFeats(
    CreatureAttributes &attributes,
    const IFeats &feats,
    std::optional<RacialType> race,
    int experience,
    const std::string &tag);

/**
 * Add the powers the creature's coming level in its last class grants
 * (ISpells::addGrantedPowers, on the creature with that level), each one it
 * can learn in that class, known through that class; the level itself is not
 * added.
 *
 * @return the powers the level grants
 */
std::vector<SpellType> addLevelGrantedPowers(
    CreatureAttributes &attributes,
    const ISpells &spells,
    const std::function<bool(SpellType, ClassType)> &canLearnPower);

/** What an automatic level-up needs to know of the creature it levels. */
struct AutoLevelUpCreature : LevellingCreature {
    /** The creature's alignment, which picks the side of its recommended powers. */
    Alignment alignment {Alignment::Neutral};
    /** The feats the creature's effects give it, which count for prerequisites. */
    std::set<FeatType> effectFeats;
    /** Whether the creature can learn the power in the class. */
    std::function<bool(SpellType, ClassType)> canLearnPower;
    /**
     * Whether the creature, as it is before the level, knows the power and
     * can pay its Force point cost now: what a master power or prerequisite
     * of a chosen power needs, unless granted or chosen at this level.
     */
    std::function<bool(SpellType)> knowsAffordablePower;
};

/**
 * Make the choices of one automatic level-up in the creature's last class,
 * and add the feats and powers that level grants. The class level itself is
 * left for the level-up to add.
 *
 * Some rules are measured by the new character level: a level that is a
 * multiple of 4 raises the recommended ability by 1
 * (recommendLevelUpAbility), the first character level gives the first
 * level's skill points, and the skills' maximum ranks, the feats' minimum
 * levels and the powers' levels are measured by it.
 * The skill points are those left unspent plus the level's, from the
 * Intelligence the creature had; they go on the recommended skills
 * (addAutoLevelUpSkills), and what is left stays unspent. The feats the level
 * grants are added, though never the race's first-level feats, then the
 * recommended feats for the class level's normal and bonus picks, chosen on
 * the creature as it was before the level, a pick no ranked feat can take
 * filled by the lowest row (recommendFeats). A spell-casting class then adds
 * the powers the level grants and the powers chosen for the level
 * (ISpells::getAutoLevelUpPowers), each only when the creature can learn it.
 */
void chooseAutoLevelUp(
    CreatureAttributes &attributes,
    const IFeats &feats,
    const ISpells &spells,
    const ISkills &skills,
    const AutoLevelUpCreature &creature,
    bool tsl);

} // namespace game

} // namespace reone
