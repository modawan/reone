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

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"
#include "reone/gui/control/listbox.h"

#include "../gui.h"
#include "../runtimeref.h"
#include "../types.h"

namespace reone {

namespace graphics {

class Texture;

}

namespace resource {

class TwoDA;

}

namespace scene {

class ModelSceneNode;

}

namespace game {

class Creature;
class Item;
class Object;

/** What a script asks of a workbench: whose workbench it is, and which halves it offers. */
struct UpgradeScreenRequest {
    /** The item to upgrade at once, skipping the selection; none to choose one. */
    std::shared_ptr<Item> item;
    /** TSL: the creature whose Repair and inventory the breakdown uses. */
    std::shared_ptr<Creature> character;
    /** The Create Items button is hidden. */
    bool disableItemCreation {false};
    /** Creation opens at once, and closing it closes the workbench. */
    bool disableUpgrade {false};
    /** The table creation lists, in place of ItemCreate when not empty. */
    std::string itemCreateTable;
};

/**
 * A TSL crafting station's panel. In the create view it lists the items the
 * leader can make from the party's count; in the breakdown view, the
 * character's items it can break down into that count. The workbench and the
 * lab station differ in their tables, filters, values and skill.
 */
class CraftingPanel : public GameGUI {
public:
    /** A panel making items from, and breaking them into, the given count; breakdown yields by the given skill. */
    CraftingPanel(Game &game, ServicesView &services, CraftingCount count, SkillType breakdownSkill);

    /** Escape closes the panel. */
    bool handle(const input::Event &event) override;
    /** Describes the entry the pointer is on. */
    void update(float dt) override;

protected:
    /** The workbench's breakdown filters, in the order of their buttons. The lab station has none. */
    enum class BreakdownFilter {
        All,
        Weapons,
        Armor,
        Usable,
        Misc
    };

    /** The panel's two views: the items to make, and the character's items to break down. */
    enum class View {
        Create,
        Breakdown
    };

    /** The strings a view shows: its title, cost caption and two buttons. */
    struct ViewText {
        /** The view's title (LBL_TITLE). */
        int title {0};
        /** The caption of the cost or yield (LBL_COST). */
        int cost {0};
        /** The Accept button: make or break down. */
        int accept {0};
        /** The Examine button, which switches to the other view. */
        int examine {0};
    };

    /** The station's message boxes. */
    struct Messages {
        /** The leader cannot make the entry. */
        int unavailable {0};
        /** The count cannot pay for the entry. */
        int tooFew {0};
        /** Asks before making an entry above the threshold. */
        int confirmCreate {0};
        /** Asks before breaking down an item above the threshold. */
        int confirmBreakdown {0};
    };

    /** A row of the create list: a template item, the table row it answers to, and whether the leader can make it. */
    struct Creatable {
        /** The template item the entry makes a copy of. */
        std::shared_ptr<Item> item;
        /** None when no row names the item: it is shown, and cannot be made. */
        std::optional<int> row;
        /** The leader's rank in the row's skill reaches the row's requirement. */
        bool available {false};
    };

    /** The controls both stations' panels have. */
    struct Controls {
        std::shared_ptr<gui::Button> BTN_Accept;
        std::shared_ptr<gui::Button> BTN_Cancel;
        std::shared_ptr<gui::Button> BTN_Examine;
        std::shared_ptr<gui::Label> LBL_BAR2;
        std::shared_ptr<gui::Label> LBL_BAR3;
        std::shared_ptr<gui::Label> LBL_COST;
        std::shared_ptr<gui::Label> LBL_COST_VALUE;
        std::shared_ptr<gui::Label> LBL_CREDITS_VALUE;
        std::shared_ptr<gui::Label> LBL_RELATEDSKILL;
        std::shared_ptr<gui::Label> LBL_STOCK_VALUE;
        std::shared_ptr<gui::Label> LBL_TITLE;
        std::shared_ptr<gui::ListBox> LB_DESCRIPTION;
        std::shared_ptr<gui::ListBox> LB_DESCRIPTIONINV;
        std::shared_ptr<gui::ListBox> LB_INVITEMS;
        std::shared_ptr<gui::ListBox> LB_SHOPITEMS;
    };

