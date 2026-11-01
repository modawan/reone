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

static constexpr int kStrRefEquipped = 32346;
static constexpr int kStrRefK1InventoryTitle = 32171;
static constexpr int kStrRefK1ShowFilter = 42359;

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

// The names of KotOR's inventory filters.
static constexpr int kStrRefNewItems = 42165;
static constexpr int kStrRefQuestItems = 41818;
static constexpr int kStrRefUtilityItems = 41819;
static constexpr int kStrRefUseableItems = 41820;
static constexpr int kStrRefEquippableItems = 41821;
static constexpr int kStrRefAllItems = 41822;

static int k1FilterName(InventoryFilter filter) {
    switch (filter) {
    case InventoryFilter::New:
        return kStrRefNewItems;
    case InventoryFilter::Quest:
        return kStrRefQuestItems;
    case InventoryFilter::Equippable:
        return kStrRefEquippableItems;
    case InventoryFilter::Utility:
        return kStrRefUtilityItems;
    case InventoryFilter::Useable:
        return kStrRefUseableItems;
    default:
        return kStrRefAllItems;
    }
}

// The title names the shown filter after the inventory's name; the filter
// button offers the next one.
static std::string k1FilterTitle(const IInventoryMenuBacking &backing, InventoryFilter filter) {
    return backing.interfaceText(kStrRefK1InventoryTitle) + " - " + backing.interfaceText(k1FilterName(filter));
}

static std::string k1NextFilterAction(const IInventoryMenuBacking &backing, InventoryFilter filter) {
    return backing.interfaceText(kStrRefK1ShowFilter) + " " + backing.interfaceText(k1FilterName(nextK1Filter(filter)));
}

void InventoryMenu::onGUILoaded() {
    loadBackground(BackgroundType::Menu);
    bindControls();

    if (_controls.LBL_CREDITS_VALUE) {
        _controls.LBL_CREDITS_VALUE->setVisible(false);
    }
    // The use button activates the selected entry, whatever it looks like.
    if (_controls.BTN_USEITEM) {
        _useItemTextColor = _controls.BTN_USEITEM->text().color;
        _controls.BTN_USEITEM->setOnClick([this]() {
            activateSelectedItem();
        });
    }
    if (_controls.BTN_EXIT) {
        _controls.BTN_EXIT->setOnClick([this]() {
            if (_onExit) _onExit();
        });
    }

    configureItemsListBox();
    configureFilterControls();
    if (isTSL()) {
        fillK2SectionStrip(_controls.LBL_BAR1, _controls.LBL_BAR2);
        enableBorderFillTint(_controls.LBL_BAR1);
        enableBorderFillTint(_controls.LBL_BAR2);
        enableBorderFillTint(_controls.LBL_BAR6);
        useK2ShellTitle(_controls.LBL_INV);
        enableK2ButtonBodyFill(_controls.BTN_USEITEM);
        enableK2ButtonBodyFill(_controls.BTN_EXIT);
    }
    if (_controls.LB_DESCRIPTION) {
        _controls.LB_DESCRIPTION->setProtoMatchContent(true);
    }

    if (!isTSL()) {
        if (_controls.BTN_CHANGE1) {
            _controls.BTN_CHANGE1->setSelectable(false);
        }
        if (_controls.BTN_CHANGE2) {
            _controls.BTN_CHANGE2->setSelectable(false);
        }
        // KotOR's inventory has no party arrows.
        for (auto &arrow : {_controls.BTN_CHARLEFT, _controls.BTN_CHARRIGHT}) {
            if (arrow) {
                arrow->setVisible(false);
            }
        }
    }
}

void InventoryMenu::configureItemsListBox() {
    if (!_controls.LB_ITEMS) {
        return;
    }

    if (isTSL()) {
        tintK2PanelFill(_controls.LB_ITEMS, _baseColor);
        tintK2PanelFill(_controls.LB_DESCRIPTION, _baseColor);
    }

    _controls.LB_ITEMS->setSelectionMode(ListBox::SelectionMode::OnClick);
    _controls.LB_ITEMS->setRenderItemIconsForButtonProto(true);
    // K1's panel art is a 512x512 texture stretched onto the 640x480 canvas,
    // so its baked slot strip repeats every 61 texels - 57.19 authored pixels,
    // which no integer proto height plus padding can match. The rows are
    // repainted over that strip instead, which leaves this a free density
    // knob. K2 uses the five-pixel gap established by its menu layout.
    _controls.LB_ITEMS->setPadding(isTSL() ? 5 : 8);
    useBakedItemSlotArt(*_controls.LB_ITEMS);
    _controls.LB_ITEMS->setOnItemClick([this](const std::string &) {
        updateItemDescription();
    });
    _controls.LB_ITEMS->setOnItemDoubleClick([this](const std::string &) {
        activateSelectedItem();
    });

    if (auto protoItem = _controls.LB_ITEMS->protoItemOrNull()) {
        if (isTSL()) {
            enableK2ButtonBodyFill(*protoItem);
            protoItem->setBorderFill("uibit_fill_2wt");
            protoItem->setHilightFill("uibit_fill_2wt");
            protoItem->setTintBorderFill(true);
        } else {
            protoItem->setBorderColor(_baseColor);
            protoItem->setHilightColor(_hilightColor);
        }
    }
}

