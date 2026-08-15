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

#include "reone/audio/clip.h"
#include "reone/audio/source.h"
#include "reone/graphics/model.h"
#include "reone/graphics/texture.h"
#include "reone/resource/strings.h"

#include "../object.h"
#include "../types.h"

namespace reone {

namespace resources {
class Gff;
}

namespace game {

class Item : public Object {
public:
    // itemclass of the Credits base item in baseitems.2da, lower-cased
    static constexpr char kCreditsItemClass[] = "i_credits";

    struct AmmunitionType {
        std::shared_ptr<graphics::Model> model;
        std::shared_ptr<graphics::Model> muzzleFlash;
        std::shared_ptr<audio::AudioClip> shotSound1;
        std::shared_ptr<audio::AudioClip> shotSound2;
        std::shared_ptr<audio::AudioClip> impactSound1;
        std::shared_ptr<audio::AudioClip> impactSound2;
    };

    struct PropertyEntry {
        uint8_t chanceAppear {0};
        uint8_t costTable {0};
        uint16_t costValue {0};
        uint8_t paramTable {0};
        uint8_t paramValue {0};
        uint16_t propertyName {0};
        uint16_t subtype {0};
        uint8_t upgradeType {0};
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
    void deserialize(const resource::Gff &gff);
    void captureOwnerLocalSaveRecord(
        const resource::Gff &gff,
        SaveRecordOrigin origin);

    std::optional<uint32_t> originalOwnerLocalObjectId() const {
        return _originalOwnerLocalObjectId;
    }

    void update(float dt) override;

    void playShotSound(int variant, glm::vec3 position);
    void playImpactSound(int variant, glm::vec3 position);
    void powerUp(glm::vec3 position);
    void powerDown(glm::vec3 position);
    void updatePoweredSoundPosition(glm::vec3 position);

    bool isEquippable() const;
    bool isEquippable(int slot) const;
    bool isCredits() const { return _itemClass == kCreditsItemClass; }
    bool isDropable() const { return _dropable; }
    bool isIdentified() const { return _identified; }
    bool isEquipped() const { return _equipped; }
    bool isLightsaber() const { return _baseItem >= 8 && _baseItem <= 10; }
    bool isRanged() const { return _weaponType == WeaponType::Ranged; }

    const std::string &baseBodyVariation() const { return _baseBodyVariation; }
    const std::string &itemClass() const { return _itemClass; }
    const std::string &localizedName() const { return _localizedName.str(); }
    float attackRange() const { return _attackRange; }
    int bodyVariation() const { return _bodyVariation; }
    int damageFlags() const {
        return _damageFlags != 0
                   ? _damageFlags
                   : static_cast<int>(DamageType::Universal);
    }
    int dieToRoll() const { return _dieToRoll; }
    int modelVariation() const { return _modelVariation; }
    int numDice() const { return _numDice; }
    int stackSize() const { return _stackSize; }
    int textureVariation() const { return _textureVariation; }
    std::shared_ptr<AmmunitionType> ammunitionType() const { return _ammunitionType; }
    std::shared_ptr<graphics::Texture> icon() const { return _icon; }
    WeaponType weaponType() const { return _weaponType; }
    WeaponWield weaponWield() const { return _weaponWield; }
    CreatureSize weaponSize() const { return _weaponSize; }
    const std::string &description() const { return _description.str(); }
    const std::string &descIdentified() const { return _descIdentified.str(); }
    int baseItemType() const { return _baseItem; }
    int criticalThreat() const { return _criticalThreat; }
    int criticalHitMultiplier() const { return _criticalHitMultiplier; }
    FeatType weaponFocusFeat() const { return _weaponFocusFeat; }
    FeatType weaponSpecializationFeat() const { return _weaponSpecializationFeat; }
    int baseDefense() const { return _baseDefense; }
    int maxDexterityBonus() const { return _maxDexterityBonus; }
    ACBonus acBonusType() const { return _acBonusType; }
    std::optional<SpellType> activateSpell() const { return _activateSpell; }
    const std::vector<PropertyEntry> &properties() const { return _properties; }

    bool hasDisguise() const { return _disguiseAppearance >= 0; }
    int disguiseAppearance() const { return _disguiseAppearance; }

    void setDropable(bool dropable);
    void setStackSize(int size);
    void setIdentified(bool value);
    void setEquipped(bool equipped);

private:
    // Serializable
    int32_t _baseItem {0};
    resource::LocString _localizedName;
    resource::LocString _description;
    resource::LocString _descIdentified;
    uint8_t _charges {0};
    uint32_t _cost {0};
    uint32_t _addCost {0};
    bool _stolen {false};
    uint16_t _stackSize {1};
    bool _identified {true};
    uint8_t _modelVariation {0};
    uint8_t _bodyVariation {0};
    uint8_t _textureVariation {0};
    bool _dropable {false};
    // END Serializable

    std::string _baseBodyVariation;
    std::string _itemClass;

    std::shared_ptr<graphics::Texture> _icon;
    uint32_t _equipableSlots {0};
    float _attackRange {0.0f};
    int _numDice {0};
    int _dieToRoll {0};
    int _damageFlags {0};
    WeaponType _weaponType {WeaponType::None};
    WeaponWield _weaponWield {WeaponWield::None};
    CreatureSize _weaponSize {CreatureSize::Invalid};

    bool _equipped {false};
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
    int _maxDexterityBonus {-1};
    ACBonus _acBonusType {ACBonus::Invalid};

    std::optional<SpellType> _activateSpell;
    int _disguiseAppearance {-1};
    std::vector<PropertyEntry> _properties;
    std::optional<uint32_t> _originalOwnerLocalObjectId;

    std::shared_ptr<audio::AudioSource> _audioSource;

    // Blueprint
    void deserializeAll(const resource::Gff &gff);
    void deserializeProperties(const resource::Gff &gff);
    void deserializeBase(const resource::Gff &gff);
    void loadAmmunitionType();
    // END Blueprint
};

} // namespace game

} // namespace reone
