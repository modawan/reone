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

#include "reone/game/attack.h"
#include "reone/game/castspell.h"
#include "reone/game/combat.h"
#include "reone/game/di/services.h"
#include "reone/game/effect.h"
#include "reone/game/equipmentoperation.h"
#include "reone/game/equipmentrules.h"
#include "reone/game/game.h"
#include "reone/game/itemdescription.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/game/runtimeref.h"
#include "reone/game/twodautil.h"
#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"

using namespace reone::resource;
namespace reone::game {
namespace {

// Why the equipment screen will not open a slot or equip an item.
static constexpr int kRestrictModeSlotStrRef = 42346;
static constexpr int kLockedSlotStrRef = 125554;
static constexpr int kArmorInCombatSlotStrRef = 1506;
static constexpr int kOffHandUnderLargeWeaponStrRef = 42344;
static constexpr int kNoItemsForSlotStrRef = 42345;
static constexpr int kCannotEquipStrRef = 38450;
static constexpr int kWieldMismatchStrRef = 42271;
static constexpr int kLargeWeaponWithLockedOffHandStrRef = 125569;

// What the inventory tells the player about an item the leader cannot use now.
static constexpr int kNotUsableEquipmentStrRef = 42485;
static constexpr int kNotUsableStrRef = 42486;
static constexpr int kFullHealthStrRef = 42499;
static constexpr int kSquadFullHealthStrRef = 48494;
static constexpr int kTSLSquadFullHealthStrRef = 48731;
static constexpr int kItemUseTooSoonStrRef = 42409;

// In TSL's Ebon Hawk every companion aboard stands in the module, so there the
// equipment screen pages through the whole roster instead of the party.
static constexpr char kRosterBrowsingModule[] = "003EBO";

// The roster places a TSL companion can take, browsed after the player, and
// the browse index of the player, before the first companion.
static constexpr int kRosterSize = static_cast<int>(Party::kK2NpcCount);
static constexpr int kPlayerBrowseIndex = -1;

// The party members a squad recovery kit heals besides the leader.
static constexpr int kSquadMembersBesideLeader = 2;

static bool isHurt(const Creature &creature) {
    return creature.currentHitPoints() < creature.maxHitPoints();
}

// Activating an entry uses the item, or explains why not. Equipment and
// anything else the leader cannot use get a message; so do healing kits with
// nobody to heal, except that a poisoned leader at full vitality and a squad
// kit while anyone is hurt get no response at all.
static void describeActivation(Game &game, const Creature &leader, const Item &item, MenuItemView &view) {
    auto message = [&view](int strRef) {
        view.activation = InventoryActivation::Message;
        view.messageStrRef = strRef;
    };
    if (!isLeaderUsableItem(leader, item)) {
        message(item.isEquippable() ? kNotUsableEquipmentStrRef : kNotUsableStrRef);
        return;
    }
    const bool tsl = game.isTSL();
    const bool droid = leader.racialType() == RacialType::Droid;
    switch (item.itemType()) {
    case Item::kDroidRepairItemType:
    case Item::kMedicalItemType:
        if (!isHurt(leader)) {
            if (leader.activePoisonEffectId() == kUnassignedEffectId) {
                message(kFullHealthStrRef);
            }
            return;
        }
        // TSL keeps repair kits for droids and medical kits for everyone else.
        if (tsl && droid != (item.itemType() == Item::kDroidRepairItemType)) {
            message(kNotUsableStrRef);
            return;
        }
        break;
    case Item::kSquadRecoveryItemType: {
        if (tsl && droid) {
            break;
        }
        bool anyoneHurt = isHurt(leader);
        for (int i = 1; i <= kSquadMembersBesideLeader; ++i) {
            auto member = game.party().getMember(i);
            if (member && isHurt(*member)) {
                anyoneHurt = true;
            }
        }
        if (!anyoneHurt) {
            message(tsl ? kTSLSquadFullHealthStrRef : kSquadFullHealthStrRef);
        }
        return;
    }
    default:
        break;
    }
    view.activation = InventoryActivation::Use;
}

// TSL appearances can lock slots (a mask of equipment slots); a blank cell
// locks none. A locked hand also locks its alternate.
static bool isEquipmentSlotLocked(ServicesView &services, const Creature &creature, int slot) {
    auto appearances = getRequiredTwoDA(services.resource.twoDas, "appearance");
    const auto locked = static_cast<uint32_t>(appearances->getInt(creature.appearance(), "equipslotslocked", 0));
    if (slot == InventorySlots::leftWeapon2) {
        slot = InventorySlots::leftWeapon;
    }
    if (slot == InventorySlots::rightWeapon2) {
        slot = InventorySlots::rightWeapon;
    }
    return (locked & equipmentSlotMask(slot)) != 0;
}
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

