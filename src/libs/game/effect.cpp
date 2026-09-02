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

#include "reone/game/effect.h"

#include <stdexcept>

#include "reone/game/game.h"
#include "reone/game/effect/abilitydecrease.h"
#include "reone/game/effect/abilityincrease.h"
#include "reone/game/effect/acdecrease.h"
#include "reone/game/effect/acincrease.h"
#include "reone/game/effect/attackdecrease.h"
#include "reone/game/effect/attackincrease.h"
#include "reone/game/effect/bonusfeat.h"
#include "reone/game/effect/concealment.h"
#include "reone/game/effect/damagedecrease.h"
#include "reone/game/effect/damageimmunitydecrease.h"
#include "reone/game/effect/damageimmunityincrease.h"
#include "reone/game/effect/damageincrease.h"
#include "reone/game/effect/damagereduction.h"
#include "reone/game/effect/damageresistance.h"
#include "reone/game/effect/immunity.h"
#include "reone/game/effect/invisibility.h"
#include "reone/game/effect/savingthrowdecrease.h"
#include "reone/game/effect/savingthrowincrease.h"
#include "reone/game/effect/seeinvisible.h"
#include "reone/game/effect/skilldecrease.h"
#include "reone/game/effect/skillincrease.h"
#include "reone/game/effect/trueseeing.h"
#include "reone/game/effect/ultravision.h"
#include "reone/game/location.h"
#include "reone/game/object.h"
#include "reone/game/object/creature.h"
#include "reone/resource/gff.h"
#include "reone/script/variable.h"
#include "reone/system/logutil.h"

