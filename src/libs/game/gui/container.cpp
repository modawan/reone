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

#include "reone/game/gui/container.h"

#include "reone/gui/control/imagebutton.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/strings.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/placeable.h"
#include "reone/game/party.h"

using namespace reone::audio;

using namespace reone::gui;
using namespace reone::graphics;
using namespace reone::resource;

namespace reone {

namespace game {

static constexpr int kSwitchToResRef = 47884;
static constexpr int kGetItemsResRef = 38542;
static constexpr int kGiveItemResRef = 38543;
static constexpr int kInventoryResRef = 393;

void ContainerGUI::onGUILoaded() {
    bindControls();
    centerRootInCanvas(_game.isTSL() ? 800 : 640, _game.isTSL() ? 600 : 480);

    _giveItemMsg = _services.resource.strings.getText(kSwitchToResRef) + " " + _services.resource.strings.getText(kGiveItemResRef);
    _getItemsMsg = _services.resource.strings.getText(kSwitchToResRef) + " " + _services.resource.strings.getText(kGetItemsResRef);

    _controls.BTN_GIVEITEMS->setTextMessage(_giveItemMsg);

    std::string LBL_MESSAGE(_services.resource.strings.getText(kInventoryResRef));
    _controls.LBL_MESSAGE->setTextMessage(LBL_MESSAGE);

    _controls.BTN_OK->setOnClick([this]() {
        transferItemsToPlayer();
        _game.openInGame();
    });
    _controls.BTN_CANCEL->setOnClick([this]() {
        close();
    });
    _controls.BTN_GIVEITEMS->setOnClick([this]() {
        switchMode();
    });

    configureItemsListBox();
}

void ContainerGUI::configureItemsListBox() {
    ImageButton &protoItem = static_cast<ImageButton &>(_controls.LB_ITEMS->protoItem());

    Control::Text text(protoItem.text());
    // Centre the name on the scaled glyphs; authored top alignment assumed
    // the retail font filled the row.
    text.align = Control::TextAlign::LeftCenter;

    protoItem.setText(text);

    _controls.LB_ITEMS->setOnItemDoubleClick([this](const std::string &tag) {
        onItemDoubleClick(tag);
    });
}

void ContainerGUI::populateItems(Object &source, bool onlyDropable, bool skipCredits) {
    _controls.LB_ITEMS->clearItems();
    for (auto &item : source.items()) {
        if (onlyDropable && !item->isDropable()) {
            continue;
        }

        if (skipCredits && item->isCredits()) {
            continue;
        }

        ListBox::Item lbItem;
        lbItem.tag = item->tag();
        lbItem.text = item->localizedName();
        lbItem.iconTexture = item->icon();
        lbItem.iconFrame = itemFrameTexture(item->stackSize());
        if (item->stackSize() > 1) {
            lbItem.iconText = std::to_string(item->stackSize());
        }

        _controls.LB_ITEMS->addItem(std::move(lbItem));
    }
}

void ContainerGUI::open(std::shared_ptr<Object> container) {
    _controls.BTN_GIVEITEMS->setTextMessage(_giveItemMsg);
    _container = std::move(container);
    _mode = Mode::ContainerToPlayer;
    populateItems(*_container, /*onlyDropable=*/true, /*skipCredits=*/false);

    auto placeable = dyn_cast<Placeable>(_container);
    if (placeable) {
        placeable->onOpen(_game.party().getLeader()->id());
    }
}

void ContainerGUI::close() {
    _game.openInGame();
}

void ContainerGUI::switchMode() {
    switch (_mode) {
    case Mode::ContainerToPlayer: {
        _mode = Mode::PlayerToContainer;
        _controls.BTN_GIVEITEMS->setTextMessage(_getItemsMsg);
        populateItems(*_game.party().getLeader(), /*onlyDropable=*/false, /*skipCredits=*/true);
        break;
    }
    case Mode::PlayerToContainer: {
        _mode = Mode::ContainerToPlayer;
        _controls.BTN_GIVEITEMS->setTextMessage(_giveItemMsg);
        populateItems(*_container, /*onlyDropable=*/true, /*skipCredits=*/false);
        break;
    }
    }
}

void ContainerGUI::transferItemsToPlayer() {
    std::shared_ptr<Creature> player = _game.party().player();
    _container->moveDropableItemsTo(*player);

    auto placeable = dyn_cast<Placeable>(_container);
    if (placeable) {
        placeable->runOnInvDisturbed(player->id(), InventoryDisturbType::Removed, script::kObjectInvalid);
    }

    close();
}

void ContainerGUI::onItemDoubleClick(const std::string &tag) {
    if (_mode == Mode::ContainerToPlayer) {
        // Do nothing for the player for now.
        return;
    }

    std::shared_ptr<Creature> player = _game.party().player();
    std::shared_ptr<Item> item = player->getItemByTag(tag);
    if (!item) {
        return;
    }

    bool last = false;
    player->removeItem(item, last);

    // Add item to the container if it does not exist.
    uint32_t itemId = script::kObjectInvalid;
    for (const std::shared_ptr<Item> &containerItem : _container->items()) {
        if (containerItem->tag() == item->tag()) {
            _container->addItem(containerItem);
            itemId = containerItem->id();
        }
    }
    if (itemId == script::kObjectInvalid) {
        // No existing item in the container. Clone the item instead of
        // adding it directly.
        std::shared_ptr<Item> newItem = _game.newItem();
        newItem->clone(*item);
        newItem->setStackSize(1);
        _container->addItem(newItem);
        itemId = newItem->id();
    }
    if (last) {
        _game.destroyRuntimeObjectGraph(item);
    }

    // Repopulate the list after the number of items changes.
    int offset = _controls.LB_ITEMS->getItemOffset();
    populateItems(*player, /*onlyDropable=*/false, /*skipCredits=*/true);

    // Try to keep scroll offset the same.
    _controls.LB_ITEMS->setItemOffset(offset);

    // Execute a script for every item individually, because it only
    // supports a single InventoryDisturbItem.
    auto placeable = dyn_cast<Placeable>(_container);
    if (placeable) {
        placeable->runOnInvDisturbed(player->id(), InventoryDisturbType::Added, itemId);
    }
}

} // namespace game

} // namespace reone
