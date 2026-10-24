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

#include <limits>

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
    static constexpr int kStimItemType = 25;
    static constexpr int kDroidRepairItemType = 26;
    static constexpr int kTrapKitItemType = 28;
    /** Body armour of armour classes 4 to 9, the first of six item types. */
    static constexpr int kArmorClass4ItemType = 31;
    static constexpr int kArmorClass9ItemType = 36;
    /** The last of the body armour, robe and clothing item types, which start at kArmorClass4ItemType. */
    static constexpr int kLastBodyItemType = 38;
    /** Double-bladed, short and ordinary lightsabers, the first of three item types. */
    static constexpr int kDoubleLightsaberItemType = 39;
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

        uint8_t chanceAppear {0};
        uint8_t costTable {0};
        uint16_t costValue {0};
        uint8_t paramTable {0};
        uint8_t paramValue {0};
        uint16_t propertyName {0};
        uint16_t subtype {0};
        uint8_t upgradeType {kNoUpgrade};
        /** The uses left today of a property used so many times a day; none when not recorded. */
        std::optional<int> usesPerDay;
        bool usable {true};
        /**
         * The calendar day and time of day of the last use of a property
         * used once in so many minutes.
         */
        uint32_t usedDay {0};
        uint32_t usedTime {0};

        ItemProperty type() const { return static_cast<ItemProperty>(propertyName); }
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

    void loadFromBlueprint(const std::string &resRef);
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
    void clone(const Item &from);

    void update(float dt) override;

    void playShotSound(int variant, glm::vec3 position);
    void playImpactSound(int variant, glm::vec3 position);
    /**
     * Powers the item up or down at \p position. The power-up and power-down
     * sounds play only with the \p transition; the item's lit state and its
     * hum change either way.
     */
    void powerUp(glm::vec3 position, bool transition);
    void powerDown(glm::vec3 position, bool transition);
    void updatePoweredSoundPosition(glm::vec3 position);
    bool isPowered() const { return _isPowered; }

    bool isEquippable() const;
    bool isEquippable(int slot) const;
    bool isStackCompatibleWith(const Item &other) const;
    bool mergeStackFrom(Item &other);
    bool isCredits() const { return _itemType == kCreditsItemType; }
    bool isDropable() const { return _dropable; }
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
    bool isEquipped() const { return _equipped; }
    bool isLightsaber() const { return _baseItem >= 8 && _baseItem <= 10; }
    /** Whether the base item is a ranged weapon (baseitems.2da RangedWeapon). */
    bool isRanged() const { return _rangedWeapon; }
    /** A mine kit: a trap-kit base item carrying a usable Trap property. */
    bool isMineKit() const;

    const std::string &baseBodyVariation() const { return _baseBodyVariation; }
    const std::string &itemClass() const { return _itemClass; }
    const std::string &localizedName() const { return _localizedName.str(); }
    float attackRange() const { return _attackRange; }
    int bodyVariation() const { return _bodyVariation; }
    /** The base item's row of weapon sounds. */
    int weaponMaterialType() const { return _weaponMaterialType; }
    /** The base item's armour sound material, lower case. */
    const std::string &armorType() const { return _armorType; }
    int damageFlags() const {
        // A zero or missing base-item damage flag selects bludgeoning.
        return _damageFlags != 0
                   ? _damageFlags
                   : static_cast<int>(DamageType::Bludgeoning);
    }
    int dieToRoll() const { return _dieToRoll; }
    int modelVariation() const { return _modelVariation; }
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
    /** The item's value: none for a plot item, else its added cost scaled by its base item, at least 1. */
    int cost(float baseCostMultiplier) const;
    int itemType() const { return _itemType; }
    int criticalThreat() const { return _criticalThreat; }
    int criticalHitMultiplier() const { return _criticalHitMultiplier; }
    FeatType weaponFocusFeat() const { return _weaponFocusFeat; }
    FeatType weaponSpecializationFeat() const { return _weaponSpecializationFeat; }
    int baseDefense() const { return _baseDefense; }
    /**
     * The armour class that limits its wearer's Dexterity: the base armour
     * class of an item drawn as the wearer's body model, else none.
     */
    int bodyArmorClass() const { return _bodyModel ? _baseDefense : 0; }
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

    /** Whether the item's upgrades leave the property in force. */
    bool isPropertyActive(const PropertyEntry &property) const;
    /** Whether \p upgrades leave a property of \p upgradeType in force. */
    static bool isPropertyActive(uint32_t upgrades, int upgradeType);

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
    uint32_t upgrades() const { return _upgrades; }

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
    void setStackSize(int size);
    void setIdentified(bool value);
    void setEquipped(bool equipped);

    uint32_t owner() const { return _ownerId; }
    /** Whether a creature or a container holds the item. */
    bool isHeld() const { return _ownerId != script::kObjectInvalid; }
    void setOwner(uint32_t id) { _ownerId = id; }
    void clearOwner() { _ownerId = script::kObjectInvalid; }

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
    uint32_t _upgrades {0};
    bool _identified {true};
    uint8_t _modelVariation {0};
    uint8_t _bodyVariation {0};
    uint8_t _textureVariation {0};
    bool _dropable {false};
    bool _nonEquippable {false};
    // END Serializable

    uint32_t _ownerId {script::kObjectInvalid};

    std::vector<FeatType> _requiredFeats;
    DroidOrHuman _droidOrHuman {DroidOrHuman::Anyone};
    uint32_t _deniedSubraces {0};
    bool _container {false};

    // baseitems.2da defines the repository limit. The permissive
    // fallback preserves behavior for incomplete custom/test tables.
    uint16_t _maxStackSize {std::numeric_limits<uint16_t>::max()};

    std::string _baseBodyVariation;
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
    /** The base item is drawn as the wearer's body model. */
    bool _bodyModel {false};
    int _maxDexterityBonus {-1};
    ACBonus _acBonusType {ACBonus::Invalid};

    std::optional<SpellType> _activateSpell;
    int _itemType {0};
    int _disguiseAppearance {-1};
    std::vector<PropertyEntry> _properties;
    std::shared_ptr<audio::AudioSource> _audioSource;

    /** Whether a property used once in so many minutes may be used again. */
    bool isUseIntervalOver(const PropertyEntry &property) const;

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
    void loadAmmunitionType();
    // END Blueprint
};

} // namespace game

} // namespace reone
