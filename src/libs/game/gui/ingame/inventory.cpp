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

#include "reone/game/gui/ingame/inventory.h"

#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"
#include "reone/gui/control/listbox.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/itemdescription.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/party.h"
#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/textures.h"

#include <algorithm>

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

static constexpr char kEquippedItemSuffix[] = " (equipped)";
static constexpr char kK1InventoryTitlePrefix[] = "Party Inventory - ";
static constexpr glm::vec3 kK2InventoryFilterSelectedColor {1.0f, 1.0f, 1.0f};

static bool isInventoryListedEquipmentSlot(int slot) {
    switch (slot) {
    case InventorySlots::head:
    case InventorySlots::body:
    case InventorySlots::hands:
    case InventorySlots::rightWeapon:
    case InventorySlots::leftWeapon:
    case InventorySlots::leftArm:
    case InventorySlots::rightArm:
    case InventorySlots::implant:
    case InventorySlots::belt:
        return true;
    default:
        return false;
    }
}

static void tintK2PanelFill(const std::shared_ptr<ListBox> &listBox, const glm::vec3 &baseColor) {
    if (!listBox) {
        return;
    }
    listBox->setBorderColor(baseColor);
    listBox->setTintBorderFill(true);
}

static void enableBorderFillTint(const std::shared_ptr<Label> &label) {
    if (!label) {
        return;
    }
    label->setTintBorderFill(true);
}

struct BaseItemFilterInfo {
    std::optional<int> itemType;
    std::optional<int> storePanelSort;
    std::optional<int> weaponType;
};

static BaseItemFilterInfo getBaseItemFilterInfo(ServicesView &services, const Item &item) {
    BaseItemFilterInfo info;
    auto baseItems = services.resource.twoDas.get("baseitems");
    if (!baseItems) {
        return info;
    }

    int baseItemType = item.baseItemType();
    info.itemType = baseItems->getIntOpt(baseItemType, "itemtype");
    info.storePanelSort = baseItems->getIntOpt(baseItemType, "storepanelsort");
    info.weaponType = baseItems->getIntOpt(baseItemType, "weapontype");
    return info;
}

static bool isDatapad(const Item &item, const BaseItemFilterInfo &baseItem) {
    return baseItem.itemType == 24 || item.itemClass() == "i_datapad";
}

static bool isWeapon(const Item &item, const BaseItemFilterInfo &baseItem) {
    if (item.weaponType() != WeaponType::None) {
        return true;
    }
    if (baseItem.weaponType && *baseItem.weaponType != static_cast<int>(WeaponType::None)) {
        return true;
    }
    if (baseItem.itemType) {
        switch (*baseItem.itemType) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 39:
        case 40:
        case 41:
            return true;
        default:
            break;
        }
    }
    if (baseItem.storePanelSort) {
        switch (*baseItem.storePanelSort) {
        case 20:
        case 25:
        case 30:
        case 35:
        case 40:
        case 45:
            return true;
        default:
            break;
        }
    }
    return false;
}

static bool isArmor(const Item &item, const BaseItemFilterInfo &baseItem) {
    return item.isEquippable() && !isWeapon(item, baseItem);
}

static bool isUseable(const Item &item, const BaseItemFilterInfo &baseItem) {
    if (item.activateSpell()) {
        return true;
    }
    if (!baseItem.storePanelSort) {
        return false;
    }

    switch (*baseItem.storePanelSort) {
    case 1:
    case 50:
        return true;
    case 80:
        return !isDatapad(item, baseItem);
    default:
        return false;
    }
}

