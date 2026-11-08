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

#pragma once

#include <array>
#include <bitset>
#include <limits>
#include <optional>
#include <set>

#include "reone/audio/clip.h"
#include "reone/audio/source.h"
#include "reone/graphics/model.h"
#include "reone/graphics/texture.h"
#include "reone/resource/strings.h"

#include "../attack.h"
#include "../object.h"
#include "../types.h"

namespace reone {

namespace resources {
class Gff;
}

namespace resource {
class IGffs;
}

namespace game {

class Item : public Object {
public:
    /** Item types (baseitems.2da ItemType) with rules of their own. */
    static constexpr int kGrenadeItemType = 6;
    static constexpr int kDroidUtilityItemType = 12;
    static constexpr int kDroidShieldItemType = 13;
    static constexpr int kGauntletsItemType = 19;
    static constexpr int kForearmBandsItemType = 20;
    static constexpr int kCreditsItemType = 23;
    static constexpr int kDatapadItemType = 24;
    static constexpr int kStimItemType = 25;
    static constexpr int kDroidRepairItemType = 26;
    /** Security spikes, which add to an unlock and offer themselves on a locked object's menu. */
    static constexpr int kSecuritySpikeItemType = 27;
    static constexpr int kTrapKitItemType = 28;
    /** Body armour of armour classes 4 to 9, the first of six item types. */
    static constexpr int kArmorClass4ItemType = 31;
    static constexpr int kArmorClass9ItemType = 36;
    /** The last of the body armour, robe and clothing item types, which start at kArmorClass4ItemType. */
    static constexpr int kLastBodyItemType = 38;
    /** Double-bladed, short and ordinary lightsabers, the first of three item types. */
    static constexpr int kDoubleLightsaberItemType = 39;
    static constexpr int kShortLightsaberItemType = 40;
    static constexpr int kLightsaberItemType = 41;
    static constexpr int kPazaakCardItemType = 42;
    static constexpr int kPazaakSideboardItemType = 43;
    static constexpr int kStealthUnitItemType = 44;
    static constexpr int kMedicalItemType = 45;
    static constexpr int kSquadRecoveryItemType = 47;
    static constexpr int kWristLauncherItemType = 48;
    static constexpr int kRocketItemType = 49;
    static constexpr int kChemicalsItemType = 50;
    static constexpr int kComponentsItemType = 51;

    /** The local boolean set on a datapad once its text has been shown, or once it is taken without being shown. */
    static constexpr int kDatapadShownBoolean = 94;

    /** The baseitems.2da model type of an item drawn as its wearer's body model. */
    static constexpr int kBodyModelType = 1;

    /**
     * Activate Item cost values (iprp_chargecost.2da): how a cast-spell
     * property is paid for. Codes 2 to 6 take 5 down to 1 charges a use, 8 to
     * 12 allow 1 to 5 uses a day, and 14 to 18 allow a use every 1 to 5
     * minutes.
     */
    static constexpr int kSingleUse = 1;
    static constexpr int kFiveChargesPerUse = 2;
    static constexpr int kOneChargePerUse = 6;
    static constexpr int kNoChargesPerUse = 7;
    static constexpr int kOneUsePerDay = 8;
    static constexpr int kFiveUsesPerDay = 12;
    static constexpr int kUnlimitedUse = 13;
    static constexpr int kOneUsePerMinute = 14;
    static constexpr int kOneUsePerFiveMinutes = 18;

    /** The template of a stack of credits. */
    static constexpr char kCreditsResRef[] = "g_i_credits001";

    /** TSL: the upgrade slots of an item. */
    static constexpr int kUpgradeSlotCount = 6;
    /** TSL: the upgrade.2da row an empty upgrade slot is saved with. */
    static constexpr int kSavedEmptyUpgradeSlot = -1;
    /**
     * The upgrade.2da rows an item can have installed, 0 to 254: a property's
     * upgrade type is a byte whose 255 keys it to no upgrade.
     */
    static constexpr int kUpgradeRowCount = 255;
    /** The installed rows a record's Upgrades field holds, one bit each: rows 0 to 31. */
    static constexpr int kRecordedUpgradeBits = 32;

