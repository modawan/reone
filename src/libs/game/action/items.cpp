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
#include "reone/game/action/pickupitem.h"
#include "reone/game/action/putdownitem.h"
#include "reone/game/action/switchweapons.h"
#include "reone/game/action/takeitem.h"
#include "reone/game/action/unequipitem.h"
#include "reone/game/combat.h"
#include "reone/game/equipmentoperation.h"
#include "reone/game/equipmentrules.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/placeable.h"
#include "reone/game/party.h"
#include "reone/game/statussummary.h"

namespace reone {

namespace game {

// Beyond this squared distance from the receiver's use point the giver runs.
static constexpr float kGiveRunDistance2 = 25.0f;

// The giver must hold the item, or it and the item's holder must both be in
// the party, and it must take commands.
bool GiveItemAction::queue(Game &game, Object &giver, std::shared_ptr<Item> item, std::shared_ptr<Object> giveTo, int count, bool givenByScript) {
    if (!giver.isCommandable()) {
        return false;
    }
    auto &party = game.party();
    auto possessor = game.getObjectById<Creature>(item->owner());
    const bool partyItem = possessor && isa<Creature>(giver) && party.isMember(*possessor) && party.isMember(giver);
    if (item->owner() != giver.id() && !partyItem) {
        return false;
    }
    count = std::min(count, item->stackSize());
    giver.addAction(game.newAction<GiveItemAction>(std::move(item), std::move(giveTo), count, givenByScript));
    return true;
}

// A creature gives from within use range of the receiver: out of it, it goes
// to the receiver's use point, running when it sets off more than five metres
// from it, and turns to that point. Credits it gives come out of its credits,
// and a pazaak card out of the party's collection. A count short of the stack
// is split off it; otherwise the stack moves. A creature or placeable
// receives the item (a creature counts credits and the other counted items it
// acquires). A party member that receives an item a script gave reports it in
// the status summary. A placeable that receives one, unless it is destroyed
// once empty, runs its inventory disturbed script for the stack that holds
// it, disturbed by the item's holder before the give. A creature that
// receives one runs its disturbed script the same way, unless it gave the
// item itself.
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
    // The item's holder before the give disturbs the receiver: the creature
    // holding the item, or holding the item it is inside.
    uint32_t disturber = _item->owner();
    if (auto holder = _game.getObjectById<Item>(disturber)) {
        if (_game.getObjectById<Creature>(holder->owner())) {
            disturber = holder->owner();
        }
    }
    std::shared_ptr<Item> split;
    if (_count > 0 && _count < _item->stackSize()) {
        split = _game.newItemClone(*_item);
        split->setStackSize(_count);
        _item->setStackSize(_item->stackSize() - _count);
    }
    const auto deliver = [&](Object &receiver) -> ItemTransfer {
        if (split) {
            return {true, receiver.addItem(split)};
        }
        return transferItemTo(_game, _item, receiver);
    };
    switch (_giveTo->type()) {
    case ObjectType::Placeable: {
        auto &placeable = static_cast<Placeable &>(*_giveTo);
        auto delivered = deliver(placeable);
        if (delivered && !placeable.dieWhenEmpty()) {
            placeable.runOnInvDisturbed(disturber, InventoryDisturbType::Added, delivered.held->id());
        }
        break;
    }
    case ObjectType::Creature: {
        const std::string name = _item->localizedName();
        auto delivered = deliver(*party.sharedInventoryReceiver(_giveTo));
        if (_givenByScript && party.isMember(*_giveTo)) {
            _game.submitStatusSummary(StatusSummaryCategory::ItemsReceived, 0, {name});
        }
        if (delivered && disturber != _giveTo->id()) {
            // A counted item, such as credits, leaves no stack to name.
            const uint32_t held = delivered.held ? delivered.held->id() : script::kObjectInvalid;
            static_cast<Creature &>(*_giveTo).runOnDisturbed(disturber, InventoryDisturbType::Added, held);
        }
        break;
    }
    default:
        break;
    }
    complete();
}

// A give is kept as the item, the receiver, the count and whether a script
// gave it; taken up again it walks up afresh.
std::optional<SavedActionRecord> GiveItemAction::saveFacingState() const {
    return SavedActionRecord::command(SavedActionType::GiveItem, {
                                                                     {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(_item->id())},
                                                                     {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(_giveTo->id())},
                                                                     {kSavedIntegerParameter, static_cast<int32_t>(_count)},
                                                                     {kSavedIntegerParameter, static_cast<int32_t>(_givenByScript ? 1 : 0)},
                                                                 });
}

std::shared_ptr<Action> GiveItemAction::restore(const SavedActionRecord &record, Game &game) {
    auto item = std::dynamic_pointer_cast<Item>(record.objectParameter(0).boundObject());
    auto receiver = record.objectParameter(1).boundObject();
    if (!item || !receiver) {
        return nullptr;
    }
    return game.newAction<GiveItemAction>(std::move(item), std::move(receiver), record.integerParameter(2), record.integerParameter(3) != 0);
}

// A creature or placeable takes the item. A creature that takes it from a
// party member reports the item lost in the status summary. A creature the
// item is taken from runs its disturbed script, disturbed by the taker.
void TakeItemAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto &party = _game.party();
    const uint32_t itemId = _item->id();
    bool taken = false;
    switch (actor.type()) {
    case ObjectType::Placeable:
        taken = static_cast<bool>(transferItemTo(_game, _item, actor));
        break;
    case ObjectType::Creature:
        taken = static_cast<bool>(transferItemTo(_game, _item, *party.sharedInventoryReceiver(_game.getObjectById(actor.id()))));
        if (taken && party.isMember(*_takeFrom)) {
            _game.submitStatusSummary(StatusSummaryCategory::ItemsLost);
        }
        break;
    default:
        break;
    }
    if (auto *owner = dyn_cast<Creature>(_takeFrom.get()); owner && taken && owner != &actor) {
        owner->runOnDisturbed(actor.id(), InventoryDisturbType::Removed, itemId);
    }
    complete();
}