    /** The controls both stations' panels bind. */
    Controls _controls;
    /** The create view's filter buttons, in the order of the table groups they list. */
    std::vector<std::shared_ptr<gui::Button>> _createFilters;
    /** The breakdown view's filter buttons and the filter each one picks. */
    std::vector<std::pair<BreakdownFilter, std::shared_ptr<gui::Button>>> _breakdownFilters;
    /** The strings of the create view. */
    ViewText _createText;
    /** The strings of the breakdown view. */
    ViewText _breakdownText;
    /** The station's message boxes. */
    Messages _messages;
    /** The group of the station's table the create list shows. */
    int _createGroup {0};
    /** The breakdown view's filter; the lab station keeps ALL. */
    BreakdownFilter _breakdownFilter {BreakdownFilter::All};

    /** Binds both stations' controls, colours the lists and wires the buttons. */
    void onGUILoaded() override;

    /**
     * Opens the create view for a character. The count shown is the
     * character's; the breakdown factor is fixed by whoever opens the panel.
     */
    void openFor(const std::shared_ptr<Creature> &character, float breakdownFactor);
    /** The share of a full breakdown the creature's skill gives, at most all. */
    float breakdownFactorOf(const Creature &character) const;
    /** The creature's carried items: the party's for a party member. */
    const std::vector<std::shared_ptr<Item>> &inventoryOf(const std::shared_ptr<Creature> &creature) const;

    /** The table the create list is built from; none only when a script names a table that does not exist. */
    virtual std::shared_ptr<resource::TwoDA> createTable() const = 0;
    /** The column holding a row's skill requirement. */
    virtual std::string skillColumn() const = 0;
    /** Whether a create row also asks for a level and an alignment. */
    virtual bool checksLevelAndAlignment() const = 0;
    /** The item tag a create row answers to. */
    virtual std::string rowTag(const resource::TwoDA &table, int row) const = 0;
    /** What an item is worth at this station. */
    virtual int itemValue(const Item &item) const = 0;
    /** The character's items the breakdown list offers under the current filter, in order. */
    virtual std::vector<std::shared_ptr<Item>> listBreakable(const std::shared_ptr<Creature> &character) const = 0;
    /** The message refusing to break an item down, if the station refuses it. */
    virtual std::optional<int> breakdownRefusal(const Item &item) const { return std::nullopt; }
    /** Closes the panel; escape tells a key from the Cancel button. */
    virtual void close(bool escape) = 0;

private:
    /** The count the stations make items from and break them into. */
    CraftingCount _count;
    /** The skill whose ranks set the breakdown factor. */
    SkillType _breakdownSkill;
    /** The creature whose inventory the breakdown view lists. */
    RuntimeObjectRef<Creature> _character;
    /** The share of an item's value a breakdown yields. */
    float _breakdownFactor {0.0f};
    /** The count the panel works with, shown in LBL_CREDITS_VALUE. */
    int _shownCount {0};
    /** The view shown. */
    View _view {View::Create};
    /** The create list's entries, by list row. */
    std::vector<Creatable> _creatable;
    /** The breakdown list's items, by list row. */
    std::vector<RuntimeObjectRef<Item>> _breakable;
    /** The list row described; none while the list is empty. */
    std::optional<int> _describedRow;
    /** The create list still shows a previous group's entries, which stand for nothing. */
    bool _createListStale {false};

    /** The list of the current view. */
    gui::ListBox &currentList() const;
    /** Shows a view: its strings, controls and freshly built list. */
    void showView(View view);
    /** Highlights the current filter of each view. */
    void colourFilters();