    void beginEquipment() override {
        const auto module = _game.module();
        _browseRoster = _game.isTSL() && module && boost::iequals(module->name(), kRosterBrowsingModule);
        _browseIndex = kPlayerBrowseIndex;
        _browseSubject = _browseRoster ? _game.party().player() : nullptr;
        _game.setInventoryMenuCharacter(kPlayerBrowseIndex);
    }

    void endEquipment() override {
        _browseRoster = false;
        _browseSubject.reset();
        _game.setInventoryMenuCharacter(kPlayerBrowseIndex);
    }

    // Paging forward goes from the player through each available companion
    // standing aboard, which the party takes as that companion's creature,
    // and back to the player.
    void nextCharacter() override {
        if (!_browseRoster) {
            return;
        }
        const auto &area = *_game.module()->area();
        for (int attempts = 0; attempts < kRosterSize; ++attempts) {
            if (++_browseIndex == kRosterSize) {
                selectCharacter(kPlayerBrowseIndex, _game.party().player());
                return;
            }
            if (!_game.party().isMemberAvailable(_browseIndex)) {
                continue;
            }
            auto creature = _game.party().bindPlacedCompanion(_browseIndex, area);
            if (!creature) {
                continue;
            }
            selectCharacter(_browseIndex, creature);
            return;
        }
    }

    // A member who is down cannot take control. Browsing the roster changes
    // no one.
    void changeCharacter(int member) override {
        if (_browseRoster) {
            return;
        }
        if (member < 0) {
            _game.party().selectNextStandingMember();
            return;
        }
        auto creature = _game.party().getMember(member);
        if (!creature || creature->isDead() || creature->isTemporarilyDead()) {
            return;
        }
        _game.party().setPartyLeaderByIndex(member);
    }

    void previousCharacter() override {
        if (!_browseRoster) {
            return;
        }
        for (int attempts = 0; attempts < kRosterSize; ++attempts) {
            if (--_browseIndex == kPlayerBrowseIndex) {
                selectCharacter(kPlayerBrowseIndex, _game.party().player());
                return;
            }
            // Before the player comes the last companion.
            if (_browseIndex < kPlayerBrowseIndex) {
                _browseIndex = kRosterSize - 1;
            }
            if (!_game.party().isMemberAvailable(_browseIndex)) {
                continue;
            }
            // Paging back shows only companions the party already has a creature for.
            auto creature = _game.party().getAvailableMember(_browseIndex);
            if (!creature) {
                continue;
            }
            selectCharacter(_browseIndex, creature);
            return;
        }
    }