    /** Who a base item is limited to (baseitems.2da DroidOrHuman). */
    enum class DroidOrHuman {
        Anyone = 0,
        HumanOnly = 1,
        DroidOnly = 2
    };

    struct AmmunitionType {
        std::shared_ptr<graphics::Model> model;
        std::shared_ptr<graphics::Model> muzzleFlash;
        std::shared_ptr<audio::AudioClip> shotSound1;
        std::shared_ptr<audio::AudioClip> shotSound2;
        std::shared_ptr<audio::AudioClip> impactSound1;
        std::shared_ptr<audio::AudioClip> impactSound2;
        bool shieldHit {false};
    };

    /** An item property, as an item's PropertiesList records it. */
    struct PropertyEntry {
        /** The upgrade type of a property in force whatever the item's upgrades. */
        static constexpr uint8_t kNoUpgrade = 0xff;
        /** The saved uses per day of a property that records none. */
        static constexpr int kUnrecordedUsesPerDay = 255;
        /** TSL: the upgrade mark of a property no upgrade added. */
        static constexpr uint16_t kNoUpgradeMark = 0xff;

        uint8_t chanceAppear {0};
        uint8_t costTable {0};
        uint16_t costValue {0};
        uint8_t paramTable {0};
        uint8_t paramValue {0};
        uint16_t propertyName {0};
        uint16_t subtype {0};
        uint8_t upgradeType {kNoUpgrade};
        /**
         * TSL: the slot plus one of the upgrade that added the property. Any
         * other value names no slot, and is kept and saved as it was read.
         */
        uint16_t upgradeMark {kNoUpgradeMark};
        /** The uses left today of a property used so many times a day; none when not recorded. */
        std::optional<int> usesPerDay;
        bool usable {true};
        /**
         * The calendar day and time of day of the last use of a property
         * used once in so many minutes.
         */
        uint32_t usedDay {0};
        uint32_t usedTime {0};

        /** TSL: the upgrade mark of a property the upgrade in \p slot adds. */
        static uint16_t markOfSlot(int slot) { return static_cast<uint16_t>(slot + 1); }

        ItemProperty type() const { return static_cast<ItemProperty>(propertyName); }
        /** TSL: whether the upgrade in \p slot added the property. */
        bool addedBySlot(int slot) const { return upgradeMark == markOfSlot(slot); }
    };

    Item(
        uint32_t id,
        Game &game,
        ServicesView &services) :
        Object(
            id,
            ObjectType::Item,
            "",
            game,
            services) {
    }

    static bool classof(const Object *from) {
        return from->type() == ObjectType::Item;
    }

    /** Reads the item's template; false when there is none. */
    bool loadFromBlueprint(const std::string &resRef);
    /**
     * Makes a new item whose template is missing what such an item is: base
     * item 30 in model variation 1 with 50 charges, droppable, its tag, name
     * and cost left empty.
     */
    void loadBlank();
    /**
     * Reads a saved item record whole. Any other entry that names a template
     * (see entryTemplate) is that template with the entry's Dropable flag,
     * when it has one; an entry naming none is a whole item record.
     */
    void deserialize(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);
    /** The template an inventory or equipment entry names: its EquippedRes, else its InventoryRes. */
    static std::optional<std::string> entryTemplate(const resource::Gff &entry);
    /** Whether an entry names a template that does not exist; such an entry makes no item. */
    static bool namesMissingTemplate(const resource::Gff &entry, resource::IGffs &gffs);
    /**
     * Takes another item's tag, name and item state. The plot flag and the
     * stolen mark are not taken: a copy starts as not plot and not stolen,
     * dropable and pickpocketable.
     */
    void clone(const Item &from);
    /**
     * Takes the plot flag and the stolen, dropable and pickpocketable marks
     * of the stack this item was split off, which a copy doesn't take.
     */
    void keepMarksOf(const Item &stack);

