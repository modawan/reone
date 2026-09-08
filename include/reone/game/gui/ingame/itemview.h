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

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace reone::graphics {
class Texture;
}
namespace reone::game {

enum class InventoryFilter {
    All,
    New,
    Quest,
    Equippable,
    Utility,
    Useable,
    Datapad,
    Weapon,
    Armor,
    Misc
};

/** Values copied from the selected backing. Handles have meaning only there. */
struct MenuItemView {
    uint64_t handle {0};
    std::string name;
    std::string description;
    std::shared_ptr<graphics::Texture> icon;
    int stackSize {1};
    bool equipped {false};
    bool valid {true};
};

struct MenuSubjectView {
    bool present {false};
    std::array<std::shared_ptr<graphics::Texture>, 3> portraits;
    std::string vitality;
    std::string defense;
    std::string credits;
};

struct InventoryView {
    MenuSubjectView subject;
    std::vector<MenuItemView> items;
};

struct EquipmentView {
    uint64_t revision {0};
    MenuSubjectView subject;
    bool slotAvailable {false};
    std::vector<MenuItemView> items;
    std::unordered_map<int, std::shared_ptr<graphics::Texture>> equipment;
    std::string mainDamage;
    std::string offDamage;
    std::string mainAttack;
    std::string offAttack;
};

enum class EquipmentRequestOutcome { Applied,
                                     Unchanged,
                                     Rejected,
                                     Failed };
struct EquipmentRequestResult {
    uint64_t revision;
    EquipmentRequestOutcome outcome;
};

class IInventoryMenuBacking {
public:
    virtual ~IInventoryMenuBacking() = default;
    virtual InventoryView readInventory(InventoryFilter filter) = 0;
};

class IEquipmentMenuBacking {
public:
    virtual ~IEquipmentMenuBacking() = default;
    // A negative slot requests the normal overview.
    virtual EquipmentView readEquipment(int slot) = 0;
    // Delivery does not imply completion. Read the correlated result separately.
    // Handle zero explicitly requests clearing the selected slot.
    virtual void equip(uint64_t revision, uint64_t handle, int slot) = 0;
    virtual std::optional<EquipmentRequestResult> equipmentResult() const = 0;
};

} // namespace reone::game
