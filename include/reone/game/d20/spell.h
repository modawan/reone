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

struct Spell {
    /** The innate level of a power whose spells.2da row leaves it blank. */
    static constexpr int kBlankInnateLevel = 255;
    /** The lowest caster level of a power used from an item or as a special ability. */
    static constexpr int kMinimumInnateCasterLevel = 10;

    SpellType type;
    std::string name;
    std::string description;
    std::shared_ptr<graphics::Texture> icon;
    uint32_t pips {1};
    std::vector<SpellType> prerequisites;
    std::optional<SpellType> masterSpell;
    int userType {-1};
    int innateLevel {kBlankInnateLevel};
    int forcePointCost {0};
    /** The power's side (spells.2da GoodEvil): 'G' light, 'E' dark, 'N' neither. */
    char alignment {'N'};
    std::unordered_map<ClassType, int> classLevelRequirements;
    uint32_t category {0};
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
    /** The least distance the power menu offers the power at; none without a single-letter range tag. */
    std::optional<float> minimumRange;
    /** The combat forms the power works in (TSL). */
    uint32_t formMask {0};
    bool hostile {false};
    /** The slots of the hostile and friendly power menus; negative off that menu. */
    int hostileSlot {-1};
    int friendlySlot {-1};
    /** Of two powers in one menu slot, the higher priority is offered. */
    int menuPriority {0};
    std::shared_ptr<graphics::Model> projModel;
    bool projectile {false};
    ProjectilePathType projectilePath {ProjectilePathType::Default};
    std::string projectileSpawn;
    std::string projectileOrientation;
    /** The head, hand and ground models shown while conjuring and after the release. */
    std::array<std::string, 3> conjureVisuals;
    std::array<std::string, 3> castVisuals;
    /** Target kinds a hostile power menu does not offer the power against. */
    uint32_t exclusion {0};

    /**
     * The caster level of a power used from an item or as a special ability:
     * twice its innate level less one, at least 10.
     */
    int innateCasterLevel() const {
        return std::max(kMinimumInnateCasterLevel, 2 * innateLevel - 1);
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
};

} // namespace game

} // namespace reone
