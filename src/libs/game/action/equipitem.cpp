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

#include "reone/game/action/equipitem.h"
#include "reone/game/equipmentrules.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"

namespace reone {

namespace game {

void EquipItemAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto *creature = dyn_cast<Creature>(&actor);
    if (!creature) {
        complete();
        return;
    }
    auto candidate = _item;
    int equipabilitySlot = _inventorySlot;
    if (_inventorySlot == InventorySlots::rightWeapon2) {
        equipabilitySlot = InventorySlots::rightWeapon;
    } else if (_inventorySlot == InventorySlots::leftWeapon2) {
        equipabilitySlot = InventorySlots::leftWeapon;
    }
    if (!candidate ||
        !candidate->isEquippable(equipabilitySlot)) {
        complete();
        return;
    }
    for (const auto &[slot, equipped] : creature->equipment()) {
        if (equipped == candidate && slot != _inventorySlot) {
            complete();
            return;
        }
    }

    auto actorObject = _game.getObjectById(actor.id());
    auto displacedReceiver =
        _game.party().sharedInventoryReceiver(actorObject);
    auto previous = creature->getEquippedItem(_inventorySlot);
    if (previous && previous != candidate && !displacedReceiver) {
        complete();
        return;
    }

    std::shared_ptr<Object> sourceOwner;
    uint32_t ownerId = candidate->owner();
    if (ownerId != 0 && ownerId != script::kObjectInvalid) {
        sourceOwner = _game.getObjectById(ownerId);
        if (!sourceOwner) {
            complete();
            return;
        }
        if (auto equippedOwner =
                std::dynamic_pointer_cast<Creature>(sourceOwner);
            equippedOwner && candidate->isEquipped()) {
            candidate = equippedOwner->takeEquippedItem(candidate);
        } else {
            candidate = takeEquipmentCandidate(_game, *sourceOwner, candidate);
        }
    }
    bool equipped = candidate &&
                    (previous && previous != candidate
                         ? creature->replaceEquipment(
                               _inventorySlot, candidate, *displacedReceiver)
                         : creature->equip(_inventorySlot, candidate));
    if (!equipped) {
        if (candidate && candidate->owner() == 0 && sourceOwner) {
            sourceOwner->addItem(candidate);
        }
        complete();
        return;
    }
    creature->playAnimation(CombatAnimation::Draw, creature->getWieldType());
    complete();
}

} // namespace game

} // namespace reone