    void update(float dt) override;

    void playShotSound(int variant, glm::vec3 position);
    void playImpactSound(int variant, glm::vec3 position);
    /**
     * Powers the item up or down at \p position. The power-up and power-down
     * sounds play only with the \p transition; the item's lit state and its
     * hum change either way. An item the player cannot hear (\p audible
     * false) plays no power sound and starts no hum.
     */
    void powerUp(glm::vec3 position, bool transition, bool audible);
    void powerDown(glm::vec3 position, bool transition, bool audible);
    /** Starts the hum of a powered item at \p position, unless it is humming already. */
    void startPoweredSound(glm::vec3 position);
    /**
     * Plays the sound of the item put down, picked up or taken off: the
     * inventorysnds.2da row its base item's inventory sound type names, heard
     * everywhere in the GUI's group.
     */
    void playDropSound() const;
    /** Stops the hum; the item stays powered. */
    void stopPoweredSound();
    void updatePoweredSoundPosition(glm::vec3 position);
    bool isPowered() const { return _isPowered; }
    /**
     * The item's model, named by its class and model variation. A base item
     * whose models differ by gender also names the given gender letter
     * between the two; without a letter, no gender is named. The name is
     * cut to the length of a resource name.
     */
    std::string modelResRef(std::optional<char> genderLetter = std::nullopt) const;

    bool isEquippable() const;
    bool isEquippable(int slot) const;
    /** Whether another item of a stack joins this one: a stackable item identical to it. */
    bool isStackCompatibleWith(const Item &other) const;
    /**
     * Whether \p other is the same item: base item, tag, name, plot flag,
     * charges, upgrades, stolen flag and model variations alike, its use
     * properties alike in order, and its other properties alike in order.
     */
    bool isIdenticalTo(const Item &other) const;
    bool mergeStackFrom(Item &other);
    bool isCredits() const { return _itemType == kCreditsItemType; }
    bool isDropable() const { return _dropable; }
    /** Whether the item may be picked from its holder's pocket. */
    bool isPickpocketable() const { return _pickpocketable; }
    /** Whether the base item is drawn as its wearer's body model. */
    bool isBodyModel() const { return _modelType == kBodyModelType; }
    /** Scripts can mark an item as never equippable. */
    bool isNonEquippable() const { return _nonEquippable; }
    void setNonEquippable(bool nonEquippable) { _nonEquippable = nonEquippable; }
    /** Feats all needed to equip the base item. */
    const std::vector<FeatType> &requiredFeats() const { return _requiredFeats; }
    /** Whether only humans or only droids may equip the base item. */
    DroidOrHuman droidOrHuman() const { return _droidOrHuman; }
    /** Bit per subrace that cannot equip the base item. */
    uint32_t deniedSubraces() const { return _deniedSubraces; }
    bool isIdentified() const { return _identified; }
    /** The party took the item in and has not yet looked at it in the inventory. */
    bool isNewlyAcquired() const { return _newlyAcquired; }
    void setNewlyAcquired(bool newlyAcquired) { _newlyAcquired = newlyAcquired; }
    bool isEquipped() const { return _equipped; }
    bool isLightsaber() const { return _baseItem >= 8 && _baseItem <= 10; }
    /** Whether the base item is a ranged weapon (baseitems.2da RangedWeapon). */
    bool isRanged() const { return _rangedWeapon; }
    /** A mine kit: a trap-kit base item carrying a usable Trap property. */
    bool isMineKit() const;

