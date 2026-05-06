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
#include "reone/resource/provider/textures.h"

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

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
    if (_controls.LB_DESCRIPTION) {
        _controls.LB_DESCRIPTION->setProtoMatchContent(true);
    }

    if (!_game.isTSL()) {
        if (_controls.LBL_VIT) {
            _controls.LBL_VIT->setVisible(false);
        }
        if (_controls.LBL_DEF) {
            _controls.LBL_DEF->setVisible(false);
        }
        if (_controls.BTN_CHANGE1) {
            _controls.BTN_CHANGE1->setSelectable(false);
        }
        if (_controls.BTN_CHANGE2) {
            _controls.BTN_CHANGE2->setSelectable(false);
        }
        if (_controls.BTN_QUESTITEMS) {
            _controls.BTN_QUESTITEMS->setDisabled(true);
        }
    }
}

void InventoryMenu::configureItemsListBox() {
    if (!_controls.LB_ITEMS) {
        return;
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

void InventoryMenu::refreshPortraits() {
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

void InventoryMenu::refreshItems() {
    if (!_controls.LB_ITEMS) {
        return;
    }

    _controls.LB_ITEMS->clearItems();
    clearItemDescription();

    std::shared_ptr<Creature> player(_game.party().player());
    if (!player) {
        return;
    }

    for (auto &item : player->items()) {
        ListBox::Item lbItem;
        lbItem.tag = item->tag();
        lbItem.text = item->localizedName();
        lbItem.iconTexture = item->icon();
        lbItem.iconFrame = getItemFrameTexture(item->stackSize());

        if (item->stackSize() > 1) {
            lbItem.iconText = std::to_string(item->stackSize());
        }
        _controls.LB_ITEMS->addItem(std::move(lbItem));
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

    if (selectedItemIdx < 0 || selectedItemIdx >= _controls.LB_ITEMS->getItemCount()) {
        return;
    }

    const ListBox::Item &lbItem = _controls.LB_ITEMS->getItemAt(selectedItemIdx);
    std::shared_ptr<Creature> player(_game.party().player());
    if (!player) {
        return;
    }

    std::shared_ptr<Item> itemObj;
    for (auto &playerItem : player->items()) {
        if (playerItem->tag() == lbItem.tag) {
            itemObj = playerItem;
            break;
        }
    }
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
