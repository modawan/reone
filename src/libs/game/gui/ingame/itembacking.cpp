/*
 * Copyright (c) 2020-2026 The reone project contributors
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

#include "reone/game/gui/ingame/itembacking.h"
#include "reone/game/di/services.h"
#include "reone/game/equipmentoperation.h"
#include "reone/game/game.h"
#include "reone/game/itemdescription.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/party.h"
#include "reone/game/runtimeref.h"
#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"

using namespace reone::resource;
namespace reone::game {
namespace {
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

static bool itemMatchesFilter(ServicesView &services, const Item &item, InventoryFilter filter) {
    BaseItemFilterInfo baseItem(getBaseItemFilterInfo(services, item));
    switch (filter) {
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

class ItemMenuBacking final : public IInventoryMenuBacking, public IEquipmentMenuBacking {
public:
    ItemMenuBacking(Game &game, ServicesView &services) : _game(game), _services(services) {}

    InventoryView readInventory(InventoryFilter filter) override {
        bindSubject();
        InventoryView view;
        view.subject = subjectView();
        auto owner = _owner.resolve();
        auto subject = _subject.resolve();
        if (!owner)
            return view;
        std::vector<std::shared_ptr<Item>> listed;
        if (subject) {
            for (const auto &[slot, item] : subject->equipment()) {
                if (isInventoryListedEquipmentSlot(slot) && live(item) && itemMatchesFilter(_services, *item, filter)) {
                    view.items.push_back(describe(item, true));
                    listed.push_back(item);
                }
            }
        }
        for (const auto &item : owner->items()) {
            if (live(item) && std::find(listed.begin(), listed.end(), item) == listed.end() && itemMatchesFilter(_services, *item, filter))
                view.items.push_back(describe(item, false));
        }
        return view;
    }

    EquipmentView readEquipment(int slot) override {
        bindSubject();
        _slot = slot;
        EquipmentView view;
        view.revision = _revision;
        view.subject = subjectView();
        auto owner = _owner.resolve();
        auto subject = _subject.resolve();
        if (!owner || !subject)
            return view;
        for (const auto &[equippedSlot, item] : subject->equipment())
            if (live(item))
                view.equipment[equippedSlot] = item->icon();
        int minimum, maximum;
        subject->getMainHandDamage(minimum, maximum);
        view.mainDamage = str(boost::format("%d-%d") % minimum % maximum);
        subject->getOffhandDamage(minimum, maximum);
        view.offDamage = str(boost::format("%d-%d") % minimum % maximum);
        auto attack = [](int bonus) { return (bonus > 0 ? "+" : "") + std::to_string(bonus); };
        view.mainAttack = attack(subject->getAttackBonus());
        view.offAttack = attack(subject->getAttackBonus(true));
        view.slotAvailable = slot < 0 || evaluateEquipmentSlotActivation(*subject, slot).available;
        if (!view.slotAvailable)
            return view;
        auto equipped = slot >= 0 ? subject->getEquippedItem(slot) : nullptr;
        if (live(equipped))
            view.items.push_back(describe(equipped, true));
        for (const auto &item : owner->items()) {
            if (!live(item) || item == equipped)
                continue;
            if (slot < 0) {
                if (item->isEquippable())
                    view.items.push_back(describe(item, false));
            } else {
                auto decision = evaluateEquipmentCandidate(*subject, slot, item.get());
                if (decision.visible) {
                    auto entry = describe(item, false);
                    entry.valid = decision.valid;
                    view.items.push_back(std::move(entry));
                }
            }
        }
        return view;
    }

    void equip(uint64_t revision, uint64_t handle, int slot) override {
        auto reject = [&]() { _result = EquipmentRequestResult {revision, EquipmentRequestOutcome::Rejected}; };
        auto subject = _subject.resolve();
        auto owner = _owner.resolve();
        if (revision != _revision || slot != _slot || slot < 0 || !subject || !owner ||
            subject != _game.party().getLeader() || owner != _game.party().player()) {
            reject();
            return;
        }
        if (subject->equipment().size() != _equipment.size()) {
            reject();
            return;
        }
        for (const auto &[equippedSlot, reference] : _equipment) {
            auto equipped = reference.resolve();
            if (!equipped || subject->getEquippedItem(equippedSlot) != equipped) {
                reject();
                return;
            }
        }
        std::shared_ptr<Item> item;
        if (handle != 0) {
            auto entry = _items.find(handle);
            item = entry != _items.end() ? entry->second.reference.resolve() : nullptr;
            if (!item || item->stackSize() != entry->second.stackSize) {
                reject();
                return;
            }
        }
        // Invalidate all selections before calling authority, including on failure.
        ++_revision;
        _items.clear();
        auto outcome = applyEquipmentOperation(_game, *subject, *owner, item, slot);
        EquipmentRequestOutcome result;
        switch (outcome) {
        case EquipmentOperationOutcome::Applied:
            result = EquipmentRequestOutcome::Applied;
            break;
        case EquipmentOperationOutcome::Unchanged:
            result = EquipmentRequestOutcome::Unchanged;
            break;
        case EquipmentOperationOutcome::Rejected:
            result = EquipmentRequestOutcome::Rejected;
            break;
        default:
            result = EquipmentRequestOutcome::Failed;
            break;
        }
        _result = EquipmentRequestResult {revision, result};
    }

    std::optional<EquipmentRequestResult> equipmentResult() const override { return _result; }

private:
    Game &_game;
    ServicesView &_services;
    RuntimeObjectRef<Creature> _subject;
    RuntimeObjectRef<Creature> _owner;
    uint64_t _revision {0};
    uint64_t _nextHandle {1};
    int _slot {-1};
    struct BoundItem {
        RuntimeObjectRef<Item> reference;
        int stackSize;
    };
    std::unordered_map<uint64_t, BoundItem> _items;
    std::map<int, RuntimeObjectRef<Item>> _equipment;
    std::optional<EquipmentRequestResult> _result;

    bool live(const std::shared_ptr<Item> &item) const {
        return item && item->isRuntimeLive() && _game.getObjectById(item->id()) == item;
    }
    void bindSubject() {
        ++_revision;
        _items.clear();
        _subject = _game.party().getLeader();
        _owner = _game.party().player();
        _equipment.clear();
        if (auto subject = _subject.resolve()) {
            for (const auto &[slot, item] : subject->equipment())
                _equipment.emplace(slot, RuntimeObjectRef<Item>(item));
        }
    }
    MenuSubjectView subjectView() const {
        MenuSubjectView view;
        view.credits = std::to_string(_game.party().gold());
        auto subject = _subject.resolve();
        if (!subject)
            return view;
        view.present = true;
        for (int i = 0; i < 3; ++i) {
            auto member = _game.party().getMember(i);
            if (member && member->isRuntimeLive())
                view.portraits[i] = member->portrait();
        }
        view.vitality = std::to_string(subject->currentHitPoints()) + "/\n" + std::to_string(subject->maxHitPoints());
        view.defense = std::to_string(subject->getDefense());
        return view;
    }
    MenuItemView describe(const std::shared_ptr<Item> &item, bool equipped) {
        MenuItemView view;
        view.handle = _nextHandle++;
        _items.emplace(view.handle, BoundItem {RuntimeObjectRef<Item>(item), item->stackSize()});
        view.name = item->localizedName();
        view.description = joinItemDescriptionLines(buildItemDescriptionLines(*item, _services));
        view.icon = item->icon();
        view.stackSize = item->stackSize();
        view.equipped = equipped;
        return view;
    }
};
} // namespace

std::shared_ptr<IInventoryMenuBacking> newInventoryMenuBacking(Game &game, ServicesView &services) {
    return std::make_shared<ItemMenuBacking>(game, services);
}
std::shared_ptr<IEquipmentMenuBacking> newEquipmentMenuBacking(Game &game, ServicesView &services) {
    return std::make_shared<ItemMenuBacking>(game, services);
}
} // namespace reone::game