    /**
     * Stocks the create view from the station's table under the current group.
     *
     * @return false when a blank cell ended the table, which stocks only the rows before it
     */
    bool restockCreatable();
    /** Builds the create list from the stock. */
    void showCreateList();
    /** Adds a template item to the create list, joining an identical entry; plot items are left out. */
    void addCreatable(const std::shared_ptr<Item> &item);
    /** Finds the row an entry answers to and whether the leader can make it; false when that row has no skill. */
    bool matchRow(Creatable &creatable, const resource::TwoDA &table, const Creature &leader) const;
    /** Builds the breakdown list, choosing the given row or the nearest one left. */
    void fillBreakdownList(int keepRow);
    /** Chooses and describes a row of the current list, the nearest one if it is past the end. */
    void showList(int row);
    /** Clears the cost, stock and descriptions. */
    void clearFocus();

    /** Describes a row of the current list. */
    void describe(int row);
    /** Shows an entry's cost, stock, skill requirement and item. */
    void describeCreatable(const Creatable &creatable);
    /** Shows what breaking the item down yields, how many there are, and the item. */
    void describeBreakable(const Item &item);
    /** How many of the item the party holds. */
    int stockOf(const Item &item) const;
    /** What breaking the item down adds to the count: at least one. */
    int breakdownGain(const Item &item) const;

    /** Makes the entry, or breaks the item down, of a row of the current list. */
    void act(int row);
    /** Makes an entry, refusing what the leader can't make or the count can't pay, and asking first above the threshold. */
    void create(int row);
    /** Pays for an entry and gives the leader a copy of it. */
    void finishCreate(int row);
    /** Breaks an item down, refusing what the station refuses and asking first above the threshold. */
    void breakDown(int row);
    /** Adds an item's yield to the count and takes one of it away. */
    void finishBreakdown(int row);
    /** Sets the count the panel works with and shows it. */
    void setShownCount(int count);
};

/** The workbench's creation and breakdown panel, opened from the upgrade-selection screen. */
class ItemCreationPanel : public CraftingPanel {
public:
    /** Loads panel component_p. */
    ItemCreationPanel(Game &game, ServicesView &services);

    /**
     * Opens on the MISC filter in the create view. onClose runs when the panel
     * closes, told whether a key closed it.
     */
    void open(const std::shared_ptr<Creature> &character,
              float breakdownFactor,
              std::string itemCreateTable,
              bool disableUpgrade,
              std::function<void(bool)> onClose);
    /** The breakdown factor the creature's Repair gives. */
    float repairFactorOf(const Creature &character) const { return breakdownFactorOf(character); }
    /** The breakdown filter starts at ALL each time the workbench opens. */
    void resetBreakdownFilter();

private:
    /** The itemcreate.2da groups, in the order of the create filters. */
    enum class CreateGroup {
        Armor,
        Ranged,
        Melee,
        Lightsaber,
        Misc
    };

    /** ItemCreate, read when the panel loads. */
    std::shared_ptr<resource::TwoDA> _itemCreate;
    /** ChemicalCreate, read when the panel loads: what the lab station makes, which the breakdown leaves out. */
    std::shared_ptr<resource::TwoDA> _chemicalCreate;
    /**
     * The table the create list is built from while the panel is open:
     * ItemCreate, or the table a script named, none when that does not exist.
     */
    std::shared_ptr<resource::TwoDA> _createTable;
    /**
     * The table of items the breakdown leaves out while the panel is open:
     * ChemicalCreate, or TutChemCreate when a script named a table.
     */
    std::shared_ptr<resource::TwoDA> _excludedTable;
    /** Runs when the panel closes, told whether a key closed it. */
    std::function<void(bool)> _onClose;

    /** Binds the create and breakdown filters and reads the station's tables. */
    void onGUILoaded() override;

    /** ItemCreate, or the table a script named, which lists nothing when it does not exist. */
    std::shared_ptr<resource::TwoDA> createTable() const override;
    /** A row's skill requirement is in "skill". */
    std::string skillColumn() const override { return "skill"; }
    /** A row also asks for a level and an alignment. */
    bool checksLevelAndAlignment() const override { return true; }
    /** The row's OverrideTag, else its template. */
    std::string rowTag(const resource::TwoDA &table, int row) const override;
    /** The item's component value. */
    int itemValue(const Item &item) const override;
    /** The character's items under the filter, leaving out plot items, worthless ones and what the lab station makes. */
    std::vector<std::shared_ptr<Item>> listBreakable(const std::shared_ptr<Creature> &character) const override;
    /** An item with an upgrade installed is refused. */
    std::optional<int> breakdownRefusal(const Item &item) const override;
    /** Runs the closing callback the workbench gave. */
    void close(bool escape) override;

