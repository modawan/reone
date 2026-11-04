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

#include "reone/game/gui/store.h"

#include <algorithm>

#include "reone/resource/strings.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/itemdescription.h"
#include "reone/game/object/item.h"
#include "reone/game/object/store.h"
#include "reone/game/party.h"
#include "reone/game/storetrade.h"

using namespace reone::gui;

namespace reone {

namespace game {

static constexpr int kStrRefSell = 32130;
static constexpr int kStrRefBuy = 32132;
static constexpr int kStrRefShowBuyList = 41937;
static constexpr int kStrRefShowSellList = 41938;
static constexpr int kStrRefItemCost = 41943;
static constexpr int kStrRefItemValue = 41945;

void StoreGUI::onGUILoaded() {
    bindControls();
    centerRootInCanvas(_game.isTSL() ? 800 : 640, _game.isTSL() ? 600 : 480);

    if (_controls.BTN_Accept) {
        _controls.BTN_Accept->setOnClick([this]() {
            accept();
        });
    }
    if (_controls.BTN_Cancel) {
        _controls.BTN_Cancel->setOnClick([this]() {
            close();
        });
    }
    if (_controls.BTN_Examine) {
        _controls.BTN_Examine->setOnClick([this]() {
            setMode(_mode == Mode::Buy ? Mode::Sell : Mode::Buy);
        });
    }
    for (auto &list : {_controls.LB_SHOPITEMS, _controls.LB_INVITEMS}) {
        if (list) {
            configureItemList(*list);
        }
    }
    if (_controls.LB_DESCRIPTION) {
        _controls.LB_DESCRIPTION->setProtoMatchContent(true);
    }
}

void StoreGUI::configureItemList(ListBox &list) {
    list.setSelectionMode(ListBox::SelectionMode::OnClick);
    // The rows are buttons that draw the item's icon beside its name.
    list.setRenderItemIconsForButtonProto(true);
    list.setOnItemClick([this](const std::string &) {
        showSelection();
    });
}

void StoreGUI::open(const std::shared_ptr<Store> &store, int bonusMarkUp, int bonusMarkDown) {
    _store = store;
    _bonusMarkUp = bonusMarkUp;
    _bonusMarkDown = bonusMarkDown;
    _itemsListed = false;
    for (auto &list : {_controls.LB_SHOPITEMS, _controls.LB_INVITEMS}) {
        if (list) {
            list->clearItems();
        }
    }
    setMode(Mode::Buy);
}

void StoreGUI::update(float dt) {
    GameGUI::update(dt);
    if (_itemsListed) {
        return;
    }
    _itemsListed = true;
    refresh();
}

void StoreGUI::setMode(Mode mode) {
    _mode = mode;
    const bool buying = mode == Mode::Buy;
    const auto text = [this](int strRef) {
        return _services.resource.strings.getText(strRef);
    };
    if (_controls.LBL_BUYSELL) {
        _controls.LBL_BUYSELL->setTextMessage(text(buying ? kStrRefBuy : kStrRefSell));
    }
    if (_controls.BTN_Accept) {
        _controls.BTN_Accept->setTextMessage(text(buying ? kStrRefBuy : kStrRefSell));
    }
    // The button names the list it switches to.
    if (_controls.BTN_Examine) {
        _controls.BTN_Examine->setTextMessage(text(buying ? kStrRefShowSellList : kStrRefShowBuyList));
    }
    if (_controls.LBL_COST) {
        _controls.LBL_COST->setTextMessage(text(buying ? kStrRefItemCost : kStrRefItemValue));
    }
    // Both lists share one place on the panel.
    if (_controls.LB_SHOPITEMS) {
        _controls.LB_SHOPITEMS->setVisible(buying);
        _controls.LB_SHOPITEMS->setSelectedItemIndex(-1);
    }
    if (_controls.LB_INVITEMS) {
        _controls.LB_INVITEMS->setVisible(!buying);
        _controls.LB_INVITEMS->setSelectedItemIndex(-1);
    }
    if (_itemsListed) {
        refresh();
    } else {
        showSelection();
    }
}

void StoreGUI::refresh() {
    auto store = _store.resolve();
    if (!store) {
        close();
        return;
    }
    auto customer = storeCustomer(_game);
    const auto relist = [](ListBox *list, auto &&fill) {
        if (!list) {
            return;
        }
        const int offset = list->getItemOffset();
        const int selected = list->selectedItemIndex();
        fill(*list);
        list->setItemOffset(offset);
        list->setSelectedItemIndex(std::min(selected, list->getItemCount() - 1));
    };
    relist(_controls.LB_SHOPITEMS.get(), [&](ListBox &list) {
        listItems(list, store.get(), false);
    });
    relist(_controls.LB_INVITEMS.get(), [&](ListBox &list) {
        listItems(list, customer.get(), true);
    });
    showSelection();
}

void StoreGUI::listItems(ListBox &list, const Object *source, bool sellableOnly) {
    list.clearItems();
    if (!source) {
        return;
    }
    for (auto &item : source->items()) {
        if (sellableOnly && !isSellableToStore(*item)) {
            continue;
        }
        ListBox::Item lbItem;
        lbItem.tag = std::to_string(item->id());
        lbItem.text = item->localizedName();
        // TSL shows item names with their actions hidden.
        if (_game.isTSL()) {
            lbItem.text = _game.substituteLogTokens(std::move(lbItem.text));
        }
        lbItem.iconTexture = item->icon();
        lbItem.iconFrame = itemFrameTexture(item->stackSize());
        if (item->stackSize() > 1 && !item->isInfinite()) {
            lbItem.iconText = std::to_string(item->stackSize());
        }
        list.addItem(std::move(lbItem));
    }
}

void StoreGUI::showCredits() {
    if (_controls.LBL_CREDITS_VALUE) {
        _controls.LBL_CREDITS_VALUE->setTextMessage(std::to_string(_game.party().gold()));
    }
}

// The price and amount labels and the description follow the selected row of
// the shown list; with none selected they are empty.
void StoreGUI::showSelection() {
    showCredits();
    auto item = selectedItem();
    auto store = _store.resolve();
    std::string cost;
    std::string stock;
    if (item && store) {
        const int price = _mode == Mode::Buy
                              ? storeBuyPrice(_game, *store, *item, _bonusMarkUp)
                              : storeSellPrice(_game, *store, *item, _bonusMarkDown);
        cost = std::to_string(price);
        // Unlimited stock shows no amount.
        if (!item->isInfinite()) {
            stock = std::to_string(item->stackSize());
        }
    }
    if (_controls.LBL_COST_VALUE) {
        _controls.LBL_COST_VALUE->setTextMessage(cost);
    }
    if (_controls.LBL_STOCK_VALUE) {
        _controls.LBL_STOCK_VALUE->setTextMessage(stock);
    }
    if (_controls.LB_DESCRIPTION) {
        _controls.LB_DESCRIPTION->clearItems();
        if (item) {
            _controls.LB_DESCRIPTION->addTextLinesAsItems(buildItemDescription(*item, _game, _services));
        }
    }
}

ListBox *StoreGUI::shownList() const {
    return (_mode == Mode::Buy ? _controls.LB_SHOPITEMS : _controls.LB_INVITEMS).get();
}

std::shared_ptr<Item> StoreGUI::selectedItem() const {
    auto list = shownList();
    if (!list) {
        return nullptr;
    }
    const int row = list->selectedItemIndex();
    if (row < 0 || row >= list->getItemCount()) {
        return nullptr;
    }
    return _game.getObjectById<Item>(static_cast<uint32_t>(std::stoul(list->getItemAt(row).tag)));
}

void StoreGUI::accept() {
    auto store = _store.resolve();
    if (!store) {
        close();
        return;
    }
    auto item = selectedItem();
    if (!item) {
        return;
    }
    const bool traded = _mode == Mode::Buy
                            ? buyFromStore(_game, *store, item, _bonusMarkUp)
                            : sellToStore(_game, *store, item, _bonusMarkDown);
    if (traded) {
        refresh();
    }
}

void StoreGUI::close() {
    _store.reset();
    _game.openInGame();
}

} // namespace game

} // namespace reone
