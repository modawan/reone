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

#include "reone/game/action/giveitem.h"
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
static constexpr int kEmptyInventoryResRef = 394;
static constexpr int kNothingFoundResRef = 119906;

void ContainerGUI::onGUILoaded() {
    bindControls();
    centerRootInCanvas(_game.isTSL() ? 800 : 640, _game.isTSL() ? 600 : 480);

    _giveItemMsg = _game.getInterfaceText(kSwitchToResRef) + " " + _game.getInterfaceText(kGiveItemResRef);
    _getItemsMsg = _game.getInterfaceText(kSwitchToResRef) + " " + _game.getInterfaceText(kGetItemsResRef);

    _controls.BTN_GIVEITEMS->setTextMessage(_giveItemMsg);

    _controls.BTN_OK->setOnClick([this]() {
        close(_heldItemsWhenOpened);
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
    useK1ListEntryColors(protoItem);

    _controls.LB_ITEMS->setOnItemDoubleClick([this](const std::string &tag) {
        onItemDoubleClick(tag);
    });
}

void ContainerGUI::populateItems(Object &source, bool skipCredits) {
    _controls.LB_ITEMS->clearItems();
    for (auto &item : source.items()) {
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
    // The list takes the keyboard afresh at each listing.
    _gui->clearActiveControl();
    _gui->setActiveControl(*_controls.LB_ITEMS);
}

// The caption tells whether the container held items when it opened; TSL
// tells of an empty body bag, or an empty container that is searched, that
// nothing was found.
void ContainerGUI::open(std::shared_ptr<Object> container) {
    _controls.BTN_GIVEITEMS->setTextMessage(_giveItemMsg);
    _heldItemsWhenOpened = !container->items().empty();
    int caption = _heldItemsWhenOpened ? kInventoryResRef : kEmptyInventoryResRef;
    if (const auto placeable = dyn_cast<Placeable>(container);
        _game.isTSL() && !_heldItemsWhenOpened && placeable && (placeable->isBodyBag() || placeable->useSearch())) {
        caption = kNothingFoundResRef;
    }
    _controls.LBL_MESSAGE->setTextMessage(_services.resource.strings.getText(caption));
    _container = container;
    _mode = Mode::ContainerToPlayer;
    _controls.LB_ITEMS->clearItems();
    _itemsListed = false;
    _giveRefresh = GiveRefresh::None;
}

void ContainerGUI::update(float dt) {
    GameGUI::update(dt);
    updateGiveRefresh();
    if (_itemsListed) {
        return;
    }
    _itemsListed = true;
    // A container that its open script destroyed lists nothing.
    if (auto container = _container.resolve()) {
        populateItems(*container, /*skipCredits=*/false);
    }
}

// The leader's items are listed again, at the same scroll offset, while the
// screen still shows them.
void ContainerGUI::updateGiveRefresh() {
    switch (_giveRefresh) {
    case GiveRefresh::AfterNextFrame:
        _giveRefresh = GiveRefresh::Due;
        break;
    case GiveRefresh::Due: {
        _giveRefresh = GiveRefresh::None;
        if (_mode != Mode::PlayerToContainer) {
            break;
        }
        const int offset = _controls.LB_ITEMS->getItemOffset();
        const int row = _controls.LB_ITEMS->selectedItemIndex();
        populateItems(*_game.party().getLeader(), /*skipCredits=*/true);
        _controls.LB_ITEMS->setItemOffset(offset);
        // The selection stays on its row, or the last while the list is shorter.
        const int count = _controls.LB_ITEMS->getItemCount();
        if (count > 0) {
            _controls.LB_ITEMS->setSelectedItemIndex(std::min(row, count - 1));
        }
        break;
    }
    case GiveRefresh::None:
        break;
    }
}

Object &ContainerGUI::container() const {
    auto container = _container.resolve();
    if (!container) {
        throw ValidationException("Container is no longer a live runtime object");
    }
    return *container;
}

// Every way out of the container closes its inventory. The game is back on
// screen first, so a datapad the take-all shows stays up over it.
void ContainerGUI::close(bool takeAll) {
    auto placeable = std::dynamic_pointer_cast<Placeable>(_container.resolve());
    _game.openInGame();
    if (placeable) {
        placeable->closeInventory(*_game.party().getLeader(), takeAll);
    }
}

void ContainerGUI::closeTakingAll() {
    close(true);
}

void ContainerGUI::switchMode() {
    switch (_mode) {
    case Mode::ContainerToPlayer: {
        _mode = Mode::PlayerToContainer;
        _controls.BTN_GIVEITEMS->setTextMessage(_getItemsMsg);
        populateItems(*_game.party().getLeader(), /*skipCredits=*/true);
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
        populateItems(*container, /*skipCredits=*/false);
        break;
    }
    }
}

// The party leader gives the container one of the stack, or the whole stack
// while the alternate actions key is held, through a give action. The list
// shows the leader's items again a frame after the next.
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
    auto item = _game.getObjectById<Item>(static_cast<uint32_t>(std::stoul(entry)));
    if (!item) {
        return;
    }
    const int count = _game.alternateActionsHeld() ? item->stackSize() : 1;
    GiveItemAction::queue(_game, *_game.party().getLeader(), std::move(item), std::move(container), count, false);
    _giveRefresh = GiveRefresh::AfterNextFrame;
}

} // namespace game

} // namespace reone