    /** Whether the item passes the current breakdown filter, judged for the character. */
    bool passesBreakdownFilter(const Creature &character, const Item &item) const;
};

/** The lab station's panel (ShowChemicalUpgradeScreen). */
class LabStationPanel : public CraftingPanel {
public:
    /** Loads panel chemical_p. */
    LabStationPanel(Game &game, ServicesView &services);

    /** Opens on the first filter in the create view, for the creature whose Treat Injury and inventory it uses. */
    void open(const std::shared_ptr<Creature> &character);

private:
    /** The chemicalcreate.2da groups, in the order of the create filters. */
    enum class CreateGroup {
        Health,
        Stims,
        Mines,
        Grenades
    };

    /** ChemicalCreate, read when the panel loads. */
    std::shared_ptr<resource::TwoDA> _chemicalCreate;
    /** baseitems.2da, read when the panel loads: the breakdown list follows its store order. */
    std::shared_ptr<resource::TwoDA> _baseItems;

    /** Binds the create filters and reads the station's tables. */
    void onGUILoaded() override;

    /** ChemicalCreate. */
    std::shared_ptr<resource::TwoDA> createTable() const override;
    /** A row's skill requirement is in "skillLevel". */
    std::string skillColumn() const override { return "skillLevel"; }
    /** A row asks for no level or alignment. */
    bool checksLevelAndAlignment() const override { return false; }
    /** The row's template. */
    std::string rowTag(const resource::TwoDA &table, int row) const override;
    /** The item's chemical value. */
    int itemValue(const Item &item) const override;
    /** The character's items the station makes, apart from one group, by their base items' store order. */
    std::vector<std::shared_ptr<Item>> listBreakable(const std::shared_ptr<Creature> &character) const override;
    /** Back to the game. */
    void close(bool escape) override;
};

/** The kinds of item a workbench upgrades, in the order of KotOR's kind buttons and of TSL's filters after ALL. */
enum class UpgradableType {
    Lightsaber,
    Ranged,
    Melee,
    Armor
};

/** An item a workbench can upgrade, and where it is: worn by a creature in a slot, or carried by the party. */
struct UpgradableItem {
    /** The item itself. */
    RuntimeObjectRef<Item> item;
    /** The creature wearing it; none when carried. */
    RuntimeObjectRef<Creature> wearer;
    /** The equipment slot it is worn in; none when carried. */
    std::optional<int> slot;
    /** The kind of upgrades it takes. */
    UpgradableType type {UpgradableType::Lightsaber};
};

/** KotOR's list of the upgradable items of one kind (upgradeitems). */
class UpgradeItemsPanel : public GameGUI {
public:
    /** Loads panel upgradeitems. */
    UpgradeItemsPanel(Game &game, ServicesView &services);

    /** Escape goes back. */
    bool handle(const input::Event &event) override;
    /** Describes the item the pointer is on. */
    void update(float dt) override;

    /** Lists the items, the row holding selected chosen; picking one calls onPick, and Back or Escape onBack. */
    void open(std::vector<UpgradableItem> items,
              const std::shared_ptr<Item> &selected,
              std::function<void(const UpgradableItem &)> onPick,
              std::function<void()> onBack);

private:
    /** The screen's own controls. */
    struct Controls {
        std::shared_ptr<gui::Button> BTN_BACK;
        std::shared_ptr<gui::Button> BTN_UPGRADEITEM;
        std::shared_ptr<gui::ListBox> LB_DESCRIPTION;
        std::shared_ptr<gui::ListBox> LB_ITEMS;
    };

