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

class Game;
class Item;
class Object;
class Store;

/**
 * What one unit trades for at \p percent of \p itemCost: truncated toward
 * zero, and at least 1 for an item with a cost. A negative percentage counts
 * as none.
 */
int storeTradePrice(int itemCost, int percent);

/** What the player pays the store for one unit of \p item. */
int storeBuyPrice(const Game &game, const Store &store, const Item &item, int bonusMarkUp);
/** What the store pays the player for one unit of \p item. */
int storeSellPrice(const Game &game, const Store &store, const Item &item, int bonusMarkDown);

/** Whether the store screen offers \p item for sale: not plot, not equipped, not credits. */
bool isSellableToStore(const Item &item);

/** The object holding what the party carries: the party's shared inventory. */
std::shared_ptr<Object> storeCustomer(Game &game);

/**
 * The party buys one unit of \p item from \p store, when the store sells, it
 * holds the item and the party has the credits. The store keeps an infinite
 * stack whole.
 *
 * @return whether the unit was bought
 */
bool buyFromStore(Game &game, Store &store, const std::shared_ptr<Item> &item, int bonusMarkUp);

/**
 * The party sells one unit of \p item to \p store, when the store buys, the
 * party's inventory holds the item and it is sellable.
 *
 * @return whether the unit was sold
 */
bool sellToStore(Game &game, Store &store, const std::shared_ptr<Item> &item, int bonusMarkDown);

} // namespace game

} // namespace reone
