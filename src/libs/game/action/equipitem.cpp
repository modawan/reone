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
    uint32_t ownerId = candidate->owner();
    if (ownerId != 0 && ownerId != script::kObjectInvalid) {
        auto owner = _game.getObjectById(ownerId);
        if (!owner) {
            complete();
            return;
        }
        if (auto equippedOwner = std::dynamic_pointer_cast<Creature>(owner);
            equippedOwner && candidate->isEquipped()) {
            equippedOwner->unequip(candidate);
        } else {
            candidate = takeEquipmentCandidate(_game, *owner, candidate);
        }
    }
    if (!candidate || !creature->equip(_inventorySlot, candidate)) {
        if (candidate && candidate->owner() == 0) {
            auto receiver = _game.party().sharedInventoryReceiver(
                _game.getObjectById(actor.id()));
            if (receiver) receiver->addItem(candidate);
        }
        complete();
        return;
    }
    creature->playAnimation(CombatAnimation::Draw, creature->getWieldType());
    complete();
}

} // namespace game

} // namespace reone
