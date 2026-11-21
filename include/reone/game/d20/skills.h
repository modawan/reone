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

#include <functional>
#include <set>
#include <string>

#include "../types.h"

#include "skill.h"

namespace reone {

namespace resource {

class Strings;
class TwoDAs;
class Textures;

} // namespace resource

namespace game {

class CreatureAttributes;
class CreatureClass;

class ISkills {
public:
    virtual ~ISkills() = default;

    virtual std::shared_ptr<Skill> get(SkillType type) const = 0;
};

/**
 * @return whether the character can use the skill: one it has ranks in,
 *         or, for a skill used untrained, one any of its classes can use
 */
bool canUseSkill(const ISkills &skills, SkillType skill, const CreatureAttributes &attributes, bool tsl);

/**
 * Whether a creature can use the skill by the rule its own skill use and its
 * automatic level-up's feat requirements follow: a Demolitions sub-skill
 * (100 to 102) as Demolitions, which it must have ranks in; in TSL, Stealth
 * also with the personal cloaking shield feat from an effect; otherwise as
 * canUseSkill.
 *
 * @param skill a skill or a Demolitions sub-skill
 * @param hasFeat whether the creature has the feat, its effects' feats
 *        included
 */
bool canCreatureUseSkill(
    const ISkills &skills,
    int skill,
    const CreatureAttributes &attributes,
    const std::function<bool(FeatType)> &hasFeat,
    bool tsl);

/** canCreatureUseSkill for a skill rather than a Demolitions sub-skill. */
bool canCreatureUseSkill(
    const ISkills &skills,
    SkillType skill,
    const CreatureAttributes &attributes,
    const std::function<bool(FeatType)> &hasFeat,
    bool tsl);

class Skills : public ISkills, boost::noncopyable {
public:
    Skills(
        resource::Textures &textures,
        resource::Strings &strings,
        resource::TwoDAs &twoDas) :
        _textures(textures),
        _strings(strings),
        _twoDas(twoDas) {
    }

    void init();

    std::shared_ptr<Skill> get(SkillType type) const override;

private:
    std::unordered_map<SkillType, std::shared_ptr<Skill>> _skills;

    // Services

    resource::Textures &_textures;
    resource::Strings &_strings;
    resource::TwoDAs &_twoDas;

    // END Services
};

/**
 * @return whether one of the creature's classes can use the skill: every
 *         class can, or one of them has a skills table
 */
bool canClassesUseSkill(const Skill &rules, const CreatureAttributes &attributes);

/**
 * The creature a character creation or level-up is for, as far as the skill
 * rules that refuse a raise need it.
 */
struct LevellingCreature {
    /** The creature levels as the player character (Creature::levelsAsPlayerCharacter). */
    bool pcRules {false};
    /** Droids may not raise some skills. */
    bool droid {false};
    /** The creature's tag, for the TSL companions' own rules. */
    std::string tag;
};

/**
 * @return whether ranks may be put into the skill by the recommended
 *         spending, an automatic level-up or the skills screen's restriction
 *         marks: every skill under player character rules; otherwise not one
 *         the skills table closes to non-player characters, nor, for a droid,
 *         one it closes to droids, though on TSL the creature tagged g0t0 may
 *         still raise Stealth
 */
bool canRaiseSkill(const Skill &skill, SkillType type, const LevellingCreature &creature, bool tsl);

/**
 * Spend points on the class's recommended skills, in its order
 * (recommendSkills). Each skill the creature may raise is raised until it
 * reaches its maximum rank at totalLevel or the points run short
 * (CreatureAttributes::increaseSkillRank); spending stops when no points are
 * left.
 *
 * @param effectFeats the feats the creature's effects give it
 */
void addRecommendedSkills(
    const ISkills &skills,
    CreatureAttributes &attributes,
    const CreatureClass &clazz,
    int &points,
    int totalLevel,
    const LevellingCreature &creature,
    const std::set<FeatType> &effectFeats,
    bool tsl);

} // namespace game

} // namespace reone