    /**
     * The body variation of the base item, 0 for the first (A): a single
     * upper-case letter names one, KotOR's from A to J and TSL's from A to Z,
     * and anything else, a blank cell included, names the first.
     */
    int baseBodyVariation() const { return _baseBodyVariation; }
    const std::string &itemClass() const { return _itemClass; }
    const std::string &localizedName() const { return _localizedName.str(); }
    float attackRange() const { return _attackRange; }
    /**
     * The body variation the item gives its wearer's body model, counted from
     * 1 for the first (A): its base item's for an item drawn as its wearer's
     * body model, and 0 for any other.
     */
    int bodyVariation() const { return _bodyVariation; }
    /** The base item's row of weapon sounds. */
    int weaponMaterialType() const { return _weaponMaterialType; }
    /** The base item's armour sound material, lower case. */
    const std::string &armorType() const { return _armorType; }
    /** The base item's damage flags: without any, Universal in KotOR and Bludgeoning in TSL. */
    int damageFlags() const { return _damageFlags; }
    int dieToRoll() const { return _dieToRoll; }
    int modelVariation() const { return _modelVariation; }
    /** Whether the base item plays its powered animation. */
    bool isPoweredItem() const { return _poweredItem; }
    int numDice() const { return _numDice; }
    /** The dice of the first Monster Damage property as {count, sides}; {0, 0} without one. */
    std::pair<int, int> monsterDamageDice() const;
    int stackSize() const { return _stackSize; }
    int charges() const { return _charges; }
    /** The charges a full item holds. */
    int maxCharges() const { return _maxCharges; }
    int maxStackSize() const { return _maxStackSize; }
    /** Whether the base item holds other items. */
    bool isContainer() const { return _container; }
    int textureVariation() const { return _textureVariation; }
    std::shared_ptr<AmmunitionType> ammunitionType() const { return _ammunitionType; }
    std::shared_ptr<graphics::Texture> icon() const { return _icon; }
    WeaponType weaponType() const { return _weaponType; }
    WeaponWield weaponWield() const { return _weaponWield; }
    CreatureSize weaponSize() const { return _weaponSize; }
    const std::string &description() const { return _description.str(); }
    const std::string &descIdentified() const { return _descIdentified.str(); }
    int baseItemType() const { return _baseItem; }
    /**
     * The item's value: none when it is plot, else its added
     * cost, a signed number, scaled by its base item and held within an int,
     * and 1 when that is 0 or 1. KotOR raises a negative cost to 0; TSL keeps
     * it negative.
     */
    int cost(float baseCostMultiplier) const;
    /**
     * TSL: what the item is worth in components at a workbench: a twentieth
     * of its cost, and 1 below a cost of 20.
     */
    int componentValue(float baseCostMultiplier) const;
    /**
     * TSL: what the item is worth in chemicals at a lab station: half its
     * cost, and 1 below a cost of 2.
     */
    int chemicalValue(float baseCostMultiplier) const;
    int itemType() const { return _itemType; }
    /** Where the base item is sorted among the store's and inventory's lists (baseitems.2da StorePanelSort). */
    int storePanelSort() const { return _storePanelSort; }
    int criticalThreat() const { return _criticalThreat; }
    int criticalHitMultiplier() const { return _criticalHitMultiplier; }
    FeatType weaponFocusFeat() const { return _weaponFocusFeat; }
    FeatType weaponSpecializationFeat() const { return _weaponSpecializationFeat; }
    int baseDefense() const { return _baseDefense; }
    /**
     * The armour class that limits its wearer's Dexterity: the base armour
     * class of an item drawn as the wearer's body model, else none.
     */
    int bodyArmorClass() const { return isBodyModel() ? _baseDefense : 0; }
    int maxDexterityBonus() const { return _maxDexterityBonus; }
    /** TSL: the sum of the cost values of the Max Dexterity Bonus properties in force; zero in KotOR. */
    int maxDexterityBonusAdjustment() const;
    ACBonus acBonusType() const { return _acBonusType; }
    std::optional<SpellType> activateSpell() const { return _activateSpell; }
    /**
     * The item's bit in a Force power's required and forbidden item masks
     * (spells.2da): one bit for each armour class from 4 to 9, then one for
     * all lightsabers; none for other items.
     */
    uint32_t forceItemMask() const;
    /** The index of a usable cast-spell property for \p spell, if any. */
    std::optional<size_t> spellProperty(SpellType spell) const;
    /**
     * The first property the item is used through: a cast spell, security
     * spike, trap or computer spike, in list order.
     */
    std::optional<size_t> firstUseProperty() const;
    /**
     * The place of property \p property among the properties the item is used
     * through: the number of them before it, in list order.
     */
    size_t usePropertyPlace(size_t property) const;
    /** The property the item is used through at place \p place among them, if it has one there. */
    std::optional<size_t> usePropertyAt(size_t place) const;
    /** A cast-spell property with a use left, whatever its upgrade. */
    bool hasSpellUse(size_t property) const;
    /** A cast-spell property with a use left that the item's upgrades leave in force. */
    bool canUseSpell(size_t property) const;
    /**
     * A property used once in so many minutes is ready again once that many
     * minutes of the time of day have passed since its last use.
     */
    void refreshSpellReadiness();
    /**
     * Debit a just-dispatched use, whatever the property's upgrade.
     *
     * @return true when the item is exhausted
     */
    bool consumeSpellUse(size_t property);
    const std::vector<PropertyEntry> &properties() const { return _properties; }
    /** The On Hit properties in force, each with its save, chance and duration. */
    std::vector<ItemOnHitProperty> itemOnHitProperties() const;