static bool isUtility(const Item &item, const BaseItemFilterInfo &baseItem) {
    if (item.plotFlag() || isWeapon(item, baseItem) || isArmor(item, baseItem) || isUseable(item, baseItem)) {
        return false;
    }
    if (isDatapad(item, baseItem)) {
        return true;
    }
    if (baseItem.storePanelSort) {
        switch (*baseItem.storePanelSort) {
        case 2:
        case 85:
        case 90:
        case 95:
            return true;
        default:
            break;
        }
    }
    if (baseItem.itemType) {
        switch (*baseItem.itemType) {
        case 27:
        case 28:
        case 29:
        case 30:
        case 42:
        case 43:
        case 46:
        case 50:
        case 51:
            return true;
        default:
            break;
        }
    }
    return false;
}

static InventoryFilter nextK1Filter(InventoryFilter filter) {
    switch (filter) {
    case InventoryFilter::All:
        return InventoryFilter::Quest;
    case InventoryFilter::Quest:
        return InventoryFilter::Equippable;
    case InventoryFilter::Equippable:
        return InventoryFilter::Utility;
    case InventoryFilter::Utility:
        return InventoryFilter::Useable;
    case InventoryFilter::Useable:
        return InventoryFilter::All;
    default:
        return InventoryFilter::All;
    }
}

static std::string k1FilterTitle(InventoryFilter filter) {
    switch (filter) {
    case InventoryFilter::Quest:
        return std::string(kK1InventoryTitlePrefix) + "Quest Items";
    case InventoryFilter::Equippable:
        return std::string(kK1InventoryTitlePrefix) + "Equippable";
    case InventoryFilter::Utility:
        return std::string(kK1InventoryTitlePrefix) + "Utility Items";
    case InventoryFilter::Useable:
        return std::string(kK1InventoryTitlePrefix) + "Useable Items";
    case InventoryFilter::New:
        return std::string(kK1InventoryTitlePrefix) + "New Items";
    default:
        return std::string(kK1InventoryTitlePrefix) + "All Items";
    }
}

static std::string k1NextFilterAction(InventoryFilter filter) {
    switch (nextK1Filter(filter)) {
    case InventoryFilter::Quest:
        return "Show Quest Items";
    case InventoryFilter::Equippable:
        return "Show Equippable";
    case InventoryFilter::Utility:
        return "Show Utility Items";
    case InventoryFilter::Useable:
        return "Show Useable Items";
    default:
        return "Show All Items";
    }
}

static void updateK2FilterButton(const std::shared_ptr<Button> &button, bool selected, const glm::vec3 &baseColor) {
    if (!button) {
        return;
    }

    button->setSelected(selected);
    button->setTextColor(selected ? kK2InventoryFilterSelectedColor : baseColor);
}

void InventoryMenu::onGUILoaded() {
    loadBackground(BackgroundType::Menu);
    bindControls();

    if (_controls.LBL_CREDITS_VALUE) {
        _controls.LBL_CREDITS_VALUE->setVisible(false);
    }
    if (_controls.BTN_USEITEM) {
        _controls.BTN_USEITEM->setDisabled(true);
    }
    if (_controls.BTN_EXIT) {
        _controls.BTN_EXIT->setOnClick([this]() {
            _game.openInGame();
        });
    }
    if (_controls.BTN_ALL) {
        _controls.BTN_ALL->setSelected(true);
    }

    configureItemsListBox();
    configureFilterControls();
    if (_game.isTSL()) {
        enableBorderFillTint(_controls.LBL_BAR1);
        enableBorderFillTint(_controls.LBL_BAR2);
        enableBorderFillTint(_controls.LBL_BAR6);
    }
    if (_controls.LB_DESCRIPTION) {
        _controls.LB_DESCRIPTION->setProtoMatchContent(true);
    }

    if (!_game.isTSL()) {
        if (_controls.BTN_CHANGE1) {
            _controls.BTN_CHANGE1->setSelectable(false);
        }
        if (_controls.BTN_CHANGE2) {
            _controls.BTN_CHANGE2->setSelectable(false);
        }
    }
}