    InventoryView readInventory(InventoryFilter filter) override {
        bindSubject();
        InventoryView view;
        view.subject = subjectView();
        auto owner = _owner.resolve();
        auto subject = _subject.resolve();
        if (!owner)
            return view;
        std::vector<std::shared_ptr<Item>> listed;
        // The inventory's subject is the leader, who uses its items.
        auto describeForUse = [&](const std::shared_ptr<Item> &item, bool equipped) {
            auto entry = describe(item, equipped);
            if (subject) {
                describeActivation(_game, *subject, *item, entry);
            }
            return entry;
        };
        if (subject) {
            view.itemUseCoolingDown = subject->isItemUseCoolingDown();
            for (const auto &[slot, item] : subject->equipment()) {
                if (isInventoryListedEquipmentSlot(slot) && listable(item) && itemMatchesFilter(_services, *item, filter)) {
                    view.items.push_back(describeForUse(item, true));
                    listed.push_back(item);
                }
            }
        }
        for (const auto &item : owner->items()) {
            if (listable(item) && std::find(listed.begin(), listed.end(), item) == listed.end() && itemMatchesFilter(_services, *item, filter)) {
                view.items.push_back(describeForUse(item, false));
            }
        }
        return view;
    }

    std::string interfaceText(int strRef) const override { return _game.getInterfaceText(strRef); }

    std::optional<int> useItem(uint64_t handle) override {
        auto entry = _items.find(handle);
        auto item = entry != _items.end() ? entry->second.reference.resolve() : nullptr;
        auto leader = _subject.resolve();
        if (!item || !live(item) || !leader || leader != _game.party().getLeader()) {
            return std::nullopt;
        }
        // In direct combat, an item used too recently holds back the next.
        if (leader->isInCombat() && leader->combatActivationType() == CombatActivation::Direct &&
            leader->isItemUseCoolingDown())
            return kItemUseTooSoonStrRef;
        useItemAtOnce(_game, *leader, item);
        return std::nullopt;
    }