    /**
     * Whether the item's upgrades leave the property in force: one keyed to no
     * upgrade always is, and one keyed to an upgrade.2da row while that row is
     * installed.
     */
    bool isPropertyActive(const PropertyEntry &property) const;

    /** Codes 2 to 6: the property costs charges a use. */
    static constexpr bool isChargedUse(int costValue) {
        return costValue >= kFiveChargesPerUse && costValue <= kOneChargePerUse;
    }
    /** The charges one use of a charged property costs, 5 down to 1. */
    static constexpr int chargesPerUse(int costValue) { return kNoChargesPerUse - costValue; }
    /** Codes 8 to 12: the property allows so many uses a day. */
    static constexpr bool isDailyUse(int costValue) {
        return costValue >= kOneUsePerDay && costValue <= kFiveUsesPerDay;
    }
    /** Codes 14 to 18: the property allows a use every so many minutes. */
    static constexpr bool isTimedUse(int costValue) {
        return costValue >= kOneUsePerMinute && costValue <= kOneUsePerFiveMinutes;
    }
    /** Whether the item has a property of the type that its upgrades leave in force. */
    bool hasActiveProperty(ItemProperty type) const;
    /** Whether an item is used through a property of this type: a cast spell, security spike, trap or computer spike. */
    static bool isUseProperty(uint16_t propertyName);
    /**
     * The base armour class of armour worn as the body model, plus the raw
     * values of the armour class bonus properties; with installedUpgradesOnly,
     * a bonus tied to an upgrade that is not installed is left out.
     */
    int armorValue(bool installedUpgradesOnly) const;
    /** The installed upgrade.2da rows, one bit each. */
    const std::bitset<kUpgradeRowCount> &upgrades() const { return _upgrades; }
    /** KotOR: installs or removes an upgrade.2da row; a row past the rows an item can have does nothing. */
    void setUpgradeInstalled(int row, bool installed);
    /** Whether an upgrade.2da row is installed. */
    bool isUpgradeInstalled(int row) const { return row >= 0 && row < kUpgradeRowCount && _upgrades.test(static_cast<size_t>(row)); }
    /** Replaces the installed rows with another item's. */
    void setUpgrades(const std::bitset<kUpgradeRowCount> &upgrades) { _upgrades = upgrades; }
    /** TSL: the item's upgrade level. */
    int upgradeLevel() const { return _upgradeLevel; }
    /** TSL: the upgrade.2da row installed in each of the item's slots; none when a slot is empty. */
    const std::array<std::optional<int>, kUpgradeSlotCount> &upgradeSlots() const { return _upgradeSlots; }
    /** TSL: installs an upgrade.2da row in a slot, or empties the slot. */
    void setUpgradeSlot(int slot, std::optional<int> row) { _upgradeSlots[slot] = row; }
    /**
     * TSL: the upgrade's properties, apart from its use properties, are added
     * to the item, marked as the slot's.
     */
    void addUpgrade(const Item &upgrade, int slot);
    /** TSL: takes off the properties the upgrade in \p slot added, and no others. */
    void removeUpgrade(int slot);
    /** Whether the item is marked stolen. */
    bool stolen() const { return _stolen; }
    /** Marks the item stolen or not. */
    void setStolen(bool stolen) { _stolen = stolen; }
    /** TSL: whether any of the item's slots holds an upgrade. */
    bool hasInstalledUpgrade() const;

