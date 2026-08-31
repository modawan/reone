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
#include "reone/game/location.h"
#include "reone/game/object.h"
#include "reone/resource/gff.h"
#include "reone/script/variable.h"
#include "reone/system/logutil.h"

namespace reone {

namespace game {

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

bool Effect::onApply(Object &object) {
    applyTo(object);
    return true;
}

void Effect::onRemove(Object &object) {
}

EffectInstance Effect::saveFacingInstance() const {
    if (!_saveFacingRepresentable) {
        throw std::runtime_error(
            "live effect has no representable retail CGameEffect value");
    }
    EffectInstance result;
    result.retailType = static_cast<uint16_t>(_type);
    // Retail VM-created effects remain engine values until ApplyEffectToObject
    // supplies the duration bits. Bit 3 identifies that un-applied form.
    result.subType = 0x8;
    result.creatorId = kSavedEffectInvalidObjectId;
    result.integerParameters = _saveFacingIntegers;
    result.floatParameters = _saveFacingFloats;
    result.stringParameters = _saveFacingStrings;
    if (auto creator = _saveFacingCreator.resolve()) {
        result.creatorId = creator->id();
        result.creator = creator;
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

std::shared_ptr<Object> EffectInstance::boundCreator() const {
    return creator.resolve();
}

std::shared_ptr<Object> EffectInstance::boundObjectParameter(
    size_t index) const {
    return index < objectParameterObjects.size()
               ? objectParameterObjects[index].resolve()
               : nullptr;
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
    Effect(static_cast<EffectType>(instance.retailType)),
    _instance(std::move(instance)) {
}

} // namespace game

} // namespace reone