void InventoryMenu::configureItemsListBox() {
    if (!_controls.LB_ITEMS) {
        return;
    }

    if (_game.isTSL()) {
        tintK2PanelFill(_controls.LB_ITEMS, _baseColor);
        tintK2PanelFill(_controls.LB_DESCRIPTION, _baseColor);
    }

    _controls.LB_ITEMS->setSelectionMode(ListBox::SelectionMode::OnClick);
    _controls.LB_ITEMS->setRenderItemIconsForButtonProto(true);
    _controls.LB_ITEMS->setPadding(5);
    _controls.LB_ITEMS->setOnItemClick([this](const std::string &) {
        updateItemDescription();
    });

    if (auto protoItem = _controls.LB_ITEMS->protoItemOrNull()) {
        protoItem->setBorderColor(_baseColor);
        protoItem->setHilightColor(_hilightColor);
    }
}

void InventoryMenu::configureFilterControls() {
    if (_game.isTSL()) {
        if (_controls.BTN_ALL) {
            _controls.BTN_ALL->setOnClick([this]() {
                setFilter(InventoryFilter::All);
            });
        }
        if (_controls.BTN_DATAPADS) {
            _controls.BTN_DATAPADS->setOnClick([this]() {
                setFilter(InventoryFilter::Datapad);
            });
        }
        if (_controls.BTN_WEAPONS) {
            _controls.BTN_WEAPONS->setOnClick([this]() {
                setFilter(InventoryFilter::Weapon);
            });
        }
        if (_controls.BTN_ARMOR) {
            _controls.BTN_ARMOR->setOnClick([this]() {
                setFilter(InventoryFilter::Armor);
            });
        }
        if (_controls.BTN_USEABLE) {
            _controls.BTN_USEABLE->setOnClick([this]() {
                setFilter(InventoryFilter::Useable);
            });
        }
        if (_controls.BTN_QUESTS) {
            _controls.BTN_QUESTS->setOnClick([this]() {
                setFilter(InventoryFilter::Quest);
            });
        }
        if (_controls.BTN_MISC) {
            _controls.BTN_MISC->setOnClick([this]() {
                setFilter(InventoryFilter::Misc);
            });
        }
    } else if (_controls.BTN_QUESTITEMS) {
        _controls.BTN_QUESTITEMS->setDisabled(false);
        _controls.BTN_QUESTITEMS->setOnClick([this]() {
            advanceK1Filter();
        });
    }

    updateFilterControls();
}

void InventoryMenu::refreshPortraits() {
    refreshCredits();
    refreshStats();

    if (!!_game.isTSL())
        return;

    Party &party = _game.party();
    std::shared_ptr<Creature> partyLeader(party.getLeader());
    std::shared_ptr<Creature> partyMember1(party.getMember(1));
    std::shared_ptr<Creature> partyMember2(party.getMember(2));

    _controls.LBL_PORT->setBorderFill(partyLeader->portrait());

    _controls.BTN_CHANGE1->setBorderFill(partyMember1 ? partyMember1->portrait() : nullptr);
    _controls.BTN_CHANGE1->setHilightFill(partyMember1 ? partyMember1->portrait() : nullptr);

    _controls.BTN_CHANGE2->setBorderFill(partyMember2 ? partyMember2->portrait() : nullptr);
    _controls.BTN_CHANGE2->setHilightFill(partyMember2 ? partyMember2->portrait() : nullptr);
}

void InventoryMenu::refreshCredits() {
    if (!_controls.LBL_CREDITS_VALUE) {
        return;
    }
    _controls.LBL_CREDITS_VALUE->setTextMessage(std::to_string(_game.party().gold()));
    _controls.LBL_CREDITS_VALUE->setVisible(true);
}