    /** The panel's controls. */
    Controls _controls;
    /** The listed items, by list row. */
    std::vector<UpgradableItem> _items;
    /** Runs with the item picked. */
    std::function<void(const UpgradableItem &)> _onPick;
    /** Runs on Back or Escape. */
    std::function<void()> _onBack;
    /** The list row described; none while the list is empty. */
    std::optional<int> _describedRow;

    /** Binds the controls and wires the list and the buttons. */
    void onGUILoaded() override;
    /** Picks the item of a list row. */
    void pick(int row);
    /** Describes the item of a list row. */
    void describe(int row);
};

/**
 * The panel that installs and removes an item's upgrades (upgrade). KotOR
 * keeps the upgrade.2da rows an item has installed; TSL keeps an upgrade in a
 * slot, the upgrade's properties added to the item. A lightsaber's colour
 * crystal changes the whole lightsaber.
 */
class ItemUpgradePanel : public GameGUI {
public:
    /** Loads panel upgrade. */
    ItemUpgradePanel(Game &game, ServicesView &services);

    /** Escape closes a list, else cancels. */
    bool handle(const input::Event &event) override;
    /** Describes the list entry the pointer is on, and turns an item playing no animation. */
    void update(float dt) override;

    /** Whether the panel opens for the item: a lightsaber's tag must name its colour in upcrystals.2da. */
    bool opensFor(const Item &item, UpgradableType type) const;
    /**
     * Opens for an item taken out of its place. wearer is the creature that
     * wore it, none when it was carried, which KotOR asks about before an
     * upgrade it couldn't wear; onClose gets the item as it ends, which may be
     * another object.
     */
    void open(std::shared_ptr<Item> item,
              UpgradableType type,
              const std::shared_ptr<Creature> &wearer,
              std::function<void(std::shared_ptr<Item>)> onClose);

    /** upgrade.2da, as the panel read it when it loaded, which tells what an item's upgrades fit. */
    const resource::TwoDA &upgradeTable() const { return *_upgradeTable; }

protected:
    /** KotOR's Assemble plays the upgrade sound in place of the click. */
    void onClick(const std::string &control) override;
    /** Pointing at a slot describes it. */
    void onSelectionChanged(const std::string &control, bool selected) override;

private:
    /** Where an upgrade goes on one kind of item. */
    struct SlotInfo {
        /** The upgrade.2da UpgradeType the slot takes; none for a lightsaber's colour crystal. */
        std::optional<int> upgradeType;
        /** The icon the slot shows while empty. */
        std::string icon;
        /** The slot's name. */
        int nameStrRef {0};
        /** The tag of the slot's button; its label's tag differs only in the prefix. */
        std::string button;
    };

    /** A slot as the panel shows it. */
    struct Slot {
        /** The upgrade the slot holds; none while it is empty. */
        std::shared_ptr<Item> held;
        /** The upgrade.2da row the slot answers to; none for the colour crystal's slot. */
        std::optional<int> row;
        /** The template of that row, which is the tag of its upgrade. */
        std::string tag;
        /** The slot's button, showing its upgrade or its empty icon. */
        std::shared_ptr<gui::Button> button;
        /** The label under the slot's button. */
        std::shared_ptr<gui::Label> label;
    };

    /** An entry of a slot's list: a party item, the slot's current upgrade, or none. */
    struct Candidate {
        /** The upgrade; none for the entry that empties the slot. */
        std::shared_ptr<Item> item;
        /**
         * The upgrade.2da row of an upgrade, or the upcrystals.2da row of a
         * colour crystal; none for the entry that empties the slot and for
         * the current colour crystal.
         */
        std::optional<int> row;
        /** The slot already holds it. */
        bool current {false};
        /** TSL: the upgrade is above the item's upgrade level. */
        bool disabled {false};
    };

    /** An armour look: the model armour is shown in, and its texture's name before the variation. */
    struct ArmourLook {
        std::string model;
        /** Blank for a look that keeps the model's own texture. */
        std::string texture;
    };