    EquipmentView readEquipment(int slot) override {
        bindSubject();
        _slot = slot;
        EquipmentView view;
        view.canBrowseCharacters = _browseRoster;
        view.revision = _revision;
        view.subject = subjectView();
        auto owner = _owner.resolve();
        auto subject = _subject.resolve();
        if (!owner || !subject)
            return view;
        view.droid = subject->racialType() == RacialType::Droid;
        for (const auto &[equippedSlot, item] : subject->equipment())
            if (live(item))
                view.equipment[equippedSlot] = item->icon();
        // The main hand shows the right-hand weapon, or an unarmed hit. The
        // off hand shows a double-bladed right-hand weapon, else the left-hand
        // weapon, and stays blank without either. The attack is that of the
        // equipped hand for the shown weapon's kind. A value counts as raised
        // when the item and effect modifiers lift a damage bound, or when the
        // attack exceeds the base attack bonus.
        auto describeHand = [&subject](const std::shared_ptr<Item> &weapon, bool offHand, std::string &damage, bool &damageRaised,
                                       std::string &attack, bool &attackRaised) {
            int minimum, maximum, baseMinimum, baseMaximum;
            subject->getDamageRange(weapon, offHand, true, minimum, maximum);
            subject->getDamageRange(weapon, offHand, false, baseMinimum, baseMaximum);
            damage = str(boost::format("%d-%d") % minimum % maximum);
            damageRaised = minimum > baseMinimum || maximum > baseMaximum;
            const auto breakdown = subject->getHandAttackBonusBreakdown(
                weapon && weapon->isRanged(), offHand,
                weapon && weapon->weaponWield() == WeaponWield::DoubleBladedSword, true);
            const int bonus = breakdown.total();
            attack = (bonus > 0 ? "+" : "") + std::to_string(bonus);
            attackRaised = bonus > breakdown.baseAttackBonus;
        };
        auto right = subject->getEquippedItem(InventorySlots::rightWeapon);
        describeHand(right, false, view.mainDamage, view.mainDamageRaised, view.mainAttack, view.mainAttackRaised);
        auto off = subject->getEquippedOffhandWeapon();
        if (off) {
            describeHand(off, true, view.offDamage, view.offDamageRaised, view.offAttack, view.offAttackRaised);
        }
        // TSL also shows the other weapon set the same way, with the equipped
        // hands' modifiers and attack. Only one double-bladed weapon across
        // both sets gets its off-hand values: with one in the first set's
        // right hand, the second set's off hand stays blank.
        if (_game.isTSL()) {
            // TSL does not colour these values.
            bool raised = false;
            auto right2 = subject->getEquippedItem(InventorySlots::rightWeapon2);
            auto left2 = subject->getEquippedItem(InventorySlots::leftWeapon2);
            const bool firstSetDouble = right && right->weaponWield() == WeaponWield::DoubleBladedSword;
            if (left2) {
                describeHand(left2, true, view.offDamage2, raised, view.offAttack2, raised);
            }
            if (right2 && right2->weaponWield() == WeaponWield::DoubleBladedSword && !firstSetDouble) {
                describeHand(right2, true, view.offDamage2, raised, view.offAttack2, raised);
            }
            describeHand(right2, false, view.mainDamage2, raised, view.mainAttack2, raised);
        }
        // A slot opens unless the area restricts play, the slot is locked, it
        // is the armour slot in direct combat, it is the off hand under a
        // large weapon, or nothing can go in it.
        auto refuse = [&view](int strRef) {
            view.slotAvailable = false;
            view.slotRefusalStrRef = strRef;
            return view;
        };
        const bool tsl = _game.isTSL();
        view.slotAvailable = true;
        if (slot >= 0) {
            if (_game.module()->area()->playerRestrictMode()) {
                return refuse(kRestrictModeSlotStrRef);
            }
            if (tsl && isEquipmentSlotLocked(_services, *subject, slot)) {
                return refuse(kLockedSlotStrRef);
            }
            if (slot == InventorySlots::body && isArmorChangeRefused(*subject)) {
                return refuse(kArmorInCombatSlotStrRef);
            }
            if (!evaluateEquipmentSlotActivation(*subject, slot).available) {
                return refuse(kOffHandUnderLargeWeaponStrRef);
            }
        }
        auto equipped = slot >= 0 ? subject->getEquippedItem(slot) : nullptr;
        if (live(equipped))
            view.items.push_back(describe(equipped, true));
        const bool hideUnequippable = _game.feedbackOptions().hideUnequippable;
        const bool weaponHand = isMainHandWeaponSlot(slot) || isOffHandWeaponSlot(slot);
        // TSL's other weapon set lists its rows by admission alone: the pair
        // rules apply when the change is made.
        const bool alternateHand = slot == InventorySlots::leftWeapon2 || slot == InventorySlots::rightWeapon2;
        const int otherHand = isMainHandWeaponSlot(slot) ? getPairedOffHandSlot(slot) : getPairedMainHandSlot(slot);
        auto other = weaponHand && !alternateHand ? subject->getEquippedItem(otherHand) : nullptr;
        const bool offHandLocked = tsl && isMainHandWeaponSlot(slot) &&
                                   isEquipmentSlotLocked(_services, *subject, getPairedOffHandSlot(slot));
        for (const auto &item : owner->items()) {
            if (!live(item) || item == equipped)
                continue;
            if (slot < 0) {
                if (item->isEquippable())
                    view.items.push_back(describe(item, false));
                continue;
            }
            auto decision = evaluateEquipmentCandidate(*subject, slot, item.get());
            if (!decision.visible) {
                continue;
            }
            // Items only droids or only humans wear are not listed for the others.
            if ((item->droidOrHuman() == Item::DroidOrHuman::HumanOnly && subject->racialType() != RacialType::Human) ||
                (item->droidOrHuman() == Item::DroidOrHuman::DroidOnly && subject->racialType() != RacialType::Droid))
                continue;
            const bool listedValid = alternateHand
                                         ? evaluateEquipmentAdmission(*subject, *item) == EquipmentCandidateReason::None
                                         : decision.valid;
            if (hideUnequippable && !(hasEquipmentProficiency(*subject, *item) && listedValid)) {
                continue;
            }
            int refusal = listedValid ? 0 : kCannotEquipStrRef;
            // Beside a held weapon the screen pairs only matching swords or pistols.
            if (other && !(isMainHandWeaponSlot(slot) && item->weaponSize() == CreatureSize::Large) &&
                (other->weaponWield() != item->weaponWield() ||
                 (item->weaponWield() != WeaponWield::SingleSword && item->weaponWield() != WeaponWield::BlasterPistol))) {
                refusal = kWieldMismatchStrRef;
            }
            if (offHandLocked && item->weaponSize() == CreatureSize::Large) {
                refusal = kLargeWeaponWithLockedOffHandStrRef;
            }
            auto entry = describe(item, false);
            entry.valid = refusal == 0;
            entry.refusalStrRef = refusal;
            view.items.push_back(std::move(entry));
        }
        if (slot >= 0 && view.items.empty()) {
            return refuse(kNoItemsForSlotStrRef);
        }
        return view;
    }

