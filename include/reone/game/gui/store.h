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

#include <optional>

#include "../gui.h"

#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"
#include "reone/gui/control/listbox.h"

#include "../runtimeref.h"

namespace reone {

namespace game {

class Item;
class Store;

class StoreGUI : public GameGUI {
public:
    StoreGUI(Game &game, ServicesView &services) :
        GameGUI(game, services) {
        _resRef = guiResRef("store");
    }

    /** Shows the store's lists, its buy list first unless it only buys. */
    void open(const std::shared_ptr<Store> &store);

    bool handle(const input::Event &event) override;
    void update(float dt) override;

private:
    struct Controls {
        std::shared_ptr<gui::Button> BTN_Accept;
        std::shared_ptr<gui::Button> BTN_Cancel;
        std::shared_ptr<gui::Button> BTN_Examine;
        std::shared_ptr<gui::Label> LBL_BUYSELL;
        std::shared_ptr<gui::Label> LBL_COST;
        std::shared_ptr<gui::Label> LBL_COST_VALUE;
        std::shared_ptr<gui::Label> LBL_CREDITS_VALUE;
        std::shared_ptr<gui::Label> LBL_STOCK_VALUE;
        std::shared_ptr<gui::ListBox> LB_DESCRIPTION;
        std::shared_ptr<gui::ListBox> LB_INVITEMS;
        std::shared_ptr<gui::ListBox> LB_SHOPITEMS;
        // TSL
        std::shared_ptr<gui::Button> BTN_ALL;
        std::shared_ptr<gui::Button> BTN_ARMOR;
        std::shared_ptr<gui::Button> BTN_MISC;
        std::shared_ptr<gui::Button> BTN_WEAPONS;
        std::shared_ptr<gui::Label> LBL_ONHAND_VALUE;
    };

    /** TSL: which items the lists show. */
    enum class Filter {
        All,
        Weapons,
        Armor,
        Misc
    };

    Controls _controls;
    RuntimeObjectRef<Store> _store;
    bool _selling {false};
    Filter _filter {Filter::All};
    /** The credits the screen shows, kept to the party's as they change. */
    std::optional<int> _shownCredits;

    void onGUILoaded() override;

    void bindControls() {
        _controls.BTN_Accept = findControl<gui::Button>("BTN_Accept");
        _controls.BTN_Cancel = findControl<gui::Button>("BTN_Cancel");
        _controls.BTN_Examine = findControl<gui::Button>("BTN_Examine");
        _controls.LBL_BUYSELL = findControl<gui::Label>("LBL_BUYSELL");
        _controls.LBL_COST = findControl<gui::Label>("LBL_COST");
        _controls.LBL_COST_VALUE = findControl<gui::Label>("LBL_COST_VALUE");
        _controls.LBL_CREDITS_VALUE = findControl<gui::Label>("LBL_CREDITS_VALUE");
        _controls.LBL_STOCK_VALUE = findControl<gui::Label>("LBL_STOCK_VALUE");
        _controls.LB_DESCRIPTION = findControl<gui::ListBox>("LB_DESCRIPTION");
        _controls.LB_INVITEMS = findControl<gui::ListBox>("LB_INVITEMS");
        _controls.LB_SHOPITEMS = findControl<gui::ListBox>("LB_SHOPITEMS");
        if (isTSL()) {
            _controls.BTN_ALL = findControl<gui::Button>("BTN_ALL");
            _controls.BTN_ARMOR = findControl<gui::Button>("BTN_ARMOR");
            _controls.BTN_MISC = findControl<gui::Button>("BTN_MISC");
            _controls.BTN_WEAPONS = findControl<gui::Button>("BTN_WEAPONS");
            _controls.LBL_ONHAND_VALUE = findControl<gui::Label>("LBL_ONHAND_VALUE");
        }
    }

    void configureItemList(gui::ListBox &list);
    void showBuyList();
    void showSellList();
    void toggleLists();
    void setFilter(Filter filter);
    bool passesFilter(const Item &item) const;
    void listShopItems();
    void listInventoryItems();
    /** Lists the items with each base item's store panel sort, the lowest first; plot items are left out. */
    void addSortedRows(gui::ListBox &list, const std::vector<std::shared_ptr<Item>> &items);
    void addRow(gui::ListBox &list, const Item &item, bool equipped);
    /** Selects the first row and shows it, or clears the readouts for an empty list. */
    void selectFirstRow(gui::ListBox &list);
    void showRow(gui::ListBox &list);
    void clearReadouts();
    std::shared_ptr<Item> itemOfRow(const std::string &tag) const;
    std::shared_ptr<Item> selectedItem(const gui::ListBox &list) const;

    void offerToBuy(const std::shared_ptr<Item> &item);
    void offerToSell(const std::shared_ptr<Item> &item);
    void buy(const std::shared_ptr<Item> &item);
    void sell(const std::shared_ptr<Item> &item);
    /** Lists the items again, keeping the first row shown and the row selected where they still are. */
    void relist(gui::ListBox &list, bool keepSelection);
    void showCredits();
    void close();
};

} // namespace game

} // namespace reone
