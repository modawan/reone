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
        deserialize(*uti);
        return;
    }
}

bool Item::deserialize(const resource::Gff &gff) {
    return deserializeAll(gff, /*max_recursion=*/2);
}

bool Item::deserializeAll(const resource::Gff &gff, int max_recursion) {
    if (max_recursion > 0) {
        if (auto res = gff.findString("EquippedRes")) {
            std::shared_ptr<Gff> uti(_services.resource.gffs.get(boost::to_lower_copy(*res), ResType::Uti));
            if (!uti || !deserializeAll(*uti, max_recursion - 1)) {
                return false;
            }
        }

        if (auto res = gff.findString("InventoryRes")) {
            std::shared_ptr<Gff> uti(_services.resource.gffs.get(boost::to_lower_copy(*res), ResType::Uti));
            if (!uti || !deserializeAll(*uti, max_recursion - 1)) {
                return false;
            }
        }

        if (auto res = gff.findString("TemplateResRef")) {
            _blueprintResRef = boost::to_lower_copy(*res);
            std::shared_ptr<Gff> uti(_services.resource.gffs.get(_blueprintResRef, ResType::Uti));
            // Templates often have TemplateResRef to themselves, so do not
            // allow further recursion.
            if (!uti || !deserializeAll(*uti, /*max_recursion=*/0)) {
                return false;
            }
        }
    }

    // index into baseitems.2da
    _baseItem = gff.getInt("BaseItem", _baseItem);

    if (auto name = gff.findInt("LocalizedName")) {
        _localizedName = _services.resource.strings.getText(*name);
    }

    if (auto desc = gff.findInt("Description")) {
        _description = _services.resource.strings.getText(*desc);
    }

    if (auto desc = gff.findInt("DescIdentified")) {
        _descIdentified = _services.resource.strings.getText(*desc);
    }

    if (auto tag = gff.findString("Tag")) {
        _tag = boost::to_lower_copy(*tag);
    }

    _charges = gff.getUint("Charges", _charges);
    _cost = gff.getUint("Cost", _cost);
    _stolen = gff.getUint("Stolen", _stolen);
    _stackSize = gff.getUint("StackSize", _stackSize);
    _plot = gff.getUint("Plot", _plot);
    _addCost = gff.getUint("AddCost", _addCost);
    _identified = gff.getUint("Identified", _identified);
    _modelVariation = gff.getUint("ModelVariation", _modelVariation);
    _textureVariation = gff.getUint("TextureVar", _textureVariation);
    _bodyVariation = gff.getUint("BodyVariation", _bodyVariation);
    _dropable = gff.getBool("Dropable", _dropable);

    std::shared_ptr<TwoDA> baseItems(_services.resource.twoDas.get("baseitems"));
    if (!baseItems) {
        return false;
    }
    _attackRange = baseItems->getInt(_baseItem, "maxattackrange");
    _criticalHitMultiplier = baseItems->getInt(_baseItem, "crithitmult");
    _criticalThreat = baseItems->getInt(_baseItem, "critthreat");
    _damageFlags = baseItems->getInt(_baseItem, "damageflags");
    _dieToRoll = baseItems->getInt(_baseItem, "dietoroll");
    _equipableSlots = baseItems->getHexInt(_baseItem, "equipableslots", 0);
    _itemClass = boost::to_lower_copy(baseItems->getString(_baseItem, "itemclass"));
    _numDice = baseItems->getInt(_baseItem, "numdice");
    _weaponType = static_cast<WeaponType>(baseItems->getInt(_baseItem, "weapontype"));
    _weaponWield = static_cast<WeaponWield>(baseItems->getInt(_baseItem, "weaponwield"));

    std::string iconResRef;
    if (isEquippable(InventorySlots::body)) {
        _baseBodyVariation = boost::to_lower_copy(baseItems->getString(_baseItem, "bodyvar"));
        iconResRef = str(boost::format("i%s_%03d") % _itemClass % _textureVariation);
    } else if (isEquippable(InventorySlots::rightWeapon)) {
        iconResRef = str(boost::format("i%s_%03d") % _itemClass % _modelVariation);
    } else {
        iconResRef = str(boost::format("i%s_%03d") % _itemClass % _modelVariation);
    }
    _icon = _services.resource.textures.get(iconResRef, TextureUsage::GUI);

    loadAmmunitionType();

    // TODO: load other properties
    for (const auto &prop : gff.getList("PropertiesList")) {
        auto property = static_cast<ItemProperty>(prop->getUint("PropertyName"));
        switch (property) {
        case ItemProperty::ActivateItem:
            _activateSpell = static_cast<SpellType>(prop->getUint("Subtype"));
            break;
        default:
            break;
        }
    }

    return true;
}

void Item::loadAmmunitionType() {
    std::shared_ptr<TwoDA> baseItems(_services.resource.twoDas.get("baseitems"));

    int ammunitionIdx = baseItems->getInt(_baseItem, "ammunitiontype", -1);
    if (ammunitionIdx != -1) {
        std::shared_ptr<TwoDA> twoDa(_services.resource.twoDas.get("ammunitiontypes"));
        _ammunitionType = std::make_shared<Item::AmmunitionType>();
        _ammunitionType->model = _services.resource.models.get(boost::to_lower_copy(twoDa->getString(ammunitionIdx, "model")));
        _ammunitionType->muzzleFlash = _services.resource.models.get(boost::to_lower_copy(twoDa->getString(ammunitionIdx, "muzzleflash")));
        _ammunitionType->shotSound1 = _services.resource.audioClips.get(boost::to_lower_copy(twoDa->getString(ammunitionIdx, "shotsound0")));
        _ammunitionType->shotSound2 = _services.resource.audioClips.get(boost::to_lower_copy(twoDa->getString(ammunitionIdx, "shotsound1")));
        _ammunitionType->impactSound1 = _services.resource.audioClips.get(boost::to_lower_copy(twoDa->getString(ammunitionIdx, "impactsound0")));
        _ammunitionType->impactSound2 = _services.resource.audioClips.get(boost::to_lower_copy(twoDa->getString(ammunitionIdx, "impactsound1")));
    }
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
