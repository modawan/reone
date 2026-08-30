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

#include <memory>

namespace reone {

namespace game {

class Creature;
class Game;
class Item;
class Object;

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

/**
 * Move one complete runtime Item from its current nested or Area ownership
 * edge to receiver. An ownerless nonresident Item is not transferable.
 */
bool transferItemTo(
    Game &game,
    const std::shared_ptr<Item> &item,
    Object &receiver);

} // namespace game

} // namespace reone
