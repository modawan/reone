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

#include "reone/game/object.h"
#include "reone/resource/gff.h"
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

EffectInstance EffectInstance::fromGff(const resource::Gff &gff) {
    EffectInstance result;
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

bool EffectInstance::bindCreator(const std::shared_ptr<Object> &object) {
    creator.reset();
    if (!object || object->id() != creatorId) {
        return false;
    }
    creator = object;
    return true;
}

SavedEffectValue::SavedEffectValue(EffectInstance instance) :
    Effect(static_cast<EffectType>(instance.retailType)),
    _instance(std::move(instance)) {
}

} // namespace game

} // namespace reone
