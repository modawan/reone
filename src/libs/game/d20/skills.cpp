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

#include "reone/game/d20/skills.h"

#include <algorithm>

#include <boost/algorithm/string.hpp>

#include "reone/game/d20/attributes.h"
#include "reone/game/d20/class.h"
#include "reone/game/d20/recommended.h"
#include "reone/game/twodautil.h"
#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/strings.h"

using namespace reone::graphics;
using namespace reone::resource;

namespace reone {

namespace game {

void Skills::init() {
    std::shared_ptr<TwoDA> skills(_twoDas.get("skills"));
    if (!skills) {
        return;
    }

    for (int row = 0; row < skills->getRowCount(); ++row) {
        std::string name(_strings.getText(skills->getInt(row, "name", -1)));
        std::string description(_strings.getText(skills->getInt(row, "description", -1)));
        std::shared_ptr<Texture> icon(_textures.get(skills->getString(row, "icon"), TextureUsage::GUI));

        auto skill = std::make_shared<Skill>();
        skill->name = std::move(name);
        skill->description = std::move(description);
        skill->icon = std::move(icon);
        // The untrained, all-classes and hostile flags are the low bits of their cells.
        skill->untrained = (skills->getInt(row, "untrained", 0) & 1) != 0;
        skill->allClassesCanUse = (skills->getInt(row, "allclassescanuse", 0) & 1) != 0;
        skill->hostile = (skills->getInt(row, "hostileskill", 0) & 1) != 0;
        skill->keyAbility = readAbilityColumn(*skills, row, "keyability").value_or(Ability::Strength);
        // So are the non-player and droid flags, which leave the skill open
        // when the table does not say.
        skill->npcCanUse = (skills->getInt(row, "npccanuse", 1) & 1) != 0;
        skill->droidCanUse = (skills->getInt(row, "droidcanuse", 1) & 1) != 0;
        _skills.insert(std::make_pair(static_cast<SkillType>(row), std::move(skill)));
    }
}

std::shared_ptr<Skill> Skills::get(SkillType type) const {
    auto it = _skills.find(type);
    return it != _skills.end() ? it->second : nullptr;
}

// Whether a character of the class can use the skill: every class can, or the
// class has a skills table.
static bool canClassUseSkill(const Skill &rules, const CreatureClass &clazz) {
    return rules.allClassesCanUse || clazz.hasSkills();
}

// Whether one of the creature's classes can use the skill.
bool canClassesUseSkill(const Skill &rules, const CreatureAttributes &attributes) {
    const auto &classLevels = attributes.classLevels();
    return std::any_of(classLevels.begin(), classLevels.end(), [&rules](const auto &classLevel) {
        return canClassUseSkill(rules, *classLevel.first);
    });
}

bool canUseSkill(const ISkills &skills, SkillType skill, const CreatureAttributes &attributes, bool tsl) {
    // A skill past the skills table cannot be used.
    std::shared_ptr<Skill> rules(skills.get(skill));
    if (!rules) {
        return false;
    }
    // In TSL, the camouflage powers and the personal cloaking shield let a
    // character use Stealth.
    if (tsl && skill == SkillType::Stealth &&
        (attributes.hasSpell(SpellType::ForceCamouflage) ||
         attributes.hasSpell(SpellType::ImprovedForceCamouflage) ||
         attributes.hasSpell(SpellType::MasterForceCamouflage) ||
         attributes.hasFeat(FeatType::PersonalCloakingShield))) {
        return true;
    }
    if (rules->untrained) {
        return canClassesUseSkill(*rules, attributes);
    }
    return attributes.getSkillRank(skill) > 0;
}

bool canCreatureUseSkill(
    const ISkills &skills,
    int skill,
    const CreatureAttributes &attributes,
    const std::function<bool(FeatType)> &hasFeat,
    bool tsl) {
    // A Demolitions sub-skill is looked up as Demolitions, but no class's
    // skills table lists it, so it is never used untrained.
    if (skill >= static_cast<int>(SubSkill::FlagTrap) && skill <= static_cast<int>(SubSkill::ExamineTrap)) {
        const auto rules = skills.get(SkillType::Demolitions);
        return rules && !rules->untrained && attributes.getSkillRank(SkillType::Demolitions) > 0;
    }
    return canCreatureUseSkill(skills, static_cast<SkillType>(skill), attributes, hasFeat, tsl);
}

bool canCreatureUseSkill(
    const ISkills &skills,
    SkillType skill,
    const CreatureAttributes &attributes,
    const std::function<bool(FeatType)> &hasFeat,
    bool tsl) {
    // In TSL, the stealth feat counts for Stealth also when an effect grants it.
    if (tsl && skill == SkillType::Stealth && skills.get(SkillType::Stealth) &&
        hasFeat(FeatType::PersonalCloakingShield)) {
        return true;
    }
    return canUseSkill(skills, skill, attributes, tsl);
}

// The TSL companion G0-T0, a droid that may still raise Stealth.
static constexpr char kG0T0Tag[] = "g0t0";

bool canRaiseSkill(const Skill &skill, SkillType type, const LevellingCreature &creature, bool tsl) {
    if (creature.pcRules) {
        return true;
    }
    if (!skill.npcCanUse) {
        return false;
    }
    const bool g0t0Stealth {tsl && type == SkillType::Stealth && boost::iequals(creature.tag, kG0T0Tag)};
    return !creature.droid || skill.droidCanUse || g0t0Stealth;
}

void addRecommendedSkills(
    const ISkills &skills,
    CreatureAttributes &attributes,
    const CreatureClass &clazz,
    int &points,
    int totalLevel,
    const LevellingCreature &creature,
    const std::set<FeatType> &effectFeats,
    bool tsl) {

    recommendSkills(clazz, points, [&](SkillType type, int &left) {
        const Skill &rules = *skills.get(type);
        return canRaiseSkill(rules, type, creature, tsl) &&
               attributes.increaseSkillRank(rules, type, left, totalLevel, effectFeats, tsl);
    });
}

void addAutoLevelUpSkills(
    const ISkills &skills,
    CreatureAttributes &attributes,
    const CreatureClass &clazz,
    int &points,
    int level,
    const LevellingCreature &creature,
    const std::set<FeatType> &effectFeats,
    bool tsl) {

    recommendSkills(clazz, points, [&](SkillType type, int &left) {
        const Skill &rules = *skills.get(type);
        return canClassUseSkill(rules, clazz) && canRaiseSkill(rules, type, creature, tsl) &&
               attributes.raiseSkillRank(type, attributes.isClassSkillIn(clazz, type, effectFeats, tsl), level, left);
    });
}

} // namespace game

} // namespace reone
