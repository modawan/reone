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

#include <array>
#include <memory>
#include <optional>
#include <string>

#include "types.h"

namespace reone {

namespace game {

struct Spell;
class Item;

/** Why a power in the power menu cannot be used now. */
enum class PowerUnavailableReason {
    None,
    InsufficientForce,
    ForbiddenEquipment,
    RequiredEquipment,
    MinimumRange,
    InsufficientVitality,
    /** KotOR: a healing kit while no one it would treat needs it. */
    NoOneToHeal,
};

/**
 * Whether a power menu entry can be used now and, when it cannot, the one
 * reason the menu shows.
 */
struct PowerMenuStatus {
    bool ready {true};
    PowerUnavailableReason reason {PowerUnavailableReason::None};

    bool available() const { return ready; }
    void disallow(PowerUnavailableReason value) {
        ready = false;
        reason = value;
    }
};

/** An implant mode the implant-switching feat offers: its menu name and icon and the message its selection shows. */
struct ImplantMode {
    int mode;
    uint32_t nameStrRef;
    const char *icon;
    uint32_t messageStrRef;
};

/** The implant modes in menu order: regeneration, strength, agility, endurance. */
inline constexpr std::array<ImplantMode, 4> kImplantModes {{
    {1, 48429, "ii_implant1_001", 48438},
    {2, 48430, "ii_implant3_003", 48439},
    {4, 48431, "ii_implant2_002", 48441},
    {3, 48432, "ii_implant3_004", 48440},
}};

/** A party behaviour entry: the combat style it gives the leader, with its menu name and icon. */
struct BehaviorEntry {
    NPCAIStyle style;
    uint32_t nameStrRef;
    std::string icon;
};

/** An entry of a menu of actions: a feat, skill, power or item use, or a mode to select. */
struct ContextAction {
    ActionType type {ActionType::Invalid};
    /** The feat used, or for a power a feat grants, the granting feat, which names the entry. */
    FeatType feat {FeatType::Invalid};
    SkillType skill {SkillType::Invalid};
    int subSkill {0};
    std::shared_ptr<Spell> spell;
    std::shared_ptr<Item> item;
    /** An item entry for an item its user wears rather than one from the party's inventory. */
    bool worn {false};
    PowerMenuStatus availability;
    /** A power of an equipped item in a droid leader's power menu. */
    bool equipmentPower {false};
    /** A form entry: selecting it makes its power the current form at once. */
    bool form {false};
    /** An implant-mode entry: selecting it switches the implant mode at once. */
    std::optional<ImplantMode> implant;
    /** A behaviour entry: selecting it gives the leader its combat style at once. */
    std::optional<BehaviorEntry> behavior;

    ContextAction(ActionType type) :
        type(type) {}
    ContextAction(const ImplantMode &implant) :
        implant(implant) {}
    ContextAction(BehaviorEntry behavior) :
        behavior(std::move(behavior)) {}
    ContextAction(FeatType feat) :
        type(ActionType::UseFeat), feat(feat) {}
    ContextAction(SkillType skill) :
        type(ActionType::UseSkill), skill(skill) {}
    ContextAction(SkillType skill, int subSkill, std::shared_ptr<Item> item = nullptr) :
        type(ActionType::UseSkill), skill(skill), subSkill(subSkill), item(std::move(item)) {}
    ContextAction(std::shared_ptr<Spell> spell) :
        type(ActionType::CastSpellAtObject), spell(spell) {}
    ContextAction(std::shared_ptr<Item> item, std::shared_ptr<Spell> spell) :
        type(ActionType::CastSpellAtObject), spell(spell), item(item) {}
};

} // namespace game

} // namespace reone