    /** Latches set by applying an item property, which removing it does not clear. */
    void enableSniperBonus() { _sniperBonus = true; }
    void enableRapidShotBonus() { _rapidShotBonus = true; }
    void enableDoorCutting() { _doorCutting = true; }
    void enableDoorSabering() { _doorSabering = true; }
    bool sniperBonus() const { return _sniperBonus; }
    bool rapidShotBonus() const { return _rapidShotBonus; }
    /** Whether the item's hits cut doors, as set when it was worn with a Door Cutting property. */
    bool cutsDoors() const { return _doorCutting; }
    /** Whether the item burns through locked doors, as set when it was worn with a Door Sabering property. */
    bool sabersDoors() const { return _doorSabering; }

    bool hasDisguise() const { return _disguiseAppearance >= 0; }
    int disguiseAppearance() const { return _disguiseAppearance; }

    void setDropable(bool dropable);
    /** Marks the item as one that may be picked from its holder's pocket, or not. */
    void setPickpocketable(bool pickpocketable) { _pickpocketable = pickpocketable; }
    void setStackSize(int size);
    void setIdentified(bool value);
    /** Whether a store holds the item without end: a stack that never runs out. */
    bool isInfinite() const { return _infinite; }
    void setInfinite(bool infinite) { _infinite = infinite; }
    void setEquipped(bool equipped);

