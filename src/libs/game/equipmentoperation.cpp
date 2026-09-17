/*
 * Copyright (c) 2020-2026 The reone project contributors
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

#include "reone/game/equipmentoperation.h"

#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"

namespace reone::game {

EquipmentOperationOutcome applyEquipmentOperation(
    Game &game,
    Creature &subject,
    Object &sourceInventory,
    const std::shared_ptr<Item> &item,
    int requestedSlot) {
    auto live = [&game](const Object &object) {
        return object.isRuntimeLive() &&
               game.getObjectById(object.id()).get() == &object;
    };
    if (!live(subject) || !live(sourceInventory))
        return EquipmentOperationOutcome::Rejected;

    // Accept only the slots exposed by the ordinary equipment screen.
    switch (requestedSlot) {
    case InventorySlots::implant:
    case InventorySlots::head:
    case InventorySlots::hands:
    case InventorySlots::leftArm:
    case InventorySlots::body:
    case InventorySlots::rightArm:
    case InventorySlots::leftWeapon:
    case InventorySlots::belt:
    case InventorySlots::rightWeapon:
    case InventorySlots::leftWeapon2:
    case InventorySlots::rightWeapon2:
        break;
    default:
        return EquipmentOperationOutcome::Rejected;
    }
    if (item && (!live(*item) || item->isEquipped() ||
                 item->owner() != sourceInventory.id() ||
                 std::find(sourceInventory.items().begin(), sourceInventory.items().end(), item) == sourceInventory.items().end()))
        return EquipmentOperationOutcome::Rejected;

    auto decision = evaluateEquipmentCandidate(subject, requestedSlot, item.get());
    if (!decision.valid)
        return EquipmentOperationOutcome::Rejected;
    auto equipped = subject.getEquippedItem(decision.actualSlot);
    bool clearPaired = decision.action == EquipmentCandidateAction::ClearMainHandAndOffHand ||
                       decision.action == EquipmentCandidateAction::EquipAndClearOffHand;
    auto paired = clearPaired ? subject.getEquippedItem(decision.pairedSlot) : nullptr;
    if ((equipped && !live(*equipped)) || (paired && !live(*paired)))
        return EquipmentOperationOutcome::Rejected;
    if (equipped == item && !paired)
        return EquipmentOperationOutcome::Unchanged;

    if (item) {
        auto candidate = takeEquipmentCandidate(game, sourceInventory, item);
        if (!candidate)
            return EquipmentOperationOutcome::Failed;
        if (paired && !subject.moveEquippedItemTo(paired, sourceInventory)) {
            sourceInventory.addItem(candidate);
            return EquipmentOperationOutcome::Failed;
        }
        bool applied = equipped
                           ? subject.replaceEquipment(decision.actualSlot, candidate, sourceInventory)
                           : subject.equip(decision.actualSlot, candidate);
        if (!applied) {
            sourceInventory.addItem(candidate);
            return EquipmentOperationOutcome::Failed;
        }
    } else {
        if (equipped && !subject.moveEquippedItemTo(equipped, sourceInventory))
            return EquipmentOperationOutcome::Failed;
        if (paired && !subject.moveEquippedItemTo(paired, sourceInventory))
            return EquipmentOperationOutcome::Failed;
    }
    return EquipmentOperationOutcome::Applied;
}

} // namespace reone::game