// A take is kept as the item and the giver, and a third value, always 1.
static constexpr int32_t kTakeThirdValue = 1;

std::optional<SavedActionRecord> TakeItemAction::saveFacingState() const {
    return SavedActionRecord::command(SavedActionType::TakeItem, {
                                                                     {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(_item->id())},
                                                                     {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(_takeFrom->id())},
                                                                     {kSavedIntegerParameter, kTakeThirdValue},
                                                                 });
}

std::shared_ptr<Action> TakeItemAction::restore(const SavedActionRecord &record, Game &game) {
    auto item = std::dynamic_pointer_cast<Item>(record.objectParameter(0).boundObject());
    auto giver = record.objectParameter(1).boundObject();
    if (!item || !giver) {
        return nullptr;
    }
    return game.newAction<TakeItemAction>(std::move(item), std::move(giver));
}

// A pick-up is kept as the item, from no container and no inventory slot.
std::optional<SavedActionRecord> PickUpItemAction::saveFacingState() const {
    static constexpr int32_t kNoInventorySlot = 0xff;
    return SavedActionRecord::command(SavedActionType::PickUpItem, {
                                                                       {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(_item->id())},
                                                                       {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(kSavedRuntimeInvalidObjectId)},
                                                                       {kSavedIntegerParameter, kNoInventorySlot},
                                                                   });
}

std::shared_ptr<Action> PickUpItemAction::restore(const SavedActionRecord &record, Game &game) {
    auto item = std::dynamic_pointer_cast<Item>(record.objectParameter(0).boundObject());
    if (!item) {
        return nullptr;
    }
    return game.newAction<PickUpItemAction>(std::move(item));
}

// A put-down is kept as the item and where it goes down: at the feet of the one
// holding it, or where it lies when nobody does.
std::optional<SavedActionRecord> PutDownItemAction::saveFacingState() const {
    auto holder = _game.getObjectById(_item->owner());
    const glm::vec3 point = holder ? holder->position() : _item->position();
    return SavedActionRecord::command(SavedActionType::PutDownItem, {
                                                                        {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(_item->id())},
                                                                        {kSavedFloatParameter, point.x},
                                                                        {kSavedFloatParameter, point.y},
                                                                        {kSavedFloatParameter, point.z},
                                                                        {kSavedIntegerParameter, int32_t {0}},
                                                                    });
}

std::shared_ptr<Action> PutDownItemAction::restore(const SavedActionRecord &record, Game &game) {
    auto item = std::dynamic_pointer_cast<Item>(record.objectParameter(0).boundObject());
    if (!item) {
        return nullptr;
    }
    return game.newAction<PutDownItemAction>(std::move(item));
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
    return SavedActionRecord::command(SavedActionType::EquipItem, {
                                                                      {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(_item->id())},
                                                                      {kSavedIntegerParameter, static_cast<int32_t>(slot)},
                                                                      {kSavedIntegerParameter, _instant},
                                                                  });
}

std::shared_ptr<Action> EquipItemAction::restore(const SavedActionRecord &record, Game &game) {
    auto item = std::dynamic_pointer_cast<Item>(record.objectParameter(0).boundObject());
    if (!item) {
        return nullptr;
    }
    const auto slot = *equipmentSlotFromMask(static_cast<uint32_t>(record.integerParameter(1)));
    return game.newAction<EquipItemAction>(std::move(item), slot, record.integerParameter(2));
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

std::shared_ptr<Action> UnequipItemAction::restore(const SavedActionRecord &record, Game &game) {
    auto item = std::dynamic_pointer_cast<Item>(record.objectParameter(0).boundObject());
    if (!item) {
        return nullptr;
    }
    const auto &reference = record.objectParameter(1);
    auto container = std::dynamic_pointer_cast<Item>(reference.boundObject());
    auto unequip = game.newAction<UnequipItemAction>(std::move(item), record.integerParameter(2), container);
    // A container value that names no item is kept as it is.
    if (!reference.isInvalid() && !container) {
        unequip->overwriteContainer(static_cast<int32_t>(reference.id));
    }
    return unequip;
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
    return SavedActionRecord::command(SavedActionType::SwitchWeapons, {});
}

std::shared_ptr<Action> SwitchWeaponsAction::restore(const SavedActionRecord &record, Game &game) {
    return game.newAction<SwitchWeaponsAction>();
}

} // namespace game

} // namespace reone