    uint32_t owner() const { return _ownerId; }
    /** Whether a creature or a container holds the item. */
    bool isHeld() const { return _ownerId != script::kObjectInvalid; }
    /**
     * The object possessing the item: in the party's shared inventory, the
     * creature possessing it there; otherwise its holder. Invalid when nothing
     * holds it.
     */
    uint32_t possessorId() const;
    /** The object possessing the item; none when nothing holds it. */
    std::shared_ptr<Object> possessor() const;
    /**
     * Gives the item to a holder: a creature, a container, or an item holding
     * it. An item coming to the party's shared inventory, kept on the player
     * character, is possessed there by the creature in control. When the
     * creature at the top, the one possessing the item or the item that holds
     * it, is a player character and was not at the item's top before, the
     * item becomes dropable and pickpocketable. An item with no holder is
     * compared with the one possessing it when it last left a holder, so an
     * item moving within one creature's belongings is not marked again.
     */
    void setOwner(const Object &holder);
    /**
     * Gives the item back to the holder a save records it with, as setOwner
     * does, its dropable and pickpocketable marks staying as they were read.
     */
    void restoreOwner(const Object &holder);
    /**
     * Takes the item again for the creature now in control: one in the shared
     * inventory comes to be possessed by that creature, and one worn stays
     * with its wearer. The item becomes dropable and pickpocketable when the
     * creature at its top is a player character other than \p previousTop.
     *
     * @param previousTop the creature at the item's top before, none when the
     *                    item had none
     */
    void retakeForTop(const std::shared_ptr<Creature> &previousTop);
    /** Takes the item from its holder, which is remembered until the item is given to another. */
    void clearOwner() {
        _releasedFrom = _ownerId;
        _ownerId = script::kObjectInvalid;
    }
    /**
     * Announces that the item has passed to a possessor: a creature, a
     * placeable, or an item holding it. A creature coming to possess an item
     * in the shared inventory possesses it there from then on; the item
     * becomes dropable and pickpocketable when that creature is a player
     * character and was not at the item's top before. The module's
     * acquisition event names the creature at the top, the possessor or the
     * one possessing the item that is the possessor, when there is one. That
     * creature, when it leads the party and was not at the item's top before,
     * is told it gained the item. With the events, a placeable the item left
     * and a placeable it came to, unless destroyed once empty, have their
     * inventory disturbed (removed, added), each by the creature at the top on
     * the other side or else by the other placeable, and not without either;
     * a placeable taking back an item it held is disturbed both ways.
     *
     * @param previousPossessor the object possessing the item before, invalid when none
     * @param possessor the object possessing it now
     * @param events whether the module's acquisition event and the disturbed
     *               events are posted
     * @param feedback whether the gain is told
     */
    void announceAcquisition(uint32_t previousPossessor, const Object &possessor, bool events, bool feedback);
    /**
     * Announces to the module that the item's possessor has acquired it
     * again, as it does whenever an item passes to the object already
     * possessing it.
     */
    void announceKept();
    /**
     * Passes the item to a possessor: when it already possesses the item, the
     * item is announced kept, with the events; otherwise its acquisition is announced, from
     * the item's possessor before, after the loss of the creature at its top
     * before, when another is at its top now.
     *
     * @param events whether the module's lost, acquisition and disturbed
     *               events are posted
     * @param feedback whether the loss and the gain are told
     */
    void passTo(const Object &possessor, bool events, bool feedback);
    /**
     * Announces that the item, which has left its holder, is possessed by no
     * one: when a creature was at its top, the module's lost event, and that
     * creature, when it leads the party, is told it lost the item. The item
     * forgets the one possessing it, so whoever takes it next takes it anew.
     *
     * @param feedback whether the loss is told
     */
    void announceLoss(bool feedback);
    /**
     * Announces the loss of this copy, split off \p stack, by the one
     * possessing the stack, as announceLoss does for an item that leaves it.
     *
     * @param feedback whether the loss is told
     */
    void announceSplitLoss(const Item &stack, bool feedback);
    /** Whether the item is in the party's shared inventory. */
    bool inSharedInventory() const { return isHeld() && _sharedPossessorId != script::kObjectInvalid; }

private:
    friend class ModuleSnapshotBuilder;
    // Serializable
    int32_t _baseItem {0};
    resource::LocString _localizedName;
    resource::LocString _description;
    resource::LocString _descIdentified;
    uint8_t _charges {0};
    int _maxCharges {0};
    uint32_t _cost {0};
    uint32_t _addCost {0};
    bool _stolen {false};
    uint16_t _stackSize {1};
    /** The installed upgrade.2da rows, each row its own bit. */
    std::bitset<kUpgradeRowCount> _upgrades;
    /**
     * TSL: the item's upgrade level. An item at level 0 takes no upgrades; an
     * upgrade fits an item whose level is at least its own.
     */
    int _upgradeLevel {0};
    /** TSL: the upgrade.2da row installed in each of the item's slots; none when a slot is empty. */
    std::array<std::optional<int>, kUpgradeSlotCount> _upgradeSlots {};
    bool _identified {true};
    bool _infinite {false};
    uint8_t _modelVariation {0};
    uint8_t _bodyVariation {0};
    uint8_t _textureVariation {0};
    /** A new item is dropable and pickpocketable, a copy included. */
    bool _dropable {true};
    bool _pickpocketable {true};
    bool _nonEquippable {false};
    bool _newlyAcquired {false};
    // END Serializable

    uint32_t _ownerId {script::kObjectInvalid};
    /** The holder the item last left, while it has none; a holder it next comes to is compared with it. */
    uint32_t _releasedFrom {script::kObjectInvalid};
    /**
     * The creature possessing the item while the party's shared inventory
     * holds it, kept when the item leaves until it comes to another holder;
     * invalid otherwise.
     */
    uint32_t _sharedPossessorId {script::kObjectInvalid};

