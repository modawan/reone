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

#include "reone/game/action/attackobject.h"
#include "reone/game/action/usefeat.h"
#include "reone/game/action/followleader.h"
#include "reone/game/action/wait.h"
#include "reone/game/action/playanimation.h"
#include "reone/game/action/movetolocation.h"
#include "reone/game/action/movetoobject.h"
#include "reone/game/action/startconversation.h"
#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/object/area.h"
#include "reone/game/script/savedsituation.h"

namespace reone {

namespace game {

std::shared_ptr<Object> SavedObjectReference::boundObject() const {
    return _object.resolve();
}

namespace {

template <class... Visitors>
struct Overloaded : Visitors... {
    using Visitors::operator()...;
};

template <class... Visitors>
Overloaded(Visitors...) -> Overloaded<Visitors...>;

struct SavedPhysicalAttack {
    SavedObjectReference target;
    FeatType feat {FeatType::Invalid};
};

std::optional<SavedPhysicalAttack> decodePhysicalAttack(
    const SavedActionRecord &record) {
    if (record.actionId != 12 || record.declaredParameterCount != 10 ||
        record.parameters.size() != 10) {
        return std::nullopt;
    }
    static constexpr std::array<uint32_t, 10> types {
        1, 3, 1, 1, 1, 1, 1, 1, 1, 1};
    for (size_t index = 0; index < types.size(); ++index) {
        if (record.parameters[index].type != types[index]) {
            return std::nullopt;
        }
    }
    if (!std::holds_alternative<int32_t>(record.parameters[0].payload) ||
        !std::holds_alternative<SavedObjectReference>(
            record.parameters[1].payload)) {
        return std::nullopt;
    }
    for (size_t index = 2; index < record.parameters.size(); ++index) {
        if (!std::holds_alternative<int32_t>(
                record.parameters[index].payload)) {
            return std::nullopt;
        }
    }
    auto parameter = [&record](size_t index) {
        return std::get<int32_t>(record.parameters[index].payload);
    };
    if ((parameter(0) != 0 && parameter(0) != 1) ||
        parameter(2) != 1 || parameter(3) != 10009 ||
        parameter(4) != 1500 || parameter(5) != 1 ||
        parameter(7) != 0 || parameter(8) != 4 || parameter(9) != 0) {
        return std::nullopt;
    }
    auto feat = static_cast<FeatType>(parameter(6));
    if (feat != FeatType::Invalid && !isPhysicalAttackFeat(feat)) {
        return std::nullopt;
    }
    return SavedPhysicalAttack {
        std::get<SavedObjectReference>(record.parameters[1].payload), feat};
}

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

SavedScriptEvent savedScriptEventFromGff(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
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
        result.objects.push_back(SavedObjectReference::fromSerializedId(
            item->getUint("Parameter"), identityContext));
    }
    return result;
}

SavedSpellImpact savedSpellImpactFromGff(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    SavedSpellImpact result;
    result.spellId = gff.getInt("SpellId");
    result.caster = SavedObjectReference::fromSerializedId(
        gff.getUint("CasterId"), identityContext);
    result.target = SavedObjectReference::fromSerializedId(
        gff.getUint("TargetId"), identityContext);
    result.area = SavedObjectReference::fromSerializedId(
        gff.getUint("AreaId"), identityContext);
    result.item = SavedObjectReference::fromSerializedId(
        gff.getUint("ItemId"), identityContext);
    result.script = gff.getString("Script");
    result.targetPosition = glm::vec3(
        gff.getFloat("TargetPosX"),
        gff.getFloat("TargetPosY"),
        gff.getFloat("TargetPosZ"));
    result.finalForceCost = gff.getInt("FinalForceCost");
    return result;
}

SavedBodyBag savedBodyBagFromGff(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    SavedBodyBag result;
    result.object = SavedObjectReference::fromSerializedId(
        gff.getUint("BodyBagId"), identityContext);
    result.position = glm::vec3(
        gff.getFloat("PositionX"),
        gff.getFloat("PositionY"),
        gff.getFloat("PositionZ"));
    return result;
}

SavedCombatAttack savedCombatAttackFromGff(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    SavedCombatAttack result;
    result.data = SavedStruct::fromGff(gff);
    result.reactionObject = SavedObjectReference::fromSerializedId(
        gff.getUint("ReactObject"), identityContext);
    result.ammoItem = SavedObjectReference::fromSerializedId(
        gff.getUint("AmmoItem"), identityContext);
    return result;
}

SavedFeedbackMessage savedFeedbackMessageFromGff(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    SavedFeedbackMessage result;
    result.data = SavedStruct::fromGff(gff);
    for (const auto &item : gff.getList("ObjectIDList")) {
        result.objects.push_back(SavedObjectReference::fromSerializedId(
            item->getUint("ObjectValue"), identityContext));
    }
    return result;
}

SavedTalentValue savedTalentFromGff(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    SavedTalentValue result;
    result.id = gff.getInt("ID");
    result.type = gff.getInt("Type");
    result.multiClass = static_cast<uint8_t>(gff.getUint("MultiClass"));
    result.item = SavedObjectReference::fromSerializedId(
        gff.getUint("Item"), identityContext);
    // Retail passes "ItemPropertyIndex" to a 16-byte GFF label API; the
    // on-wire label is therefore truncated to this value.
    result.itemPropertyIndex = gff.getInt("ItemPropertyInde");
    result.casterLevel = static_cast<uint8_t>(gff.getUint("CasterLevel"));
    result.metaType = static_cast<uint8_t>(gff.getUint("MetaType"));
    return result;
}

struct SavedMoveToPoint {
    glm::vec3 destination {0.0f};
    SavedObjectReference area;
    SavedObjectReference target;
    bool run {false};
    float range {0.0f};
    float timeout {0.0f};
    bool forcedPending {false};
    bool forcedActive {false};
    uint32_t expiryDay {0};
    uint32_t expiryTime {0};
};

std::optional<SavedMoveToPoint> decodeMoveToPoint(const SavedActionRecord &record) {
    if (record.actionId != 1 || record.declaredParameterCount != 13 ||
        record.parameters.size() != 13) {
        return std::nullopt;
    }
    static constexpr std::array<uint32_t, 13> types {
        2, 2, 2, 3, 3, 1, 2, 1, 2, 2, 2, 1, 1};
    for (size_t i = 0; i < types.size(); ++i) {
        if (record.parameters[i].type != types[i]) {
            return std::nullopt;
        }
    }
    bool payloads =
        std::holds_alternative<float>(record.parameters[0].payload) &&
        std::holds_alternative<float>(record.parameters[1].payload) &&
        std::holds_alternative<float>(record.parameters[2].payload) &&
        std::holds_alternative<SavedObjectReference>(record.parameters[3].payload) &&
        std::holds_alternative<SavedObjectReference>(record.parameters[4].payload) &&
        std::holds_alternative<int32_t>(record.parameters[5].payload) &&
        std::holds_alternative<float>(record.parameters[6].payload) &&
        std::holds_alternative<int32_t>(record.parameters[7].payload) &&
        std::holds_alternative<float>(record.parameters[8].payload) &&
        std::holds_alternative<float>(record.parameters[9].payload) &&
        std::holds_alternative<float>(record.parameters[10].payload) &&
        std::holds_alternative<int32_t>(record.parameters[11].payload) &&
        std::holds_alternative<int32_t>(record.parameters[12].payload);
    if (!payloads) {
        return std::nullopt;
    }

    SavedMoveToPoint result;
    result.destination = glm::vec3(
        std::get<float>(record.parameters[0].payload),
        std::get<float>(record.parameters[1].payload),
        std::get<float>(record.parameters[2].payload));
    result.area = std::get<SavedObjectReference>(record.parameters[3].payload);
    result.target = std::get<SavedObjectReference>(record.parameters[4].payload);
    int32_t flags = std::get<int32_t>(record.parameters[5].payload);
    result.run = (flags & 1) != 0;
    result.range = std::get<float>(record.parameters[6].payload);
    int32_t subtype = std::get<int32_t>(record.parameters[7].payload);
    result.timeout = std::get<float>(record.parameters[8].payload);
    glm::vec2 offset(
        std::get<float>(record.parameters[9].payload),
        std::get<float>(record.parameters[10].payload));
    int32_t day = std::get<int32_t>(record.parameters[11].payload);
    int32_t time = std::get<int32_t>(record.parameters[12].payload);
    bool timed = (flags & 4) != 0;
    bool ordinary = !timed && day == 0 && time == 0 && result.timeout == 0.0f;
    result.forcedPending = timed && result.timeout > 0.0f && day == 0 && time == 0;
    // Only non-negative day/time are required. A time of day larger than the
    // current day length is unnormalized, not invalid: records written before
    // the day length became Mod_MinPerHour-derived used a fixed 24-hour scale,
    // and composing them into absolute milliseconds carries the excess into
    // later days. Snapshots written from here on are always normalized.
    result.forcedActive = !timed && (day != 0 || time != 0) &&
                          result.timeout == 0.0f && day >= 0 && time >= 0;
    bool valid =
        std::isfinite(result.destination.x) && std::isfinite(result.destination.y) &&
        std::isfinite(result.destination.z) && std::isfinite(result.range) &&
        result.range >= 0.0f && std::isfinite(result.timeout) &&
        std::isfinite(offset.x) && std::isfinite(offset.y) &&
        offset == glm::vec2(0.0f) && subtype == 0 && (flags & ~5) == 0 &&
        !result.area.isInvalid() &&
        (ordinary || result.forcedPending || result.forcedActive) &&
        (result.target.isInvalid() ? result.range == 0.0f : !ordinary);
    if (!valid) {
        return std::nullopt;
    }
    result.expiryDay = static_cast<uint32_t>(day);
    result.expiryTime = static_cast<uint32_t>(time);
    return result;
}

bool bindReference(const Game &game, SavedObjectReference &reference, bool &allBound) {
    if (reference.isInvalid()) {
        return true;
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

SerializedScriptSituation SerializedScriptSituation::fromGff(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
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
            value.payload = SavedObjectReference::fromSerializedId(
                item->getUint("Value"), identityContext);
            break;
        case SavedVmStackType::Effect: {
            auto structure = item->findStruct("GameDefinedStrct");
            value.payload = structure ? SavedVmStackPayload(EffectInstance::fromGff(
                                                    *structure, identityContext))
                                      : SavedVmStackPayload(unsupportedPayload(*item));
            break;
        }
        case SavedVmStackType::Event: {
            auto structure = item->findStruct("GameDefinedStrct");
            value.payload = structure ? SavedVmStackPayload(savedScriptEventFromGff(
                                                    *structure, identityContext))
                                      : SavedVmStackPayload(unsupportedPayload(*item));
            break;
        }
        case SavedVmStackType::Location: {
            auto structure = item->findStruct("GameDefinedStrct");
            value.payload = structure ? SavedVmStackPayload(savedLocationFromGff(*structure))
                                      : SavedVmStackPayload(unsupportedPayload(*item));
            break;
        }
        case SavedVmStackType::Talent: {
            auto structure = item->findStruct("GameDefinedStrct");
            value.payload = structure ? SavedVmStackPayload(savedTalentFromGff(
                                                    *structure, identityContext))
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
    if (_runtimeSession && *_runtimeSession != game._runtimeSessionGeneration) {
        _referencesBound = false;
        return false;
    }
    if (!_runtimeSession) {
        _runtimeSession = game._runtimeSessionGeneration;
    }

    bool allBound = true;
    for (auto &entry : stack) {
        std::visit(
            Overloaded {
                [](UnsupportedSavedPayload &) {},
                [](int32_t &) {},
                [](float &) {},
                [](std::string &) {},
                [&game, &allBound](SavedObjectReference &reference) {
                    bindReference(game, reference, allBound);
                },
                [&game, &allBound](EffectInstance &effect) {
                    allBound = game.bindEffectCreator(effect) && allBound;
                },
                [&game, &allBound](SavedScriptEvent &event) {
                    for (auto &reference : event.objects) {
                        bindReference(game, reference, allBound);
                    }
                },
                [](SavedLocationValue &) {},
                [&game, &allBound](SavedTalentValue &talent) {
                    bindReference(game, talent.item, allBound);
                },
            },
            entry.payload);
    }
    _referencesBound = allBound;
    return allBound;
}

bool SerializedScriptSituation::isBoundToCurrentRuntimeSession(const Game &game) const {
    return _referencesBound && _runtimeSession &&
           *_runtimeSession == game._runtimeSessionGeneration;
}

SavedActionParameter SavedActionParameter::fromGff(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
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
        result.payload = SavedObjectReference::fromSerializedId(
            gff.getUint("Value"), identityContext);
        break;
    case SavedActionParameterType::String:
        result.payload = gff.getString("Value");
        break;
    case SavedActionParameterType::ScriptSituation: {
        auto value = gff.findStruct("Value");
        result.payload = value ? SavedActionParameterPayload(
                                     SerializedScriptSituation::fromGff(
                                         *value, identityContext))
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

SavedActionRecord SavedActionRecord::fromGff(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    SavedActionRecord result;
    result.actionId = gff.getUint("ActionId");
    result.groupActionId = static_cast<uint16_t>(gff.getUint("GroupActionId"));
    result.declaredParameterCount = static_cast<uint16_t>(gff.getUint("NumParams"));
    for (const auto &parameter : gff.getList("Paramaters")) {
        result.parameters.push_back(SavedActionParameter::fromGff(
            *parameter, identityContext));
    }
    result.unsupportedFields = collectUnsupportedFields(
        gff,
        {"ActionId", "GroupActionId", "NumParams", "Paramaters"});
    return result;
}

SavedExecutionSupport SavedActionRecord::executionSupport() const {
    if (actionId == 61 && declaredParameterCount == 0 && parameters.empty()) {
        return SavedExecutionSupport::Executable;
    }
    if (actionId == 12) {
        return decodePhysicalAttack(*this)
                   ? SavedExecutionSupport::Executable
                   : SavedExecutionSupport::RepresentableButUnsupported;
    }
    if (actionId == 24 && declaredParameterCount == 3 && parameters.size() == 3 &&
        parameters[0].type == static_cast<uint32_t>(SavedActionParameterType::Object) &&
        parameters[1].type == static_cast<uint32_t>(SavedActionParameterType::String) &&
        parameters[2].type == static_cast<uint32_t>(SavedActionParameterType::Integer) &&
        std::holds_alternative<SavedObjectReference>(parameters[0].payload) &&
        std::holds_alternative<std::string>(parameters[1].payload) &&
        std::holds_alternative<int32_t>(parameters[2].payload)) {
        return SavedExecutionSupport::Executable;
    }
    if (actionId == 1 && declaredParameterCount == 13 && parameters.size() == 13) {
        return decodeMoveToPoint(*this)
                   ? SavedExecutionSupport::Executable
                   : SavedExecutionSupport::RepresentableButUnsupported;
    }
    if (actionId == 17 && declaredParameterCount == 5 && parameters.size() == 5 &&
        parameters[0].type == static_cast<uint32_t>(SavedActionParameterType::Object) &&
        parameters[1].type == static_cast<uint32_t>(SavedActionParameterType::Integer) &&
        parameters[2].type == static_cast<uint32_t>(SavedActionParameterType::Float) &&
        parameters[3].type == static_cast<uint32_t>(SavedActionParameterType::Float) &&
        parameters[4].type == static_cast<uint32_t>(SavedActionParameterType::Integer) &&
        std::holds_alternative<SavedObjectReference>(parameters[0].payload) &&
        std::holds_alternative<int32_t>(parameters[1].payload) &&
        std::holds_alternative<float>(parameters[2].payload) &&
        std::holds_alternative<float>(parameters[3].payload) &&
        std::holds_alternative<int32_t>(parameters[4].payload)) {
        return SavedExecutionSupport::Executable;
    }
    if (actionId == 6 && declaredParameterCount == 5 && parameters.size() == 5 &&
        parameters[0].type == static_cast<uint32_t>(SavedActionParameterType::Object) &&
        parameters[1].type == static_cast<uint32_t>(SavedActionParameterType::Float) &&
        parameters[2].type == static_cast<uint32_t>(SavedActionParameterType::Float) &&
        parameters[3].type == static_cast<uint32_t>(SavedActionParameterType::Integer) &&
        parameters[4].type == static_cast<uint32_t>(SavedActionParameterType::Integer) &&
        std::holds_alternative<SavedObjectReference>(parameters[0].payload) &&
        std::holds_alternative<float>(parameters[1].payload) &&
        std::holds_alternative<float>(parameters[2].payload) &&
        std::holds_alternative<int32_t>(parameters[3].payload) &&
        std::holds_alternative<int32_t>(parameters[4].payload)) {
        return SavedExecutionSupport::Executable;
    }
    if (actionId == 30 && !parameters.empty() && std::holds_alternative<float>(parameters.front().payload)) {
        return SavedExecutionSupport::Executable;
    }
    if (actionId == 37 && declaredParameterCount == 1 &&
        parameters.size() == 1 &&
        parameters.front().type ==
            static_cast<uint32_t>(SavedActionParameterType::ScriptSituation) &&
        std::holds_alternative<SerializedScriptSituation>(parameters.front().payload)) {
        return SavedExecutionSupport::Executable;
    }
    return SavedExecutionSupport::RepresentableButUnsupported;
}

std::shared_ptr<Action> SavedActionRecord::toRuntimeAction(
    Game &game, const SavedScriptSituationImporter *importer) const {
    if (executionSupport() != SavedExecutionSupport::Executable) {
        return nullptr;
    }
    if (actionId == 61) {
        auto action = game.newAction<FollowLeaderAction>();
        action->attachSavedAction(*this);
        return action;
    }
    if (actionId == 30) {
        auto action = game.newAction<WaitAction>(std::get<float>(parameters.front().payload));
        action->attachSavedAction(*this);
        return action;
    }
    if (actionId == 12) {
        auto decoded = decodePhysicalAttack(*this);
        if (!decoded) {
            return nullptr;
        }
        auto target = decoded->target.boundObject();
        if (!target) {
            return nullptr;
        }
        std::shared_ptr<Action> action;
        if (decoded->feat == FeatType::Invalid) {
            action = game.newAction<AttackObjectAction>(std::move(target));
        } else {
            action = game.newAction<UseFeatAction>(
                decoded->feat, std::move(target));
        }
        action->attachSavedAction(*this);
        return action;
    }
    if (actionId == 24) {
        auto target = std::get<SavedObjectReference>(parameters[0].payload).boundObject();
        const auto &dialog = std::get<std::string>(parameters[1].payload);
        int32_t privateConversation = std::get<int32_t>(parameters[2].payload);
        if (!target || dialog.size() > 16 ||
            (privateConversation != 0 && privateConversation != 1)) {
            return nullptr;
        }
        auto action = game.newAction<StartConversationAction>(
            std::move(target), dialog, privateConversation != 0);
        action->attachSavedAction(*this);
        return action;
    }
    if (actionId == 1) {
        auto decoded = decodeMoveToPoint(*this);
        if (!decoded || !std::dynamic_pointer_cast<Area>(decoded->area.boundObject())) {
            return nullptr;
        }
        if (decoded->target.isInvalid()) {
            MoveToLocationAction::ForcedState state;
            state.areaId = decoded->area.id;
            state.active = decoded->forcedActive;
            state.expiryMilliseconds =
                static_cast<uint64_t>(decoded->expiryDay) *
                    game.millisecondsPerWorldDay() +
                decoded->expiryTime;
            auto location = std::make_shared<Location>(decoded->destination, 0.0f);
            auto action = game.newAction<MoveToLocationAction>(
                std::move(location), decoded->run,
                decoded->forcedPending || decoded->forcedActive,
                decoded->forcedPending ? decoded->timeout : 0.0f, state);
            action->attachSavedAction(*this);
            return action;
        }
        auto target = decoded->target.boundObject();
        if (!target) {
            return nullptr;
        }
        MoveToObjectAction::ForcedState state;
        state.destination = decoded->destination;
        state.areaId = decoded->area.id;
        state.active = decoded->forcedActive;
        state.expiryMilliseconds =
            static_cast<uint64_t>(decoded->expiryDay) *
                game.millisecondsPerWorldDay() +
            decoded->expiryTime;
        auto action = game.newAction<MoveToObjectAction>(
            std::move(target), decoded->run, decoded->range,
            decoded->forcedPending ? decoded->timeout : 0.0f, state);
        action->attachSavedAction(*this);
        return action;
    }
    if (actionId == 17) {
        auto target = std::get<SavedObjectReference>(parameters[0].payload).boundObject();
        auto run = std::get<int32_t>(parameters[1].payload);
        auto moveRange = std::get<float>(parameters[2].payload);
        auto useRadius = std::get<float>(parameters[3].payload);
        auto moveFlags = std::get<int32_t>(parameters[4].payload);
        if (!target || (run != 0 && run != 1) ||
            !std::isfinite(moveRange) || moveRange < 0.0f ||
            !std::isfinite(useRadius) || useRadius != moveRange ||
            moveFlags != 1) {
            return nullptr;
        }
        auto action = game.newAction<MoveToObjectAction>(
            std::move(target), run != 0, moveRange);
        action->attachSavedAction(*this);
        return action;
    }
    if (actionId == 6) {
        auto animation = std::get<SavedObjectReference>(parameters[0].payload).id;
        auto speed = std::get<float>(parameters[1].payload);
        auto duration = std::get<float>(parameters[2].payload);
        auto start = std::get<int32_t>(parameters[3].payload);
        auto looping = std::get<int32_t>(parameters[4].payload);
        bool validAnimation = animation <= 41 ||
                              (animation >= 43 && animation <= 46) ||
                              (animation >= 100 && animation <= 110) ||
                              (animation >= 112 && animation <= 124) ||
                              (animation >= 200 && animation <= 213);
        if (!validAnimation ||
            !std::isfinite(speed) || !std::isfinite(duration) ||
            (start != 0 && start != 1) || (looping != 0 && looping != 1)) {
            return nullptr;
        }
        auto action = game.newAction<PlayAnimationAction>(
            static_cast<AnimationType>(animation), speed, duration,
            start == 0, looping != 0);
        action->attachSavedAction(*this);
        return action;
    }
    if (!importer) {
        return nullptr;
    }
    auto result = importer->import(
        std::get<SerializedScriptSituation>(parameters.front().payload));
    if (!result) {
        return nullptr;
    }
    auto action = game.newAction<SavedDoCommandAction>(std::move(result.continuation));
    action->attachSavedAction(*this);
    return action;
}

bool SavedActionRecord::bindObjectReferences(const Game &game) {
    bool allBound = true;
    for (size_t index = 0; index < parameters.size(); ++index) {
        auto &parameter = parameters[index];
        // ActionId 6 reuses the retail type-3 storage slot for an animation
        // identifier rather than an object identity.
        if (actionId == 6 && index == 0) continue;
        allBound = parameter.bindObjectReferences(game) && allBound;
    }
    return allBound;
}

SavedActionQueue SavedActionQueue::fromGff(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    const std::string &label) {
    SavedActionQueue result;
    for (const auto &action : gff.getList(label)) {
        result.actions.push_back(SavedActionRecord::fromGff(
            *action, identityContext));
    }
    return result;
}

SavedEventRecord SavedEventRecord::fromGff(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    SavedEventRecord result;
    result.day = gff.getUint("Day");
    result.time = gff.getUint("Time");
    result.object = SavedObjectReference::fromSerializedId(
        gff.getUint("ObjectId"), identityContext);
    result.caller = SavedObjectReference::fromSerializedId(
        gff.getUint("CallerId"), identityContext);
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
        result.payload = SerializedScriptSituation::fromGff(
            *data, identityContext);
        break;
    case SavedEventType::RemoveFromArea:
        result.payload = SavedBytePayload {static_cast<uint8_t>(data->getUint("Value"))};
        break;
    case SavedEventType::ApplyEffect:
    case SavedEventType::RemoveEffect:
        result.payload = EffectInstance::fromGff(*data, identityContext);
        break;
    case SavedEventType::SpellImpact:
    case SavedEventType::ItemOnHitSpellImpact:
        result.payload = savedSpellImpactFromGff(*data, identityContext);
        break;
    case SavedEventType::PlayAnimation:
    case SavedEventType::ControllerRumble:
        result.payload = SavedIntPayload {data->getInt("Value")};
        break;
    case SavedEventType::SignalEvent:
    case SavedEventType::SummonCreature:
    case SavedEventType::AreaTransition:
        result.payload = savedScriptEventFromGff(*data, identityContext);
        break;
    case SavedEventType::SpawnBodyBag:
        result.payload = savedBodyBagFromGff(*data, identityContext);
        break;
    case SavedEventType::OnMeleeAttacked:
    case SavedEventType::BroadcastSafeProjectile:
        result.payload = savedCombatAttackFromGff(*data, identityContext);
        break;
    case SavedEventType::BroadcastAoo:
        result.payload = SavedBroadcastAoo {
            SavedObjectReference::fromSerializedId(
                data->getUint("Value"), identityContext)};
        break;
    case SavedEventType::FeedbackMessage:
        result.payload = savedFeedbackMessageFromGff(
            *data, identityContext);
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
    if (eventId == static_cast<uint32_t>(SavedEventType::Timed) &&
        std::holds_alternative<SerializedScriptSituation>(payload)) {
        return SavedExecutionSupport::Executable;
    }
    if ((eventId == static_cast<uint32_t>(SavedEventType::ApplyEffect) ||
         eventId == static_cast<uint32_t>(SavedEventType::RemoveEffect)) &&
        std::holds_alternative<EffectInstance>(payload)) {
        return SavedExecutionSupport::Executable;
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
        allBound = game.bindEffectCreator(*effect) && allBound;
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
    } else if (auto broadcast = std::get_if<SavedBroadcastAoo>(&payload)) {
        bindReference(game, broadcast->target, allBound);
    } else if (auto combat = std::get_if<SavedCombatAttack>(&payload)) {
        bindReference(game, combat->reactionObject, allBound);
        bindReference(game, combat->ammoItem, allBound);
    } else if (auto feedback = std::get_if<SavedFeedbackMessage>(&payload)) {
        for (auto &reference : feedback->objects) {
            bindReference(game, reference, allBound);
        }
    }
    return allBound;
}

SavedEventQueue SavedEventQueue::fromGff(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    const std::string &label) {
    SavedEventQueue result;
    for (const auto &event : gff.getList(label)) {
        result.events.push_back(SavedEventRecord::fromGff(
            *event, identityContext));
    }
    return result;
}

} // namespace game

} // namespace reone
