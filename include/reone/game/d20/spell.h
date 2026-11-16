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
#include <array>
#include <optional>

#include "reone/graphics/texture.h"

#include "../types.h"

namespace reone {

namespace audio {
class AudioClip;
}

namespace graphics {
class Model;
}

namespace game {

/** The spells.2da user types of Force powers, special abilities and item abilities. */
constexpr int kSpellUserTypeForcePower = 1;
constexpr int kSpellUserTypeSpecialAbility = 2;
constexpr int kSpellUserTypeItem = 4;

/** The spells.2da CastAnim code, which picks both the conjure and the cast clip. */
enum class SpellCastAnimation {
    Other = 0,
    Self = 1,
    Dark = 2,
    Up = 3,
    Area = 4,
    Touch = 6,
    Throw = 7,
    Jump = 8,
    Monster = 9,
    Fury = 10,
    Crush = 11,
    MonsterFury = 12
};

/** A place on a power menu. */
struct SpellMenuPlace {
    /** The menu group: hostile 1, passive 2, friendly 3. */
    int group {0};
    /** The slot within the group, ordering its menu. */
    int slot {0};
    /** The priority within the slot: 0 for the slot's first power. */
    int priority {0};
};

struct Spell {
    /** The lowest caster level of a power used from an item or as a special ability. */
    static constexpr int kMinimumInnateCasterLevel = 10;

    SpellType type;
    /** The string reference of the name; 0 when the name cell is blank. */
    int nameStrRef {0};
    std::string name;
    /**
     * The line that announces the power's use in place of "<caster> uses
     * <power>.", naming the caster alone; none when spells.2da AltMessage is
     * blank or 0.
     */
    std::optional<int> altMessage;
    std::string description;
    std::shared_ptr<graphics::Texture> icon;
    /** The power's position in its chain, from 1. */
    int pips {1};
    /**
     * The powers the power needs, each known or chosen: up to five entries,
     * before the first of -1; an entry below zero names no power.
     */
    std::vector<SpellType> prerequisites;
    /**
     * The master powers the power needs as well, each known or chosen, read
     * as prerequisites are.
     */
    std::vector<SpellType> masterSpells;
    /** The level-up screen does not offer the power: its first master power entry is -2. */
    bool unofferedOnLevelUp {false};
    int userType {-1};
    /** The innate level; none when the cell is blank. */
    std::optional<int> innateLevel;
    int forcePointCost {0};
    /** The power's side (spells.2da GoodEvil): 'G' light, 'E' dark, 'N' neither. */
    char alignment {'N'};
    /**
     * The level each Jedi or Sith class's column gives the power; no entry for
     * a blank cell. A level below zero gives the class none
     * (getLevelForClass).
     */
    std::unordered_map<ClassType, int> classLevelRequirements;
    /**
     * The power's places in the orders recommended to light side and to
     * dark side characters, 1 first (spells.2da light_recom and dark_recom);
     * none for a side the power is never recommended to.
     */
    std::optional<int> lightRecommendation;
    std::optional<int> darkRecommendation;
    /** The talent category: four four-bit fields. */
    uint32_t category {0};
    /** The challenge rating that ranks the spell as a talent. */
    int maxCR {0};
    std::string impactScript;
    SpellCastAnimation castAnimation {SpellCastAnimation::Other};
    std::shared_ptr<audio::AudioClip> castSound;
    float conjTime {0.0f};
    float castTime {0.0f};
    float catchTime {0.0f};
    uint32_t itemTargeting {0};
    /** The item mask bits (Item::forceItemMask) all of which the caster must wear. */
    uint32_t requireItemMask {0};
    /** The item mask bits none of which the caster may wear. */
    uint32_t forbidItemMask {0};
    /** The casting range, from ranges.2da. */
    float range {0.0f};
    /** The spells.2da range tag as written. */
    std::string rangeTag;
    /**
     * The least distance the power menu offers the power at, from the row of
     * its range tag; none when the tag names no range (see
     * unrangedPowerMinimumRange).
     */
    std::optional<float> minimumRange;
    /** The combat forms the power works in (TSL). */
    uint32_t formMask {0};
    bool hostile {false};
    /**
     * The hostile menu slot cell as written, a negative value too; none when
     * blank. A filled cell gives a power taken from a menu the hostile power
     * tutorial.
     */
    std::optional<int> hostileSlot;
    /**
     * The places on the menus the power is on, one for each filled slot cell
     * in the order friendly, hostile and passive, each with the power's
     * priority.
     */
    std::vector<SpellMenuPlace> menuPlaces;
    std::shared_ptr<graphics::Model> projModel;
    bool projectile {false};
    ProjectilePathType projectilePath {ProjectilePathType::Default};
    std::string projectileSpawn;
    std::string projectileOrientation;
    /** The sound a projectile of the spell plays while it flies and once where it first arrives. */
    std::shared_ptr<audio::AudioClip> projectileSound;
    /** The head, hand and ground models shown while conjuring and after the release. */
    std::array<std::string, 3> conjureVisuals;
    /** The voice a male or female caster of a player race conjures the spell with. */
    std::shared_ptr<audio::AudioClip> conjureSoundMale;
    std::shared_ptr<audio::AudioClip> conjureSoundFemale;
    /** The sound the conjure visuals play. */
    std::shared_ptr<audio::AudioClip> conjureSoundVisual;
    std::array<std::string, 3> castVisuals;
    /**
     * Target kinds a hostile power menu does not offer the power against.
     * Talent selection also tests it against the requested inclusion.
     */
    uint32_t exclusion {0};

