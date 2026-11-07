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

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "upgraderules.h"

namespace reone {

namespace game {

class Game;
class Item;
class Object;
struct ServicesView;

/**
 * The upgrade bench applied to the party: which of its items the bench lists,
 * which upgrade items it carries, and fitting one into an item.
 *
 * KotOR keeps one party inventory, the player's items. Chosen: the bench
 * lists the items in it and the items the party members wear, and takes
 * upgrades from and gives them back to that inventory only.
 */
class UpgradeBench {
public:
    UpgradeBench(Game &game, ServicesView &services);

    /** upgrade.2da, read once. */
    const UpgradeTable &table();

    /** The template that names an item: its template, else its tag, lower case. */
    static std::string templateOf(const Item &item);

    std::vector<UpgradeSlot> slotsOf(const Item &item);
    /** The upgrade.2da row in each slot of the item, -1 for none. */
    std::vector<int> installedRowsOf(const Item &item);
    /** Whether the item has a slot, which is what lists it on the bench. */
    bool isUpgradeable(const Item &item);
    UpgradeCategory categoryOf(const Item &item) const;

    /** The party's items of a category the bench lists: the inventory's, then the members' worn ones. */
    std::vector<std::shared_ptr<Item>> upgradeableItems(UpgradeCategory category);
    /** The upgrade items and colour crystals in the party inventory. */
    std::vector<std::shared_ptr<Item>> carriedUpgrades();
    /** The carried upgrade items that a slot of the item could take now. */
    std::vector<std::shared_ptr<Item>> carriedFittingSlot(const Item &item, int slotIndex);

    /**
     * Fits the upgrade item of template \p chosen into slot \p slotIndex, or
     * takes it out when the slot already holds it. The change applies at
     * once: the upgrade item leaves the inventory, the one it replaces comes
     * back, and a worn lightsaber shows its new blade.
     *
     * @return whether anything changed
     */
    bool apply(Item &item, int slotIndex, const std::string &chosen);

private:
    Game &_game;
    ServicesView &_services;
    std::optional<UpgradeTable> _table;

    std::shared_ptr<Object> inventory() const;
    std::shared_ptr<Item> findCarried(const std::string &resRef) const;
    void refreshWearer(const Item &item);
};

} // namespace game

} // namespace reone
