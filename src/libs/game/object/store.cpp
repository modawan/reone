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

#include "reone/game/object/store.h"

#include <algorithm>

#include "reone/game/di/services.h"
#include "reone/game/equipmentrules.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/party.h"
#include "reone/game/script/runner.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/strings.h"

using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::script;

namespace reone {

namespace game {

void Store::loadFromBlueprint(const std::string &resRef) {
    std::shared_ptr<Gff> utm(_services.resource.gffs.get(resRef, ResType::Utm));
    if (!utm) {
        return;
    }
    deserializeAll(*utm, SerializedIdentityContext::templateResource(resRef), false);
}

// The fields a store placed from its template takes from its area entry.
static const std::vector<std::string> kEntryFields {
    "XPosition", "YPosition", "ZPosition", "XOrientation", "YOrientation", "ZOrientation"};

// A store placed from its template takes everything from the template but
// its position and orientation, which its entry gives. A saved store takes
// everything from its record.
void Store::deserialize(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    std::string templateRes;
    std::shared_ptr<Gff> utm;
    if (!identityContext.isSerializedState() &&
        gff.readResRef(templateRes, "ResRef")) {
        utm = _services.resource.gffs.get(templateRes, ResType::Utm);
    }
    if (utm) {
        deserializeAll(*utm, SerializedIdentityContext::templateResource(templateRes), true);
        captureAuthoredRecord(*utm, gff, kEntryFields);
        readPlacement(gff, true);
    } else {
        deserializeAll(gff, identityContext, false);
    }
    updateTransform();
}

void Store::deserializeAll(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    bool placedTemplate) {
    deserializeRuntimeState(gff, identityContext);
    if (gff.readString(_tag, "Tag")) {
        boost::to_lower(_tag);
    }

    if (gff.readLocString(_locName, "LocName", _services.resource.strings)) {
        _name = _locName.str();
    }

    gff.readInt(_markDown, "MarkDown");
    gff.readInt(_markUp, "MarkUp");
    gff.readResRef(_onOpenStore, "OnOpenStore");
    gff.readByte(_buySellFlag, "BuySellFlag");

    readPlacement(gff, false);

    gff.readBool(_commandable, "Commandable");

    deserializeItems(gff, identityContext, placedTemplate);
}

// The orientation needs both of its fields unless missing ones count as 0.
void Store::readPlacement(const resource::Gff &gff, bool zeroWhereMissing) {
    if (zeroWhereMissing) {
        _position = glm::vec3(0.0f);
    }
    gff.readFloat(_position[0], "XPosition");
    gff.readFloat(_position[1], "YPosition");
    gff.readFloat(_position[2], "ZPosition");
    float cosine = 0.0f;
    float sine = 0.0f;
    const bool cosineRead = gff.readFloat(cosine, "XOrientation");
    const bool sineRead = gff.readFloat(sine, "YOrientation");
    if (zeroWhereMissing || (cosineRead && sineRead)) {
        _orientation = glm::quat(glm::vec3(0.0f, 0.0f, -glm::atan(cosine, sine)));
    }
}

// Loaded stock lies dearest first, items of one cost in the order read. It
// is filled cheapest first, the last read of one cost first, each item at
// the front unless it joins an identical stack already there: so an item
// joins one read after it, whose flags, Infinite among them, stay.
void Store::deserializeItems(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    bool placedTemplate) {
    if (!gff.has("ItemList")) {
        return;
    }
    std::vector<std::shared_ptr<Object>> obsolete(_items.begin(), _items.end());
    std::vector<std::shared_ptr<Item>> replacement;
    _game.replaceRuntimeObjectGraph(
        obsolete,
        [&]() {
            std::vector<std::shared_ptr<Item>> loaded;
            for (const auto &entry : gff.getList("ItemList")) {
                std::shared_ptr<Item> item;
                if (placedTemplate) {
                    // The template of a store an area places makes each entry
                    // its InventoryRes template and, of the rest of the entry,
                    // only its Infinite; an entry without one makes no item.
                    std::string resRef;
                    if (!entry->readResRef(resRef, "InventoryRes") ||
                        !_services.resource.gffs.get(resRef, ResType::Uti)) {
                        continue;
                    }
                    item = _game.newItemFromBlueprint(resRef);
                } else {
                    // An entry naming a missing template makes no item.
                    if (!identityContext.isSerializedState() &&
                        Item::namesMissingTemplate(*entry, _services.resource.gffs)) {
                        continue;
                    }
                    item = _game.newOwnedItem(*entry, identityContext);
                }
                if (identityContext.isSerializedState()) {
                    item->captureSaveRecord(
                        *entry,
                        identityContext,
                        {SaveRecordOriginKind::StoreItem, std::to_string(_id)});
                }
                // An entry never runs out when its Infinite is any value but
                // 0; an entry without one runs out.
                item->setInfinite(entry->getUint("Infinite", 0) != 0);
                item->setOwner(*this);
                loaded.push_back(std::move(item));
            }
            std::stable_sort(loaded.begin(), loaded.end(), [this](const auto &left, const auto &right) {
                return itemCost(*left) > itemCost(*right);
            });
            for (auto it = loaded.rbegin(); it != loaded.rend(); ++it) {
                const auto &item = *it;
                const bool joined = std::any_of(replacement.begin(), replacement.end(), [&](const auto &stocked) {
                    return stocked->mergeStackFrom(*item);
                });
                if (joined) {
                    item->clearOwner();
                    _game.discardStagedRuntimeObjects({item});
                } else {
                    replacement.insert(replacement.begin(), item);
                }
            }
        },
        [&]() noexcept {
            _items = std::move(replacement);
        });
}

int Store::itemCost(const Item &item) const {
    return item.cost(_game.baseItemCostMultiplier(item.baseItemType()));
}

bool Store::fileItem(const std::shared_ptr<Item> &item) {
    const int cost = itemCost(*item);
    if (!_items.empty() && cost < itemCost(*_items.front())) {
        // The first item is dearer, so there is a last dearer item.
        const auto lastDearer = std::find_if(_items.rbegin(), _items.rend(), [&](const auto &stocked) {
            return itemCost(*stocked) > cost;
        });
        _items.insert(std::prev(lastDearer.base()), item);
        return true;
    }
    const bool joined = std::any_of(_items.begin(), _items.end(), [&](const auto &stocked) {
        return stocked->mergeStackFrom(*item);
    });
    if (!joined) {
        _items.insert(_items.begin(), item);
    }
    return !joined;
}

void Store::setBonusMarks(int markUp, int markDown) {
    _bonusMarkUp = std::clamp(markUp, -100, 100);
    _bonusMarkDown = std::clamp(markDown, -100, 100);
}

// A price is counted unsigned, so marks summing below zero wrap.
static int markedPrice(int cost, int percent) {
    return static_cast<int>(static_cast<uint32_t>(cost) * static_cast<uint32_t>(percent) / 100u);
}

int Store::buyPrice(const Item &item) const {
    return markedPrice(itemCost(item), _markUp + _bonusMarkUp);
}

int Store::sellPrice(const Item &item) const {
    return markedPrice(itemCost(item), _markDown + _bonusMarkDown);
}

std::shared_ptr<Item> Store::acquireItem(const std::shared_ptr<Item> &item) {
    if (item->plotFlag()) {
        _game.destroyRuntimeObjectGraph(item);
        return nullptr;
    }
    item->setIdentified(true);
    if (auto stocked = getItemByTag(item->tag()); stocked && stocked->isIdenticalTo(*item)) {
        // The stock grows by the whole stack taken in.
        if (!stocked->isInfinite()) {
            stocked->setStackSize(stocked->stackSize() + item->stackSize());
        }
        _game.destroyRuntimeObjectGraph(item);
        return nullptr;
    }
    if (!fileItem(item)) {
        _game.destroyRuntimeObjectGraph(item);
        return nullptr;
    }
    item->setOwner(*this);
    return item;
}

void Store::sellItem(const std::shared_ptr<Item> &item, const std::shared_ptr<Creature> &buyer) {
    auto sold = item;
    if (item->isInfinite() || item->stackSize() > 1) {
        // The one sold is never infinite.
        sold = _game.newItemClone(*item);
        sold->keepMarksOf(*item);
        sold->setInfinite(false);
        sold->setStackSize(1);
        if (!item->isInfinite()) {
            item->setStackSize(item->stackSize() - 1);
        }
    } else {
        removeItemStack(item);
    }
    announceAcquiredBy(sold, _game.party().sharedInventoryReceiver(buyer)->addItem(sold), *buyer, true);
}

void Store::onOpened(uint32_t openerId) {
    if (_onOpenStore.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onOpenStore,
        {{ArgKind::Caller, Variable::ofObject(_id)},
         {ArgKind::LastOpenedBy, Variable::ofObject(openerId)}});
}

} // namespace game

} // namespace reone
