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

#include "reone/game/equipmentrules.h"

#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/types.h"

namespace reone {

namespace game {

bool isMainHandWeaponSlot(int slot) {
    return slot == InventorySlots::rightWeapon || slot == InventorySlots::rightWeapon2;
}

bool isOffHandWeaponSlot(int slot) {
    return slot == InventorySlots::leftWeapon || slot == InventorySlots::leftWeapon2;
}

int getPairedMainHandSlot(int offHandSlot) {
    return offHandSlot == InventorySlots::leftWeapon2 ? InventorySlots::rightWeapon2 : InventorySlots::rightWeapon;
}

int getPairedOffHandSlot(int mainHandSlot) {
    return mainHandSlot == InventorySlots::rightWeapon2 ? InventorySlots::leftWeapon2 : InventorySlots::leftWeapon;
}

bool isOneHandedWeapon(const Item &item) {
    switch (item.weaponWield()) {
    case WeaponWield::StunBaton:
    case WeaponWield::SingleSword:
    case WeaponWield::BlasterPistol:
        return true;
    default:
        return false;
    }
}

bool isTwoHandedWeapon(const Item &item) {
    switch (item.weaponWield()) {
    case WeaponWield::DoubleBladedSword:
    case WeaponWield::BlasterRifle:
    case WeaponWield::HeavyWeapon:
        return true;
    default:
        return false;
    }
}

bool areWeaponsCompatible(const Item &mainHand, const Item &offHand) {
    return isOneHandedWeapon(mainHand) &&
           isOneHandedWeapon(offHand) &&
           mainHand.weaponType() == offHand.weaponType() &&
           mainHand.weaponType() != WeaponType::None;
}

static int getCandidateEquipSlot(int requestedSlot) {
    switch (requestedSlot) {
    case InventorySlots::rightWeapon2:
        return InventorySlots::rightWeapon;
    case InventorySlots::leftWeapon2:
        return InventorySlots::leftWeapon;
    default:
        return requestedSlot;
    }
}

static int resolveActualEquipSlot(int requestedSlot, const Item &item, const Creature &creature) {
    if (!isOffHandWeaponSlot(requestedSlot))
        return requestedSlot;

    int mainHandSlot = getPairedMainHandSlot(requestedSlot);
    if (creature.getEquippedItem(mainHandSlot) || creature.getEquippedItem(requestedSlot))
        return requestedSlot;

    return isOneHandedWeapon(item) && item.isEquippable(getCandidateEquipSlot(mainHandSlot)) ? mainHandSlot : requestedSlot;
}

EquipmentSlotActivationDecision evaluateEquipmentSlotActivation(
    const Creature &creature,
    int requestedSlot) {

    EquipmentSlotActivationDecision result;
    result.requestedSlot = requestedSlot;

    if (!isOffHandWeaponSlot(requestedSlot))
        return result;

    result.pairedSlot = getPairedMainHandSlot(requestedSlot);
    auto mainHand = creature.getEquippedItem(result.pairedSlot);
    if (mainHand && isTwoHandedWeapon(*mainHand)) {
        result.available = false;
        result.reason = EquipmentSlotActivationReason::OffHandBlockedByTwoHandedMainHand;
    }

    return result;
}

EquipmentCandidateDecision evaluateEquipmentCandidate(
    const Creature &creature,
    int requestedSlot,
    const Item *item) {

    EquipmentCandidateDecision result;
    result.requestedSlot = requestedSlot;
    result.actualSlot = requestedSlot;

    if (!item) {
        result.visible = true;
        result.valid = true;
        if (isMainHandWeaponSlot(requestedSlot)) {
            result.pairedSlot = getPairedOffHandSlot(requestedSlot);
            result.action = EquipmentCandidateAction::ClearMainHandAndOffHand;
        } else {
            result.action = EquipmentCandidateAction::ClearSlot;
        }
        return result;
    }

    if (!item->isEquippable(getCandidateEquipSlot(requestedSlot))) {
        result.action = EquipmentCandidateAction::Reject;
        result.reason = EquipmentCandidateReason::NotEquippableInRequestedSlot;
        return result;
    }

    result.visible = true;
    result.actualSlot = resolveActualEquipSlot(requestedSlot, *item, creature);
    result.valid = true;
    result.action = result.actualSlot != requestedSlot ? EquipmentCandidateAction::EquipMainHandFromOffHand : EquipmentCandidateAction::Equip;

    if (isOffHandWeaponSlot(result.actualSlot)) {
        if (isTwoHandedWeapon(*item)) {
            result.valid = false;
            result.action = EquipmentCandidateAction::Reject;
            result.reason = EquipmentCandidateReason::TwoHandedInOffHand;
            return result;
        }

        result.pairedSlot = getPairedMainHandSlot(result.actualSlot);
        auto mainHand = creature.getEquippedItem(result.pairedSlot);
        if (!mainHand) {
            result.valid = false;
            result.action = EquipmentCandidateAction::Reject;
            result.reason = EquipmentCandidateReason::OffHandRequiresMainHand;
            return result;
        }

        if (isTwoHandedWeapon(*mainHand)) {
            result.valid = false;
            result.action = EquipmentCandidateAction::Reject;
            result.reason = EquipmentCandidateReason::IncompatibleWithMainHand;
            return result;
        }

        if (!areWeaponsCompatible(*mainHand, *item)) {
            result.valid = false;
            result.action = EquipmentCandidateAction::Reject;
            result.reason = EquipmentCandidateReason::IncompatibleWithMainHand;
        }
        return result;
    }

    if (isMainHandWeaponSlot(result.actualSlot)) {
        result.pairedSlot = getPairedOffHandSlot(result.actualSlot);
        auto offHand = creature.getEquippedItem(result.pairedSlot);
        if (!offHand)
            return result;

        if (isTwoHandedWeapon(*item)) {
            result.action = EquipmentCandidateAction::EquipAndClearOffHand;
            result.reason = EquipmentCandidateReason::MainHandTwoHandedClearsOffHand;
            return result;
        }

        if (!areWeaponsCompatible(*item, *offHand)) {
            result.valid = false;
            result.action = EquipmentCandidateAction::Reject;
            result.reason = EquipmentCandidateReason::IncompatibleWithOffHand;
        }
    }

    return result;
}

} // namespace game

} // namespace reone
