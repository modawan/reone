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
#include "reone/system/exception/validation.h"

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

    _giveItemMsg = _game.getInterfaceText(kSwitchToResRef) + " " + _game.getInterfaceText(kGiveItemResRef);
    _getItemsMsg = _game.getInterfaceText(kSwitchToResRef) + " " + _game.getInterfaceText(kGetItemsResRef);

    _controls.BTN_GIVEITEMS->setTextMessage(_giveItemMsg);

    std::string LBL_MESSAGE(_services.resource.strings.getText(kInventoryResRef));
    _controls.LBL_MESSAGE->setTextMessage(LBL_MESSAGE);

    _controls.BTN_OK->setOnClick([this]() {
        transferItemsToPlayer();
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
    // the font filled the row.
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

        // An entry names the stack it shows.
        ListBox::Item lbItem;
        lbItem.tag = std::to_string(item->id());
        lbItem.text = item->localizedName();
        // TSL shows item names with their actions hidden.
        if (_game.isTSL()) {
            lbItem.text = _game.substituteLogTokens(std::move(lbItem.text));
        }
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
    _container = container;
    _mode = Mode::ContainerToPlayer;
    _controls.LB_ITEMS->clearItems();
    _itemsListed = false;
}

void ContainerGUI::update(float dt) {
    GameGUI::update(dt);
    if (_itemsListed) {
        return;
    }
    _itemsListed = true;
    // A container that its open script destroyed lists nothing.
    if (auto container = _container.resolve()) {
        populateItems(*container, /*onlyDropable=*/true, /*skipCredits=*/false);
    }
}

Object &ContainerGUI::container() const {
    auto container = _container.resolve();
    if (!container) {
        throw ValidationException("Container is no longer a live runtime object");
    }
    return *container;
}

// Every way out of the container closes its inventory.
void ContainerGUI::close(bool takeAll) {
    if (auto placeable = std::dynamic_pointer_cast<Placeable>(_container.resolve())) {
        placeable->closeInventory(*_game.party().getLeader(), takeAll);
    }
    _game.openInGame();
}

void ContainerGUI::closeTakingAll() {
    close(true);
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
        auto container = _container.resolve();
        if (!container) {
            close();
            return;
        }
        populateItems(*container, /*onlyDropable=*/true, /*skipCredits=*/false);
        break;
    }
    }
}

void ContainerGUI::transferItemsToPlayer() {
    auto container = _container.resolve();
    if (!container) {
        close();
        return;
    }
    std::shared_ptr<Creature> player = _game.party().player();
    container->moveDropableItemsTo(*player);

    auto placeable = dyn_cast<Placeable>(container);
    if (placeable) {
        placeable->runOnInvDisturbed(player->id(), InventoryDisturbType::Removed, script::kObjectInvalid);
    }

    close();
}

// Giving the container an item from a stack moves one of the stack there; a
// stack of one moves whole. The container takes it into a matching stack of
// its own where one has room.
void ContainerGUI::onItemDoubleClick(const std::string &entry) {
    if (_mode == Mode::ContainerToPlayer) {
        // Do nothing for the player for now.
        return;
    }
    auto container = _container.resolve();
    if (!container) {
        close();
        return;
    }

    std::shared_ptr<Creature> player = _game.party().player();
    auto item = _game.getObjectById<Item>(static_cast<uint32_t>(std::stoul(entry)));
    auto owner = item ? _game.getObjectById(item->owner()) : nullptr;
    if (!owner) {
        return;
    }

    std::shared_ptr<Item> given(item);
    if (item->stackSize() > 1) {
        given = _game.newItemClone(*item);
        given->setStackSize(1);
        item->setStackSize(item->stackSize() - 1);
    } else {
        owner->removeItemStack(item);
    }
    auto held = container->addItem(given);
    const uint32_t itemId = held ? held->id() : script::kObjectInvalid;

    // Repopulate the list after the number of items changes.
    int offset = _controls.LB_ITEMS->getItemOffset();
    populateItems(*player, /*onlyDropable=*/false, /*skipCredits=*/true);

    // Try to keep scroll offset the same.
    _controls.LB_ITEMS->setItemOffset(offset);

    // Execute a script for every item individually, because it only
    // supports a single InventoryDisturbItem.
    auto placeable = dyn_cast<Placeable>(container);
    if (placeable) {
        placeable->runOnInvDisturbed(player->id(), InventoryDisturbType::Added, itemId);
    }
}

} // namespace game

} // namespace reone