    /** The panel's controls; KotOR alone has the lightsaber description and the upgrade count, TSL alone OK. */
    struct Controls {
        std::shared_ptr<gui::Button> BTN_ASSEMBLE;
        std::shared_ptr<gui::Button> BTN_BACK;
        std::shared_ptr<gui::Button> BTN_OK;
        std::shared_ptr<gui::Label> LBL_DESCBG;
        std::shared_ptr<gui::Label> LBL_DESCBG_LS;
        std::shared_ptr<gui::Label> LBL_LSSLOTNAME;
        std::shared_ptr<gui::Label> LBL_PROPERTY;
        std::shared_ptr<gui::Label> LBL_SLOTNAME;
        std::shared_ptr<gui::Label> LBL_TITLE;
        std::shared_ptr<gui::Label> LBL_UPGRADES;
        std::shared_ptr<gui::Label> LBL_UPGRADE_COUNT;
        std::shared_ptr<gui::ListBox> LB_DESC;
        std::shared_ptr<gui::ListBox> LB_DESC_LS;
        std::shared_ptr<gui::ListBox> LB_ITEMS;
        /** The 3D view of every item but a lightsaber. */
        std::shared_ptr<gui::Label> THREE_D_MODEL;
        /** The 3D view of a lightsaber. */
        std::shared_ptr<gui::Label> THREE_D_MODEL_LS;
        /** Every slot button of the game and its label, by the button's tag. */
        std::map<std::string, std::pair<std::shared_ptr<gui::Button>, std::shared_ptr<gui::Label>>> slots;
    };

    /** The panel's controls. */
    Controls _controls;
    /** upgrade.2da, read when the panel loads. */
    std::shared_ptr<resource::TwoDA> _upgradeTable;
    /** upcrystals.2da, read when the panel loads: the lightsaber colours. */
    std::shared_ptr<resource::TwoDA> _crystalTable;
    /** The model and texture of each armour look, in look order: appearance.2da row 137's. */
    std::vector<ArmourLook> _armourLooks;
    /** The item shown in the 3D view; none while the panel is closed or when the item's model does not load. */
    std::shared_ptr<scene::ModelSceneNode> _itemModel;
    /** The texture an armour is drawn with in the 3D view; none for other items. */
    std::shared_ptr<graphics::Texture> _itemTexture;
    /** The kind of the item upgraded. */
    UpgradableType _type {UpgradableType::Lightsaber};
    /** The item upgraded. */
    std::shared_ptr<Item> _item;
    /** The item as it was opened, which Back restores. */
    std::shared_ptr<Item> _backup;
    /** The creature that wore the item; none when it was carried. */
    RuntimeObjectRef<Creature> _wearer;
    /** The slots of the item's kind. */
    std::vector<Slot> _slots;
    /** Upgrades taken from the party inventory this opening. */
    std::vector<std::shared_ptr<Item>> _taken;
    /** The stacks upgrades the item held went back to this opening. */
    std::vector<std::shared_ptr<Item>> _returned;
    /** The entries of the open list, by list row. */
    std::vector<Candidate> _candidates;
    /** The slot whose list is open; none while the slots are shown. */
    std::optional<int> _listSlot;
    /** The list row described; none until one is. */
    std::optional<int> _describedCandidate;
    /** Runs with the item as it ends. */
    std::function<void(std::shared_ptr<Item>)> _onClose;

    /** Reads the tables and binds this game's controls and slots. */
    void onGUILoaded() override;

