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

class Creature;

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

/** What activating an inventory entry does. */
enum class InventoryActivation {
    None,
    Use,
    Message
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
    /** Why the equipment screen will not equip the item, or 0. */
    int refusalStrRef {0};
    /** In the inventory: use the item, show messageStrRef, or nothing. */
    InventoryActivation activation {InventoryActivation::None};
    /** The message activating the entry shows, when it shows one. */
    int messageStrRef {0};
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
    /** The leader used an item in combat too recently to use another. */
    bool itemUseCoolingDown {false};
};

struct EquipmentView {
    uint64_t revision {0};
    MenuSubjectView subject;
    /** The shown character is a droid, whose slots have their own names. */
    bool droid {false};
    bool slotAvailable {false};
    /** Why the slot cannot be opened, or 0. */
    int slotRefusalStrRef {0};
    /** The screen pages through the roster rather than the party. */
    bool canBrowseCharacters {false};
    std::vector<MenuItemView> items;
    std::unordered_map<int, std::shared_ptr<graphics::Texture>> equipment;
    std::string mainDamage;
    std::string offDamage;
    std::string mainAttack;
    std::string offAttack;
    /** Item and effect modifiers raise the damage range. */
    bool mainDamageRaised {false};
    bool offDamageRaised {false};
    /** The attack exceeds the base attack bonus. */
    bool mainAttackRaised {false};
    bool offAttackRaised {false};
    /** TSL's other weapon set. */
    std::string mainDamage2;
    std::string offDamage2;
    std::string mainAttack2;
    std::string offAttack2;
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
    /** The interface string of strRef, its tokens resolved. */
    virtual std::string interfaceText(int strRef) const = 0;
    /**
     * The leader uses a listed item at once. Returns the message shown instead
     * when the use is refused.
     */
    virtual std::optional<int> useItem(uint64_t handle) = 0;
};

class IEquipmentMenuBacking {
public:
    virtual ~IEquipmentMenuBacking() = default;
    /** The interface string of strRef, its tokens resolved. */
    virtual std::string interfaceText(int strRef) const = 0;
    /** The equipment screen opens: it shows the leader, or in a roster-browsing module the player. */
    virtual void beginEquipment() = 0;
    /** The equipment screen closes. */
    virtual void endEquipment() = 0;
    /** While browsing the roster, show the next character in it. */
    virtual void nextCharacter() = 0;
    /** While browsing the roster, show the previous character in it. */
    virtual void previousCharacter() = 0;
    /** The screen browses the whole roster instead of the party. */
    virtual bool browsingRoster() const = 0;
    /** While the roster is browsed, the character shown, if it still exists. */
    virtual std::shared_ptr<Creature> browsedCharacter() const = 0;
    /**
     * While the roster is browsed, the roster index of the companion shown;
     * -1 for the player character or when the roster is not browsed.
     */
    virtual int browsedRosterIndex() const = 0;
    /**
     * Gives control to the party member at the index, or to the next member
     * standing when negative.
     */
    virtual void changeCharacter(int member) = 0;
    /**
     * What the screen shows with no slot open: the character, the equipped
     * items' icons and the hands' damage and attack, with no item list.
     */
    virtual EquipmentView readEquipmentOverview() = 0;
    /** A negative slot lists every equippable item. */
    virtual EquipmentView readEquipment(int slot) = 0;
    // Delivery does not imply completion. Read the correlated result separately.
    // Handle zero explicitly requests clearing the selected slot.
    virtual void equip(uint64_t revision, uint64_t handle, int slot) = 0;
    virtual std::optional<EquipmentRequestResult> equipmentResult() const = 0;
    /** Asks for the shown character's weapon sets to be swapped; the slots show the swap once it is made. */
    virtual void switchWeapons() = 0;
};

} // namespace reone::game