namespace reone {

namespace game {

namespace {

struct VersusParameterIndices {
    size_t race;
    size_t lawChaos;
    size_t goodEvil;
};

uint16_t retailEffectType(EffectType type) {
    switch (type) {
    case EffectType::DamageResistance: return 2;
    case EffectType::AbilityIncrease: return 36;
    case EffectType::AbilityDecrease: return 37;
    case EffectType::AttackIncrease: return 10;
    case EffectType::AttackDecrease: return 11;
    case EffectType::DamageReduction: return 12;
    case EffectType::DamageIncrease: return 13;
    case EffectType::DamageDecrease: return 14;
    case EffectType::DamageImmunityIncrease: return 16;
    case EffectType::DamageImmunityDecrease: return 17;
    case EffectType::Immunity: return 22;
    case EffectType::SavingThrowIncrease: return 26;
    case EffectType::SavingThrowDecrease: return 27;
    case EffectType::Invisibility: return 47;
    case EffectType::ACIncrease: return 48;
    case EffectType::ACDecrease: return 49;
    case EffectType::SkillIncrease: return 55;
    case EffectType::SkillDecrease: return 56;
    case EffectType::Sanctuary: return 63;
    case EffectType::SeeInvisible: return 70;
    case EffectType::Ultravision: return 71;
    case EffectType::TrueSeeing: return 72;
    case EffectType::Blindness: return 73;
    case EffectType::Concealment: return 76;
    case EffectType::BonusFeat: return 83;
    default: return static_cast<uint16_t>(type);
    }
}

EffectType runtimeEffectType(uint16_t retailType) {
    switch (retailType) {
    case 2: return EffectType::DamageResistance;
    case 36: return EffectType::AbilityIncrease;
    case 37: return EffectType::AbilityDecrease;
    case 10: return EffectType::AttackIncrease;
    case 11: return EffectType::AttackDecrease;
    case 12: return EffectType::DamageReduction;
    case 13: return EffectType::DamageIncrease;
    case 14: return EffectType::DamageDecrease;
    case 16: return EffectType::DamageImmunityIncrease;
    case 17: return EffectType::DamageImmunityDecrease;
    case 22: return EffectType::Immunity;
    case 26: return EffectType::SavingThrowIncrease;
    case 27: return EffectType::SavingThrowDecrease;
    case 47: return EffectType::Invisibility;
    case 48: return EffectType::ACIncrease;
    case 49: return EffectType::ACDecrease;
    case 55: return EffectType::SkillIncrease;
    case 56: return EffectType::SkillDecrease;
    case 63: return EffectType::Sanctuary;
    case 70: return EffectType::SeeInvisible;
    case 71: return EffectType::Ultravision;
    case 72: return EffectType::TrueSeeing;
    case 73: return EffectType::Blindness;
    case 76: return EffectType::Concealment;
    case 83: return EffectType::BonusFeat;
    default: return static_cast<EffectType>(retailType);
    }
}

std::optional<VersusParameterIndices> versusParameterIndices(uint16_t retailType) {
    switch (retailType) {
    case 10: // Attack Increase
    case 11: // Attack Decrease
    case 13: // Damage Increase
    case 14: // Damage Decrease
    case 48: // AC Increase
    case 49: // AC Decrease
    case 55: // Skill Increase
    case 56: // Skill Decrease
        return VersusParameterIndices {2, 3, 4};
    case 22: // Immunity
    case 76: // Concealment
        return VersusParameterIndices {1, 2, 3};
    case 26: // Saving Throw Increase
    case 27: // Saving Throw Decrease
        return VersusParameterIndices {3, 4, 5};
    case 47: // Invisibility
    case 63: // Sanctuary
        return VersusParameterIndices {1, 2, 3};
    default:
        return std::nullopt;
    }
}

std::shared_ptr<Effect> executableEffect(const EffectInstance &instance) {
    auto integer = [&instance](size_t index, int32_t fallback = 0) {
        return instance.integerParameter(index, fallback);
    };
    switch (instance.type()) {
    case EffectType::AbilityIncrease:
        return std::make_shared<AbilityIncreaseEffect>(
            static_cast<Ability>(integer(0)), integer(1));
    case EffectType::AbilityDecrease:
        return std::make_shared<AbilityDecreaseEffect>(
            static_cast<Ability>(integer(0)), integer(1));
    case EffectType::BonusFeat:
        return std::make_shared<BonusFeatEffect>(
            static_cast<FeatType>(integer(0)));
    case EffectType::AttackIncrease:
        return std::make_shared<AttackIncreaseEffect>(
            integer(0), static_cast<AttackBonus>(integer(1)));
    case EffectType::AttackDecrease:
        return std::make_shared<AttackDecreaseEffect>(
            integer(0), static_cast<AttackBonus>(integer(1)));
    case EffectType::DamageIncrease:
        return std::make_shared<DamageIncreaseEffect>(
            integer(0), static_cast<DamageType>(integer(1)));
    case EffectType::DamageDecrease:
        return std::make_shared<DamageDecreaseEffect>(
            integer(0), static_cast<DamageType>(integer(1)));
    case EffectType::ACIncrease:
        return std::make_shared<ACIncreaseEffect>(
            integer(1), static_cast<ACBonus>(integer(0)),
            integer(5, kAllDamageTypeFlags));
    case EffectType::ACDecrease:
        return std::make_shared<ACDecreaseEffect>(
            integer(1), static_cast<ACBonus>(integer(0)),
            integer(5, kAllDamageTypeFlags));
    case EffectType::SavingThrowIncrease:
        return std::make_shared<SavingThrowIncreaseEffect>(
            integer(1), integer(0),
            static_cast<SavingThrowType>(integer(2)));
    case EffectType::SavingThrowDecrease:
        return std::make_shared<SavingThrowDecreaseEffect>(
            integer(1), integer(0),
            static_cast<SavingThrowType>(integer(2)));
    case EffectType::SkillIncrease:
        return std::make_shared<SkillIncreaseEffect>(
            static_cast<SkillType>(integer(0)), integer(1));
    case EffectType::SkillDecrease:
        return std::make_shared<SkillDecreaseEffect>(
            static_cast<SkillType>(integer(0)), integer(1));
    case EffectType::Immunity:
        return std::make_shared<ImmunityEffect>(
            static_cast<ImmunityType>(integer(0)));
    case EffectType::Concealment:
        return std::make_shared<ConcealmentEffect>(integer(0));
    case EffectType::DamageImmunityIncrease:
        return std::make_shared<DamageImmunityIncreaseEffect>(
            static_cast<DamageType>(integer(0)), integer(1));
    case EffectType::DamageImmunityDecrease:
        return std::make_shared<DamageImmunityDecreaseEffect>(
            static_cast<DamageType>(integer(0)), integer(1));
    case EffectType::DamageResistance:
        return std::make_shared<DamageResistanceEffect>(
            static_cast<DamageType>(integer(0)), integer(1), integer(2));
    case EffectType::DamageReduction:
        return std::make_shared<DamageReductionEffect>(
            integer(0), static_cast<DamagePower>(integer(1)), integer(2));
    case EffectType::Invisibility:
        return std::make_shared<InvisibilityEffect>(
            static_cast<InvisibilityType>(integer(0)));
    case EffectType::SeeInvisible:
        return std::make_shared<SeeInvisibleEffect>();
    case EffectType::Ultravision:
        return std::make_shared<UltravisionEffect>();
    case EffectType::TrueSeeing:
        return std::make_shared<TrueSeeingEffect>();
    default:
        return nullptr;
    }
}

} // namespace

EffectId EffectIdNamespace::allocate() {
    while (_ids.count(_nextId) != 0) {
        if (_nextId == std::numeric_limits<EffectId>::max()) {
            throw std::overflow_error("Effect ID namespace exhausted");
        }
        ++_nextId;
    }
    if (_nextId == kUnassignedEffectId || _nextId == std::numeric_limits<EffectId>::max()) {
        throw std::overflow_error("Effect ID namespace exhausted");
    }
    EffectId id = _nextId++;
    _ids.insert(id);
    return id;
}

EffectIdImportResult EffectIdNamespace::importId(EffectId id) {
    if (id == kUnassignedEffectId) {
        return EffectIdImportResult::Unassigned;
    }
    auto [_, inserted] = _ids.insert(id);
    return inserted ? EffectIdImportResult::Imported : EffectIdImportResult::Existing;
}

bool EffectIdNamespace::setNextId(EffectId id) {
    if (id == kUnassignedEffectId || id == std::numeric_limits<EffectId>::max()) {
        return false;
    }
    _nextId = id;
    return true;
}

void EffectIdNamespace::reset() {
    _nextId = kFirstId;
    _ids.clear();
}

void Effect::applyTo(Object &object) {
    debug("Unsupported effect type: " + std::to_string(static_cast<int>(_type)));
}

bool Effect::onApply(Object &object, const EffectInstance &) {
    applyTo(object);
    return true;
}

void Effect::onRemove(Object &object, const EffectInstance &) {
}

EffectInstance Effect::saveFacingInstance() const {
    if (!_saveFacingRepresentable) {
        throw std::runtime_error(
            "live effect has no representable retail CGameEffect value");
    }
    EffectInstance result;
    result.retailType = retailEffectType(_type);
    // Retail VM-created effects remain engine values until ApplyEffectToObject
    // supplies the duration bits. Bit 3 identifies that un-applied form.
    result.subType = 0x8;
    result.creatorId = kSavedEffectInvalidObjectId;
    result.spellId = _saveFacingSpellId;
    result.integerParameters = _saveFacingIntegers;
    result.floatParameters = _saveFacingFloats;
    result.stringParameters = _saveFacingStrings;
    // Preserve the exact candidate binding even while its owner graph remains
    // Constructing. Resolution intentionally begins to succeed only after the
    // candidate incarnation is published Live.
    result.creator = _saveFacingCreator;
    if (auto creator = _saveFacingCreator.resolve()) {
        result.creatorId = creator->id();
    }
    for (size_t index = 0; index < _saveFacingObjects.size(); ++index) {
        if (auto object = _saveFacingObjects[index].resolve()) {
            result.objectParameters[index] = object->id();
            result.objectParameterObjects[index] = object;
        }
    }
    return result;
}

void Effect::setSaveFacingCreator(const std::shared_ptr<Object> &creator) {
    _saveFacingCreator = creator;
}

void Effect::setSaveFacingSpellId(int32_t spellId) {
    // Retail uses an all-bits-set SpellId for an independently applied effect;
    // zero is a valid semantic grouping value.
    _saveFacingSpellId = static_cast<uint32_t>(spellId);
}

bool Effect::setVersusAlignment(int lawChaos, int goodEvil) {
    auto indices = versusParameterIndices(retailEffectType(_type));
    if (!indices) {
        return false;
    }
    if (_saveFacingIntegers.size() <= indices->race) {
        setSaveFacingInteger(
            indices->race, static_cast<int>(RacialType::All));
    }
    setSaveFacingInteger(indices->lawChaos, lawChaos);
    setSaveFacingInteger(indices->goodEvil, goodEvil);
    return true;
}

bool Effect::setVersusRacialType(int racialType) {
    auto indices = versusParameterIndices(retailEffectType(_type));
    if (!indices) {
        return false;
    }
    setSaveFacingInteger(indices->race, racialType);
    return true;
}

void Effect::captureSaveFacingScriptArguments(
    const std::vector<script::Variable> &arguments,
    const Game &game) {
    if (_type == EffectType::LinkEffects) {
        // Retail SaveGameEffect writes only the flat type-40 CGameEffect and
        // does not serialize m_pLinkLeft/m_pLinkRight. Preserve that exact,
        // representable-but-inert value rather than inventing child fields.
        return;
    }

    size_t integerIndex = 0;
    size_t floatIndex = 0;
    size_t stringIndex = 0;
    size_t objectIndex = 0;
    for (const auto &argument : arguments) {
        switch (argument.type) {
        case script::VariableType::Int:
            if (_type == EffectType::Visual && integerIndex == 1) {
                setSaveFacingInteger(2, argument.intValue);
            } else if ((_type == EffectType::ACIncrease ||
                        _type == EffectType::ACDecrease) &&
                       integerIndex < 3) {
                static constexpr std::array<size_t, 3> kACParameterOrder {1, 0, 5};
                setSaveFacingInteger(
                    kACParameterOrder[integerIndex], argument.intValue);
                setSaveFacingInteger(2, static_cast<int>(RacialType::All));
            } else if ((_type == EffectType::SavingThrowIncrease ||
                        _type == EffectType::SavingThrowDecrease) &&
                       integerIndex < 3) {
                static constexpr std::array<size_t, 3> kSaveParameterOrder {1, 0, 2};
                setSaveFacingInteger(
                    kSaveParameterOrder[integerIndex], argument.intValue);
                setSaveFacingInteger(3, static_cast<int>(RacialType::All));
            } else if (_type != EffectType::LightsaberThrow) {
                setSaveFacingInteger(integerIndex, argument.intValue);
            }
            ++integerIndex;
            break;
        case script::VariableType::Float:
            if (floatIndex < _saveFacingFloats.size()) {
                setSaveFacingFloat(floatIndex++, argument.floatValue);
            }
            break;
        case script::VariableType::String:
            if (stringIndex < _saveFacingStrings.size()) {
                setSaveFacingString(stringIndex++, argument.strValue);
            }
            break;
        case script::VariableType::Object:
            if (objectIndex < _saveFacingObjects.size()) {
                setSaveFacingObject(
                    objectIndex++, game.getObjectById(argument.objectId));
            }
            break;
        case script::VariableType::Location: {
            auto location = std::dynamic_pointer_cast<Location>(
                argument.engineType);
            if (!location) {
                _saveFacingRepresentable = false;
                break;
            }
            const auto &position = location->position();
            for (float value : {position.x, position.y, position.z}) {
                if (floatIndex < _saveFacingFloats.size()) {
                    setSaveFacingFloat(floatIndex++, value);
                }
            }
            break;
        }
        case script::VariableType::Effect:
            _saveFacingRepresentable = false;
            break;
        default:
            break;
        }
    }
}

void Effect::setSaveFacingInteger(size_t index, int32_t value) {
    if (_saveFacingIntegers.size() <= index) {
        _saveFacingIntegers.resize(index + 1);
    }
    _saveFacingIntegers[index] = value;
}

void Effect::setSaveFacingFloat(size_t index, float value) {
    if (index >= _saveFacingFloats.size()) {
        throw std::out_of_range("effect float parameter index");
    }
    _saveFacingFloats[index] = value;
}

void Effect::setSaveFacingString(size_t index, std::string value) {
    if (index >= _saveFacingStrings.size()) {
        throw std::out_of_range("effect string parameter index");
    }
    _saveFacingStrings[index] = std::move(value);
}

void Effect::setSaveFacingObject(
    size_t index,
    const std::shared_ptr<Object> &object) {
    if (index >= _saveFacingObjects.size()) {
        throw std::out_of_range("effect object parameter index");
    }
    _saveFacingObjects[index] = object;
}

EffectInstance EffectInstance::fromGff(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    EffectInstance result;
    result.serializedReferenceContext = identityContext;
    result.id = gff.getUint64("Id");
    result.retailType = static_cast<uint16_t>(gff.getUint("Type"));
    result.subType = static_cast<uint16_t>(gff.getUint("SubType"));
    result.duration = gff.getFloat("Duration");
    result.skipOnLoad = gff.getBool("SkipOnLoad");
    result.expiryDay = gff.getUint("ExpireDay");
    result.expiryTime = gff.getUint("ExpireTime");
    if (result.durationType() == DurationType::Temporary) {
        result.expiryOrigin = EffectExpiryOrigin::LoadedAbsoluteGameTime;
    }
    result.creatorId = gff.getUint("CreatorId");
    result.spellId = gff.getUint("SpellId", std::numeric_limits<uint32_t>::max());
    result.exposed = gff.getInt("IsExposed");

    int32_t integerCount = glm::max(0, gff.getInt("NumIntegers"));
    result.integerParameters.assign(static_cast<size_t>(integerCount), 0);
    auto integers = gff.getList("IntList");
    size_t copyIntegerCount = glm::min(result.integerParameters.size(), integers.size());
    for (size_t i = 0; i < copyIntegerCount; ++i) {
        result.integerParameters[i] = integers[i]->getInt("Value");
    }

    auto floats = gff.getList("FloatList");
    for (size_t i = 0; i < glm::min(result.floatParameters.size(), floats.size()); ++i) {
        result.floatParameters[i] = floats[i]->getFloat("Value");
    }

    auto strings = gff.getList("StringList");
    for (size_t i = 0; i < glm::min(result.stringParameters.size(), strings.size()); ++i) {
        result.stringParameters[i] = strings[i]->getString("Value");
    }

    auto objects = gff.getList("ObjectList");
    for (size_t i = 0; i < glm::min(result.objectParameters.size(), objects.size()); ++i) {
        result.objectParameters[i] = objects[i]->getUint("Value", kSavedEffectInvalidObjectId);
    }
    result.effect = executableEffect(result);
    return result;
}

DurationType EffectInstance::durationType() const {
    switch (subType & 0x7) {
    case 0:
        return DurationType::Instant;
    case 1:
        return DurationType::Temporary;
    case 2:
        return DurationType::Permanent;
    case 3:
        return DurationType::Equipped;
    case 4:
        return DurationType::Innate;
    default:
        return DurationType::Invalid;
    }
}

EffectType EffectInstance::type() const {
    return effect ? effect->type() : runtimeEffectType(retailType);
}

int32_t EffectInstance::integerParameter(
    size_t index, int32_t defaultValue) const {
    return index < integerParameters.size()
               ? integerParameters[index]
               : defaultValue;
}

std::shared_ptr<Object> EffectInstance::boundCreator() const {
    return creator.resolve();
}

std::shared_ptr<Object> EffectInstance::boundObjectParameter(
    size_t index) const {
    return index < objectParameterObjects.size()
               ? objectParameterObjects[index].resolve()
               : nullptr;
}

bool EffectInstance::appliesVersus(const Creature *creature) const {
    auto indices = versusParameterIndices(retailType);
    if (!indices) {
        return true;
    }

    int race = integerParameter(
        indices->race, static_cast<int>(RacialType::All));
    int goodEvil = integerParameter(
        indices->goodEvil, static_cast<int>(Alignment::All));
    bool hasRace = retailType == 76
                       ? race != 0
                       : race != static_cast<int>(RacialType::All);
    bool hasAlignment = goodEvil != static_cast<int>(Alignment::All);
    if (!hasRace && !hasAlignment) {
        return true;
    }
    if (!creature) {
        return false;
    }
    if (hasRace && race != static_cast<int>(creature->racialType())) {
        return false;
    }
    return !hasAlignment || goodEvil == static_cast<int>(creature->alignment());
}

bool EffectInstance::hasLiveRuntimeSource() const {
    return durationType() != DurationType::Equipped ||
           static_cast<bool>(boundCreator());
}

bool EffectInstance::bindCreator(const std::shared_ptr<Object> &object) {
    creator.reset();
    if (!object) {
        return false;
    }
    creator = object;
    return true;
}

bool EffectInstance::bindObjectParameter(
    size_t index, const std::shared_ptr<Object> &object) {
    if (index >= objectParameterObjects.size()) {
        return false;
    }
    objectParameterObjects[index].reset();
    if (!object) {
        return false;
    }
    objectParameterObjects[index] = object;
    return true;
}

void EffectInstance::retireAreaRuntimeBindings(
    const std::set<const Object *> &retainedObjects) {
    auto retain = [&retainedObjects](
                      RuntimeObjectRef<Object> &binding,
                      uint32_t &identity) {
        auto object = binding.resolve();
        if (!object || retainedObjects.count(object.get()) == 0) {
            binding.reset();
            identity = kSavedEffectInvalidObjectId;
            return;
        }
        identity = object->id();
    };

    retain(creator, creatorId);
    for (size_t index = 0; index < objectParameters.size(); ++index) {
        retain(objectParameterObjects[index], objectParameters[index]);
    }
    serializedReferenceContext.reset();
    _savedGraph.reset();
    _runtimeSession.reset();
}

SavedEffectValue::SavedEffectValue(EffectInstance instance) :
    Effect(instance.type()),
    _instance(std::move(instance)) {
}

} // namespace game

} // namespace reone
