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

#include <algorithm>

#include "reone/game/action/equipitem.h"
#include "reone/game/action/giveitem.h"
#include "reone/game/action/switchweapons.h"
#include "reone/game/action/takeitem.h"
#include "reone/game/action/unequipitem.h"
#include "reone/game/combat.h"
#include "reone/game/equipmentoperation.h"
#include "reone/game/equipmentrules.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/party.h"
#include "reone/game/statussummary.h"

namespace reone {

namespace game {

// Beyond this squared distance from the receiver's use point the giver runs.
static constexpr float kGiveRunDistance2 = 25.0f;

// A creature gives from within use range of the receiver: out of it, it goes
// to the receiver's use point, running when it sets off more than five metres
// from it, and turns to that point. Credits it gives come out of its credits,
// and a pazaak card out of the party's collection. A creature or placeable
// receives the item (a creature counts credits and the other counted items it
// acquires); an item received by a party member is reported in the status
// summary.
void GiveItemAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto &party = _game.party();
    if (auto *giver = dyn_cast<Creature>(&actor)) {
        if (!_run && !giver->isInUseRange(*_giveTo, 0.0f)) {
            const glm::vec3 offset {giver->position() - giver->useRange(*_giveTo).point};
            _run = glm::dot(offset, offset) > kGiveRunDistance2;
        }
        if (_run) {
            if (!giver->navigateToUse(*_giveTo, 0.0f, dt, *_run)) {
                return;
            }
            giver->turnToward(giver->useRange(*_giveTo).point);
        }
        if (_item->itemType() == Item::kCreditsItemType) {
            party.removeCreatureGold(*giver, _item->stackSize());
        } else if (_item->itemType() == Item::kPazaakCardItemType) {
            // The card a pazaak card item stands for is its model variation,
            // counted from one.
            party.removePazaakCards(_item->modelVariation() - 1, _item->stackSize());
        }
    }
    switch (_giveTo->type()) {
    case ObjectType::Placeable:
        transferItemTo(_game, _item, *_giveTo);
        break;
    case ObjectType::Creature: {
        const std::string name = _item->localizedName();
        transferItemTo(_game, _item, *party.sharedInventoryReceiver(_giveTo));
        if (party.isMember(*_giveTo)) {
            _game.submitStatusSummary(StatusSummaryCategory::ItemsReceived, 0, {name});
        }
        break;
    }
    default:
        break;
    }
    complete();
}

// A creature or placeable takes the item. A creature that takes it from a
// party member reports the item lost in the status summary.
void TakeItemAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto &party = _game.party();
    switch (actor.type()) {
    case ObjectType::Placeable:
        transferItemTo(_game, _item, actor);
        break;
    case ObjectType::Creature:
        if (transferItemTo(_game, _item, *party.sharedInventoryReceiver(_game.getObjectById(actor.id()))) &&
            party.isMember(*_takeFrom)) {
            _game.submitStatusSummary(StatusSummaryCategory::ItemsLost);
        }
        break;
    default:
        break;
    }
    complete();
}

void EquipItemAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    if (auto *creature = dyn_cast<Creature>(&actor); creature && canRunEquipmentCommand(_game, *creature)) {
        runEquip(_game, *creature, _item, _inventorySlot, _instant != 0);
    }
    complete();
}

std::optional<SavedActionRecord> EquipItemAction::saveFacingState() const {
    const auto slot = equipmentSlotMask(_inventorySlot);
    if (!_item || !slot) {
        return std::nullopt;
    }
    // The item, the slot mask and the instant flag.
    SavedActionRecord result;
    result.actionId = static_cast<uint32_t>(SavedActionType::EquipItem);
    result.declaredParameterCount = 3;
    result.parameters = {
        {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(_item->id())},
        {kSavedIntegerParameter, static_cast<int32_t>(slot)},
        {kSavedIntegerParameter, _instant},
    };
    return result;
}

void UnequipItemAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    std::shared_ptr<Item> container;
    if (!_containerOverwritten && _containerId != kSavedRuntimeInvalidObjectId) {
        container = std::dynamic_pointer_cast<Item>(_game.getObjectById(_containerId));
    }
    // A container parameter that names no item takes nothing off.
    const bool containerNamed =
        !_containerOverwritten && (_containerId == kSavedRuntimeInvalidObjectId || container);
    if (auto *creature = dyn_cast<Creature>(&actor);
        creature && containerNamed && canRunEquipmentCommand(_game, *creature))
        runUnequip(_game, *creature, _item, container, _instant != 0);
    complete();
}

