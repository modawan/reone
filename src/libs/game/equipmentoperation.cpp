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

#include <algorithm>
#include <functional>
#include <map>
#include <vector>

#include "reone/game/action/equipitem.h"
#include "reone/game/attack.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/party.h"

namespace reone::game {

static bool isLiveObject(Game &game, const Object &object) {
    return object.isRuntimeLive() && game.getObjectById(object.id()).get() == &object;
}

// A party member, or a companion on the party's roster.
static bool isPartyOrRosterCreature(Game &game, const Object &object) {
    if (game.party().isMember(object)) {
        return true;
    }
    auto *creature = dyn_cast<Creature>(&object);
    auto identity = creature ? game.party().rosterIdentity(*creature) : std::nullopt;
    return identity && identity->kind == RosterKind::Npc;
}

static std::shared_ptr<Object> inventoryOwnerOf(Game &game, Creature &subject) {
    return game.party().sharedInventoryReceiver(game.getObjectById(subject.id()));
}

static constexpr int kSlotOccupiedStrRef = 1478;
static constexpr int kArmorChangeInCombatStrRef = 1506;
static constexpr int kArmorRemovalInCombatStrRef = 1507;

bool isArmorChangeRefused(const Creature &subject) {
    return subject.isInCombat() && subject.combatActivationType() == CombatActivation::Direct;
}

void reportArmorChangeRefused(Game &game, const Creature &subject, bool removal) {
    if (game.party().getLeader().get() != &subject) {
        return;
    }
    game.addFeedbackMessage(removal ? kArmorRemovalInCombatStrRef : kArmorChangeInCombatStrRef);
}

bool canRunEquipmentCommand(Game &game, const Creature &subject) {
    return !subject.isDead() && (!game.party().isMember(subject) || subject.currentHitPoints() > 0);
}

// A primary hand whose weapon changes, filled or emptied, is drawn unless the
// change is instant.
static void presentHandChange(Creature &subject, int slot, bool instant) {
    if (instant || (slot != InventorySlots::rightWeapon && slot != InventorySlots::leftWeapon)) {
        return;
    }
    subject.playWeaponDraw();
}

// A new hand configuration ends a combat mode that needs a different one.
static void dropMismatchedCombatMode(Creature &subject) {
    auto rightHand = subject.getEquippedItem(InventorySlots::rightWeapon);
    auto leftHand = subject.getEquippedItem(InventorySlots::leftWeapon);
    const bool rangedRightHand = rightHand && rightHand->isRanged();
    const int mode = subject.combatMode();
    bool drop = false;
    if (mode >= 1 && mode <= 3) {
        drop = rangedRightHand;
    } else if (mode == 5) {
        drop = rightHand || leftHand;
    } else if (mode == 6) {
        drop = !rangedRightHand;
    }
    if (drop) {
        subject.setCombatMode(0);
    }
}

// One ordered transaction serves queued commands, round dispatch and screens.
// Occupants leave for the inventory before the candidate is taken, main hand
// before its pair.
static EquipmentOperationOutcome equipFrom(
    Game &game, Creature &subject, Object &repository, Object &inventory,
    const std::shared_ptr<Item> &item, int requestedSlot, bool instant) {
    // Refusals and replacements are reported to the controlled creature only.
    const bool controlled = game.party().getLeader().get() == &subject;
    // The player character must reach the item's level before anything else.
    if (subject.isPC() && subject.attributes().getAggregateLevel() < minimumEquipLevel(subject, *item)) {
        if (controlled) {
            game.addFeedbackMessage(*equipmentRefusalFeedback(EquipmentCandidateReason::MinimumLevel));
        }
        return EquipmentOperationOutcome::Rejected;
    }
    auto decision = evaluateEquipmentCandidate(subject, requestedSlot, item.get());
    if (!decision.valid) {
        if (auto strref = equipmentRefusalFeedback(decision.reason); strref && controlled) {
            game.addFeedbackMessage(*strref);
        }
        // A held slot outside the hands is reported even when it cannot take the item.
        if (decision.slotOccupied && controlled) {
            game.addFeedbackMessage(kSlotOccupiedStrRef);
        }
        return EquipmentOperationOutcome::Rejected;
    }
    auto occupant = subject.getEquippedItem(decision.actualSlot);
    if (occupant == item) {
        return EquipmentOperationOutcome::Unchanged;
    }
    if (item->isEquipped() ||
        std::find(repository.items().begin(), repository.items().end(), item) == repository.items().end())
        return EquipmentOperationOutcome::Rejected;
    std::shared_ptr<Item> paired;
    switch (decision.action) {
    case EquipmentCandidateAction::Equip:
        break;
    case EquipmentCandidateAction::Replace:
        if (controlled && decision.slotOccupied) {
            game.addFeedbackMessage(kSlotOccupiedStrRef);
        }
        // An occupant the candidate would stack with is not exchanged for it.
        if (occupant->isStackCompatibleWith(*item)) {
            return EquipmentOperationOutcome::Rejected;
        }
        break;
    case EquipmentCandidateAction::ClearPairAndEquip:
        paired = subject.getEquippedItem(decision.pairedSlot);
        break;
    default:
        return EquipmentOperationOutcome::Rejected;
    }
    if ((occupant && !isLiveObject(game, *occupant)) || (paired && !isLiveObject(game, *paired))) {
        return EquipmentOperationOutcome::Rejected;
    }
    if (occupant && !subject.moveEquippedItemTo(occupant, inventory)) {
        return EquipmentOperationOutcome::Failed;
    }
    if (occupant && decision.actualSlot == InventorySlots::body) {
        subject.updateArmourAppearance(nullptr);
    }
    if (paired && !subject.moveEquippedItemTo(paired, inventory)) {
        return EquipmentOperationOutcome::Failed;
    }
    auto candidate = takeEquipmentCandidate(game, repository, item);
    if (!candidate) {
        return EquipmentOperationOutcome::Failed;
    }
    if (!subject.equip(decision.actualSlot, candidate)) {
        repository.addItem(candidate);
        return EquipmentOperationOutcome::Failed;
    }
    presentHandChange(subject, decision.actualSlot, instant);
    dropMismatchedCombatMode(subject);
    game.finishEquip(subject, decision.actualSlot, candidate);
    return EquipmentOperationOutcome::Applied;
}

// Removing a main-hand item promotes a weapon held in the paired off-hand slot.
// A hand left with another weapon, or with none, is drawn anew.
static EquipmentOperationOutcome unequipInto(
    Game &game, Creature &subject, const std::shared_ptr<Item> &item, Object &repository, bool instant) {
    if (!item->isEquipped() || item->owner() != subject.id()) {
        return EquipmentOperationOutcome::Rejected;
    }
    int slot = -1;
    for (const auto &[equippedSlot, equipped] : subject.equipment())
        if (equipped == item) {
            slot = equippedSlot;
        }
    int promotedFrom = -1;
    if (slot == InventorySlots::rightWeapon) {
        promotedFrom = InventorySlots::leftWeapon;
    } else if (slot == InventorySlots::rightWeapon2 && game.isTSL()) {
        promotedFrom = InventorySlots::leftWeapon2;
    }
    auto promoted = promotedFrom >= 0 ? subject.getEquippedItem(promotedFrom) : nullptr;
    if (promoted && promoted->weaponType() == WeaponType::None) {
        promoted = nullptr;
    }

    if (!subject.moveEquippedItemTo(item, repository)) {
        return EquipmentOperationOutcome::Failed;
    }
    if (slot == InventorySlots::body) {
        subject.updateArmourAppearance(nullptr);
    }
    if (promoted) {
        auto moved = subject.takeEquippedItem(promoted);
        if (!moved || !subject.equip(slot, moved)) {
            return EquipmentOperationOutcome::Failed;
        }
        // The promoted weapon counts as newly equipped in the main hand.
        game.finishEquip(subject, slot, moved);
    }
    presentHandChange(subject, slot, instant);
    return EquipmentOperationOutcome::Applied;
}

EquipmentOperationOutcome runEquip(
    Game &game,
    Creature &subject,
    const std::shared_ptr<Item> &item,
    int requestedSlot,
    bool instant) {
    if (!isLiveObject(game, subject) || !item || !isLiveObject(game, *item)) {
        return EquipmentOperationOutcome::Rejected;
    }
    auto inventoryOwner = inventoryOwnerOf(game, subject);
    if (!inventoryOwner) {
        return EquipmentOperationOutcome::Rejected;
    }
    // The candidate comes from the subject's own inventory, the shared inventory
    // it draws from, or a container the subject owns. A party or roster
    // character also takes what another one carries.
    std::shared_ptr<Object> repository;
    const uint32_t ownerId = item->owner();
    auto holder = game.getObjectById(ownerId);
    if (ownerId == subject.id() || ownerId == inventoryOwner->id()) {
        repository = holder;
    } else if (auto container = std::dynamic_pointer_cast<Item>(holder);
               container && container->owner() == subject.id()) {
        repository = container;
    } else if (holder && isPartyOrRosterCreature(game, subject) && isPartyOrRosterCreature(game, *holder)) {
        repository = holder;
    }
    if (!repository) {
        return EquipmentOperationOutcome::Rejected;
    }
    return equipFrom(game, subject, *repository, *inventoryOwner, item, requestedSlot, instant);
}

EquipmentOperationOutcome runUnequip(
    Game &game,
    Creature &subject,
    const std::shared_ptr<Item> &item,
    const std::shared_ptr<Item> &container,
    bool instant) {
    if (!isLiveObject(game, subject) || !item) {
        return EquipmentOperationOutcome::Rejected;
    }
    std::shared_ptr<Object> repository;
    if (container) {
        if (!isLiveObject(game, *container) || container->owner() != subject.id() || container == item) {
            return EquipmentOperationOutcome::Rejected;
        }
        repository = container;
    } else {
        repository = inventoryOwnerOf(game, subject);
    }
    if (!repository) {
        return EquipmentOperationOutcome::Rejected;
    }
    return unequipInto(game, subject, item, *repository, instant);
}

// The weapons of the inventory, then the one taken from the hand being chosen
// for, which so comes last among equals.
static std::vector<std::shared_ptr<Item>> weaponCandidates(const Object &inventory, const std::shared_ptr<Item> &held) {
    std::vector<std::shared_ptr<Item>> result;
    for (const auto &item : inventory.items())
        if (item->weaponType() != WeaponType::None) {
            result.push_back(item);
        }
    if (held && held->weaponType() != WeaponType::None) {
        result.push_back(held);
    }
    return result;
}

// A candidate is admitted and measured against the given equipment, standing
// for the hands as they are while the choice is made. The chosen weapon is
// equipped through the ordinary queued equip.
static std::optional<EquipmentCandidateDecision> admitWeaponCandidate(
    Creature &subject, const std::map<int, std::shared_ptr<Item>> &equipment,
    int slot, const std::shared_ptr<Item> &candidate) {
    EquipmentCandidateDecision decision;
    subject.evaluateWithEquipment(equipment, [&]() {
        decision = evaluateEquipmentCandidate(subject, slot, candidate.get());
    });
    if (!decision.valid) {
        return std::nullopt;
    }
    return decision;
}

static int measureWithWeapon(
    Creature &subject, std::map<int, std::shared_ptr<Item>> equipment,
    int slot, const std::shared_ptr<Item> &candidate, const std::function<int()> &measure) {
    equipment[slot] = candidate;
    int result = 0;
    subject.evaluateWithEquipment(std::move(equipment), [&]() { result = measure(); });
    return result;
}

static void chooseMeleeWeapon(
    Game &game, Creature &subject, const std::shared_ptr<Object> &versus, bool offHand, bool retry) {
    auto inventory = inventoryOwnerOf(game, subject);
    if (!inventory) {
        return;
    }
    // The main-hand choice of a creature fighting with creature weapons is for
    // its left creature weapon.
    int slot = InventorySlots::leftWeapon;
    if (!offHand) {
        const bool handsEmpty = !subject.getEquippedItem(InventorySlots::rightWeapon) &&
                                !subject.getEquippedItem(InventorySlots::leftWeapon);
        slot = handsEmpty && subject.getEquippedItem(InventorySlots::cWeaponL)
                   ? InventorySlots::cWeaponL
                   : InventorySlots::rightWeapon;
    }
    auto held = subject.getEquippedItem(slot);
    if (held && held->isRanged()) {
        subject.moveEquippedItemTo(held, *inventory);
        if (slot == InventorySlots::rightWeapon) {
            if (auto offHandItem = subject.getEquippedItem(InventorySlots::leftWeapon)) {
                subject.moveEquippedItemTo(offHandItem, *inventory);
            }
        }
        held.reset();
    }
    // Beside a double-bladed weapon the off-hand choice is made for the main hand.
    if (offHand && !held) {
        auto mainHand = subject.getEquippedItem(InventorySlots::rightWeapon);
        if (mainHand && mainHand->weaponWield() == WeaponWield::DoubleBladedSword) {
            held = mainHand;
            slot = InventorySlots::rightWeapon;
        }
    }
    auto equipment = subject.equipment();
    if (held) {
        equipment.erase(slot);
    }

    // Elemental damage counts against a creature that is not plot.
    auto *versusCreature = versus ? dyn_cast<Creature>(versus.get()) : nullptr;
    if (versusCreature && versusCreature->plotFlag()) {
        versusCreature = nullptr;
    }
    std::optional<std::reference_wrapper<Object>> versusObject;
    if (versus) {
        versusObject = *versus;
    }
    std::shared_ptr<Item> choice;
    int best = 0;
    for (const auto &candidate : weaponCandidates(*inventory, held)) {
        const auto decision = admitWeaponCandidate(subject, equipment, slot, candidate);
        if (!decision || candidate->isRanged()) {
            continue;
        }
        // The off hand takes only an empty-slot equip of a weapon no larger
        // than the creature and at most two sizes smaller.
        if (offHand) {
            const auto relativeSize = subject.getRelativeWeaponSize(*candidate);
            if (decision->action != EquipmentCandidateAction::Equip || !relativeSize || *relativeSize > 0) {
                continue;
            }
        }
        const int damage = measureWithWeapon(subject, equipment, decision->actualSlot, candidate, [&]() {
            return subject.getMaximumWeaponDamage(versusObject, offHand) +
                   (versusCreature ? subject.getMaximumElementalDamageBonus(*versusCreature) : 0);
        });
        // Measuring a weapon counts as putting it on.
        game.finishEquip(subject, decision->actualSlot, candidate);
        if (damage >= best) {
            best = damage;
            choice = candidate;
        }
    }
    // The held weapon goes back on after the measuring.
    if (held) {
        game.finishEquip(subject, slot, held);
    }
    if (choice) {
        if (choice != held) {
            subject.addAction(game.newAction<EquipItemAction>(choice, slot, 0));
        }
        return;
    }
    // When nothing held is worth using against the target, the choice is made
    // once more without one.
    if (held && versus && !retry) {
        chooseMeleeWeapon(game, subject, nullptr, offHand, true);
    }
}

void equipMostDamagingMeleeWeapon(
    Game &game,
    Creature &subject,
    const std::shared_ptr<Object> &versus,
    bool offHand) {
    chooseMeleeWeapon(game, subject, versus, offHand, false);
}

// The attack score any ranged weapon beats when none is held.
static constexpr int kUnheldRangedAttackScore = -30;

bool equipMostDamagingRangedWeapon(
    Game &game,
    Creature &subject,
    const std::shared_ptr<Object> &versus) {
    auto inventory = inventoryOwnerOf(game, subject);
    if (!inventory) {
        return false;
    }
    const auto versusCreature = Creature::asCreature(versus);
    auto held = subject.getEquippedItem(InventorySlots::rightWeapon);
    std::shared_ptr<Item> choice;
    int best = kUnheldRangedAttackScore;
    // A held ranged weapon is scored as it is held now and is kept unless
    // another scores higher.
    if (held && held->isRanged()) {
        best = subject.getAttackBonusBreakdown(versusCreature, held, false).total();
        choice = held;
    }
    if (held) {
        if (auto offHandItem = subject.getEquippedItem(InventorySlots::leftWeapon)) {
            subject.moveEquippedItemTo(offHandItem, *inventory);
        }
    }
    auto equipment = subject.equipment();
    equipment.erase(InventorySlots::rightWeapon);
    for (const auto &candidate : weaponCandidates(*inventory, held)) {
        if (!candidate->isRanged()) {
            continue;
        }
        const auto decision = admitWeaponCandidate(subject, equipment, InventorySlots::rightWeapon, candidate);
        if (!decision) {
            continue;
        }
        const int attack = measureWithWeapon(subject, equipment, decision->actualSlot, candidate, [&]() {
            return subject.getAttackBonusBreakdown(versusCreature, candidate, false).total();
        });
        // Measuring a weapon counts as putting it on.
        game.finishEquip(subject, decision->actualSlot, candidate);
        if (attack > best) {
            best = attack;
            choice = candidate;
        }
    }
    // The held weapon goes back on after the measuring.
    if (held) {
        game.finishEquip(subject, InventorySlots::rightWeapon, held);
    }
    if (!choice || choice == held) {
        return false;
    }
    subject.addAction(game.newAction<EquipItemAction>(choice, InventorySlots::rightWeapon, 0));
    return true;
}

void equipMostEffectiveArmor(Game &game, Creature &subject) {
    // An armour class bonus counts only when its upgrade is installed.
    auto worn = subject.getEquippedItem(InventorySlots::body);
    std::shared_ptr<Item> choice = worn;
    int best = worn ? worn->armorValue(true) : 0;
    // A party member's own items live in the shared inventory, which is not
    // searched.
    if (!game.party().isMember(subject)) {
        for (const auto &candidate : subject.items()) {
            // Armour, robes and clothing.
            const int type = candidate->itemType();
            if (type < Item::kArmorClass4ItemType || type > Item::kLastBodyItemType) {
                continue;
            }
            if (!evaluateEquipmentCandidate(subject, InventorySlots::body, candidate.get()).valid) {
                continue;
            }
            const int value = candidate->armorValue(true);
            if (value > best) {
                best = value;
                choice = candidate;
            }
        }
    }
    if (!choice || choice == worn) {
        return;
    }
    subject.addAction(game.newAction<EquipItemAction>(choice, InventorySlots::body, 0));
}

EquipmentOperationOutcome applyEquipmentOperation(
    Game &game,
    Creature &subject,
    Object &sourceInventory,
    const std::shared_ptr<Item> &item,
    int requestedSlot) {
    if (!isLiveObject(game, subject) || !isLiveObject(game, sourceInventory)) {
        return EquipmentOperationOutcome::Rejected;
    }

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
    if (item) {
        if (!isLiveObject(game, *item) || item->isEquipped() || item->owner() != sourceInventory.id() ||
            std::find(sourceInventory.items().begin(), sourceInventory.items().end(), item) == sourceInventory.items().end())
            return EquipmentOperationOutcome::Rejected;
        if (requestedSlot == InventorySlots::body && isArmorChangeRefused(subject)) {
            reportArmorChangeRefused(game, subject, false);
            return EquipmentOperationOutcome::Rejected;
        }
        return equipFrom(game, subject, sourceInventory, sourceInventory, item, requestedSlot, false);
    }

    auto decision = evaluateEquipmentCandidate(subject, requestedSlot, nullptr);
    auto equipped = subject.getEquippedItem(decision.actualSlot);
    if (equipped && requestedSlot == InventorySlots::body && isArmorChangeRefused(subject)) {
        reportArmorChangeRefused(game, subject, true);
        return EquipmentOperationOutcome::Rejected;
    }
    auto paired = decision.action == EquipmentCandidateAction::ClearMainHandAndOffHand
                      ? subject.getEquippedItem(decision.pairedSlot)
                      : nullptr;
    if (!equipped && !paired) {
        return EquipmentOperationOutcome::Unchanged;
    }
    if ((equipped && !isLiveObject(game, *equipped)) || (paired && !isLiveObject(game, *paired))) {
        return EquipmentOperationOutcome::Rejected;
    }
    // Clearing both hands releases the off-hand first so it is not promoted.
    for (const auto &clear : {paired, equipped}) {
        if (!clear) {
            continue;
        }
        const auto outcome = unequipInto(game, subject, clear, sourceInventory, false);
        if (outcome != EquipmentOperationOutcome::Applied) {
            return outcome;
        }
    }
    return EquipmentOperationOutcome::Applied;
}

} // namespace reone::game
