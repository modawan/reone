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
#include <optional>

#include "reone/audio/mixer.h"
#include "reone/input/event.h"

#include "reone/game/d20/attributes.h"
#include "reone/game/equipmentoperation.h"
#include "reone/game/equipmentrules.h"
#include "reone/game/game.h"
#include "reone/game/gui/sounds.h"
#include "reone/game/itemdescription.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/store.h"
#include "reone/game/party.h"

using namespace reone::audio;
using namespace reone::gui;

namespace reone {

namespace game {

static constexpr int kSellingItemsStrRef = 32130;
static constexpr int kBuyingItemsStrRef = 32132;
static constexpr int kEquippedStrRef = 32346;
static constexpr int kShowBuyListStrRef = 41937;
static constexpr int kShowSellListStrRef = 41938;
static constexpr int kItemCostStrRef = 41943;
static constexpr int kItemValueStrRef = 41945;
static constexpr int kNotEnoughCreditsStrRef = 41950;
static constexpr int kInfiniteStrRef = 41951;
static constexpr int kConfirmSellAbovePriceStrRef = 41985;
static constexpr int kConfirmBuyAbovePriceStrRef = 42020;
static constexpr int kConfirmSellEquippedStrRef = 105896;
static constexpr int kBuyItemStrRef = 125552;
static constexpr int kSellItemStrRef = 125553;
static constexpr int kCannotBeSoldStrRef = 125628;

/** Credits a trade may come to without being confirmed, per level of the leader. */
static constexpr int kTradeLimitPerLevel = 50;
static constexpr int kSellLimitCap = 250;

/** The party members whose equipped items count as on hand. */
static constexpr int kOnHandPartySize = 3;

void StoreGUI::onGUILoaded() {
    bindControls();
    loadBackground(BackgroundType::Menu);

    configureItemList(*_controls.LB_SHOPITEMS);
    configureItemList(*_controls.LB_INVITEMS);
    _controls.LB_DESCRIPTION->setProtoMatchContent(true);

    _controls.BTN_Cancel->setOnClick([this]() {
        close();
    });
    _controls.BTN_Examine->setOnClick([this]() {
        toggleLists();
    });
    _controls.BTN_Accept->setOnClick([this]() {
        if (_selling) {
            if (auto item = selectedItem(*_controls.LB_INVITEMS)) {
                offerToSell(item);
            }
        } else if (auto item = selectedItem(*_controls.LB_SHOPITEMS)) {
            offerToBuy(item);
        }
    });

    if (isTSL()) {
        _controls.BTN_ALL->setOnClick([this]() { setFilter(Filter::All); });
        _controls.BTN_WEAPONS->setOnClick([this]() { setFilter(Filter::Weapons); });
        _controls.BTN_ARMOR->setOnClick([this]() { setFilter(Filter::Armor); });
        _controls.BTN_MISC->setOnClick([this]() { setFilter(Filter::Misc); });
    }
}

// A click picks a row and shows it; a double click trades it.
void StoreGUI::configureItemList(ListBox &list) {
    auto &protoItem = list.protoItem();
    Control::Text text(protoItem.text());
    text.align = Control::TextAlign::LeftCenter;
    protoItem.setText(text);

    list.setRenderItemIconsForButtonProto(true);
    // A row's hilight frame pulses, and so does its icon frame while it is hilighted.
    protoItem.setHilightPulsing(true, false);
    list.setIconFrameHilightPulsing(true);
    list.setSelectionMode(ListBox::SelectionMode::OnClick);
    list.setOnItemClick([this, &list](const std::string &) {
        showRow(list);
    });
    list.setOnItemDoubleClick([this, &list](const std::string &tag) {
        auto item = itemOfRow(tag);
        if (!item) {
            return;
        }
        if (&list == _controls.LB_INVITEMS.get()) {
            offerToSell(item);
        } else {
            offerToBuy(item);
        }
    });
}

void StoreGUI::open(const std::shared_ptr<Store> &store) {
    _store = store;
    _filter = Filter::All;
    _shownCredits.reset();
    showCredits();
    // A store that only buys opens on the party's items, any other on its own
    // stock; only one that buys and sells can switch between them.
    _controls.BTN_Examine->setVisible(store->buysAndSells());
    if (isTSL()) {
        setFilter(Filter::All);
    }
    if (store->onlyBuys()) {
        showSellList();
    } else {
        showBuyList();
    }
}

bool StoreGUI::handle(const input::Event &event) {
    if (event.type == input::EventType::KeyDown && event.key.code == input::KeyCode::Escape && !event.key.repeat) {
        clickControl("BTN_Cancel");
        return true;
    }
    return GameGUI::handle(event);
}

void StoreGUI::showBuyList() {
    _selling = false;
    _controls.LB_INVITEMS->setVisible(false);
    _controls.LB_SHOPITEMS->setVisible(true);
    _controls.BTN_Accept->setTextMessage(_game.getInterfaceText(isTSL() ? kBuyItemStrRef : kBuyingItemsStrRef));
    _controls.LBL_BUYSELL->setTextMessage(_game.getInterfaceText(kBuyingItemsStrRef));
    _controls.BTN_Examine->setTextMessage(_game.getInterfaceText(kShowSellListStrRef));
    _controls.LBL_COST->setTextMessage(_game.getInterfaceText(kItemCostStrRef));
    listShopItems();
}

void StoreGUI::showSellList() {
    _selling = true;
    _controls.LB_INVITEMS->setVisible(true);
    _controls.LB_SHOPITEMS->setVisible(false);
    _controls.BTN_Accept->setTextMessage(_game.getInterfaceText(isTSL() ? kSellItemStrRef : kSellingItemsStrRef));
    _controls.LBL_BUYSELL->setTextMessage(_game.getInterfaceText(kSellingItemsStrRef));
    _controls.BTN_Examine->setTextMessage(_game.getInterfaceText(kShowBuyListStrRef));
    _controls.LBL_COST->setTextMessage(_game.getInterfaceText(kItemValueStrRef));
    listInventoryItems();
}

void StoreGUI::toggleLists() {
    auto store = _store.resolve();
    if (!store || !store->buysAndSells()) {
        return;
    }
    if (_selling) {
        showBuyList();
    } else {
        showSellList();
    }
}

// TSL: the chosen filter's text is lit, and both lists show what passes it.
void StoreGUI::setFilter(Filter filter) {
    _filter = filter;
    updateK2FilterButton(_controls.BTN_ALL, filter == Filter::All);
    updateK2FilterButton(_controls.BTN_WEAPONS, filter == Filter::Weapons);
    updateK2FilterButton(_controls.BTN_ARMOR, filter == Filter::Armor);
    updateK2FilterButton(_controls.BTN_MISC, filter == Filter::Misc);
    listInventoryItems();
    listShopItems();
}

// Weapons go in either hand, armour is worn anywhere else, and the rest are
// not equipped at all.
bool StoreGUI::passesFilter(const Item &item) const {
    switch (_filter) {
    case Filter::Weapons:
        return item.isEquippable(InventorySlots::rightWeapon) || item.isEquippable(InventorySlots::leftWeapon);
    case Filter::Armor:
        for (int slot : {InventorySlots::head, InventorySlots::body, InventorySlots::hands,
                         InventorySlots::leftArm, InventorySlots::rightArm, InventorySlots::implant,
                         InventorySlots::belt}) {
            if (item.isEquippable(slot)) {
                return true;
            }
        }
        return false;
    case Filter::Misc:
        return !item.isEquippable();
    case Filter::All:
        return true;
    }
    return true;
}

void StoreGUI::listShopItems() {
    auto &list = *_controls.LB_SHOPITEMS;
    list.clearItems();
    if (auto store = _store.resolve()) {
        addSortedRows(list, store->items());
    }
    if (list.isVisible()) {
        selectFirstRow(list);
    }
}

// TSL lists the leader's equipped items first, those of the creature slots
// left out.
void StoreGUI::listInventoryItems() {
    auto &list = *_controls.LB_INVITEMS;
    list.clearItems();
    auto leader = _game.party().getLeader();
    if (!leader) {
        return;
    }
    if (isTSL()) {
        for (int slot = 0; slot < InventorySlots::count; ++slot) {
            if (slot >= InventorySlots::cWeaponL && slot <= InventorySlots::cArmour) {
                continue;
            }
            auto item = leader->getEquippedItem(slot);
            if (item && !item->plotFlag() && passesFilter(*item)) {
                addRow(list, *item, true);
            }
        }
    }
    addSortedRows(list, _game.party().sharedInventoryReceiver(leader)->items());
    if (list.isVisible()) {
        selectFirstRow(list);
    }
}

void StoreGUI::addSortedRows(ListBox &list, const std::vector<std::shared_ptr<Item>> &items) {
    std::vector<std::shared_ptr<Item>> listed;
    for (const auto &item : items) {
        if (!item->plotFlag() && passesFilter(*item)) {
            listed.push_back(item);
        }
    }
    std::stable_sort(listed.begin(), listed.end(), [](const auto &left, const auto &right) {
        return left->storePanelSort() < right->storePanelSort();
    });
    for (const auto &item : listed) {
        addRow(list, *item, false);
    }
}

void StoreGUI::addRow(ListBox &list, const Item &item, bool equipped) {
    ListBox::Item row;
    row.tag = std::to_string(item.id());
    row.text = item.localizedName();
    if (isTSL()) {
        row.text = _game.substituteLogTokens(std::move(row.text));
    }
    if (equipped) {
        row.text += " (" + _game.getInterfaceText(kEquippedStrRef) + ")";
    }
    row.iconTexture = item.icon();
    row.iconFrame = itemFrameTexture(item.stackSize());
    if (item.stackSize() > 1) {
        row.iconText = std::to_string(item.stackSize());
    }
    list.addItem(std::move(row));
}

void StoreGUI::selectFirstRow(ListBox &list) {
    if (list.getItemCount() == 0) {
        list.setSelectedItemIndex(-1);
        clearReadouts();
        return;
    }
    list.setSelectedItemIndex(0);
    showRow(list);
}

void StoreGUI::clearReadouts() {
    _controls.LBL_COST_VALUE->setTextMessage("");
    _controls.LBL_STOCK_VALUE->setTextMessage("");
    _controls.LB_DESCRIPTION->clearItems();
    if (isTSL()) {
        _controls.LBL_ONHAND_VALUE->setTextMessage("");
    }
}

// The count of the first of the items identical to \p item, or none when that
// stock never runs out.
static std::optional<int> matchingCount(const std::vector<std::shared_ptr<Item>> &items, const Item &item) {
    for (const auto &other : items) {
        if (item.isIdenticalTo(*other)) {
            return other->isInfinite() ? std::nullopt : std::optional<int>(other->stackSize());
        }
    }
    return 0;
}

// How many items identical to \p item the first party members have equipped.
static int matchingEquippedCount(const Party &party, const Item &item) {
    int count = 0;
    for (int i = 0; i < std::min(party.getSize(), kOnHandPartySize); ++i) {
        auto member = party.getMember(i);
        if (!member) {
            continue;
        }
        for (const auto &[slot, equipped] : member->equipment()) {
            if ((slot < InventorySlots::cWeaponL || slot > InventorySlots::cArmour) && item.isIdenticalTo(*equipped)) {
                ++count;
            }
        }
    }
    return count;
}

// The cost is what the store asks or pays. The stock is the row's own count
// on the buy list; TSL's sell list gives the store's stock of the item, and
// TSL also gives how many the party has on hand.
void StoreGUI::showRow(ListBox &list) {
    auto item = selectedItem(list);
    auto store = _store.resolve();
    auto leader = _game.party().getLeader();
    if (!item || !store || !leader) {
        return;
    }
    const auto countText = [this](std::optional<int> count) {
        return count ? std::to_string(*count) : _game.getInterfaceText(kInfiniteStrRef);
    };
    const auto ownCount = item->isInfinite() ? std::nullopt : std::optional<int>(item->stackSize());
    _controls.LBL_COST_VALUE->setTextMessage(std::to_string(_selling ? store->sellPrice(*item) : store->buyPrice(*item)));
    std::string description = buildItemDescription(*item, _game, _services);
    if (!isTSL()) {
        _controls.LBL_STOCK_VALUE->setTextMessage(countText(ownCount));
    } else {
        const auto &party = _game.party();
        const auto &inventory = party.sharedInventoryReceiver(leader)->items();
        const int equipped = matchingEquippedCount(party, *item);
        std::optional<int> onHand;
        if (_selling) {
            _controls.LBL_STOCK_VALUE->setTextMessage(countText(matchingCount(store->items(), *item)));
            if (!item->isInfinite()) {
                // An equipped item counts the matching stack as well, less the
                // one equipped, which is counted already.
                int extra = equipped;
                if (item->isEquipped()) {
                    extra += matchingCount(inventory, *item).value_or(-1) - 1;
                }
                onHand = item->stackSize() + extra;
            }
        } else {
            _controls.LBL_STOCK_VALUE->setTextMessage(countText(ownCount));
            if (auto held = matchingCount(inventory, *item)) {
                onHand = *held + equipped;
            }
        }
        _controls.LBL_ONHAND_VALUE->setTextMessage(countText(onHand));
        description = _game.substituteLogTokens(std::move(description));
    }
    _controls.LB_DESCRIPTION->clearItems();
    _controls.LB_DESCRIPTION->addTextLinesAsItems(description);
}

std::shared_ptr<Item> StoreGUI::itemOfRow(const std::string &tag) const {
    return _game.getObjectById<Item>(static_cast<uint32_t>(std::stoul(tag)));
}

std::shared_ptr<Item> StoreGUI::selectedItem(const ListBox &list) const {
    const int index = list.selectedItemIndex();
    if (index < 0 || index >= list.getItemCount()) {
        return nullptr;
    }
    return itemOfRow(list.getItemAt(index).tag);
}

static int leaderLevel(const Party &party) {
    auto leader = party.getLeader();
    return leader ? leader->attributes().getAggregateLevel() : 0;
}

// The customer pays the price when it has the credits. A price above 50
// credits a level of the leader, or 50 with no leader, is confirmed first.
void StoreGUI::offerToBuy(const std::shared_ptr<Item> &item) {
    auto store = _store.resolve();
    if (!store) {
        return;
    }
    const auto price = static_cast<uint32_t>(store->buyPrice(*item));
    // KotOR says so in its examine box.
    if (static_cast<uint32_t>(_game.party().gold()) < price) {
        if (isTSL()) {
            _game.showMessagePopup(_game.getInterfaceText(kNotEnoughCreditsStrRef));
        } else {
            _game.showExamineBox(_game.getInterfaceText(kNotEnoughCreditsStrRef));
        }
        return;
    }
    const auto &party = _game.party();
    const int limit = party.getLeader() ? leaderLevel(party) * kTradeLimitPerLevel : kTradeLimitPerLevel;
    if (price > static_cast<uint32_t>(limit)) {
        RuntimeObjectRef<Item> offered(item);
        _game.showConfirmPopup(
            _game.getInterfaceText(kConfirmBuyAbovePriceStrRef, {{0, std::to_string(limit)}}),
            [this, offered]() {
                if (auto item = offered.resolve()) {
                    buy(item);
                }
            });
        return;
    }
    buy(item);
}

// A sale above 50 credits a level of the leader, at most 250, or 250 with no
// leader, is confirmed first. TSL asks before selling an equipped item, and refuses one in a slot
// the wearer's appearance locks.
void StoreGUI::offerToSell(const std::shared_ptr<Item> &item) {
    auto store = _store.resolve();
    if (!store) {
        return;
    }
    const auto &party = _game.party();
    const int limit = party.getLeader() ? std::min(leaderLevel(party) * kTradeLimitPerLevel, kSellLimitCap) : kSellLimitCap;
    RuntimeObjectRef<Item> offered(item);
    const auto sellOffered = [this, offered]() {
        if (auto item = offered.resolve()) {
            sell(item);
        }
    };
    if (isTSL() && item->isEquipped()) {
        if (auto leader = _game.party().getLeader()) {
            for (const auto &[slot, equipped] : leader->equipment()) {
                if (equipped == item && (leader->equipSlotsLocked() & equipmentSlotMask(slot)) != 0) {
                    _game.showMessagePopup(_game.getInterfaceText(kCannotBeSoldStrRef));
                    return;
                }
            }
        }
        _game.showConfirmPopup(_game.getInterfaceText(kConfirmSellEquippedStrRef), sellOffered);
        return;
    }
    if (static_cast<uint32_t>(store->sellPrice(*item)) > static_cast<uint32_t>(limit)) {
        _game.showConfirmPopup(
            _game.getInterfaceText(kConfirmSellAbovePriceStrRef, {{0, std::to_string(limit)}}),
            sellOffered);
        return;
    }
    sell(item);
}

void StoreGUI::buy(const std::shared_ptr<Item> &item) {
    auto store = _store.resolve();
    auto leader = _game.party().getLeader();
    if (!store || !leader) {
        return;
    }
    _presentation.sounds.play(_presentation.sounds.getInventoryAdd());
    const int price = store->buyPrice(*item);
    _game.party().takeGold(price);
    store->sellItem(item, leader);
    // TSL shows the first row selected again.
    relist(*_controls.LB_SHOPITEMS, !isTSL());
    showCredits();
}

// An equipped item is taken off first. One of a stack is sold, or else the
// whole item.
void StoreGUI::sell(const std::shared_ptr<Item> &item) {
    auto store = _store.resolve();
    auto leader = _game.party().getLeader();
    if (!store || !leader) {
        return;
    }
    auto inventory = _game.party().sharedInventoryReceiver(leader);
    const bool wasEquipped = item->isEquipped();
    // An unequip that is refused, or whose item joins a stack, sells nothing.
    if (wasEquipped &&
        (runUnequip(_game, *leader, item, nullptr, true) != EquipmentOperationOutcome::Applied || !item->isRuntimeLive())) {
        return;
    }
    _presentation.sounds.play(_presentation.sounds.getInventoryDrop());
    const int price = store->sellPrice(*item);
    _game.party().giveGold(price);
    std::shared_ptr<Item> sold = item;
    if (item->stackSize() > 1) {
        sold = _game.newItemClone(*item);
        sold->setStackSize(1);
        item->setStackSize(item->stackSize() - 1);
    } else {
        inventory->removeItemStack(item);
    }
    store->acquireItem(sold);
    // TSL shows the first row selected again after an equipped item's sale.
    relist(*_controls.LB_INVITEMS, !wasEquipped);
    showCredits();
}

void StoreGUI::relist(ListBox &list, bool keepSelection) {
    const int selected = list.selectedItemIndex();
    const int offset = list.getItemOffset();
    if (&list == _controls.LB_INVITEMS.get()) {
        listInventoryItems();
    } else {
        listShopItems();
    }
    // The list scrolls back no further than keeps it full.
    list.setItemOffset(0);
    const int lastTop = std::max(0, list.getItemCount() - list.visibleItemCount());
    list.setItemOffset(std::min(offset, lastTop));
    if (keepSelection && list.getItemCount() > 0) {
        list.setSelectedItemIndex(std::min(selected, list.getItemCount() - 1));
        showRow(list);
    }
}

// The screen shows the party's credits, and follows them when anything else,
// such as the store's open script, changes them while it is up.
void StoreGUI::showCredits() {
    const int credits = _game.party().gold();
    if (_shownCredits == credits) {
        return;
    }
    _shownCredits = credits;
    _controls.LBL_CREDITS_VALUE->setTextMessage(std::to_string(credits));
}

void StoreGUI::update(float dt) {
    GameGUI::update(dt);
    showCredits();
}

void StoreGUI::close() {
    _presentation.sounds.play(_presentation.sounds.getPanelRemoved());
    _game.closeStore();
}

} // namespace game

} // namespace reone
