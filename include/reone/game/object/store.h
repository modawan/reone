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

#pragma once

#include <optional>
#include <set>

#include "../object.h"
#include "reone/resource/gff.h"
#include "reone/resource/strings.h"

namespace reone {

namespace game {

class Creature;

class Store : public Object {
public:
    Store(
        uint32_t id,
        std::string sceneName,
        Game &game,
        ServicesView &services) :
        Object(
            id,
            ObjectType::Store,
            std::move(sceneName),
            game,
            services) {
    }

    static bool classof(const Object *from) {
        return from->type() == ObjectType::Store;
    }

    /** Loads the store's blueprint; a missing blueprint leaves the store as it is. */
    void loadFromBlueprint(const std::string &resRef);
    void deserialize(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);

    /** Whether the store buys from the party as well as selling to it. */
    bool buysAndSells() const { return _buySellFlag == kBuysAndSells; }
    /** Whether the store only buys from the party. */
    bool onlyBuys() const { return _buySellFlag == kOnlyBuys; }

    /** The bonuses a script opens the store with, each held to -100 to 100. */
    void setBonusMarks(int markUp, int markDown);
    /** What the store asks for the item: its cost by the mark-up and its bonus, in percent. */
    int buyPrice(const Item &item) const;
    /** What the store pays for the item: its cost by the mark-down and its bonus, in percent. */
    int sellPrice(const Item &item) const;

    /**
     * Takes an item into the stock. A plot item is refused and destroyed. An
     * item identical to the first stocked item with its tag adds its stack
     * there (never for an endless stock) and is destroyed. Otherwise the
     * store files it, identified, by cost.
     *
     * @return the item filed, or none
     */
    std::shared_ptr<Item> acquireItem(const std::shared_ptr<Item> &item);
    /**
     * Sells the buyer one of the stocked item: a copy of an endless stock,
     * one off a stack, or else the item itself.
     */
    void sellItem(const std::shared_ptr<Item> &item, const std::shared_ptr<Creature> &buyer);
    /** Runs the store's open script, with the one who opened it. */
    void onOpened(uint32_t openerId);

private:
    friend class ModuleSnapshotBuilder;

    /** The BuySellFlag of a store that only buys from the party. */
    static constexpr uint8_t kOnlyBuys = 2;
    /** The BuySellFlag of a store that both buys and sells. */
    static constexpr uint8_t kBuysAndSells = 3;

    // Serializable
    resource::LocString _locName;
    int32_t _markDown {100};
    int32_t _markUp {100};
    std::string _onOpenStore;
    uint8_t _buySellFlag {kBuysAndSells};
    // END Serializable

    int _bonusMarkUp {0};
    int _bonusMarkDown {0};

    /**
     * @param placedTemplate whether the record is the template of a store an
     *        area places, whose items are their templates, each with its
     *        entry's Infinite
     */
    void deserializeAll(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext,
        bool placedTemplate);
    /**
     * Replaces the stock with the record's ItemList, dearest first.
     *
     * @param placedTemplate whether the record is the template of a store an
     *        area places, whose entries are each their InventoryRes template,
     *        with the entry's Infinite
     */
    void deserializeItems(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext,
        bool placedTemplate);
    /**
     * Takes the position and orientation \p gff gives; with \p zeroWhereMissing
     * a field it lacks counts as 0, otherwise the current value stays.
     */
    void readPlacement(const resource::Gff &gff, bool zeroWhereMissing);
    /**
     * Whether an item list entry never runs out. A saved entry without
     * Infinite takes it from the store's template, read once into
     * \p endlessTemplates.
     */
    bool entryInfinite(
        const resource::Gff &record,
        const resource::Gff &entry,
        const SerializedIdentityContext &identityContext,
        std::optional<std::set<std::string>> &endlessTemplates) const;
    int itemCost(const Item &item) const;
    /**
     * Files an item by cost. An item cheaper than the first goes in before
     * the last item dearer than it; any other joins an identical stack or
     * goes first.
     *
     * @return whether the item is stocked as a stack of its own, rather than
     *         taken in whole by another
     */
    bool fileItem(const std::shared_ptr<Item> &item);
};

} // namespace game

} // namespace reone
