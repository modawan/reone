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

#include <set>
#include <vector>

#include "../types.h"

#include "feat.h"

namespace reone {

namespace resource {

class IStrings;
class ITwoDAs;
class ITextures;

} // namespace resource

namespace game {

class CreatureAttributes;
class CreatureClass;
class IRaces;
class ISkills;

struct FeatDisplayEntry {
    FeatType type {FeatType::Invalid};
    FeatType chainRoot {FeatType::Invalid};
    int tier {0};
    int visualIndex {0};
};

/** A character choosing feats on the feats screen. */
struct FeatChoice {
    /** The character as it is with the level it gains. */
    const CreatureAttributes &attributes;
    /** The class the level is gained in. */
    const CreatureClass &clazz;
    /** The normal picks the screen counts with: one more than the level's normal feats, or 0 when it gains none. */
    int normalPicks {0};
    /** The bonus picks the screen counts with: one more than the level's bonus feats, or 0 when it gains none. */
    int bonusPicks {0};
    /**
     * The feats the character's effects give it: they count as held for
     * prerequisites, though not as held feats it cannot choose again.
     */
    const std::set<FeatType> &effectFeats;
    /** The feats chosen on the screen so far. */
    const std::set<FeatType> &chosen;
    /** The character's tag; in TSL, Bao-Dur and Kreia cannot choose the feats their columns never grant them. */
    std::string tag;
    /** True in TSL, whose choice rules KotOR does not have. */
    bool tsl {false};
};

class IFeats {
public:
    virtual ~IFeats() = default;

    virtual void init() = 0;

    virtual std::shared_ptr<Feat> get(FeatType type) const = 0;

    /**
     * @return the feats, normal and bonus, the coming level of the class
     *         offers to choose; \p attributes are the character's before the level
     */
    virtual int getLevelUpChoiceCount(const CreatureAttributes &attributes, const CreatureClass &clazz) const = 0;

    /**
     * @return the feats the character can choose, in feat order: every feat
     *         it does not hold or has not chosen whose requirements it meets
     *         and that its class lists as a normal or a bonus feat, while the
     *         picks of that kind, less those the chosen feats take, last
     */
    virtual std::vector<FeatType> getChoosableFeats(const FeatChoice &choice) const = 0;

    /**
     * @param effectFeats the feats the character's effects give it
     * @return whether the character holds, through its feats or its
     *         effects, or has chosen, the feats the feat needs: each of its
     *         prerequisites, and one of its alternative prerequisites when it
     *         has any
     */
    virtual bool hasPrerequisites(FeatType type, const CreatureAttributes &attributes, const std::set<FeatType> &effectFeats, const std::set<FeatType> &chosen) const = 0;

    virtual std::vector<FeatDisplayEntry> getLevelUpDisplayEntries(const CreatureAttributes &attributes, const CreatureClass &clazz) const = 0;

    /**
     * Adds the feats a character is granted with its newest level, which is
     * in the last of its classes. At the first character level the race's
     * first-level feats are granted as well.
     *
     * @param race the character's race
     * @param tag the character's tag; TSL's companions have feats of their own
     * @param tsl true in TSL, whose companion, Unarmed Specialist and new
     *        character rules KotOR does not have
     * @param newCharacter true for a character being created, which in TSL
     *        also starts with its class's player-character feats and War Veteran
     * @return the feats the grant lists as granted: every feat it grants,
     *         held before or not, except the class's grants of the feats no
     *         TSL character chooses, and War Veteran when it was held
     */
    virtual std::vector<FeatType> addGrantedFeats(CreatureAttributes &attributes, RacialType race, const std::string &tag, bool tsl, bool newCharacter) const = 0;
};

class Feats : public IFeats, boost::noncopyable {
public:
    Feats(
        resource::ITextures &textures,
        resource::IStrings &strings,
        resource::ITwoDAs &twoDas,
        IRaces &races,
        ISkills &skills) :
        _textures(textures),
        _strings(strings),
        _twoDas(twoDas),
        _races(races),
        _skills(skills) {
    }

    void init() override;

    std::shared_ptr<Feat> get(FeatType type) const override;
    int getLevelUpChoiceCount(const CreatureAttributes &attributes, const CreatureClass &clazz) const override;
    std::vector<FeatType> getChoosableFeats(const FeatChoice &choice) const override;
    bool hasPrerequisites(FeatType type, const CreatureAttributes &attributes, const std::set<FeatType> &effectFeats, const std::set<FeatType> &chosen) const override;
    std::vector<FeatDisplayEntry> getLevelUpDisplayEntries(const CreatureAttributes &attributes, const CreatureClass &clazz) const override;
    std::vector<FeatType> addGrantedFeats(CreatureAttributes &attributes, RacialType race, const std::string &tag, bool tsl, bool newCharacter) const override;

private:
    std::unordered_map<FeatType, std::shared_ptr<Feat>> _feats;
    /** The feats in feat order. */
    std::vector<FeatType> _featOrder;

    // Services

    resource::ITextures &_textures;
    resource::IStrings &_strings;
    resource::ITwoDAs &_twoDas;
    IRaces &_races;
    ISkills &_skills;

    // END Services

    /**
     * @return whether the character can choose the feat: the feat choice rule
     *         of getChoosableFeats for one feat
     */
    bool canChooseFeat(FeatType type, const FeatChoice &choice) const;

    /**
     * @return whether the character meets the feat's requirements: character
     *         level, base attack bonus, base ability scores, prerequisite
     *         feats and the skill it must be able to use
     */
    bool meetsRequirements(const Feat &feat, const FeatChoice &choice) const;

    /**
     * @return whether the character can use the skill: one it has ranks in,
     *         or, for a skill used untrained, one any of its classes can use
     */
    bool canUseSkill(SkillType skill, const CreatureAttributes &attributes, bool tsl) const;
};

} // namespace game

} // namespace reone
