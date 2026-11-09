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

} // namespace game

} // namespace reone