void InventoryMenu::refreshStats() {
    if (_game.isTSL()) {
        if (_controls.LBL_VIT) {
            _controls.LBL_VIT->setTextMessage("");
            _controls.LBL_VIT->setVisible(false);
        }
        if (_controls.LBL_DEF) {
            _controls.LBL_DEF->setTextMessage("");
            _controls.LBL_DEF->setVisible(false);
        }
        return;
    }

    std::shared_ptr<Creature> partyLeader(_game.party().getLeader());
    if (!partyLeader) {
        if (_controls.LBL_VIT) {
            _controls.LBL_VIT->setTextMessage("");
            _controls.LBL_VIT->setVisible(false);
        }
        if (_controls.LBL_DEF) {
            _controls.LBL_DEF->setTextMessage("");
            _controls.LBL_DEF->setVisible(false);
        }
        return;
    }

    if (_controls.LBL_VIT) {
        _controls.LBL_VIT->setTextMessage(std::to_string(partyLeader->currentHitPoints()) + "/\n" + std::to_string(partyLeader->hitPoints()));
        _controls.LBL_VIT->setVisible(true);
    }
    if (_controls.LBL_DEF) {
        _controls.LBL_DEF->setTextMessage(std::to_string(partyLeader->getDefense()));
        _controls.LBL_DEF->setVisible(true);
    }
}

void InventoryMenu::advanceK1Filter() {
    setFilter(nextK1Filter(_filter));
}

void InventoryMenu::setFilter(InventoryFilter filter) {
    if (_filter == filter) {
        updateFilterControls();
        return;
    }
    _filter = filter;
    updateFilterControls();
    refreshItems();
}

void InventoryMenu::updateFilterControls() {
    if (_game.isTSL()) {
        updateK2FilterButton(_controls.BTN_ALL, _filter == InventoryFilter::All, _baseColor);
        updateK2FilterButton(_controls.BTN_DATAPADS, _filter == InventoryFilter::Datapad, _baseColor);
        updateK2FilterButton(_controls.BTN_WEAPONS, _filter == InventoryFilter::Weapon, _baseColor);
        updateK2FilterButton(_controls.BTN_ARMOR, _filter == InventoryFilter::Armor, _baseColor);
        updateK2FilterButton(_controls.BTN_USEABLE, _filter == InventoryFilter::Useable, _baseColor);
        updateK2FilterButton(_controls.BTN_QUESTS, _filter == InventoryFilter::Quest, _baseColor);
        updateK2FilterButton(_controls.BTN_MISC, _filter == InventoryFilter::Misc, _baseColor);
        return;
    }

    if (_controls.LBL_INV) {
        _controls.LBL_INV->setTextMessage(k1FilterTitle(_filter));
    }
    if (_controls.BTN_QUESTITEMS) {
        _controls.BTN_QUESTITEMS->setTextMessage(k1NextFilterAction(_filter));
        _controls.BTN_QUESTITEMS->setDisabled(false);
    }
}

bool InventoryMenu::itemMatchesFilter(const Item &item) const {
    BaseItemFilterInfo baseItem(getBaseItemFilterInfo(_services, item));
    switch (_filter) {
    case InventoryFilter::All:
        return true;
    case InventoryFilter::New:
        // New item tracking is not retained by the runtime inventory yet.
        return false;
    case InventoryFilter::Quest:
        return item.plotFlag();
    case InventoryFilter::Equippable:
        return item.isEquippable();
    case InventoryFilter::Utility:
        return isUtility(item, baseItem);
    case InventoryFilter::Useable:
        return isUseable(item, baseItem);
    case InventoryFilter::Datapad:
        return isDatapad(item, baseItem);
    case InventoryFilter::Weapon:
        return isWeapon(item, baseItem);
    case InventoryFilter::Armor:
        return isArmor(item, baseItem);
    case InventoryFilter::Misc:
        return !item.plotFlag() &&
               !isDatapad(item, baseItem) &&
               !isWeapon(item, baseItem) &&
               !isArmor(item, baseItem) &&
               !isUseable(item, baseItem);
    default:
        return true;
    }
}