void InventoryMenu::configureFilterControls() {
    if (isTSL()) {
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
    _view = _backing ? _backing->readInventory(_filter) : InventoryView {};
    refreshCredits();
    refreshStats();
    if (isTSL()) return;
    _controls.LBL_PORT->setBorderFill(_view.subject.portraits[0]);
    _controls.BTN_CHANGE1->setBorderFill(_view.subject.portraits[1]);
    _controls.BTN_CHANGE1->setHilightFill(_view.subject.portraits[1]);
    _controls.BTN_CHANGE2->setBorderFill(_view.subject.portraits[2]);
    _controls.BTN_CHANGE2->setHilightFill(_view.subject.portraits[2]);
}

void InventoryMenu::refreshCredits() {
    if (!_controls.LBL_CREDITS_VALUE) {
        return;
    }
    _controls.LBL_CREDITS_VALUE->setTextMessage(_view.subject.credits);
    _controls.LBL_CREDITS_VALUE->setVisible(true);
}

void InventoryMenu::refreshStats() {
    if (isTSL()) {
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

    if (!_view.subject.present) {
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
        _controls.LBL_VIT->setTextMessage(_view.subject.vitality);
        _controls.LBL_VIT->setVisible(true);
    }
    if (_controls.LBL_DEF) {
        _controls.LBL_DEF->setTextMessage(_view.subject.defense);
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
    if (isTSL()) {
        updateK2FilterButton(_controls.BTN_ALL, _filter == InventoryFilter::All);
        updateK2FilterButton(_controls.BTN_DATAPADS, _filter == InventoryFilter::Datapad);
        updateK2FilterButton(_controls.BTN_WEAPONS, _filter == InventoryFilter::Weapon);
        updateK2FilterButton(_controls.BTN_ARMOR, _filter == InventoryFilter::Armor);
        updateK2FilterButton(_controls.BTN_USEABLE, _filter == InventoryFilter::Useable);
        updateK2FilterButton(_controls.BTN_QUESTS, _filter == InventoryFilter::Quest);
        updateK2FilterButton(_controls.BTN_MISC, _filter == InventoryFilter::Misc);
        return;
    }

    // The labels come from the backing, which a menu may be left without.
    if (_controls.LBL_INV && _backing) {
        _controls.LBL_INV->setTextMessage(k1FilterTitle(*_backing, _filter));
    }
    if (_controls.BTN_QUESTITEMS) {
        if (_backing) {
            _controls.BTN_QUESTITEMS->setTextMessage(k1NextFilterAction(*_backing, _filter));
        }
        _controls.BTN_QUESTITEMS->setDisabled(false);
    }
}

void InventoryMenu::setBacking(std::shared_ptr<IInventoryMenuBacking> backing) {
    _backing = std::move(backing);
    _view = {};
    _listedItems.clear();
    if (_gui) { refreshPortraits(); refreshItems(); }
}

void InventoryMenu::refreshItems(int selectedRow) {
    if (!_controls.LB_ITEMS) return;
    _view = _backing ? _backing->readInventory(_filter) : InventoryView {};
    _controls.LB_ITEMS->clearItems();
    _listedItems = _view.items;
    clearItemDescription();
    for (const auto &item : _listedItems) {
        ListBox::Item row;
        row.tag = std::to_string(item.handle);
        row.text = item.name + (item.equipped ? " (" + _backing->interfaceText(kStrRefEquipped) + ")" : "");
        row.iconTexture = item.icon;
        row.iconFrame = itemFrameTexture(item.stackSize);
        if (item.stackSize > 1) row.iconText = std::to_string(item.stackSize);
        _controls.LB_ITEMS->addItem(std::move(row));
    }
    if (!_listedItems.empty()) {
        _controls.LB_ITEMS->setSelectedItemIndex(std::min(selectedRow, static_cast<int>(_listedItems.size()) - 1));
        updateItemDescription();
    }
    updateUseItemButton();
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

    if (selectedItemIdx >= 0 && selectedItemIdx < static_cast<int>(_listedItems.size()) && _controls.LB_DESCRIPTION)
        _controls.LB_DESCRIPTION->addTextLinesAsItems(_listedItems[selectedItemIdx].description);
    updateUseItemButton();
}

// An entry either uses its item at once, shows why it cannot, or does nothing.
// A use refreshes the list and keeps the row selected.
void InventoryMenu::activateSelectedItem() {
    if (!_controls.LB_ITEMS || !_backing) {
        return;
    }
    const int row = _controls.LB_ITEMS->selectedItemIndex();
    if (row < 0 || row >= static_cast<int>(_listedItems.size())) {
        return;
    }
    const auto &item = _listedItems[row];
    switch (item.activation) {
    case InventoryActivation::Message:
        if (_onMessage) {
            _onMessage(item.messageStrRef);
        }
        break;
    case InventoryActivation::Use:
        if (auto refusal = _backing->useItem(item.handle)) {
            if (_onMessage) {
                _onMessage(*refusal);
            }
            return;
        }
        refreshItems(row);
        break;
    default:
        break;
    }
}

// The use button looks available only for an entry that uses its item, and
// not while the leader's last item use in combat holds the next one back.
void InventoryMenu::updateUseItemButton() {
    if (!_controls.BTN_USEITEM) {
        return;
    }
    const bool usable = _selectedItemIdx >= 0 && _selectedItemIdx < static_cast<int>(_listedItems.size()) &&
                        _listedItems[_selectedItemIdx].activation == InventoryActivation::Use &&
                        !_view.itemUseCoolingDown;
    _controls.BTN_USEITEM->setTextColor(usable ? _useItemTextColor : _disabledColor);
}

} // namespace game

} // namespace reone
