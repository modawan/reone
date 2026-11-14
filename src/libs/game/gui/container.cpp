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
static constexpr int kGiveCaptionResRef = 392;
static constexpr int kInventoryResRef = 393;
static constexpr int kEmptyInventoryResRef = 394;
static constexpr int kNothingFoundResRef = 119906;

void ContainerGUI::onGUILoaded() {
    bindControls();
    // The hilight follows the keys; the buttons leave the keys with the list
    // when the pointer comes onto them.
    _gui->setHilightFollowsKeyboard(true);
    _controls.BTN_OK->setNavigable(false);
    _controls.BTN_GIVEITEMS->setNavigable(false);
    _controls.BTN_CANCEL->setNavigable(false);
    centerRootInCanvas(_game.isTSL() ? 800 : 640, _game.isTSL() ? 600 : 480);

    _giveItemMsg = _game.getInterfaceText(kSwitchToResRef) + " " + _game.getInterfaceText(kGiveItemResRef);
    _getItemsMsg = _game.getInterfaceText(kSwitchToResRef) + " " + _game.getInterfaceText(kGetItemsResRef);

    // OK takes everything when the container held items at opening; while
    // giving, it gives the selected entry's item and the screen stays open.
    _controls.BTN_OK->setOnClick([this]() {
        if (_mode == Mode::PlayerToContainer) {
            const int row = _controls.LB_ITEMS->selectedItemIndex();
            if (row >= 0) {
                queueGiveOfEntry(_controls.LB_ITEMS->getItemAt(row).tag);
            }
            return;
        }
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

    // A click on an entry of the list of items to give gives its item.
    _controls.LB_ITEMS->setOnItemClick([this](const std::string &tag) {
        queueGiveOfEntry(tag);
    });
}

// Whether the list shows an item: the list of items to give leaves out
// credits and plot items.
static bool listsItem(const Item &item, bool giving) {
    return !giving || (!item.isCredits() && !item.plotFlag());
}

std::vector<ContainerGUI::ListedEntry> ContainerGUI::listedEntries(const Object &source, bool giving) {
    std::vector<ListedEntry> result;
    for (const auto &item : source.items()) {
        if (!listsItem(*item, giving)) {
            continue;
        }
        result.emplace_back(item->id(), item->stackSize());
    }
    return result;
}

void ContainerGUI::populateItems(Object &source, bool giving) {
    _controls.LB_ITEMS->clearItems();
    _listed = listedEntries(source, giving);
    for (auto &item : source.items()) {
        if (!listsItem(*item, giving)) {
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

void ContainerGUI::open(std::shared_ptr<Object> container) {
    _heldItemsWhenOpened = !container->items().empty();
    _container = container;
    showMode(Mode::ContainerToPlayer);
    _controls.LB_ITEMS->clearItems();
    _listed.clear();
    _itemsListed = false;
}

void ContainerGUI::update(float dt) {
    GameGUI::update(dt);
    if (_itemsListed) {
        refreshItems();
        return;
    }
    _itemsListed = true;
    // A container that its open script destroyed lists nothing.
    if (auto container = _container.resolve()) {
        populateItems(*container, /*giving=*/false);
    }
}

// The list follows what it shows, whenever that changes: a give that has
// gone through, or a script that changed the container. It is listed again at
// the same scroll offset, the selection staying on its row, or on the last
// while the list is shorter.
void ContainerGUI::refreshItems() {
    const bool giving = _mode == Mode::PlayerToContainer;
    std::shared_ptr<Object> source;
    if (giving) {
        source = _game.party().sharedInventoryReceiver(_game.party().getLeader());
    } else {
        source = _container.resolve();
    }
    if (!source || listedEntries(*source, giving) == _listed) {
        return;
    }
    const int offset = _controls.LB_ITEMS->getItemOffset();
    const int row = _controls.LB_ITEMS->selectedItemIndex();
    populateItems(*source, giving);
    _controls.LB_ITEMS->setItemOffset(offset);
    const int count = _controls.LB_ITEMS->getItemCount();
    if (count > 0 && row >= 0) {
        _controls.LB_ITEMS->setSelectedItemIndex(std::min(row, count - 1));
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

// Taking, the list only shows the container's items: no entry is selected,
// and the pointer neither selects nor clicks one. Giving, the pointer selects
// the list's entries and a click gives one.
void ContainerGUI::showMode(Mode mode) {
    _mode = mode;
    const bool giving = mode == Mode::PlayerToContainer;
    _controls.LB_ITEMS->setItemsInteractive(giving);
    _controls.BTN_GIVEITEMS->setTextMessage(giving ? _getItemsMsg : _giveItemMsg);
    const std::string okText = _services.resource.strings.getText(giving ? kGiveItemResRef : kGetItemsResRef);
    _controls.BTN_OK->setTextMessage(okText);
    // The label beside OK is only in layouts that carry it.
    if (_controls.LBL_OK) {
        _controls.LBL_OK->setTextMessage(okText);
    }
    const int caption = giving ? kGiveCaptionResRef : takeCaption(container());
    _controls.LBL_MESSAGE->setTextMessage(_services.resource.strings.getText(caption));
}

// The caption tells whether the container held items when it opened; TSL
// tells of an empty body bag, or an empty container that is searched, that
// nothing was found.
int ContainerGUI::takeCaption(const Object &container) const {
    if (_heldItemsWhenOpened) {
        return kInventoryResRef;
    }
    const auto *placeable = dyn_cast<const Placeable>(&container);
    if (_game.isTSL() && placeable && (placeable->isBodyBag() || placeable->useSearch())) {
        return kNothingFoundResRef;
    }
    return kEmptyInventoryResRef;
}

void ContainerGUI::switchMode() {
    switch (_mode) {
    case Mode::ContainerToPlayer: {
        showMode(Mode::PlayerToContainer);
        populateItems(*_game.party().sharedInventoryReceiver(_game.party().getLeader()), /*giving=*/true);
        break;
    }
    case Mode::PlayerToContainer: {
        auto container = _container.resolve();
        if (!container) {
            close();
            return;
        }
        showMode(Mode::ContainerToPlayer);
        populateItems(*container, /*giving=*/false);
        break;
    }
    }
}

// The party leader gives the container one of the stack, or the whole stack
// while the alternate actions key is held, through a give action. The list
// follows once the give has gone through.
void ContainerGUI::queueGiveOfEntry(const std::string &entry) {
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
}

} // namespace game

} // namespace reone
