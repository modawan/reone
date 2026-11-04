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

#include "../gui.h"

#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"
#include "reone/gui/control/listbox.h"

#include "../runtimeref.h"

namespace reone {

namespace game {

class Item;
class Object;
class Store;

/**
 * The merchant screen. In buy mode it lists the store's stock and Accept buys
 * one unit of the selected item; in sell mode it lists what the party can
 * sell and Accept sells one unit. The examine button switches between them.
 */
class StoreGUI : public GameGUI {
public:
    StoreGUI(Game &game, ServicesView &services) :
        GameGUI(game, services) {
        _resRef = guiResRef("store");
    }

    void open(const std::shared_ptr<Store> &store, int bonusMarkUp, int bonusMarkDown);
    void update(float dt) override;

private:
    struct Controls {
        std::shared_ptr<gui::Button> BTN_Accept;
        std::shared_ptr<gui::Button> BTN_Cancel;
        std::shared_ptr<gui::Button> BTN_Examine;
        std::shared_ptr<gui::Label> LBL_BUYSELL;
        std::shared_ptr<gui::Label> LBL_COST;
        std::shared_ptr<gui::Label> LBL_COST_VALUE;
        std::shared_ptr<gui::Label> LBL_CREDITS;
        std::shared_ptr<gui::Label> LBL_CREDITS_VALUE;
        std::shared_ptr<gui::Label> LBL_STOCK;
        std::shared_ptr<gui::Label> LBL_STOCK_VALUE;
        std::shared_ptr<gui::ListBox> LB_DESCRIPTION;
        std::shared_ptr<gui::ListBox> LB_INVITEMS;
        std::shared_ptr<gui::ListBox> LB_SHOPITEMS;
    };

    enum class Mode {
        Buy,
        Sell,
    };

    Controls _controls;
    Mode _mode {Mode::Buy};
    RuntimeObjectRef<Store> _store;
    int _bonusMarkUp {0};
    int _bonusMarkDown {0};
    /** The lists show the store as it is once the frame the screen opened in has finished. */
    bool _itemsListed {false};

    void onGUILoaded() override;

    void bindControls() {
        _controls.BTN_Accept = findControl<gui::Button>("BTN_Accept");
        _controls.BTN_Cancel = findControl<gui::Button>("BTN_Cancel");
        _controls.BTN_Examine = findControl<gui::Button>("BTN_Examine");
        _controls.LBL_BUYSELL = findControl<gui::Label>("LBL_BUYSELL");
        _controls.LBL_COST = findControl<gui::Label>("LBL_COST");
        _controls.LBL_COST_VALUE = findControl<gui::Label>("LBL_COST_VALUE");
        _controls.LBL_CREDITS = findControl<gui::Label>("LBL_CREDITS");
        _controls.LBL_CREDITS_VALUE = findControl<gui::Label>("LBL_CREDITS_VALUE");
        _controls.LBL_STOCK = findControl<gui::Label>("LBL_STOCK");
        _controls.LBL_STOCK_VALUE = findControl<gui::Label>("LBL_STOCK_VALUE");
        _controls.LB_DESCRIPTION = findControl<gui::ListBox>("LB_DESCRIPTION");
        _controls.LB_INVITEMS = findControl<gui::ListBox>("LB_INVITEMS");
        _controls.LB_SHOPITEMS = findControl<gui::ListBox>("LB_SHOPITEMS");
    }

    void configureItemList(gui::ListBox &list);
    void setMode(Mode mode);
    /** Lists both sides again, keeping the shown list's selection and scroll where they can stay. */
    void refresh();
    void listItems(gui::ListBox &list, const Object *source, bool sellableOnly);
    void showSelection();
    void showCredits();
    gui::ListBox *shownList() const;
    std::shared_ptr<Item> selectedItem() const;
    void accept();
    void close();
};

} // namespace game

} // namespace reone
