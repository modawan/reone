/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "reone/game/savedruntime.h"

#include <set>

#include "reone/game/action/wait.h"
#include "reone/game/game.h"

namespace reone {

namespace game {

namespace {

SavedField savedFieldFromGff(const resource::Gff::Field &field) {
    SavedField result;
    result.type = field.type;
    result.label = field.label;
    switch (field.type) {
    case resource::Gff::FieldType::Byte:
    case resource::Gff::FieldType::Word:
    case resource::Gff::FieldType::Dword:
        result.value = static_cast<uint64_t>(field.uintValue);
        break;
    case resource::Gff::FieldType::Dword64:
        result.value = field.uint64Value;
        break;
    case resource::Gff::FieldType::Char:
    case resource::Gff::FieldType::Short:
    case resource::Gff::FieldType::Int:
    case resource::Gff::FieldType::StrRef:
        result.value = static_cast<int64_t>(field.intValue);
        break;
    case resource::Gff::FieldType::Int64:
        result.value = field.int64Value;
        break;
    case resource::Gff::FieldType::Float:
        result.value = static_cast<double>(field.floatValue);
        break;
    case resource::Gff::FieldType::Double:
        result.value = field.doubleValue;
        break;
    case resource::Gff::FieldType::CExoString:
    case resource::Gff::FieldType::ResRef:
        result.value = field.strValue;
        break;
    case resource::Gff::FieldType::CExoLocString:
        result.value = SavedLocString {field.intValue, field.strValue};
        break;
    case resource::Gff::FieldType::Void:
        result.value = field.data;
        break;
    case resource::Gff::FieldType::Orientation:
        result.value = field.quatValue;
        break;
    case resource::Gff::FieldType::Vector:
        result.value = field.vecValue;
        break;
    case resource::Gff::FieldType::Struct:
    case resource::Gff::FieldType::List: {
        SavedStructChildren children;
        children.reserve(field.children.size());
        for (const auto &child : field.children) {
            children.push_back(std::make_shared<SavedStruct>(SavedStruct::fromGff(*child)));
        }
        result.value = std::move(children);
        break;
    }
    }
    return result;
}

std::vector<SavedField> collectUnsupportedFields(
    const resource::Gff &gff,
    const std::set<std::string> &known) {
    std::vector<SavedField> result;
    for (const auto &field : gff.fields()) {
        if (known.count(field.label) == 0) {
            result.push_back(savedFieldFromGff(field));
        }
    }
    return result;
}

UnsupportedSavedPayload unsupportedPayload(const resource::Gff &gff) {
    return UnsupportedSavedPayload {SavedStruct::fromGff(gff)};
}

SavedLocationValue savedLocationFromGff(const resource::Gff &gff) {
    SavedLocationValue result;
    result.position = glm::vec3(
        gff.getFloat("PositionX"),
        gff.getFloat("PositionY"),
        gff.getFloat("PositionZ"));
    result.orientation = glm::vec3(
        gff.getFloat("OrientationX"),
        gff.getFloat("OrientationY"),
        gff.getFloat("OrientationZ"));
    return result;
}

SavedScriptEvent savedScriptEventFromGff(const resource::Gff &gff) {
    SavedScriptEvent result;
    result.type = static_cast<uint16_t>(gff.getUint("EventType"));
    for (const auto &item : gff.getList("IntList")) {
        result.integers.push_back(item->getInt("Parameter"));
    }
    for (const auto &item : gff.getList("FloatList")) {
        result.floats.push_back(item->getFloat("Parameter"));
    }
    for (const auto &item : gff.getList("StringList")) {
        result.strings.push_back(item->getString("Parameter"));
    }
    for (const auto &item : gff.getList("ObjectList")) {
        result.objects.push_back(SavedObjectReference {item->getUint("Parameter")});
    }
    return result;
}

SavedSpellImpact savedSpellImpactFromGff(const resource::Gff &gff) {
    SavedSpellImpact result;
    result.spellId = gff.getInt("SpellId");
    result.caster.id = gff.getUint("CasterId");
    result.target.id = gff.getUint("TargetId");
    result.area.id = gff.getUint("AreaId");
    result.item.id = gff.getUint("ItemId");
    result.script = gff.getString("Script");
    result.targetPosition = glm::vec3(
        gff.getFloat("TargetPosX"),
        gff.getFloat("TargetPosY"),
        gff.getFloat("TargetPosZ"));
    result.finalForceCost = gff.getInt("FinalForceCost");
    return result;
}

SavedBodyBag savedBodyBagFromGff(const resource::Gff &gff) {
    SavedBodyBag result;
    result.object.id = gff.getUint("BodyBagId");
    result.position = glm::vec3(
        gff.getFloat("PositionX"),
        gff.getFloat("PositionY"),
        gff.getFloat("PositionZ"));
    return result;
}

bool bindReference(const Game &game, SavedObjectReference &reference, bool &allBound) {
    if (reference.isInvalid()) {
        return false;
    }
    bool bound = game.bindSavedObjectReference(reference);
    allBound = allBound && bound;
    return bound;
}

} // namespace

SavedStruct SavedStruct::fromGff(const resource::Gff &gff) {
    SavedStruct result;
    result.type = gff.type();
    result.fields.reserve(gff.fields().size());
    for (const auto &field : gff.fields()) {
        result.fields.push_back(savedFieldFromGff(field));
    }
    return result;
}

SerializedScriptSituation SerializedScriptSituation::fromGff(const resource::Gff &gff) {
    SerializedScriptSituation result;
    result.codeSize = gff.getInt("CodeSize");
    result.code = gff.getData("Code");
    result.crc = gff.getUint("CRC");
    result.instructionPointer = gff.getInt("InstructionPtr");
    result.secondaryPointer = gff.getInt("SecondaryPtr");
    result.scriptName = gff.getString("Name");
    result.stackSize = gff.getInt("StackSize");
    result.unsupportedFields = collectUnsupportedFields(
        gff,
        {"CodeSize", "Code", "CRC", "InstructionPtr", "SecondaryPtr", "Name", "StackSize", "Stack"});

    auto stackStruct = gff.findStruct("Stack");
    if (!stackStruct) {
        return result;
    }
    result.basePointer = stackStruct->getInt("BasePointer");
    result.stackPointer = stackStruct->getInt("StackPointer");
    result.totalSize = stackStruct->getInt("TotalSize");
    for (const auto &item : stackStruct->getList("Stack")) {
        SavedVmStackValue value;
        int8_t type = 0;
        item->readChar(type, "Type");
        value.type = type;
        switch (static_cast<SavedVmStackType>(type)) {
        case SavedVmStackType::Integer:
            value.payload = item->getInt("Value");
            break;
        case SavedVmStackType::Float:
            value.payload = item->getFloat("Value");
            break;
        case SavedVmStackType::String:
            value.payload = item->getString("Value");
            break;
        case SavedVmStackType::Object:
            value.payload = SavedObjectReference {item->getUint("Value")};
            break;
        case SavedVmStackType::Effect: {
            auto structure = item->findStruct("GameDefinedStrct");
            value.payload = structure ? SavedVmStackPayload(EffectInstance::fromGff(*structure))
                                      : SavedVmStackPayload(unsupportedPayload(*item));
            break;
        }
        case SavedVmStackType::Location: {
            auto structure = item->findStruct("GameDefinedStrct");
            value.payload = structure ? SavedVmStackPayload(savedLocationFromGff(*structure))
                                      : SavedVmStackPayload(unsupportedPayload(*item));
            break;
        }
        default:
            value.payload = unsupportedPayload(*item);
            break;
        }
        result.stack.push_back(std::move(value));
    }
    return result;
}

bool SerializedScriptSituation::bindObjectReferences(const Game &game) {
    bool allBound = true;
    for (auto &entry : stack) {
        if (auto reference = std::get_if<SavedObjectReference>(&entry.payload)) {
            bindReference(game, *reference, allBound);
        } else if (auto effect = std::get_if<EffectInstance>(&entry.payload)) {
            if (effect->creatorId != kSavedRuntimeInvalidObjectId) {
                allBound = game.bindEffectCreator(*effect) && allBound;
            }
        }
    }
    return allBound;
}

SavedActionParameter SavedActionParameter::fromGff(const resource::Gff &gff) {
    SavedActionParameter result;
    result.type = gff.getUint("Type");
    switch (static_cast<SavedActionParameterType>(result.type)) {
    case SavedActionParameterType::Integer:
        result.payload = gff.getInt("Value");
        break;
    case SavedActionParameterType::Float:
        result.payload = gff.getFloat("Value");
        break;
    case SavedActionParameterType::Object:
        result.payload = SavedObjectReference {gff.getUint("Value")};
        break;
    case SavedActionParameterType::String:
        result.payload = gff.getString("Value");
        break;
    case SavedActionParameterType::ScriptSituation: {
        auto value = gff.findStruct("Value");
        result.payload = value ? SavedActionParameterPayload(SerializedScriptSituation::fromGff(*value))
                               : SavedActionParameterPayload(unsupportedPayload(gff));
        break;
    }
    default:
        result.payload = unsupportedPayload(gff);
        break;
    }
    return result;
}

bool SavedActionParameter::bindObjectReferences(const Game &game) {
    if (auto reference = std::get_if<SavedObjectReference>(&payload)) {
        return reference->isInvalid() || game.bindSavedObjectReference(*reference);
    }
    if (auto situation = std::get_if<SerializedScriptSituation>(&payload)) {
        return situation->bindObjectReferences(game);
    }
    return true;
}

SavedActionRecord SavedActionRecord::fromGff(const resource::Gff &gff) {
    SavedActionRecord result;
    result.actionId = gff.getUint("ActionId");
    result.groupActionId = static_cast<uint16_t>(gff.getUint("GroupActionId"));
    result.declaredParameterCount = static_cast<uint16_t>(gff.getUint("NumParams"));
    for (const auto &parameter : gff.getList("Paramaters")) {
        result.parameters.push_back(SavedActionParameter::fromGff(*parameter));
    }
    result.unsupportedFields = collectUnsupportedFields(
        gff,
        {"ActionId", "GroupActionId", "NumParams", "Paramaters"});
    return result;
}

SavedExecutionSupport SavedActionRecord::executionSupport() const {
    if (actionId == 30 && !parameters.empty() && std::holds_alternative<float>(parameters.front().payload)) {
        return SavedExecutionSupport::Executable;
    }
    return SavedExecutionSupport::RepresentableButUnsupported;
}

std::shared_ptr<Action> SavedActionRecord::toRuntimeAction(Game &game) const {
    if (executionSupport() != SavedExecutionSupport::Executable) {
        return nullptr;
    }
    return game.newAction<WaitAction>(std::get<float>(parameters.front().payload));
}

bool SavedActionRecord::bindObjectReferences(const Game &game) {
    bool allBound = true;
    for (auto &parameter : parameters) {
        allBound = parameter.bindObjectReferences(game) && allBound;
    }
    return allBound;
}

SavedActionQueue SavedActionQueue::fromGff(const resource::Gff &gff, const std::string &label) {
    SavedActionQueue result;
    for (const auto &action : gff.getList(label)) {
        result.actions.push_back(SavedActionRecord::fromGff(*action));
    }
    return result;
}

SavedEventRecord SavedEventRecord::fromGff(const resource::Gff &gff) {
    SavedEventRecord result;
    result.day = gff.getUint("Day");
    result.time = gff.getUint("Time");
    result.object.id = gff.getUint("ObjectId");
    result.caller.id = gff.getUint("CallerId");
    result.eventId = gff.getUint("EventId");
    result.unsupportedFields = collectUnsupportedFields(
        gff,
        {"Day", "Time", "ObjectId", "CallerId", "EventId", "EventData"});

    auto data = gff.findStruct("EventData");
    if (!data) {
        result.payload = std::monostate {};
        return result;
    }
    switch (static_cast<SavedEventType>(result.eventId)) {
    case SavedEventType::Timed:
        result.payload = SerializedScriptSituation::fromGff(*data);
        break;
    case SavedEventType::RemoveFromArea:
        result.payload = SavedBytePayload {static_cast<uint8_t>(data->getUint("Value"))};
        break;
    case SavedEventType::ApplyEffect:
    case SavedEventType::RemoveEffect:
        result.payload = EffectInstance::fromGff(*data);
        break;
    case SavedEventType::SpellImpact:
        result.payload = savedSpellImpactFromGff(*data);
        break;
    case SavedEventType::PlayAnimation:
    case SavedEventType::ControllerRumble:
        result.payload = SavedIntPayload {data->getInt("Value")};
        break;
    case SavedEventType::SignalEvent:
    case SavedEventType::SummonCreature:
    case SavedEventType::AreaTransition:
        result.payload = savedScriptEventFromGff(*data);
        break;
    case SavedEventType::SpawnBodyBag:
        result.payload = savedBodyBagFromGff(*data);
        break;
    case SavedEventType::BroadcastAoo:
        result.payload = SavedDwordPayload {data->getUint("Value")};
        break;
    default:
        result.payload = unsupportedPayload(*data);
        break;
    }
    return result;
}

SavedExecutionSupport SavedEventRecord::executionSupport() const {
    if (eventId == static_cast<uint32_t>(SavedEventType::ForcedAction)) {
        return SavedExecutionSupport::RetailDiscards;
    }
    return SavedExecutionSupport::RepresentableButUnsupported;
}

bool SavedEventRecord::shouldRestore() const {
    return eventId > 0 &&
           eventId <= static_cast<uint32_t>(SavedEventType::ControllerRumble) &&
           eventId != static_cast<uint32_t>(SavedEventType::ForcedAction);
}

bool SavedEventRecord::bindObjectReferences(const Game &game) {
    bool allBound = true;
    bindReference(game, object, allBound);
    bindReference(game, caller, allBound);
    if (auto situation = std::get_if<SerializedScriptSituation>(&payload)) {
        allBound = situation->bindObjectReferences(game) && allBound;
    } else if (auto effect = std::get_if<EffectInstance>(&payload)) {
        if (effect->creatorId != kSavedRuntimeInvalidObjectId) {
            allBound = game.bindEffectCreator(*effect) && allBound;
        }
    } else if (auto spell = std::get_if<SavedSpellImpact>(&payload)) {
        bindReference(game, spell->caster, allBound);
        bindReference(game, spell->target, allBound);
        bindReference(game, spell->area, allBound);
        bindReference(game, spell->item, allBound);
    } else if (auto event = std::get_if<SavedScriptEvent>(&payload)) {
        for (auto &reference : event->objects) {
            bindReference(game, reference, allBound);
        }
    } else if (auto bodyBag = std::get_if<SavedBodyBag>(&payload)) {
        bindReference(game, bodyBag->object, allBound);
    }
    return allBound;
}

SavedEventQueue SavedEventQueue::fromGff(const resource::Gff &gff, const std::string &label) {
    SavedEventQueue result;
    for (const auto &event : gff.getList(label)) {
        result.events.push_back(SavedEventRecord::fromGff(*event));
    }
    return result;
}

} // namespace game

} // namespace reone