    void equip(uint64_t revision, uint64_t handle, int slot) override {
        auto reject = [&]() { _result = EquipmentRequestResult {revision, EquipmentRequestOutcome::Rejected}; };
        auto subject = _subject.resolve();
        auto owner = _owner.resolve();
        if (revision != _revision || slot != _slot || slot < 0 || !subject || !owner ||
            subject != selectedSubject() || owner != _game.party().actualPlayer()) {
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
        // A change made on the screen replaces whatever the character was
        // doing; one that is fighting takes up its round's target again.
        subject->clearAllActions(true);
        _game.combat().discardEquipment(*subject);
        // The item leaving the slot goes out at once, whatever the slot.
        subject->putOutItemLeavingSlot(slot);
        auto outcome = applyEquipmentOperation(_game, *subject, *owner, item, slot);
        if (auto target = subject->isInCombat() ? subject->getRoundTarget() : nullptr) {
            _game.combat().scheduleAttack(*subject, target);
        }
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

    void switchWeapons() override {
        if (auto subject = selectedSubject()) {
            subject->requestSwitchWeapons(true);
        }
    }

private:
    struct BoundItem {
        RuntimeObjectRef<Item> reference;
        int stackSize;
    };

    Game &_game;
    ServicesView &_services;
    RuntimeObjectRef<Creature> _subject;
    RuntimeObjectRef<Creature> _browseSubject;
    bool _browseRoster {false};
    /** The roster place shown while browsing, or kPlayerBrowseIndex. */
    int _browseIndex {kPlayerBrowseIndex};
    RuntimeObjectRef<Creature> _owner;
    uint64_t _revision {0};
    uint64_t _nextHandle {1};
    int _slot {-1};
    std::unordered_map<uint64_t, BoundItem> _items;
    std::map<int, RuntimeObjectRef<Item>> _equipment;
    std::optional<EquipmentRequestResult> _result;

    bool live(const std::shared_ptr<Item> &item) const {
        return item && item->isRuntimeLive() && _game.getObjectById(item->id()) == item;
    }
    // A spent item leaves the inventory list at once, before it is destroyed.
    bool listable(const std::shared_ptr<Item> &item) const {
        return live(item) && item->stackSize() > 0;
    }
    void selectCharacter(int npc, const std::shared_ptr<Creature> &creature) {
        _browseIndex = npc;
        _browseSubject = creature;
        _game.setInventoryMenuCharacter(npc);
    }
    std::shared_ptr<Creature> selectedSubject() const {
        return _browseRoster ? _browseSubject.resolve() : _game.party().getLeader();
    }
    void bindSubject() {
        ++_revision;
        _items.clear();
        _subject = selectedSubject();
        // The party's carried items are held by the actual player.
        _owner = _game.party().actualPlayer();
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
        if (_browseRoster) {
            view.portraits = {};
            view.portraits[0] = subject->portrait();
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
        view.description = buildItemDescription(*item, _game, _services);
        // TSL shows item names and descriptions with their actions hidden.
        if (_game.isTSL()) {
            view.name = _game.substituteLogTokens(std::move(view.name));
            view.description = _game.substituteLogTokens(std::move(view.description));
        }
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