    /** The slots of the item's kind, in this game. */
    const std::vector<SlotInfo> &slotInfos() const;
    /** TSL: the first upgrade.2da row with the same template as the given row, which may be that row. */
    int firstRowWithTemplateOf(int row) const;
    /** The list describing the item: a KotOR lightsaber has one of its own. */
    gui::ListBox &itemDescription() const;
    /** The upcrystals.2da row listing the lightsaber's colour; none when no row does. */
    std::optional<int> crystalRow(const Item &item) const;
    /** The inventory upgrades come from and go back to. */
    std::shared_ptr<Object> partyInventory() const;
    /** Puts the upgrades the item holds in their slots, and dims a slot the party has no upgrade for. */
    void placeInstalledUpgrades();
    /** Shows the slots, closing a list. */
    void showSlots();
    /** Lists a slot's choices. */
    void showList(int slot);
    /** Shows each slot's upgrade, or its empty icon, and describes the item. */
    void refreshSlots();
    /** Shows the item in its 3D view: on opening and after the colour changes. */
    void refresh3D();
    /** The model the 3D view shows the item in. */
    std::string viewedModelName() const;
    /** Describes a slot and what it holds. */
    void describeSlot(int slot);
    /** KotOR installs or removes a weapon's or armour's upgrade at a click; everything else lists the slot's choices. */
    void clickSlot(int slot);
    /** Whether the item's wearer, if it has one, could no longer wear the item. */
    bool wearerRefusesItem() const;
    /** Installs an upgrade from the party inventory in a KotOR slot. */
    void install(int slot, const std::shared_ptr<Item> &upgrade);
    /** Puts the list entry in the open list's slot. */
    void pick(int entry);
    /** Replaces the lightsaber with the template of an upcrystals.2da row's colour, keeping its upgrades. */
    void swapLightsaber(int crystalRow);
    /** The item ceases to exist, leaving the holder it was upgraded in. */
    void destroyItem();
    /** Takes one upgrade off a party stack. */
    std::shared_ptr<Item> take(const std::shared_ptr<Item> &stack);
    /** Returns an upgrade to the party inventory. */
    void giveBack(const std::shared_ptr<Item> &upgrade);
    /** Keeps the item as it is now and closes. */
    void assemble();
    /** Restores the item and every upgrade moved, and closes. */
    void cancel();
    /** Disposes of the slots' upgrades and hands the item on. */
    void finish();
};

/**
 * The workbench (ShowUpgradeScreen). KotOR picks a kind of item and then the
 * item; TSL lists the items by filter, and also opens creation.
 */
class UpgradeSelectionGUI : public GameGUI {
public:
    /** Loads panel upgradesel and the panels it opens. */
    UpgradeSelectionGUI(Game &game, ServicesView &services);

    /** Loads every panel. */
    void init() override;
    /** Input goes to the panel open over the screen; otherwise Escape closes the screen. */
    bool handle(const input::Event &event) override;
    /** The panel open over the screen is updated in place of TSL's description of the list. */
    void update(float dt) override;
    /** The panel open is drawn over the screen. */
    void render() override;

    /** Opens for a script's request: an item's upgrades at once, or the selection (with disableUpgrade, creation). */
    void open(UpgradeScreenRequest request);

protected:
    /** KotOR: pointing at a kind button makes it the kind Upgrade Items opens. */
    void onSelectionChanged(const std::string &control, bool selected) override;

private:
    /** TSL's filters of the upgrade list, in the order of their buttons. */
    enum class UpgradeFilter {
        All,
        Lightsaber,
        Ranged,
        Melee,
        Armor
    };

    /** The panel open over the screen, which has the input. */
    enum class OpenPanel {
        None,
        Creation,
        Items,
        Upgrade
    };

    /** Where an item being upgraded was, so that it goes back. */
    struct Origin {
        /** The creature that wore it; none when carried. */
        RuntimeObjectRef<Creature> wearer;
        /** The equipment slot it was worn in; none when carried. */
        std::optional<int> slot;
        /** The other weapon hand, also taken off; none when the wearer held one weapon. */
        RuntimeObjectRef<Item> otherHand;
        /** The item was the off-hand weapon. */
        bool offHand {false};
        /** The item came from the script; closing its panel closes the workbench. */
        bool direct {false};
        /** A script's item the party doesn't carry, upgraded where it is; none otherwise. */
        RuntimeObjectRef<Item> leftInPlace;
        /** The item's kind, whose list KotOR shows again. */
        UpgradableType type {UpgradableType::Lightsaber};
    };

    /** A KotOR kind button and its label. */
    struct KindButton {
        /** The kind the button opens. */
        UpgradableType kind {UpgradableType::Lightsaber};
        /** The button, enabled when the party has an item of the kind. */
        std::shared_ptr<gui::Button> button;
        /** The kind's label, shown when the button is enabled. */
        std::shared_ptr<gui::Label> label;
    };

