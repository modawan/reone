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

#include "reone/game/upgradebench.h"

#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/party.h"

namespace reone {

namespace game {

UpgradeBench::UpgradeBench(Game &game, ServicesView &services) :
    _game(game),
    _services(services) {
}

const UpgradeTable &UpgradeBench::table() {
    if (!_table) {
        auto upgrade = _services.resource.twoDas.get("upgrade");
        _table = upgrade ? UpgradeTable::fromTwoDA(*upgrade) : UpgradeTable();
    }
    return *_table;
}

std::string UpgradeBench::templateOf(const Item &item) {
    return boost::to_lower_copy(item.blueprintResRef().empty() ? item.tag() : item.blueprintResRef());
}

std::vector<UpgradeSlot> UpgradeBench::slotsOf(const Item &item) {
    return upgradeSlots(item.isLightsaber(), acceptedUpgradeRows(item.properties()), table());
}

std::vector<int> UpgradeBench::installedRowsOf(const Item &item) {
    return installedRows(slotsOf(item), acceptedUpgradeRows(item.properties()), item.upgrades(), table());
}

bool UpgradeBench::isUpgradeable(const Item &item) {
    return !slotsOf(item).empty();
}

UpgradeCategory UpgradeBench::categoryOf(const Item &item) const {
    return upgradeCategoryOf(item.isLightsaber(), item.isRanged(), item.isEquippable(InventorySlots::body));
}

std::shared_ptr<Object> UpgradeBench::inventory() const {
    return _game.party().player();
}

std::vector<std::shared_ptr<Item>> UpgradeBench::upgradeableItems(UpgradeCategory category) {
    std::vector<std::shared_ptr<Item>> items;
    auto add = [&](const std::shared_ptr<Item> &item) {
        if (item && categoryOf(*item) == category && isUpgradeable(*item) &&
            std::find(items.begin(), items.end(), item) == items.end()) {
            items.push_back(item);
        }
    };
    if (auto holder = inventory()) {
        for (const auto &item : holder->items()) {
            add(item);
        }
    }
    for (int i = 0; i < _game.party().getSize(); ++i) {
        auto member = _game.party().getMember(i);
        if (!member) {
            continue;
        }
        for (const auto &[slot, item] : member->equipment()) {
            add(item);
        }
    }
    return items;
}

std::vector<std::shared_ptr<Item>> UpgradeBench::carriedUpgrades() {
    std::vector<std::shared_ptr<Item>> items;
    auto holder = inventory();
    if (!holder) {
        return items;
    }
    for (const auto &item : holder->items()) {
        auto resRef = templateOf(*item);
        if (table().findByTemplate(resRef) || colorCrystalBladeVariation(resRef)) {
            items.push_back(item);
        }
    }
    return items;
}

std::vector<std::shared_ptr<Item>> UpgradeBench::carriedFittingSlot(const Item &item, int slotIndex) {
    std::vector<std::shared_ptr<Item>> fitting;
    auto slots = slotsOf(item);
    auto accepted = acceptedUpgradeRows(item.properties());
    for (const auto &carried : carriedUpgrades()) {
        auto resRef = templateOf(*carried);
        auto change = planSlotChange(slots, slotIndex, accepted, item.upgrades(), item.modelVariation(), table(), resRef);
        // Choosing a carried copy of the upgrade the slot holds takes it out,
        // which the slot's own entry already offers.
        if (change && !change->consumed.empty()) {
            fitting.push_back(carried);
        }
    }
    return fitting;
}

std::shared_ptr<Item> UpgradeBench::findCarried(const std::string &resRef) const {
    auto holder = inventory();
    if (!holder) {
        return nullptr;
    }
    for (const auto &item : holder->items()) {
        if (templateOf(*item) == resRef) {
            return item;
        }
    }
    return nullptr;
}

bool UpgradeBench::apply(Item &item, int slotIndex, const std::string &chosen) {
    auto holder = inventory();
    if (!holder) {
        return false;
    }
    auto change = planSlotChange(
        slotsOf(item),
        slotIndex,
        acceptedUpgradeRows(item.properties()),
        item.upgrades(),
        item.modelVariation(),
        table(),
        chosen);
    if (!change) {
        return false;
    }
    std::shared_ptr<Item> consumed;
    if (!change->consumed.empty()) {
        consumed = findCarried(change->consumed);
        if (!consumed) {
            return false;
        }
    }

    if (consumed) {
        bool last = false;
        if (holder->removeItem(consumed, last) && last) {
            _game.destroyRuntimeObjectGraph(consumed);
        }
    }
    const bool newBlade = change->modelVariation != item.modelVariation();
    item.setUpgrades(change->upgrades);
    if (newBlade) {
        item.setModelVariation(change->modelVariation);
    }
    if (!change->returned.empty()) {
        holder->addItem(change->returned);
    }
    if (newBlade) {
        refreshWearer(item);
    }
    return true;
}

void UpgradeBench::refreshWearer(const Item &item) {
    for (int i = 0; i < _game.party().getSize(); ++i) {
        auto member = _game.party().getMember(i);
        if (!member) {
            continue;
        }
        for (const auto &[slot, worn] : member->equipment()) {
            if (worn.get() == &item) {
                member->updateEquipmentPresentation();
                return;
            }
        }
    }
}

} // namespace game

} // namespace reone
