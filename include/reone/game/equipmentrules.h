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

#include <cstdint>
#include <memory>
#include <optional>

#include "types.h"

namespace reone {

namespace game {

class Creature;
class Game;
class Item;
class Object;

/** The bit of an inventory slot in a slot mask; none for a slot that does not exist. */
constexpr uint32_t equipmentSlotMask(int slot) {
    return slot >= 0 && slot < InventorySlots::count ? uint32_t {1} << slot : 0;
}

/** The inventory slot a slot mask names, or none unless it names exactly one slot. */
constexpr std::optional<int> equipmentSlotFromMask(uint32_t mask) {
    if (!mask || (mask & (mask - 1)) || mask > equipmentSlotMask(InventorySlots::count - 1)) {
        return std::nullopt;
    }
    int slot = 0;
    while ((mask >>= 1) != 0) {
        ++slot;
    }
    return slot;
}

/** What equipping a candidate in a slot does. */
enum class EquipmentCandidateAction {
    None,
    /** Place the candidate in the empty actual slot. */
    Equip,
    /** Return the occupant of the actual slot to the inventory, then equip. */
    Replace,
    /** Return the main hand, then the off hand of the actual pair, then equip in the main hand. */
    ClearPairAndEquip,
    /** Return the occupant of the requested slot to the inventory. */
    ClearSlot,
    /** Return the main hand and the off hand of its pair to the inventory. */
    ClearMainHandAndOffHand,
    Reject
};

/** Why a candidate is refused. */
enum class EquipmentCandidateReason {
    None,
    NotEquippableInActualSlot,
    OffHandWithoutMainHand,
    /** Item admission, in the order it is checked. */
    NonEquippable,
    Proficiency,
    Alignment,
    Class,
    Race,
    Feat,
    Gender,
    PlayerCharacter,
    Attribute,
    ExcludedForCreature,
    MinimumLevel
};

/**
 * The feedback line for an admission refusal, or none when the refusal is
 * silent.
 */
std::optional<int> equipmentRefusalFeedback(EquipmentCandidateReason reason);

/** Whether an item's own limitations let a creature equip it, whatever the slot. */
EquipmentCandidateReason evaluateEquipmentAdmission(const Creature &creature, const Item &item);

/** Base-item proficiency alone: equipable somewhere, and every required feat held. */
bool hasEquipmentProficiency(const Creature &creature, const Item &item);
/** The level an item asks of the player character: its itemvalue row by cost, counted from 1. */
int minimumEquipLevel(const Creature &creature, const Item &item);

/**
 * Whether a creature may use an item: its alignment, class, race and feat
 * limits (TSL: also gender, player character, attribute and the Bao-Dur
 * exclusions), the player character's minimum level, a weapon no more than
 * one size above its user, and proficiency for anything equippable. The
 * menus check strictly; the use itself is lenient, and in TSL lets a droid-
 * or human-only item past every race limit and a healing kit be used at
 * full vitality, which the menus refuse.
 */
bool canUseItem(const Creature &creature, const Item &item, bool menuCheck);

/** Which usable items a leader-usable test admits: the menus ask for one kind each. */
struct LeaderUsableFlags {
    static constexpr int healing = 1; /**< healing and repair kits; TSL stims too */
    static constexpr int mines = 2;   /**< mine kits */
    static constexpr int self = 4;    /**< self-targeted item spells; KotOR stims too */
    static constexpr int all = healing | mines | self;
};

/**
 * Whether the party leader can use an item from the inventory, as one of the
 * kinds in \p flags. Outside restrict mode (plot items ignore it), a mine kit
 * needs a usable trap and the Demolitions skill; stims and healing kits need a
 * usable charge and, in TSL, someone in the party of the right race; anything
 * else needs a usable self-targeted spell, worn when it is a droid utility,
 * droid shield or forearm band. Each also passes canUseItem, except TSL's
 * stims and kits.
 */
bool isLeaderUsableItem(const Creature &leader, const Item &item, int flags = LeaderUsableFlags::all);

/** Where and how a candidate enters a requested slot. */
struct EquipmentCandidateDecision {
    /** Whether the screens list the candidate for the requested slot. */
    bool visible {false};
    bool valid {false};
    int requestedSlot {-1};
    /** The slot the candidate enters, which a weapon pair may move. */
    int actualSlot {-1};
    /** The other hand of the pair, when the action clears it. */
    int pairedSlot {-1};
    EquipmentCandidateAction action {EquipmentCandidateAction::None};
    EquipmentCandidateReason reason {EquipmentCandidateReason::None};
    /**
     * The requested slot is outside the hands and already held, found once
     * the item's own limitations pass and before the slot is tested.
     */
    bool slotOccupied {false};
};

/** Why an equipment slot cannot be selected. */
enum class EquipmentSlotActivationReason {
    None,
    OffHandBlockedByLargeMainHandWeapon
};

struct EquipmentSlotActivationDecision {
    bool available {true};
    int requestedSlot {-1};
    int pairedSlot {-1};
    EquipmentSlotActivationReason reason {EquipmentSlotActivationReason::None};
};

bool isMainHandWeaponSlot(int slot);
bool isOffHandWeaponSlot(int slot);
int getPairedMainHandSlot(int offHandSlot);
int getPairedOffHandSlot(int mainHandSlot);

bool isOneHandedWeapon(const Item &item);
bool isTwoHandedWeapon(const Item &item);

/**
 * Decide where and how an item enters a requested slot. The item's own
 * limitations come first (see evaluateEquipmentAdmission). Weapon hands follow
 * the pair rules: an empty pair takes the item in its main hand; a two-handed
 * candidate, or one whose ranged kind differs from the main hand, displaces
 * the main hand (and, when both hands are held, the whole pair); a one-handed
 * main hand accepts a matching off-hand. The resulting slot must be listed in
 * the item's equipable slots. A null item requests clearing the slot.
 */
EquipmentCandidateDecision evaluateEquipmentCandidate(
    const Creature &creature,
    int requestedSlot,
    const Item *item);

/** The off-hand selection is closed while the main hand holds a large weapon. */
EquipmentSlotActivationDecision evaluateEquipmentSlotActivation(
    const Creature &creature,
    int requestedSlot);

std::shared_ptr<Item> takeEquipmentCandidate(
    Game &game,
    Object &inventory,
    const std::shared_ptr<Item> &item);

/** True only when the exact Item is owned by the active Area graph. */
bool isActiveAreaOwnedItem(
    Game &game,
    const std::shared_ptr<Item> &item);

/** End exact active-Area ownership without retiring the runtime Item. */
bool releaseAreaOwnedItem(
    Game &game,
    const std::shared_ptr<Item> &item);

/** The outcome of moving an Item to a receiver. */
struct ItemTransfer {
    /** Whether the Item moved, or was the receiver's already. */
    bool moved {false};
    /**
     * The receiver's stack that holds the Item: the Item itself, or the stack
     * it merged into; none when a creature took it as a counted item.
     */
    std::shared_ptr<Item> held;

    explicit operator bool() const { return moved; }
};

/**
 * Move one complete runtime Item from its current nested or Area ownership
 * edge to receiver. An ownerless nonresident Item is not transferable.
 *
 * @return whether it moved, and the receiver's stack that holds it
 */
ItemTransfer transferItemTo(
    Game &game,
    const std::shared_ptr<Item> &item,
    Object &receiver);

} // namespace game

} // namespace reone
