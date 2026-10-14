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

#include "reone/game/object/item.h"

#include <algorithm>
#include <cassert>

#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/game/combattables.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/twodautil.h"
#include "reone/graphics/di/services.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/provider/models.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/resources.h"
#include "reone/resource/strings.h"

using namespace reone::audio;
using namespace reone::graphics;
using namespace reone::resource;

namespace reone {

namespace game {

// The bit of a Force power's item masks that stands for every lightsaber.
static constexpr uint32_t kLightsaberForceItemMask = uint32_t {1} << 6;

// The On Hit save DC of a cost value outside iprp_onhitdc.
static constexpr int kDefaultOnHitDifficultyClass = 20;

// The baseitems.2da model type of an item drawn as its wearer's body model.
static constexpr int kBodyModelType = 1;

// The length of a combat round.
static constexpr float kRoundSeconds = 6.0f;

// The iprp_onhit parameter table that gives a chance and duration.
static constexpr int kOnHitDurationParameterTable = 1;

int Item::cost(float baseCostMultiplier) const {
    if (plotFlag()) {
        return 0;
    }
    const auto value = static_cast<int>(static_cast<float>(_addCost) * baseCostMultiplier);
    return value < 2 ? 1 : value;
}

void Item::loadFromBlueprint(const std::string &resRef) {
    std::shared_ptr<Gff> uti(_services.resource.gffs.get(resRef, ResType::Uti));
    if (uti) {
        deserialize(*uti, SerializedIdentityContext::templateResource(resRef));
        return;
    }
}

std::optional<std::string> Item::entryTemplate(const resource::Gff &entry) {
    std::string resRef;
    if (entry.readResRef(resRef, "EquippedRes") || entry.readResRef(resRef, "InventoryRes")) {
        return resRef;
    }
    return std::nullopt;
}

bool Item::namesMissingTemplate(const resource::Gff &entry, resource::IGffs &gffs) {
    const auto resRef = entryTemplate(entry);
    return resRef && !gffs.get(*resRef, ResType::Uti);
}

// A saved item is its own record. Any other entry that names a template is
// that template with the entry's Dropable flag, when it has one; an entry
// naming none is a whole item record.
void Item::deserialize(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    if (!identityContext.isSerializedState()) {
        if (auto resRef = entryTemplate(gff)) {
            if (auto uti = _services.resource.gffs.get(*resRef, ResType::Uti)) {
                deserializeAll(*uti, SerializedIdentityContext::templateResource(*resRef), true);
            }
            uint8_t dropable = 0;
            if (gff.readByte(dropable, "Dropable")) {
                _dropable = (dropable & 1) != 0;
            }
            return;
        }
    }
    deserializeAll(gff, identityContext, true);
}

void Item::deserializeAll(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    bool completeRecord) {
    Object::deserialize(gff, identityContext);

    gff.readLocString(_localizedName, "LocalizedName", _services.resource.strings);
    // A record with a description but no identified description shows its
    // description; a record with neither keeps what the item had.
    const bool describes = gff.readLocString(_description, "Description", _services.resource.strings);
    if (!gff.readLocString(_descIdentified, "DescIdentified", _services.resource.strings) && describes) {
        _descIdentified = _description;
    }

    // A complete record without charges holds fifty; a record with charges but
    // no maximum holds as many charges as it has.
    static constexpr int kDefaultCharges = 50;
    const bool charged = gff.readByte(_charges, "Charges");
    if (!charged && completeRecord) {
        _charges = kDefaultCharges;
    }
    uint8_t maxCharges = 0;
    if (gff.readByte(maxCharges, "MaxCharges")) {
        _maxCharges = maxCharges;
    } else if (charged || completeRecord) {
        _maxCharges = _charges;
    }
    gff.readDword(_cost, "Cost");
    gff.readDword(_addCost, "AddCost");
    gff.readBool(_stolen, "Stolen");
    gff.readWord(_stackSize, "StackSize");
    gff.readDword(_upgrades, "Upgrades");

    gff.readBool(_identified, "Identified");
    gff.readByte(_modelVariation, "ModelVariation");
    gff.readByte(_textureVariation, "TextureVar");
    gff.readByte(_bodyVariation, "BodyVariation");
    gff.readBool(_dropable, "Dropable");
    gff.readBool(_nonEquippable, "NonEquippable");

    deserializeProperties(gff);
    deserializeBase(gff);

    loadAmmunitionType();
    updateTransform();
}

void Item::deserializeProperties(const resource::Gff &gff) {
    if (gff.has("PropertiesList")) {
        _properties.clear();
        _activateSpell.reset();
        _disguiseAppearance = -1;
    }
    for (const auto &prop : gff.getList("PropertiesList")) {
        PropertyEntry entry;
        prop->readByte(entry.chanceAppear, "ChanceAppear");
        prop->readByte(entry.costTable, "CostTable");
        prop->readWord(entry.costValue, "CostValue");
        prop->readByte(entry.paramTable, "Param1");
        prop->readByte(entry.paramValue, "Param1Value");
        prop->readWord(entry.subtype, "Subtype");
        prop->readByte(entry.upgradeType, "UpgradeType");
        uint8_t usesPerDay = PropertyEntry::kUnrecordedUsesPerDay;
        prop->readByte(usesPerDay, "UsesPerDay");
        if (usesPerDay != PropertyEntry::kUnrecordedUsesPerDay) {
            entry.usesPerDay = usesPerDay;
        }
        prop->readBool(entry.usable, "Useable");
        prop->readDword(entry.usedDay, "UsedDay");
        prop->readDword(entry.usedTime, "UsedTime");

        if (prop->readWord(entry.propertyName, "PropertyName")) {
            switch (entry.type()) {
            case ItemProperty::ActivateItem: {
                _activateSpell = static_cast<SpellType>(entry.subtype);

                // A property without a record of its uses is usable, and one
                // used so many times a day has them all.
                if (!entry.usesPerDay) {
                    entry.usable = true;
                    if (isDailyUse(entry.costValue)) {
                        entry.usesPerDay = entry.costValue - kOneUsePerDay + 1;
                    }
                }
                break;
            }
            case ItemProperty::Disguise:
                _disguiseAppearance = entry.subtype;
                break;
            default:
                break;
            }
        }

        _properties.push_back(entry);
    }
}

void Item::deserializeBase(const resource::Gff &gff) {
    if (!gff.readInt(_baseItem, "BaseItem")) {
        return;
    }

    auto baseItems = getRequiredTwoDA(_services.resource.twoDas, "baseitems");
    _itemType = baseItems->getInt(_baseItem, "itemtype", 0);
    _attackRange = baseItems->getFloat(_baseItem, "maxattackrange", 0.0f);
    _baseDefense = baseItems->getInt(_baseItem, "baseac", 0);
    _bodyModel = baseItems->getInt(_baseItem, "modeltype", 0) == kBodyModelType;
    _criticalHitMultiplier = baseItems->getInt(_baseItem, "crithitmult", 0);
    _criticalThreat = baseItems->getInt(_baseItem, "critthreat", 0);
    _damageFlags = baseItems->getInt(_baseItem, "damageflags", 0);
    _weaponMaterialType = baseItems->getInt(_baseItem, "weaponmattype", 0);
    _armorType = boost::to_lower_copy(baseItems->getString(_baseItem, "armortype"));
    _dieToRoll = baseItems->getInt(_baseItem, "dietoroll", 0);
    _maxDexterityBonus = baseItems->getInt(_baseItem, "dexbonus", -1);
    _equipableSlots = static_cast<uint32_t>(
        baseItems->getInt(_baseItem, "equipableslots", 0));
    _itemClass = boost::to_lower_copy(
        baseItems->getString(_baseItem, "itemclass"));
    _numDice = baseItems->getInt(_baseItem, "numdice", 0);
    _acBonusType = static_cast<ACBonus>(baseItems->getInt(
        _baseItem,
        "ac_enchant",
        static_cast<int>(ACBonus::Invalid)));
    _weaponType = static_cast<WeaponType>(
        baseItems->getInt(_baseItem, "weapontype", 0));
    _rangedWeapon = baseItems->getInt(_baseItem, "rangedweapon", 0) != 0;
    _weaponWield = static_cast<WeaponWield>(
        baseItems->getInt(_baseItem, "weaponwield", 0));
    _weaponSize = static_cast<CreatureSize>(
        baseItems->getInt(_baseItem, "weaponsize", 0));
    _weaponFocusFeat = static_cast<FeatType>(
        baseItems->getInt(_baseItem, "focfeat", 0));
    _weaponSpecializationFeat = static_cast<FeatType>(
        baseItems->getInt(_baseItem, "specfeat", 0));
    // The non-blank required feats, packed in column order.
    static constexpr int kRequiredFeatColumns = 5;
    _requiredFeats.clear();
    for (int i = 0; i < kRequiredFeatColumns; ++i) {
        auto feat = baseItems->getIntOpt(_baseItem, "reqfeat" + std::to_string(i));
        if (feat) {
            _requiredFeats.push_back(static_cast<FeatType>(*feat));
        }
    }
    _droidOrHuman = static_cast<DroidOrHuman>(baseItems->getInt(_baseItem, "droidorhuman", 0));
    _deniedSubraces = static_cast<uint32_t>(baseItems->getInt(_baseItem, "denysubrace", 0));
    _container = baseItems->getInt(_baseItem, "container", 0) != 0;
    _maxStackSize = static_cast<uint16_t>(std::clamp(
        baseItems->getInt(
            _baseItem,
            "stacking",
            std::numeric_limits<uint16_t>::max()),
        1,
        static_cast<int>(std::numeric_limits<uint16_t>::max())));

    _poweredItem = baseItems->getInt(_baseItem, "powereditem") != 0;
    if (_poweredItem) {
        auto loadSound = [this, &baseItems](const char *column) {
            auto resRef = boost::to_lower_copy(baseItems->getString(_baseItem, column));
            return resRef.empty() ? nullptr : _services.resource.audioClips.get(resRef);
        };
        _powerUpSound = loadSound("powerupsnd");
        _powerDownSound = loadSound("powerdownsnd");
        _poweredSound = loadSound("poweredsnd");
    }

    std::string iconResRef;
    if (isEquippable(InventorySlots::body)) {
        _baseBodyVariation = boost::to_lower_copy(baseItems->getString(_baseItem, "bodyvar"));
        iconResRef = str(boost::format("i%s_%03d") % _itemClass % (int)_textureVariation);
    } else if (isEquippable(InventorySlots::rightWeapon)) {
        iconResRef = str(boost::format("i%s_%03d") % _itemClass % (int)_modelVariation);
    } else {
        iconResRef = str(boost::format("i%s_%03d") % _itemClass % (int)_modelVariation);
    }
    _icon = _services.resource.textures.get(iconResRef, TextureUsage::GUI);
    if (!_icon && isEquippable(InventorySlots::body)) {
        // Some body items (e.g. disguises) key the inventory icon on ModelVariation
        // rather than TextureVar; fall back to it when the primary icon is missing.
        iconResRef = str(boost::format("i%s_%03d") % _itemClass % (int)_modelVariation);
        _icon = _services.resource.textures.get(iconResRef, TextureUsage::GUI);
    }
}

void Item::loadAmmunitionType() {
    auto baseItems = getRequiredTwoDA(_services.resource.twoDas, "baseitems");

    int ammunitionIdx = baseItems->getInt(_baseItem, "ammunitiontype", 0);
    if (ammunitionIdx < 1) {
        return;
    }

    auto twoDa = getRequiredTwoDA(_services.resource.twoDas, "ammunitiontypes");
    _ammunitionType = std::make_shared<Item::AmmunitionType>();
    _ammunitionType->shieldHit = twoDa->getInt(ammunitionIdx, "shieldhit", 0) != 0;
    _ammunitionType->model = _services.resource.models.get(boost::to_lower_copy(twoDa->getString(ammunitionIdx, "model")));
    _ammunitionType->muzzleFlash = _services.resource.models.get(boost::to_lower_copy(twoDa->getString(ammunitionIdx, "muzzleflash")));
    _ammunitionType->shotSound1 = _services.resource.audioClips.get(boost::to_lower_copy(twoDa->getString(ammunitionIdx, "shotsound0")));
    _ammunitionType->shotSound2 = _services.resource.audioClips.get(boost::to_lower_copy(twoDa->getString(ammunitionIdx, "shotsound1")));
    _ammunitionType->impactSound1 = _services.resource.audioClips.get(boost::to_lower_copy(twoDa->getString(ammunitionIdx, "impactsound0")));
    _ammunitionType->impactSound2 = _services.resource.audioClips.get(boost::to_lower_copy(twoDa->getString(ammunitionIdx, "impactsound1")));
}

int Item::maxDexterityBonusAdjustment() const {
    if (!_game.isTSL()) {
        return 0;
    }

    int result = 0;
    for (const auto &property : _properties) {
        if (property.type() == ItemProperty::MaxDexterityBonus && isPropertyActive(property)) {
            result += property.costValue;
        }
    }
    return result;
}

void Item::clone(const Item &from) {
    // Object
    _tag = from._tag;
    _name = from._name;
    _plot = from._plot;
    // END Object

    // Serializable
    _baseItem = from._baseItem;
    _localizedName = from._localizedName;
    _description = from._description;
    _descIdentified = from._descIdentified;
    _charges = from._charges;
    _maxCharges = from._maxCharges;
    _cost = from._cost;
    _addCost = from._addCost;
    _stolen = from._stolen;
    _stackSize = from._stackSize;
    _upgrades = from._upgrades;
    _identified = from._identified;
    _modelVariation = from._modelVariation;
    _bodyVariation = from._bodyVariation;
    _textureVariation = from._textureVariation;
    _dropable = from._dropable;
    _nonEquippable = from._nonEquippable;
    // END Serializable

    _requiredFeats = from._requiredFeats;
    _droidOrHuman = from._droidOrHuman;
    _deniedSubraces = from._deniedSubraces;

    _baseBodyVariation = from._baseBodyVariation;
    _itemClass = from._itemClass;
    _maxStackSize = from._maxStackSize;

    _icon = from._icon;
    _equipableSlots = from._equipableSlots;
    _attackRange = from._attackRange;
    _numDice = from._numDice;
    _dieToRoll = from._dieToRoll;
    _damageFlags = from._damageFlags;
    _weaponMaterialType = from._weaponMaterialType;
    _armorType = from._armorType;
    _weaponType = from._weaponType;
    _rangedWeapon = from._rangedWeapon;
    _weaponWield = from._weaponWield;
    _weaponSize = from._weaponSize;

    _equipped = from._equipped;
    _ammunitionType = from._ammunitionType;
    _poweredItem = from._poweredItem;
    _isPowered = from._isPowered;
    _powerUpSound = from._powerUpSound;
    _powerDownSound = from._powerDownSound;
    _poweredSound = from._poweredSound;
    _poweredAudioSource = from._poweredAudioSource;

    _criticalThreat = from._criticalThreat;
    _criticalHitMultiplier = from._criticalHitMultiplier;
    _weaponFocusFeat = from._weaponFocusFeat;
    _weaponSpecializationFeat = from._weaponSpecializationFeat;
    _baseDefense = from._baseDefense;
    _bodyModel = from._bodyModel;
    _maxDexterityBonus = from._maxDexterityBonus;
    _acBonusType = from._acBonusType;

    _activateSpell = from._activateSpell;
    _itemType = from._itemType;
    _disguiseAppearance = from._disguiseAppearance;
    _properties = from._properties;

    _audioSource = from._audioSource;
}

uint32_t Item::forceItemMask() const {
    if (_itemType >= kArmorClass4ItemType && _itemType <= kArmorClass9ItemType) {
        return uint32_t {1} << (_itemType - kArmorClass4ItemType);
    }
    return _itemType >= kDoubleLightsaberItemType && _itemType <= kLightsaberItemType ? kLightsaberForceItemMask : 0;
}

std::optional<size_t> Item::spellProperty(SpellType spell) const {
    for (size_t i = 0; i < _properties.size(); ++i) {
        const auto &property = _properties[i];
        if (property.type() == ItemProperty::ActivateItem &&
            static_cast<SpellType>(property.subtype) == spell && canUseSpell(i))
            return i;
    }
    return std::nullopt;
}

bool Item::isUseProperty(uint16_t propertyName) {
    switch (static_cast<ItemProperty>(propertyName)) {
    case ItemProperty::ActivateItem:
    case ItemProperty::SecuritySpike:
    case ItemProperty::Trap:
    case ItemProperty::ComputerSpike:
        return true;
    default:
        return false;
    }
}

std::optional<size_t> Item::firstUseProperty() const {
    for (size_t i = 0; i < _properties.size(); ++i) {
        if (isUseProperty(_properties[i].propertyName)) {
            return i;
        }
    }
    return std::nullopt;
}

bool Item::canUseSpell(size_t index) const {
    return hasSpellUse(index) && isPropertyActive(_properties[index]);
}

bool Item::hasSpellUse(size_t index) const {
    if (index >= _properties.size() || _stackSize == 0) {
        return false;
    }
    const auto &property = _properties[index];
    if (property.type() != ItemProperty::ActivateItem) {
        return false;
    }
    const int mode = property.costValue;
    if (isTimedUse(mode)) {
        return property.usable || isUseIntervalOver(property);
    }
    if (!property.usable) {
        return false;
    }
    if (mode == kSingleUse) {
        return _stackSize > 0;
    }
    if (isChargedUse(mode)) {
        return _charges >= chargesPerUse(mode);
    }
    if (isDailyUse(mode)) {
        return property.usesPerDay > 0;
    }
    return mode == kNoChargesPerUse || mode == kUnlimitedUse;
}

bool Item::consumeSpellUse(size_t index) {
    if (!hasSpellUse(index)) {
        return false;
    }
    auto &property = _properties[index];
    const int mode = property.costValue;
    if (mode == kSingleUse) {
        if (_stackSize > 0) {
            --_stackSize;
        }
        return _stackSize == 0;
    }
    if (isChargedUse(mode)) {
        auto isChargeUse = [](const PropertyEntry &entry) {
            return entry.type() == ItemProperty::ActivateItem && isChargedUse(entry.costValue);
        };
        _charges -= chargesPerUse(mode);
        // Every charged use the remaining charges no longer cover stops being usable.
        for (auto &other : _properties) {
            if (isChargeUse(other) && _charges < chargesPerUse(other.costValue)) {
                other.usable = false;
            }
        }
        // The item is spent unless the last of its use properties is still
        // usable; then stims, medical kits and squad kits are spent, and
        // anything else once no charged use is usable, whatever its upgrade.
        const auto lastUse = std::find_if(_properties.rbegin(), _properties.rend(), [](const PropertyEntry &entry) {
            return isUseProperty(entry.propertyName);
        });
        bool exhausted = false;
        if (!lastUse->usable) {
            exhausted = _itemType == kStimItemType || _itemType == kMedicalItemType || _itemType == kSquadRecoveryItemType ||
                        std::none_of(_properties.begin(), _properties.end(), [&](const PropertyEntry &entry) {
                            return isChargeUse(entry) && entry.usable;
                        });
        }
        if (exhausted) {
            _stackSize = 0;
        }
        return exhausted;
    }
    if (isDailyUse(mode)) {
        // hasSpellUse admitted the use, so a use is left.
        property.usesPerDay = *property.usesPerDay - 1;
        property.usable = *property.usesPerDay != 0;
    } else if (isTimedUse(mode)) {
        property.usable = false;
        property.usedDay = _game.worldTimeDay();
        property.usedTime = _game.worldTimeOfDay();
    }
    return false;
}

// Only the time of day part of the world time since the last use counts, and
// a use still ahead has no interval.
bool Item::isUseIntervalOver(const PropertyEntry &property) const {
    uint32_t days = 0;
    uint32_t elapsed = 0;
    _game.subtractWorldTimes(_game.worldTimeDay(), _game.worldTimeOfDay(), property.usedDay, property.usedTime,
                             days, elapsed);
    static constexpr uint32_t kMillisecondsPerMinute = 60000;
    const int minutes = property.costValue - kOneUsePerMinute + 1;
    return elapsed >= static_cast<uint32_t>(minutes) * kMillisecondsPerMinute;
}

void Item::refreshSpellReadiness() {
    for (auto &property : _properties) {
        if (property.type() == ItemProperty::ActivateItem && isTimedUse(property.costValue) && !property.usable &&
            isUseIntervalOver(property)) {
            property.usable = true;
        }
    }
}

void Item::update(float dt) {
    refreshSpellReadiness();
}

void Item::playShotSound(int variant, glm::vec3 position) {
    if (!_ammunitionType) {
        return;
    }
    auto clip = variant == 1 ? _ammunitionType->shotSound2 : _ammunitionType->shotSound1;
    if (clip) {
        _audioSource = _services.audio.mixer.play(
            std::move(clip),
            AudioType::Sound,
            1.0f,
            false,
            std::move(position));
    }
}

void Item::playImpactSound(int variant, glm::vec3 position) {
    if (!_ammunitionType) {
        return;
    }
    auto clip = variant == 1 ? _ammunitionType->impactSound2 : _ammunitionType->impactSound1;
    if (clip) {
        _services.audio.mixer.play(
            std::move(clip),
            AudioType::Sound,
            1.0f,
            false,
            std::move(position));
    }
}

void Item::powerUp(glm::vec3 position) {
    if (!_poweredItem || _isPowered) {
        return;
    }
    _isPowered = true;
    if (_powerUpSound) {
        _services.audio.mixer.play(_powerUpSound, AudioType::Sound, 1.0f, false, position);
    }
    if (_poweredAudioSource) {
        _poweredAudioSource->stop();
    }
    if (_poweredSound) {
        _poweredAudioSource = _services.audio.mixer.play(_poweredSound, AudioType::Sound, 1.0f, true, position);
    }
}

void Item::powerDown(glm::vec3 position) {
    if (!_poweredItem || !_isPowered) {
        return;
    }
    _isPowered = false;
    if (_poweredAudioSource) {
        _poweredAudioSource->stop();
        _poweredAudioSource.reset();
    }
    if (_powerDownSound) {
        _services.audio.mixer.play(_powerDownSound, AudioType::Sound, 1.0f, false, position);
    }
}

void Item::updatePoweredSoundPosition(glm::vec3 position) {
    if (_poweredAudioSource) {
        _poweredAudioSource->setPosition(position);
    }
}

bool Item::isEquippable() const {
    return _equipableSlots != 0;
}

bool Item::isEquippable(int slot) const {
    return (_equipableSlots >> slot) & 1;
}

bool Item::isStackCompatibleWith(const Item &other) const {
    auto sameProperty = [](const PropertyEntry &lhs, const PropertyEntry &rhs) {
        return lhs.costTable == rhs.costTable &&
               lhs.costValue == rhs.costValue &&
               lhs.paramTable == rhs.paramTable &&
               lhs.paramValue == rhs.paramValue &&
               lhs.propertyName == rhs.propertyName &&
               lhs.subtype == rhs.subtype &&
               lhs.upgradeType == rhs.upgradeType &&
               lhs.usesPerDay == rhs.usesPerDay;
    };
    return _maxStackSize > 1 &&
           _baseItem == other._baseItem &&
           _plot == other._plot &&
           _charges == other._charges &&
           _upgrades == other._upgrades &&
           _stolen == other._stolen &&
           _modelVariation == other._modelVariation &&
           _bodyVariation == other._bodyVariation &&
           _textureVariation == other._textureVariation &&
           _tag == other._tag &&
           _localizedName.id() == other._localizedName.id() &&
           _localizedName.str() == other._localizedName.str() &&
           _properties.size() == other._properties.size() &&
           std::equal(
               _properties.begin(),
               _properties.end(),
               other._properties.begin(),
               sameProperty);
}

bool Item::mergeStackFrom(Item &other) {
    if (&other == this || !isStackCompatibleWith(other) ||
        _stackSize >= _maxStackSize || other._stackSize == 0) {
        return false;
    }
    const int moved = std::min<int>(
        _maxStackSize - _stackSize, other._stackSize);
    _stackSize += moved;
    other._stackSize -= moved;
    return other._stackSize == 0;
}

void Item::setDropable(bool dropable) {
    _dropable = dropable;
}

void Item::setStackSize(int stackSize) {
    _stackSize = stackSize;
}

void Item::setIdentified(bool value) {
    _identified = value;
}

void Item::setEquipped(bool equipped) {
    _equipped = equipped;
}

bool Item::isMineKit() const {
    if (_itemType != kTrapKitItemType) {
        return false;
    }
    return std::any_of(_properties.begin(), _properties.end(), [](const PropertyEntry &property) {
        return property.type() == ItemProperty::Trap && property.usable;
    });
}

std::pair<int, int> Item::monsterDamageDice() const {
    for (const auto &property : _properties) {
        if (!isPropertyActive(property) || property.type() != ItemProperty::MonsterDamage) {
            continue;
        }
        if (property.costValue == 0) {
            return {0, 0};
        }
        const auto cost = _services.game.combatTables.monsterDamageCost(property.costValue);
        return {cost.numDice, cost.die};
    }
    return {0, 0};
}

bool Item::isPropertyActive(const PropertyEntry &property) const {
    return isPropertyActive(_upgrades, property.upgradeType);
}

bool Item::hasActiveProperty(ItemProperty type) const {
    for (const PropertyEntry &property : _properties) {
        if (property.type() == type && isPropertyActive(property)) {
            return true;
        }
    }
    return false;
}

static SavingThrow getItemOnHitSavingThrow(ItemOnHitSubtype subtype) {
    switch (subtype) {
    case ItemOnHitSubtype::Sleep:
    case ItemOnHitSubtype::Stun:
    case ItemOnHitSubtype::Confusion:
    case ItemOnHitSubtype::Fear:
    case ItemOnHitSubtype::Slow:
        return SavingThrow::Will;
    case ItemOnHitSubtype::Paralyze:
    case ItemOnHitSubtype::SlayRG:
    case ItemOnHitSubtype::SlayAG:
    case ItemOnHitSubtype::Knockdown:
        return SavingThrow::Fortitude;
    case ItemOnHitSubtype::AbilityDrain:
        return SavingThrow::Reflex;
    case ItemOnHitSubtype::ItemPoison:
    case ItemOnHitSubtype::InstantDeath:
        return SavingThrow::None;
    }
    assert(false && "unsupported item on-hit subtype");
    return SavingThrow::None;
}

static SavingThrowType getItemOnHitSavingThrowType(ItemOnHitSubtype subtype) {
    switch (subtype) {
    case ItemOnHitSubtype::Sleep:
    case ItemOnHitSubtype::Stun:
    case ItemOnHitSubtype::Confusion:
        return SavingThrowType::MindAffecting;
    case ItemOnHitSubtype::Fear:
        return SavingThrowType::Fear;
    case ItemOnHitSubtype::Paralyze:
        return SavingThrowType::Paralysis;
    case ItemOnHitSubtype::SlayRG:
    case ItemOnHitSubtype::SlayAG:
        return SavingThrowType::Death;
    case ItemOnHitSubtype::Slow:
    case ItemOnHitSubtype::AbilityDrain:
    case ItemOnHitSubtype::ItemPoison:
    case ItemOnHitSubtype::InstantDeath:
    case ItemOnHitSubtype::Knockdown:
        return SavingThrowType::All;
    }
    assert(false && "unsupported item on-hit subtype");
    return SavingThrowType::All;
}

std::vector<ItemOnHitProperty> Item::itemOnHitProperties() const {
    static constexpr char kOnHitDifficultyClassTable[] = "iprp_onhitdc";

    const auto &tables = _services.game.combatTables;
    std::vector<ItemOnHitProperty> result;
    ItemOnHitSelectionState selection;

    for (const PropertyEntry &property : _properties) {
        // KotOR has no Knockdown subtype.
        const auto lastSubtype = _game.isTSL() ? ItemOnHitSubtype::Knockdown : ItemOnHitSubtype::InstantDeath;
        if (!isPropertyActive(property) || property.type() != ItemProperty::OnHitProperties ||
            property.subtype > static_cast<int>(lastSubtype)) {
            continue;
        }

        const int parameterTable = tables.onHitParameterTable(property.subtype);

        ItemOnHitProperty value;
        value.subtype = static_cast<ItemOnHitSubtype>(property.subtype);
        value.savingThrow = getItemOnHitSavingThrow(value.subtype);
        value.savingThrowType = getItemOnHitSavingThrowType(value.subtype);
        value.parameter = property.paramValue;

        selection.select(value.subtype);
        value.appliesStateForDuration = appliesItemOnHitStateForDuration(value.subtype);
        if (parameterTable == kOnHitDurationParameterTable) {
            const auto duration = tables.onHitDuration(property.paramValue);
            selection.chance = duration.chance;
            selection.rounds = duration.rounds;
        }
        value.chance = selection.chance;
        value.duration = selection.rounds * kRoundSeconds;

        value.difficultyClass = tables.costTable(kOnHitDifficultyClassTable)
                                    .row(property.costValue)
                                    .value.value_or(kDefaultOnHitDifficultyClass);
        result.push_back(value);
    }
    return result;
}

bool Item::isPropertyActive(uint32_t upgrades, int upgradeType) {
    if (upgradeType == PropertyEntry::kNoUpgrade) {
        return true;
    }
    return (upgrades & (uint32_t {1} << upgradeType)) != 0;
}

} // namespace game

} // namespace reone