    /** The screen's own controls; the list, its description and Create Items are TSL's. */
    struct Controls {
        std::shared_ptr<gui::Button> BTN_BACK;
        std::shared_ptr<gui::Button> BTN_CREATEITEMS;
        std::shared_ptr<gui::Button> BTN_UPGRADEITEMS;
        std::shared_ptr<gui::ListBox> LB_DESCRIPTION;
        std::shared_ptr<gui::ListBox> LB_UPGRADELIST;
    };

    /** The screen's own controls. */
    Controls _controls;
    /** KotOR's kind buttons. */
    std::vector<KindButton> _kindButtons;
    /** KotOR: the kinds the party has an item of. */
    std::set<UpgradableType> _kindsHeld;
    /** KotOR: the kind Upgrade Items opens, the last pointed at; none when the party has no item of that kind. */
    std::optional<UpgradableType> _hoveredKind;
    /** TSL's filter buttons and the filter each one picks. */
    std::vector<std::pair<UpgradeFilter, std::shared_ptr<gui::Button>>> _filterButtons;
    /** TSL: the filter the list shows. */
    UpgradeFilter _filter {UpgradeFilter::All};
    /** TSL: the listed items, by list row. */
    std::vector<UpgradableItem> _rows;
    /** TSL: the list row described; none while the list is empty. */
    std::optional<int> _describedRow;
    /** KotOR: Upgrade Items' text colour in the layout. */
    glm::vec3 _upgradeItemsTextColor {1.0f};

    /** TSL's creation panel. */
    std::unique_ptr<ItemCreationPanel> _creation;
    /** KotOR's list of the items of a kind. */
    std::unique_ptr<UpgradeItemsPanel> _items;
    /** The upgrade panel, in both games. */
    std::unique_ptr<ItemUpgradePanel> _upgrade;
    /** The panel open over the screen. */
    OpenPanel _openPanel {OpenPanel::None};
    /** Where the item being upgraded was. */
    Origin _origin;

    /** The creature the script named, whose Repair and inventory creation uses. */
    RuntimeObjectRef<Creature> _character;
    /** Creation is all the workbench offers. */
    bool _disableUpgrade {false};
    /** The table a script named in place of ItemCreate. */
    std::string _itemCreateTable;
    /** The breakdown factor the character's Repair gave when the workbench opened. */
    float _repairFactor {0.0f};

    /** The kind of item a filter lists; none for ALL, which lists every kind. */
    static std::optional<UpgradableType> kindOf(UpgradeFilter filter);

    /** Binds this game's controls. */
    void onGUILoaded() override;
    /** The party's upgradable items, worn first and then carried; with a kind, only that kind's. */
    std::vector<UpgradableItem> upgradableItems(std::optional<UpgradableType> kind) const;
    /** KotOR offers each kind the party has an item of. */
    void showKinds();
    /** KotOR: Upgrade Items takes up the kind pointed at, showing whether the party has an item of it. */
    void describeKind(UpgradableType kind);
    /** TSL lists the items of the filter. */
    void populate();
    /** KotOR lists the items of a kind, the row holding selected chosen. */
    void openItems(UpgradableType kind, const std::shared_ptr<Item> &selected);
    /**
     * Takes the item out of its place and opens the upgrade panel for it.
     *
     * @return false, leaving the item in place, when the panel refuses it
     */
    bool beginUpgrade(const UpgradableItem &entry, bool direct);
    /** The upgrade panel closed with the item as it ends. */
    void finishUpgrade(std::shared_ptr<Item> item);
    /** Returns the item to where it was, or the stack it joined. */
    std::shared_ptr<Item> putBack(const std::shared_ptr<Item> &item);
    /** Opens the creation panel over the screen. */
    void openCreation();
    /** The creation panel closed: with creation all there is, the workbench closes too. */
    void onCreationClosed(bool escape);
    /** Back to the game. */
    void close();
};

} // namespace game

} // namespace reone