void UnequipItemAction::overwriteContainer(int32_t instant) {
    // The old container no longer holds the action.
    if (!_containerOverwritten) {
        if (auto previous = _game.getObjectById(_containerId)) {
            _runtimeDependencies.erase(std::remove_if(_runtimeDependencies.begin(), _runtimeDependencies.end(),
                                                      [&](const auto &ref) { return ref.resolve().get() == previous.get(); }),
                                       _runtimeDependencies.end());
        }
    }
    _containerId = static_cast<uint32_t>(instant);
    _containerOverwritten = true;
}

std::optional<SavedActionRecord> UnequipItemAction::saveFacingState() const {
    if (!_item) {
        return std::nullopt;
    }
    // The item, the container it goes to and the instant flag.
    SavedActionRecord result;
    result.actionId = static_cast<uint32_t>(SavedActionType::UnequipItem);
    result.declaredParameterCount = 3;
    result.parameters = {
        {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(_item->id())},
        {kSavedObjectParameter, _containerOverwritten ? SavedObjectReference::rawValue(_containerId)
                                                      : SavedObjectReference::fromRuntimeId(_containerId)},
        {kSavedIntegerParameter, _instant},
    };
    // TSL's unequip carries two more integers, always zero.
    if (_game.isTSL()) {
        result.declaredParameterCount = 5;
        result.parameters.push_back({kSavedIntegerParameter, int32_t {0}});
        result.parameters.push_back({kSavedIntegerParameter, int32_t {0}});
    }
    return result;
}

void SwitchWeaponsAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    complete();
    auto *creature = dyn_cast<Creature>(&actor);
    if (!creature) {
        actor.clearAllActions(true);
        return;
    }
    swapWeaponSets(_game, *creature);
}

void swapWeaponSets(Game &game, Creature &creature) {
    const auto right = creature.getEquippedItem(InventorySlots::rightWeapon);
    const auto left = creature.getEquippedItem(InventorySlots::leftWeapon);
    const auto alternateRight = creature.getEquippedItem(InventorySlots::rightWeapon2);
    const auto alternateLeft = creature.getEquippedItem(InventorySlots::leftWeapon2);
    // The swap clears the actions and drops the round's pending entries.
    creature.clearAllActions(true);
    game.combat().discardEquipment(creature);

    // Off-hands leave before their main hands so neither is promoted, then each
    // set moves into the other pair.
    for (const auto &item : {alternateLeft, alternateRight, left, right})
        if (item) {
            runUnequip(game, creature, item, nullptr, false);
        }
    if (alternateRight) {
        runEquip(game, creature, alternateRight, InventorySlots::rightWeapon, false);
    }
    if (alternateLeft) {
        runEquip(game, creature, alternateLeft, InventorySlots::leftWeapon, false);
    }
    if (right) {
        runEquip(game, creature, right, InventorySlots::rightWeapon2, false);
    }
    if (left) {
        runEquip(game, creature, left, InventorySlots::leftWeapon2, false);
    }

    // A directly engaged combatant resumes the target of its last round with
    // an attack entry on its round.
    if (!creature.isInCombat() || creature.combatActivationType() != CombatActivation::Direct) {
        return;
    }
    if (auto target = creature.getRoundTarget()) {
        game.combat().scheduleAttack(creature, target);
    }
}

bool SwitchWeaponsAction::cancel(std::shared_ptr<Action>, Object &actor) {
    // A swap cleared before it runs gives the pose back to the creature's
    // state; a one-shot still running plays on.
    if (auto *creature = dyn_cast<Creature>(&actor)) {
        creature->refreshPauseAnimation();
    }
    return true;
}

std::optional<SavedActionRecord> SwitchWeaponsAction::saveFacingState() const {
    SavedActionRecord result;
    result.actionId = static_cast<uint32_t>(SavedActionType::SwitchWeapons);
    return result;
}

} // namespace game

} // namespace reone
