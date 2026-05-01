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

namespace reone {

namespace game {

class Creature;
class Item;

enum class EquipmentCandidateAction {
    None,
    Equip,
    EquipMainHandFromOffHand,
    ClearSlot,
    ClearMainHandAndOffHand,
    EquipAndClearOffHand,
    Reject
};

enum class EquipmentCandidateReason {
    None,
    NotEquippableInRequestedSlot,
    OffHandRequiresMainHand,
    TwoHandedInOffHand,
    WeaponRequiresEmptyPairedSlot,
    IncompatibleWithMainHand,
    IncompatibleWithOffHand,
    MainHandWeaponClearsOffHand
};

struct EquipmentCandidateDecision {
    bool visible {false};
    bool valid {false};
    int requestedSlot {-1};
    int actualSlot {-1};
    int pairedSlot {-1};
    EquipmentCandidateAction action {EquipmentCandidateAction::None};
    EquipmentCandidateReason reason {EquipmentCandidateReason::None};
};

enum class EquipmentSlotActivationReason {
    None,
    OffHandBlockedByMainHandWeapon
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
bool weaponRequiresEmptyPairedSlot(const Item &item);
bool areWeaponsCompatible(const Item &mainHand, const Item &offHand);

EquipmentCandidateDecision evaluateEquipmentCandidate(
    const Creature &creature,
    int requestedSlot,
    const Item *item);

EquipmentSlotActivationDecision evaluateEquipmentSlotActivation(
    const Creature &creature,
    int requestedSlot);

} // namespace game

} // namespace reone
