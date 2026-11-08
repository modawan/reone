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

#include <array>
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
    /** Creation opens at once, under the item's upgrades when an item is named, and closing it closes the workbench. */
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
    CraftingPanel(Game &game, ServicesView &services, HeldCount count, SkillType breakdownSkill);

    /**
     * Escape closes the panel; Return or keypad Enter chooses the selected
     * entry, and letting it go leaves Accept idle on that entry. Up and Down
     * move the selection.
     */
    bool handle(const input::Event &event) override;
    /** Describes the selected entry, which a moved selection leaves to Accept. */
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

    /** A stocked template item and the table row that stocked it. */
    struct Stocked {
        /** The template item, holding one more for each identical row stocked after it. */
        std::shared_ptr<Item> item;
        /** The first table row that stocked the item. */
        int row {0};
        /** The stack the row's template has, which one entry makes. */
        int madeStack {1};
    };

    /** A row of the create list: a template item, the table row it answers to, and whether the leader can make it. */
    struct Creatable {
        /** The template item the entry makes a copy of. */
        std::shared_ptr<Item> item;
        /** The table row that stocked the item, by which it is described and made. */
        int row {0};
        /** The leader's rank in the row's skill reaches the row's requirement. */
        bool available {false};
        /** The stack one making gives. */
        int madeStack {1};
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

    /** Binds both stations' controls, colours the lists and wires the buttons, which never take the keyboard. */
    void onGUILoaded() override;
    /** Clicking the lists is silent. */
    void onClick(const std::string &control) override;

    /**
     * Opens the create view for a character. A negative party count is
     * reset to none first. The count shown is the character's; the breakdown
     * factor is fixed by whoever opens the panel.
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
    /** Whether a create row whose template is missing stocks a blank item. */
    virtual bool stocksMissingTemplates() const = 0;
    /** Whether the tokens in an item's description are resolved. */
    virtual bool resolvesDescriptionTokens() const = 0;
    /** Whether showing the create view clears the skill line, which otherwise keeps the last entry's. */
    virtual bool clearsSkillLineOnCreateView() const = 0;
    /** What an item is worth at this station. */
    virtual int itemValue(const Item &item) const = 0;
    /** The character's items the breakdown list offers under the current filter, in order. */
    virtual std::vector<std::shared_ptr<Item>> listBreakable(const std::shared_ptr<Creature> &character) const = 0;
    /** The message refusing to break an item down, if the station refuses it. */
    virtual std::optional<int> breakdownRefusal(const Item &item) const { return std::nullopt; }
    /** Closes the panel; escape tells a key from the Cancel button. */
    virtual void close(bool escape) = 0;

private:
    /**
     * What Accept does with the current list's selected row. After Return or
     * keypad Enter is released on a row, Accept does nothing until the
     * selection moves. After a choice by keyboard or double click, Accept
     * repeats it, a create entry with its click. Otherwise Accept acts
     * silently.
     */
    enum class AcceptAction {
        ActSilently,
        ChooseAgain,
        None
    };

    /** The count the stations make items from and break them into. */
    HeldCount _count;
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
    /** The station's stock under the current group, in list order: what the create list is built from. */
    std::vector<Stocked> _stock;
    /** The create list's entries, by list row. */
    std::vector<Creatable> _creatable;
    /** The breakdown list's items, by list row. */
    std::vector<RuntimeObjectRef<Item>> _breakable;
    /** The list row described; none while the list is empty. */
    std::optional<int> _describedRow;
    /** The current list's row Accept's action is set for; none while no row is selected. */
    int _acceptRow {-1};
    /** What Accept does with that row. */
    AcceptAction _acceptAction {AcceptAction::ActSilently};

    /** The list of the current view. */
    gui::ListBox &currentList() const;
    /** Makes or breaks down the entry at \p row of \p list, with the create list's click. */
    void choose(const gui::ListBox &list, int row);
    /** Accept does with the selected row what _acceptAction says. */
    void accept();
    /** Shows a view: its strings, controls and freshly built list, which holds the keyboard. */
    void showView(View view);
    /** Highlights the current filter of each view. */
    void colourFilters();

    /**
     * Stocks the station from its table under the current group. A blank
     * cell ends the table: only the rows before it are stocked.
     */
    void restockCreatable();
    /**
     * Builds the create list from the stock, each entry answering to the row
     * that stocked it; counts are in the row count font.
     */
    void showCreateList();
    /**
     * Adds a template item a table row stocks; plot items are left out. The
     * latest stocked item with the item's tag takes one more when identical
     * to it; otherwise identical stocked items, latest first, take what their
     * stacks have room for, and the rest is stocked on its own.
     */
    void addCreatable(const std::shared_ptr<Item> &item, int row);
    /** Builds the breakdown list, choosing the given row or the nearest one left. */
    void fillBreakdownList(int keepRow);
    /** Chooses and describes a row of the current list, the nearest one if it is past the end. */
    void showList(int row);
    /** Clears the cost, stock and descriptions. */
    void clearFocus();

    /** Describes a row of the current list. */
    void describe(int row);
    /**
     * Shows an entry's cost and, by its row, the skill line, stock, skill
     * requirement and item.
     */
    void describeCreatable(const Creatable &creatable);
    /** Shows what breaking the item down yields, how many there are, and the item. */
    void describeBreakable(const Item &item);
    /** An item's description, its tokens resolved where the station resolves them. */
    std::string itemText(const Item &item) const;
    /** How many of the item the party holds; -1 for an infinite carried match. */
    int stockOf(const Item &item) const;
    /** What breaking the item down adds to the count: at least one. */
    int breakdownGain(const Item &item) const;

    /** Makes the entry, or breaks the item down, of a row of the current list. */
    void act(int row);
    /**
     * Makes an entry, refusing a position that is unavailable or what the
     * count can't pay, and asking first above the threshold. An entry left
     * from a previous group makes nothing.
     */
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
    /**
     * The table the create list is built from while the panel is open:
     * ItemCreate, or the table a script named, none when that does not exist.
     */
    std::shared_ptr<resource::TwoDA> _createTable;
    /**
     * The table of items the breakdown leaves out while the panel is open:
     * ChemicalCreate (what the lab station makes), or TutChemCreate when a
     * script named a table, none when TutChemCreate does not exist.
     */
    std::shared_ptr<resource::TwoDA> _excludedTable;
    /** Runs when the panel closes, told whether a key closed it. */
    std::function<void(bool)> _onClose;

    /** Binds the create and breakdown filters and reads ItemCreate. */
    void onGUILoaded() override;

    /** ItemCreate, or the table a script named, which lists nothing when it does not exist. */
    std::shared_ptr<resource::TwoDA> createTable() const override;
    /** A row's skill requirement is in "skill". */
    std::string skillColumn() const override { return "skill"; }
    /** A row also asks for a level and an alignment. */
    bool checksLevelAndAlignment() const override { return true; }
    /** A row whose template is missing stocks nothing. */
    bool stocksMissingTemplates() const override { return false; }
    /** An item's description has its tokens resolved. */
    bool resolvesDescriptionTokens() const override { return true; }
    /** Showing the create view clears the skill line. */
    bool clearsSkillLineOnCreateView() const override { return true; }
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

    /** Binds the create filters. */
    void onGUILoaded() override;

    /** ChemicalCreate, read at start-up. */
    std::shared_ptr<resource::TwoDA> createTable() const override;
    /** A row's skill requirement is in "skillLevel". */
    std::string skillColumn() const override { return "skillLevel"; }
    /** A row asks for no level or alignment. */
    bool checksLevelAndAlignment() const override { return false; }
    /** A row whose template is missing stocks a blank item. */
    bool stocksMissingTemplates() const override { return true; }
    /** An item's description is shown as it stands. */
    bool resolvesDescriptionTokens() const override { return false; }
    /** Showing the create view keeps the skill line. */
    bool clearsSkillLineOnCreateView() const override { return false; }
    /** The item's chemical value. */
    int itemValue(const Item &item) const override;
    /** The character's items the station makes, apart from one group, by their base items' store order. */
    std::vector<std::shared_ptr<Item>> listBreakable(const std::shared_ptr<Creature> &character) const override;
    /** Back to the screen the station was opened over. */
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

    /** Escape goes back; Return or keypad Enter picks the selected item, and Up and Down move the selection. */
    bool handle(const input::Event &event) override;
    /** Describes the selected item. */
    void update(float dt) override;

    /**
     * Lists the items and gives the list the keyboard; picking one calls
     * onPick, and Back or Escape onBack. The first row holding selected,
     * which the items include, is chosen; without selected, the first row.
     */
    void open(std::vector<UpgradableItem> items,
              const std::shared_ptr<Item> &selected,
              std::function<void(const UpgradableItem &)> onPick,
              std::function<void()> onBack);
    /**
     * Back from upgrading the item last picked: the list stays while that
     * row holds the item as it ends, and the first row holding it is chosen.
     * An item its wearer no longer wears is held by the row afresh, carried.
     *
     * @param held the upgraded item, or the stack it joined
     * @return false when the row holds another item, and the list is to be rebuilt
     */
    bool keepAfterUpgrade(const std::shared_ptr<Item> &held);

protected:
    /** Clicking the lists is silent. */
    void onClick(const std::string &control) override;

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
    /** The list row last picked. */
    int _pickedRow {0};

    /** Binds the controls and wires the list and the buttons, which never take the keyboard. */
    void onGUILoaded() override;
    /** Adds a list row for each item. */
    void addRows();
    /** The first list row holding the item; -1 when the list doesn't hold it. */
    int rowOf(const std::shared_ptr<Item> &item) const;
    /** Chooses and describes a list row; -1 chooses none and describes nothing. */
    void select(int row);
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

    /** Escape closes a list, else cancels; the other keys go to the control holding the keyboard. */
    bool handle(const input::Event &event) override;
    /** Describes the selected list entry, and turns an item standing on no hook of the light model. */
    void update(float dt) override;

    /**
     * Builds both 3D views afresh, each with its room and light model, which
     * then stay while the workbench is open: items are swapped in them, and
     * the light model's turn carries on from one item to the next.
     */
    void build3DViews();

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
    /** Assemble plays the upgrade sound in place of the click; clicking the lists is silent. */
    void onClick(const std::string &control) override;
    /** A slot button selected describes the slot it acts for. */
    void onSelectionChanged(const std::string &control, bool selected) override;

private:
    /** The most slots a kind of item has: the slot records the panel keeps. */
    static constexpr int kSlotRecordCount = 6;

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
        /** The slot record the slot uses, which the slots of other kinds at the same place share. */
        int record {0};
    };

    /**
     * What a slot answers to, kept from one item to the next while the panel
     * lives: an opening rewrites only the records its rows reach.
     */
    struct SlotRecord {
        /** The upgrade.2da row the slot answers to; none until a row is placed in the record. */
        std::optional<int> row;
        /** The template of a row placed in the record, which is the tag of its upgrade. */
        std::string tag;
    };

    /** A slot as the panel shows it. */
    struct Slot {
        /** The upgrade the slot holds; none while it is empty. */
        std::shared_ptr<Item> held;
        /** The slot's button. */
        std::shared_ptr<gui::Button> button;
        /** The label under the slot's button, showing its upgrade or its empty icon, dimmed while the party has nothing for it. */
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
        /** The model armour of this look is shown in. */
        std::string model;
        /** Blank for a look that keeps the model's own texture. */
        std::string texture;
    };

    /** A 3D view: the scene it shows and the light model whose hooks hold the camera and the item. */
    struct ItemView {
        /** The scene the view's control shows. */
        std::string sceneName;
        /** The light model, standing in the scene's room. */
        std::shared_ptr<scene::ModelSceneNode> light;
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
    /** The 3D view of every item but a lightsaber. */
    ItemView _itemView {kSceneUpgrade};
    /** The 3D view of a lightsaber. */
    ItemView _lightsaberView {kSceneUpgradeLightsaber};
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
    /** The slot records, by place. */
    std::array<SlotRecord, kSlotRecordCount> _slotRecords;
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
    /**
     * Gives a key to the control holding the keyboard: the open list, a
     * slot's button, or a control the arrows moved the keyboard to.
     *
     * @return whether the key was taken
     */
    bool handleActiveControlKey(const input::KeyEvent &key);

    /** The slots of the item's kind, in this game. */
    const std::vector<SlotInfo> &slotInfos() const;
    /**
     * The slot of the item's kind a slot button acts for: the one whose
     * record is the button's place in its row, hidden or not.
     *
     * @return none for a control that is no slot button, or a place where the
     *         kind has no slot
     */
    std::optional<int> slotOfButton(const std::string &tag) const;
    /** The record of a slot of the item's kind. */
    SlotRecord &slotRecord(int slot);
    /** The item's properties keyed to the row of a slot's record; empty while the record holds no row. */
    std::string slotPropertyText(int slot);
    /** TSL: the first upgrade.2da row with the same template as the given row, which may be that row. */
    int firstRowWithTemplateOf(int row) const;
    /** The list describing the item: a KotOR lightsaber has one of its own. */
    gui::ListBox &itemDescription() const;
    /** The upcrystals.2da row listing the lightsaber's colour with a template; none when no row does. */
    std::optional<int> crystalRow(const Item &item) const;
    /** The inventory upgrades come from and go back to. */
    std::shared_ptr<Object> partyInventory() const;
    /** Links the slot buttons of the item's kind for the arrow keys, over the layout's links in those directions. */
    void linkSlotButtons();
    /** Puts the upgrades the item holds in their slots, and dims or lights each slot by the upgrades the party has for it. */
    void placeInstalledUpgrades();
    /**
     * KotOR gives the keyboard to the first slot of the item's kind, selecting
     * its button without a sound, which describes the slot unless the button
     * held the keyboard already.
     */
    void activateFirstSlot();
    /** Shows the slots, closing a list. */
    void showSlots();
    /**
     * Closes the open list, giving the keyboard back to the slot it was for
     * and selecting its button without a sound, and describes that slot.
     */
    void closeList();
    /**
     * Lists a slot's choices, hiding the slots and their texts; the list takes
     * the keyboard. An empty colour slot lists nothing.
     */
    void showList(int slot);
    /** Shows each slot's upgrade, or its empty icon, and describes the item; TSL lights the slots holding an upgrade. */
    void refreshSlots();
    /** Shows the item in its 3D view: on opening and after the colour changes. */
    void refresh3D();
    /** The 3D view of the item's kind. */
    ItemView &shownView();
    /** Takes the shown item out of its 3D view. */
    void removeShownItem();
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
    /**
     * Returns an upgrade to the party inventory.
     *
     * @return the stack the upgrade joined, or the upgrade itself
     */
    std::shared_ptr<Item> giveBack(const std::shared_ptr<Item> &upgrade);
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
    /**
     * Input goes to the panel open over the screen; otherwise Escape closes
     * the screen, in TSL Return or keypad Enter upgrades the selected item
     * and Up and Down move the selection, and in KotOR the keys go to the kind
     * button holding the keyboard.
     */
    bool handle(const input::Event &event) override;
    /** The panel open over the screen is updated in place of TSL's description of the list. */
    void update(float dt) override;
    /** The panel open is drawn over the screen. */
    void render() override;

    /**
     * Opens for a script's request: an item's upgrades at once, or the
     * selection. With disableUpgrade, creation opens first, a script's item
     * opening over it.
     */
    void open(UpgradeScreenRequest request);

protected:
    /** TSL: clicking the lists is silent. */
    void onClick(const std::string &control) override;
    /** KotOR: leaving Upgrade Items gives back its text colour. */
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
        /**
         * What holds a script's item the party doesn't carry, which goes back
         * there, or the one item split off its stack there; none otherwise.
         */
        RuntimeObjectRef<Object> holder;
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
    /** KotOR: the kind Upgrade Items opens, the last taken up; none when the party has no item of that kind. */
    std::optional<UpgradableType> _activeKind;
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
    /** KotOR's list of the items of a kind, drawn under the upgrade panel; TSL only draws it, empty. */
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
    /** KotOR: the kind's button becomes the active one, holding the keyboard, and Upgrade Items takes up the kind, showing whether the party has an item of it. */
    void describeKind(UpgradableType kind);
    /**
     * KotOR: gives a key to the kind button holding the keyboard, or to a
     * control the arrows moved the keyboard to.
     *
     * @return whether the key was taken
     */
    bool handleKindKey(const input::KeyEvent &key);
    /** TSL lists the items of the filter. */
    void populate();
    /** KotOR lists the items of a kind, the row holding selected chosen. */
    void openItems(UpgradableType kind, const std::shared_ptr<Item> &selected);
    /** Takes the item out of its place and opens the upgrade panel for it. */
    void beginUpgrade(const UpgradableItem &entry, bool direct);
    /**
     * The upgrade panel closed with the item as it ends. KotOR's list of items
     * stays while the row picked still holds the item, and is rebuilt when
     * the item became another: Back's, another colour's, or a stack it joined.
     */
    void finishUpgrade(std::shared_ptr<Item> item);
    /**
     * Returns the item to where it was, worn again when its wearer can wear
     * it, with the other hand's weapon; returns the item, or the stack it
     * joined.
     */
    std::shared_ptr<Item> putBack(const std::shared_ptr<Item> &item);
    /** Opens the creation panel over the screen. */
    void openCreation();
    /** The creation panel closed: with creation all there is, the workbench closes too. */
    void onCreationClosed(bool escape);
    /** Back to the screen the workbench was opened over. */
    void close();
};

} // namespace game

} // namespace reone
