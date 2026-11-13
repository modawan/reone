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

#include "equipmentrules.h"

namespace reone::game {

/** An ordinary gameplay result, independent of command delivery by a screen. */
enum class EquipmentOperationOutcome {
    Applied,
    Unchanged,
    Rejected,
    Failed
};

/** Queued equipment commands fail for the dead and for party members without vitality. */
bool canRunEquipmentCommand(Game &game, const Creature &subject);
/** Body armour is neither put on nor taken off by a creature in direct combat. */
bool isArmorChangeRefused(const Creature &subject);
/** Report a refused armour change (1506 on, 1507 off) to the controlled creature. */
void reportArmorChangeRefused(Game &game, const Creature &subject, bool removal);

/**
 * Equip an Item held in the subject's inventory, a container it owns, or the
 * shared party inventory. Displaced occupants return to the subject's
 * inventory owner, joining matching stacks, before the candidate is taken,
 * main hand before its pair. A weapon put in a primary hand is drawn unless
 * the change is instant. A hand weapon readies the wearer for combat outside
 * a conversation, unless it is an instant equip into an empty slot.
 */
EquipmentOperationOutcome runEquip(
    Game &game,
    Creature &subject,
    const std::shared_ptr<Item> &item,
    int requestedSlot,
    bool instant);

/**
 * Unequip an Item into an owned container, or into the subject's inventory
 * owner when no container is given; the item joins its stacks there, or
 * not, as Object::addItem does. Removing a main-hand item promotes a weapon
 * held in the paired off-hand slot, which readies the wearer for combat
 * outside a conversation. A primary hand the item leaves, emptied or given
 * the promoted weapon, is drawn unless the unequip is \p instant.
 *
 * @return the Item as it lies after leaving its slot, itself or the stack it
 *         joined; none when it did not leave the slot
 */
std::shared_ptr<Item> runUnequip(
    Game &game,
    Creature &subject,
    const std::shared_ptr<Item> &item,
    const std::shared_ptr<Item> &container,
    ItemStacking stacking,
    bool instant);

/**
 * Chooses the melee weapon, from the inventory and the weapon in the hand,
 * that deals the most damage to \p versus and queues its equip. A ranged
 * weapon in that hand returns to the inventory at once, the off-hand item with
 * a main-hand one; each goes in by name, in its own stack.
 */
void equipMostDamagingMeleeWeapon(
    Game &game,
    Creature &subject,
    const std::shared_ptr<Object> &versus,
    bool offHand);

/**
 * Chooses the ranged weapon with the best attack against \p versus and queues
 * its equip into the main hand. Whenever the main hand holds anything the
 * off-hand item returns to the inventory at once, by name, in its own stack.
 * False when no better ranged weapon than the one held was found.
 */
bool equipMostDamagingRangedWeapon(
    Game &game,
    Creature &subject,
    const std::shared_ptr<Object> &versus);

/**
 * Chooses the body armour with the best armour value, the worn armour or one
 * the creature carries itself, and queues its equip. The worn armour keeps
 * its place on a tie; with none worn, a candidate needs some armour value.
 * The shared party inventory is never searched.
 */
void equipMostEffectiveArmor(Game &game, Creature &subject);

/**
 * Apply a selection using existing equipment and inventory operations.
 * A null item explicitly requests clearing the slot. All non-null objects must
 * be exact live objects in game; item must belong to sourceInventory.
 *
 * Recoverable equip rejection returns the taken candidate to sourceInventory.
 * A previously cleared paired hand stays in that inventory. Exceptions from
 * core split/transfer/effect operations propagate; this is not an atomic rollback
 * boundary over those operations.
 */
EquipmentOperationOutcome applyEquipmentOperation(
    Game &game,
    Creature &subject,
    Object &sourceInventory,
    const std::shared_ptr<Item> &item,
    int requestedSlot);

} // namespace reone::game
