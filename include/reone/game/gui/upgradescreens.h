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

#include <map>
#include <optional>

#include "../gui.h"

#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"
#include "reone/gui/control/listbox.h"

#include "../runtimeref.h"
#include "../upgradebench.h"

namespace reone {

namespace game {

class Item;
class UpgradeScreensTestAccess;

/**
 * The upgrade bench's first screen (upgradesel): a button for each kind of
 * item, and one listing the upgrade items carried.
 */
class UpgradeSelectionGUI : public GameGUI {
public:
    UpgradeSelectionGUI(Game &game, ServicesView &services);

    /** Bring the buttons in line with what the party carries before it opens. */
    void prepare();

    bool handle(const input::Event &event) override;

private:
    friend class UpgradeScreensTestAccess;

    struct Controls {
        std::shared_ptr<gui::Button> BTN_ARMOR;
        std::shared_ptr<gui::Button> BTN_BACK;
        std::shared_ptr<gui::Button> BTN_LIGHTSABER;
        std::shared_ptr<gui::Button> BTN_MELEE;
        std::shared_ptr<gui::Button> BTN_RANGED;
        std::shared_ptr<gui::Button> BTN_UPGRADEITEMS;
    };

    Controls _controls;
    UpgradeBench _bench;

    void onGUILoaded() override;
};

/**
 * The upgrade bench's list (upgradeitems): the party's items of one kind, or,
 * without a kind, the upgrade items carried.
 */
class UpgradeItemsGUI : public GameGUI {
public:
    UpgradeItemsGUI(Game &game, ServicesView &services);

    /** List the items of \p category, or the carried upgrade items without one. */
    void prepare(std::optional<UpgradeCategory> category);

    std::optional<UpgradeCategory> category() const { return _category; }

    bool handle(const input::Event &event) override;

private:
    friend class UpgradeScreensTestAccess;

    struct Controls {
        std::shared_ptr<gui::Button> BTN_BACK;
        std::shared_ptr<gui::Button> BTN_UPGRADEITEM;
        std::shared_ptr<gui::ListBox> LB_DESCRIPTION;
        std::shared_ptr<gui::ListBox> LB_ITEMS;
    };

    Controls _controls;
    UpgradeBench _bench;
    std::optional<UpgradeCategory> _category;
    std::vector<RuntimeObjectRef<Item>> _listed;
    int _selected {-1};

    void onGUILoaded() override;

    void select(int index);
    void upgradeSelected();
};

/**
 * The upgrade screen (upgrade.gui) for one item: its slots on the three-slot
 * or the four-slot row, and the list of carried upgrades that fit the slot
 * clicked.
 */
class UpgradeGUI : public GameGUI {
public:
    UpgradeGUI(Game &game, ServicesView &services);

    /**
     * Show the slots of \p item. With \p backToList, leaving returns to the
     * list the item was picked from; otherwise it returns to the game.
     */
    void prepare(const std::shared_ptr<Item> &item, bool backToList);

    bool handle(const input::Event &event) override;

private:
    friend class UpgradeScreensTestAccess;

    /** An entry of the upgrade list: the upgrade item's template and whether the slot holds it. */
    struct Choice {
        std::string templateResRef;
        bool installed {false};
    };

    /** What an upgrade item looks like, read from its template. */
    struct Preview {
        std::string name;
        std::string description;
        std::shared_ptr<graphics::Texture> icon;
    };

    struct SlotControls {
        std::shared_ptr<gui::Label> label;
        std::shared_ptr<gui::Button> button;
        /** The authored fill an empty slot shows. */
        std::shared_ptr<graphics::Texture> emptyFill;
    };

    struct Controls {
        std::shared_ptr<gui::Label> THREE_D_MODEL;
        std::shared_ptr<gui::Label> THREE_D_MODEL_LS;
        std::shared_ptr<gui::Button> BTN_ASSEMBLE;
        std::shared_ptr<gui::Button> BTN_BACK;
        std::shared_ptr<gui::Label> LBL_DESCBG;
        std::shared_ptr<gui::Label> LBL_DESCBG_LS;
        std::shared_ptr<gui::Label> LBL_LSSLOTNAME;
        std::shared_ptr<gui::Label> LBL_PROPERTY;
        std::shared_ptr<gui::Label> LBL_SLOTNAME;
        std::shared_ptr<gui::Label> LBL_UPGRADES;
        std::shared_ptr<gui::Label> LBL_UPGRADE_COUNT;
        std::shared_ptr<gui::ListBox> LB_DESC;
        std::shared_ptr<gui::ListBox> LB_DESC_LS;
        std::shared_ptr<gui::ListBox> LB_ITEMS;
    };

    Controls _controls;
    /** The three-slot row (31 to 33) and the four-slot row (41 to 44), which share a place on screen. */
    std::vector<SlotControls> _threeRow;
    std::vector<SlotControls> _fourRow;
    UpgradeBench _bench;

    RuntimeObjectRef<Item> _item;
    bool _backToList {false};
    /** The list leaving returns to: the item's kind. */
    UpgradeCategory _listCategory {UpgradeCategory::Melee};
    std::vector<UpgradeSlot> _slots;
    /** The slot whose upgrade list is shown, -1 while the slots are shown. */
    int _picking {-1};
    std::vector<Choice> _choices;
    int _selectedChoice {-1};
    std::map<std::string, Preview> _previews;

    void onGUILoaded() override;
    void onSelectionChanged(const std::string &control, bool selected) override;

    bool isLightsaberLayout() const;
    bool usesLightsaberDescription() const;
    std::vector<SlotControls> &slotRow();
    std::shared_ptr<gui::ListBox> descriptionList() const;
    std::shared_ptr<gui::Label> slotNameLabel() const;

    const Preview &preview(const std::string &resRef);
    std::string slotName(int slot);
    /** The template of the upgrade item in a slot; empty for an empty slot. */
    std::string installedTemplate(int slot);

    void showSlots();
    void showDescription(const std::string &text);
    void showItemDescription();
    void openSlot(int slot);
    void selectChoice(int index);
    void fitChoice(int index);
    void back();
    void leave();
};

} // namespace game

} // namespace reone
