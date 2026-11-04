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

#include "reone/game/storetrade.h"

#include <algorithm>
#include <cstdint>
#include <limits>

#include "reone/game/equipmentrules.h"
#include "reone/game/game.h"
#include "reone/game/object/item.h"
#include "reone/game/object/store.h"
#include "reone/game/party.h"

namespace reone {

namespace game {

// The original's exact rounding cannot be observed from the game data, so the
// price truncates, and anything with a cost asks at least one credit.
int storeTradePrice(int itemCost, int percent) {
    if (itemCost <= 0) {
        return 0;
    }
    const int64_t scaled = static_cast<int64_t>(itemCost) * std::max(percent, 0) / 100;
    return static_cast<int>(std::clamp<int64_t>(scaled, 1, std::numeric_limits<int>::max()));
}

static int itemCost(const Game &game, const Item &item) {
    return item.cost(game.baseItemCostMultiplier(item.baseItemType()));
}

int storeBuyPrice(const Game &game, const Store &store, const Item &item, int bonusMarkUp) {
    return storeTradePrice(itemCost(game, item), store.markUp() + bonusMarkUp);
}

int storeSellPrice(const Game &game, const Store &store, const Item &item, int bonusMarkDown) {
    return storeTradePrice(itemCost(game, item), store.markDown() + bonusMarkDown);
}

bool isSellableToStore(const Item &item) {
    return !item.plotFlag() && !item.isEquipped() && !item.isCredits();
}

std::shared_ptr<Object> storeCustomer(Game &game) {
    auto &party = game.party();
    return party.sharedInventoryReceiver(party.getLeader());
}

static bool holds(const Object &owner, const std::shared_ptr<Item> &item) {
    const auto &items = owner.items();
    return item->owner() == owner.id() && std::find(items.begin(), items.end(), item) != items.end();
}

// One unit of item leaves its holder for receiver: a unit split off the
// stack, or the stack itself when it is the last unit. An infinite stack only
// ever gives up a copy.
static bool moveOneUnit(Game &game, const std::shared_ptr<Item> &item, Object &receiver) {
    if (item->isInfinite() || item->stackSize() > 1) {
        auto unit = game.newItemClone(*item);
        unit->setStackSize(1);
        if (!item->isInfinite()) {
            item->setStackSize(item->stackSize() - 1);
        }
        receiver.addItem(unit);
        return true;
    }
    return static_cast<bool>(transferItemTo(game, item, receiver));
}

bool buyFromStore(Game &game, Store &store, const std::shared_ptr<Item> &item, int bonusMarkUp) {
    if (!item || !store.sellsToPlayer() || !holds(store, item)) {
        return false;
    }
    auto customer = storeCustomer(game);
    if (!customer) {
        return false;
    }
    const int price = storeBuyPrice(game, store, *item, bonusMarkUp);
    if (game.party().gold() < price) {
        return false;
    }
    if (!moveOneUnit(game, item, *customer)) {
        return false;
    }
    game.party().takeGold(price);
    return true;
}

bool sellToStore(Game &game, Store &store, const std::shared_ptr<Item> &item, int bonusMarkDown) {
    if (!item || !store.buysFromPlayer() || !isSellableToStore(*item)) {
        return false;
    }
    auto customer = storeCustomer(game);
    if (!customer || !holds(*customer, item)) {
        return false;
    }
    const int price = storeSellPrice(game, store, *item, bonusMarkDown);
    if (!moveOneUnit(game, item, store)) {
        return false;
    }
    game.party().giveGold(price);
    return true;
}

} // namespace game

} // namespace reone