    /**
     * The level of a power from no class, as an area of effect and the Force
     * resistance check take it: twice its innate level less one, a blank
     * innate level counting as 0.
     */
    int innateSpellLevel() const {
        return 2 * innateLevel.value_or(0) - 1;
    }

    /**
     * The caster level of a power used from an item or as a special ability:
     * its innate spell level, at least 10.
     */
    int innateCasterLevel() const {
        return std::max(kMinimumInnateCasterLevel, innateSpellLevel());
    }

    bool isForbiddenByEquipment(uint32_t equippedMask) const {
        return (equippedMask & forbidItemMask) != 0;
    }

    bool hasRequiredEquipment(uint32_t equippedMask) const {
        return (equippedMask & requireItemMask) == requireItemMask;
    }

    std::optional<int> getClassLevelRequirement(ClassType clazz) const {
        auto maybeRequirement = classLevelRequirements.find(clazz);
        return maybeRequirement != classLevelRequirements.end()
                   ? std::optional<int>(maybeRequirement->second)
                   : std::nullopt;
    }

    /**
     * The power's level for a caster of \p clazz: the level a Jedi or Sith
     * class's column lists, and the innate level for the droid, Minion and
     * any other classes; none for the other player classes, or when that
     * cell is blank or below zero.
     */
    std::optional<int> getLevelForClass(ClassType clazz) const {
        std::optional<int> level;
        switch (clazz) {
        case ClassType::Soldier:
        case ClassType::Scout:
        case ClassType::Scoundrel:
        case ClassType::TechSpecialist:
        case ClassType::BountyHunter:
            break;
        case ClassType::JediGuardian:
        case ClassType::JediConsular:
        case ClassType::JediSentinel:
        case ClassType::JediWeaponMaster:
        case ClassType::JediMaster:
        case ClassType::JediWatchman:
        case ClassType::SithMarauder:
        case ClassType::SithLord:
        case ClassType::SithAssassin:
            level = getClassLevelRequirement(clazz);
            break;
        default:
            level = innateLevel;
            break;
        }
        return level && *level >= 0 ? level : std::nullopt;
    }
};

} // namespace game

} // namespace reone
