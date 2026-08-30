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

#include "reone/game/action/unequipitem.h"

#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/party.h"

namespace reone {

namespace game {

void UnequipItemAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto *creature = dyn_cast<Creature>(&actor);
    if (!creature || !_item->isEquipped() || _item->owner() != actor.id()) {
        complete();
        return;
    }
    creature->unequip(_item);
    _item->setOwner(0);
    auto receiver = _game.party().sharedInventoryReceiver(
        _game.getObjectById(actor.id()));
    if (receiver) {
        receiver->addItem(_item);
    } else {
        _game.destroyRuntimeObjectGraph(_item);
    }
    complete();
}

} // namespace game

} // namespace reone
