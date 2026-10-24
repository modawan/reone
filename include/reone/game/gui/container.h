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

#include "../gui.h"

#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"
#include "reone/gui/control/listbox.h"

#include "../object.h"
#include "../runtimeref.h"

namespace reone {

namespace game {

class ContainerGUI : public GameGUI {
public:
    ContainerGUI(Game &game, ServicesView &services) :
        GameGUI(game, services) {
        _resRef = guiResRef("container");
    }

    void open(std::shared_ptr<Object> contanier);
    void update(float dt) override;

    Object &container() const;
    bool displays(const Object &object) const { return _container.resolve().get() == &object; }
    /** The screen is closed from outside: the controlled creature takes everything left. */
    void closeTakingAll();

private:
    struct Controls {
        std::shared_ptr<gui::Button> BTN_CANCEL;
        std::shared_ptr<gui::Button> BTN_GIVEITEMS;
        std::shared_ptr<gui::Button> BTN_OK;
        std::shared_ptr<gui::Label> LBL_MESSAGE;
        std::shared_ptr<gui::ListBox> LB_ITEMS;
    };

    Controls _controls;

    enum class Mode {
        ContainerToPlayer,
        PlayerToContainer,
    };

    /** Where a give stands in refreshing the list. */
    enum class GiveRefresh {
        None,
        /** A give was asked for this frame; the list waits a frame. */
        AfterNextFrame,
        /** The list is shown again on this frame. */
        Due,
    };

    Mode _mode {Mode::ContainerToPlayer};
    GiveRefresh _giveRefresh {GiveRefresh::None};
    RuntimeObjectRef<Object> _container;
    /**
     * The list shows the container as it is once the world update in which
     * the screen opened has finished, its open and used scripts included.
     */
    bool _itemsListed {false};

    std::string _giveItemMsg;
    std::string _getItemsMsg;

    void onGUILoaded() override;

    void bindControls() {
        _controls.BTN_CANCEL = findControl<gui::Button>("BTN_CANCEL");
        _controls.BTN_GIVEITEMS = findControl<gui::Button>("BTN_GIVEITEMS");
        _controls.BTN_OK = findControl<gui::Button>("BTN_OK");
        _controls.LBL_MESSAGE = findControl<gui::Label>("LBL_MESSAGE");
        _controls.LB_ITEMS = findControl<gui::ListBox>("LB_ITEMS");
    }

    void configureItemsListBox();
    void populateItems(Object &source, bool onlyDropable, bool skipCredits);
    void switchMode();
    void transferItemsToPlayer();
    void onItemDoubleClick(const std::string &tag);
    /** Lists the leader's items again once a give has had a frame to run. */
    void updateGiveRefresh();
    void close(bool takeAll = false);
};

} // namespace game

} // namespace reone