void InventoryMenu::refreshItems() {
    if (!_controls.LB_ITEMS) {
        return;
    }

    _controls.LB_ITEMS->clearItems();
    _listedItems.clear();
    clearItemDescription();

    std::shared_ptr<Creature> player(_game.party().player());
    if (!player) {
        return;
    }

    std::shared_ptr<Creature> activeCreature(_game.party().getLeader());
    if (activeCreature) {
        for (const auto &equipment : activeCreature->equipment()) {
            if (!isInventoryListedEquipmentSlot(equipment.first)) {
                continue;
            }
            std::shared_ptr<Item> equipped(equipment.second);
            if (!equipped || !itemMatchesFilter(*equipped)) {
                continue;
            }

            ListBox::Item equippedItem;
            equippedItem.tag = equipped->tag();
            equippedItem.text = equipped->localizedName() + kEquippedItemSuffix;
            equippedItem.iconTexture = equipped->icon();
            equippedItem.iconFrame = getItemFrameTexture(equipped->stackSize());

            if (equipped->stackSize() > 1) {
                equippedItem.iconText = std::to_string(equipped->stackSize());
            }
            _controls.LB_ITEMS->addItem(std::move(equippedItem));
            _listedItems.push_back(equipped);
        }
    }

    for (auto &item : player->items()) {
        if (!item || std::find(_listedItems.begin(), _listedItems.end(), item) != _listedItems.end()) {
            continue;
        }
        if (!itemMatchesFilter(*item)) {
            continue;
        }

        ListBox::Item lbItem;
        lbItem.tag = item->tag();
        lbItem.text = item->localizedName();
        lbItem.iconTexture = item->icon();
        lbItem.iconFrame = getItemFrameTexture(item->stackSize());

        if (item->stackSize() > 1) {
            lbItem.iconText = std::to_string(item->stackSize());
        }
        _controls.LB_ITEMS->addItem(std::move(lbItem));
        _listedItems.push_back(item);
    }

    if (_controls.LB_ITEMS->getItemCount() > 0) {
        _controls.LB_ITEMS->setSelectedItemIndex(0);
        updateItemDescription();
    }
}

void InventoryMenu::clearItemDescription() {
    _selectedItemIdx = -1;
    if (_controls.LB_DESCRIPTION) {
        _controls.LB_DESCRIPTION->clearItems();
    }
}

void InventoryMenu::updateItemDescription() {
    if (!_controls.LB_ITEMS) {
        return;
    }

    int selectedItemIdx = _controls.LB_ITEMS->selectedItemIndex();
    if (selectedItemIdx == _selectedItemIdx) {
        return;
    }

    _selectedItemIdx = selectedItemIdx;
    if (_controls.LB_DESCRIPTION) {
        _controls.LB_DESCRIPTION->clearItems();
    }

    std::shared_ptr<Item> itemObj(
        selectedItemIdx >= 0 && selectedItemIdx < static_cast<int>(_listedItems.size())
            ? _listedItems[selectedItemIdx]
            : nullptr);
    if (!itemObj) {
        return;
    }

    if (_controls.LB_DESCRIPTION) {
        _controls.LB_DESCRIPTION->addTextLinesAsItems(joinItemDescriptionLines(buildItemDescriptionLines(*itemObj, _services)));
    }
}

std::shared_ptr<Texture> InventoryMenu::getItemFrameTexture(int stackSize) const {
    std::string resRef;
    if (_game.isTSL()) {
        if (stackSize >= 100) {
            resRef = "uibit_eqp_itm3";
        } else if (stackSize > 1) {
            resRef = "uibit_eqp_itm2";
        } else {
            resRef = "uibit_eqp_itm1";
        }
    } else {
        if (stackSize >= 100) {
            resRef = "lbl_hex_7";
        } else if (stackSize > 1) {
            resRef = "lbl_hex_6";
        } else {
            resRef = "lbl_hex_3";
        }
    }
    return _services.resource.textures.get(resRef, TextureUsage::GUI);
}

} // namespace game

} // namespace reone
