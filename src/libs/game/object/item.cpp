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

#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
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

void Item::loadFromBlueprint(const std::string &resRef) {
    std::shared_ptr<Gff> uti(_services.resource.gffs.get(resRef, ResType::Uti));
    if (uti) {
        deserialize(*uti, SerializedIdentityContext::templateResource(resRef));
        return;
    }
}

void Item::deserialize(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    std::string ref;
    if (!identityContext.isSerializedState() &&
        gff.readResRef(ref, "EquippedRes")) {
        if (auto uti = _services.resource.gffs.get(ref, ResType::Uti)) {
            deserializeAll(*uti, SerializedIdentityContext::templateResource(ref));
        }
    }

    if (!identityContext.isSerializedState() &&
        gff.readResRef(ref, "InventoryRes")) {
        if (auto uti = _services.resource.gffs.get(ref, ResType::Uti)) {
            deserializeAll(*uti, SerializedIdentityContext::templateResource(ref));
        }
    }

    if (!identityContext.isSerializedState() &&
        gff.readResRef(ref, "TemplateResRef")) {
        if (auto uti = _services.resource.gffs.get(ref, ResType::Uti)) {
            deserializeAll(*uti, SerializedIdentityContext::templateResource(ref));
        }
    }

    deserializeAll(gff, identityContext);
}

void Item::deserializeAll(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    Object::deserialize(gff, identityContext);

    gff.readLocString(_localizedName, "LocalizedName", _services.resource.strings);
    gff.readLocString(_description, "Description", _services.resource.strings);
    gff.readLocString(_descIdentified, "DescIdentified", _services.resource.strings);

    gff.readByte(_charges, "Charges");
    gff.readDword(_cost, "Cost");
    gff.readDword(_addCost, "AddCost");
    gff.readBool(_stolen, "Stolen");
    gff.readWord(_stackSize, "StackSize");

    gff.readBool(_identified, "Identified");
    gff.readByte(_modelVariation, "ModelVariation");
    gff.readByte(_textureVariation, "TextureVar");
    gff.readByte(_bodyVariation, "BodyVariation");
    gff.readBool(_dropable, "Dropable");

    deserializeProperties(gff);
    deserializeBase(gff);

    loadAmmunitionType();
    updateTransform();
}

void Item::deserializeProperties(const resource::Gff &gff) {
    for (const auto &prop : gff.getList("PropertiesList")) {
        PropertyEntry entry;
        prop->readByte(entry.chanceAppear, "ChanceAppear");
        prop->readByte(entry.costTable, "CostTable");
        prop->readWord(entry.costValue, "CostValue");
        prop->readByte(entry.paramTable, "Param1");
        prop->readByte(entry.paramValue, "Param1Value");
        prop->readWord(entry.subtype, "Subtype");
        prop->readByte(entry.upgradeType, "UpgradeType");

        if (prop->readWord(entry.propertyName, "PropertyName")) {
            switch (static_cast<ItemProperty>(entry.propertyName)) {
            case ItemProperty::ActivateItem: {
                _activateSpell = static_cast<SpellType>(entry.subtype);

                // CostTable is an index into iprp_costtable.2da: index 3 is
                // iprp_chargecost.2da table. Other tables are not used.
                //
                // CostValue is an index in the corresponding table: index 1 is
                // a "Single_Use" item. Index 13 is an "Unlimited_Use" item.
                //
                // For now, treat everything except "Single_Use" as unlimited.
                if (entry.costTable == 3 && entry.costValue == 1) {
                    _activateSpellCost = 1;
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
    _attackRange = baseItems->getFloat(_baseItem, "maxattackrange", 0.0f);
    _baseDefense = baseItems->getInt(_baseItem, "baseac", 0);
    _criticalHitMultiplier = baseItems->getInt(_baseItem, "crithitmult", 0);
    _criticalThreat = baseItems->getInt(_baseItem, "critthreat", 0);
    _damageFlags = baseItems->getInt(_baseItem, "damageflags", 0);
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
    _weaponWield = static_cast<WeaponWield>(
        baseItems->getInt(_baseItem, "weaponwield", 0));
    _weaponSize = static_cast<CreatureSize>(
        baseItems->getInt(_baseItem, "weaponsize", 0));
    _weaponFocusFeat = static_cast<FeatType>(
        baseItems->getInt(_baseItem, "focfeat", 0));
    _weaponSpecializationFeat = static_cast<FeatType>(
        baseItems->getInt(_baseItem, "specfeat", 0));

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
    _ammunitionType->model = _services.resource.models.get(boost::to_lower_copy(twoDa->getString(ammunitionIdx, "model")));
    _ammunitionType->muzzleFlash = _services.resource.models.get(boost::to_lower_copy(twoDa->getString(ammunitionIdx, "muzzleflash")));
    _ammunitionType->shotSound1 = _services.resource.audioClips.get(boost::to_lower_copy(twoDa->getString(ammunitionIdx, "shotsound0")));
    _ammunitionType->shotSound2 = _services.resource.audioClips.get(boost::to_lower_copy(twoDa->getString(ammunitionIdx, "shotsound1")));
    _ammunitionType->impactSound1 = _services.resource.audioClips.get(boost::to_lower_copy(twoDa->getString(ammunitionIdx, "impactsound0")));
    _ammunitionType->impactSound2 = _services.resource.audioClips.get(boost::to_lower_copy(twoDa->getString(ammunitionIdx, "impactsound1")));
}

void Item::clone(const Item &from) {
    // Object
    _tag = from._tag;
    _name = from._name;
    // END Object

    // Serializable
    _baseItem = from._baseItem;
    _localizedName = from._localizedName;
    _description = from._description;
    _descIdentified = from._descIdentified;
    _charges = from._charges;
    _cost = from._cost;
    _addCost = from._addCost;
    _stolen = from._stolen;
    _stackSize = from._stackSize;
    _identified = from._identified;
    _modelVariation = from._modelVariation;
    _bodyVariation = from._bodyVariation;
    _textureVariation = from._textureVariation;
    _dropable = from._dropable;
    // END Serializable

    _baseBodyVariation = from._baseBodyVariation;
    _itemClass = from._itemClass;

    _icon = from._icon;
    _equipableSlots = from._equipableSlots;
    _attackRange = from._attackRange;
    _numDice = from._numDice;
    _dieToRoll = from._dieToRoll;
    _damageFlags = from._damageFlags;
    _weaponType = from._weaponType;
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
    _maxDexterityBonus = from._maxDexterityBonus;
    _acBonusType = from._acBonusType;

    _activateSpell = from._activateSpell;
    _disguiseAppearance = from._disguiseAppearance;
    _properties = from._properties;

    _audioSource = from._audioSource;
}

void Item::update(float dt) {
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

} // namespace game

} // namespace reone