    std::vector<FeatType> _requiredFeats;
    DroidOrHuman _droidOrHuman {DroidOrHuman::Anyone};
    uint32_t _deniedSubraces {0};
    bool _container {false};

    // baseitems.2da defines the repository limit. The permissive
    // fallback preserves behavior for incomplete custom/test tables.
    uint16_t _maxStackSize {std::numeric_limits<uint16_t>::max()};

    /** The base item's body variation, read when the base item loads; 0 for the first. */
    int _baseBodyVariation {0};
    std::string _itemClass;

    std::shared_ptr<graphics::Texture> _icon;
    uint32_t _equipableSlots {0};
    float _attackRange {0.0f};
    int _numDice {0};
    int _dieToRoll {0};
    int _damageFlags {0};
    int _weaponMaterialType {0};
    std::string _armorType;
    WeaponType _weaponType {WeaponType::None};
    /** The base item is a ranged weapon. */
    bool _rangedWeapon {false};
    WeaponWield _weaponWield {WeaponWield::None};
    CreatureSize _weaponSize {CreatureSize::Invalid};

    bool _equipped {false};
    /** Item-property application sets these latches; removal does not clear them. */
    bool _sniperBonus {false};
    bool _rapidShotBonus {false};
    bool _doorCutting {false};
    bool _doorSabering {false};
    std::shared_ptr<AmmunitionType> _ammunitionType;
    bool _poweredItem {false};
    /** The base item's models differ by gender. */
    bool _genderSpecific {false};
    bool _isPowered {false};
    std::shared_ptr<audio::AudioClip> _powerUpSound;
    std::shared_ptr<audio::AudioClip> _powerDownSound;
    std::shared_ptr<audio::AudioClip> _poweredSound;
    std::shared_ptr<audio::AudioSource> _poweredAudioSource;

    int _criticalThreat {0};
    int _criticalHitMultiplier {0};
    FeatType _weaponFocusFeat {FeatType::Invalid};
    FeatType _weaponSpecializationFeat {FeatType::Invalid};
    int _baseDefense {0};
    /** The base item's baseitems.2da model type. */
    int _modelType {0};
    int _maxDexterityBonus {-1};
    ACBonus _acBonusType {ACBonus::Invalid};

    std::optional<SpellType> _activateSpell;
    int _itemType {0};
    int _storePanelSort {0};
    int _disguiseAppearance {-1};
    std::vector<PropertyEntry> _properties;
    std::shared_ptr<audio::AudioSource> _audioSource;

    /** Whether a property used once in so many minutes may be used again. */
    bool isUseIntervalOver(const PropertyEntry &property) const;
    /**
     * Marks the item dropable and pickpocketable when the creature at its top,
     * the one possessing it or \p holder, the item holding it, is a player
     * character other than \p previousTop.
     */
    void markForTop(const Object &holder, const std::shared_ptr<Creature> &previousTop);
    /**
     * Tells the module, with the events, that \p previousTop lost the item,
     * and the creature, with the feedback, when it leads the party.
     */
    void reportLoss(const std::shared_ptr<Creature> &previousTop, bool events, bool feedback);

    // Blueprint
    /**
     * @param completeRecord whether the record stands alone rather than being
     *        read over a template; a complete record without charges holds the
     *        default number
     */
    void deserializeAll(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext,
        bool completeRecord);
    void deserializeProperties(const resource::Gff &gff);
    void deserializeBase(const resource::Gff &gff);
    /** Reads the base item's row of baseitems.2da. */
    void loadBaseItem();
    /**
     * Loads the inventory icon: a base item with a model type takes the one
     * of its texture variation, any other the one of its model variation.
     */
    void loadIcon();
    void loadAmmunitionType();
    // END Blueprint
};

} // namespace game

} // namespace reone
