/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "reone/game/modulesnapshot.h"

#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <sstream>

#include "reone/game/action.h"
#include "reone/game/di/services.h"
#include "reone/game/d20/class.h"
#include "reone/game/effect.h"
#include "reone/game/event.h"
#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/object/area.h"
#include "reone/game/object/camera/static.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/encounter.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/object/sound.h"
#include "reone/game/object/store.h"
#include "reone/game/object/trigger.h"
#include "reone/game/object/waypoint.h"
#include "reone/game/party.h"
#include "reone/game/saveprovenance.h"
#include "reone/game/script/savedsituation.h"
#include "reone/resource/format/erfreader.h"
#include "reone/resource/format/erfwriter.h"
#include "reone/resource/format/gffreader.h"
#include "reone/resource/format/gffwriter.h"
#include "reone/resource/provider/gffs.h"
#include "reone/script/format/ncswriter.h"
#include "reone/script/program.h"
#include "reone/system/exception/validation.h"
#include "reone/system/stream/memoryinput.h"
#include "reone/system/stream/memoryoutput.h"

namespace reone {
namespace game {

namespace {

using resource::Gff;
using resource::ResType;

constexpr uint32_t kNcsHeaderSize = 13;
constexpr uint32_t kMillisecondsPerDay = 24u * 60u * 60u * 1000u;

std::shared_ptr<Gff> emptyRecord(uint32_t type) {
    return Gff::Builder().type(type).build();
}

void put(Gff &record, Gff::Field field) {
    replaceSaveField(record, std::move(field));
}

std::shared_ptr<Gff> savedStructToGff(const SavedStruct &saved);

Gff::Field savedFieldToGff(const SavedField &saved) {
    switch (saved.type) {
    case Gff::FieldType::Byte:
        return Gff::Field::newByte(saved.label, static_cast<uint32_t>(std::get<uint64_t>(saved.value)));
    case Gff::FieldType::Char:
        return Gff::Field::newChar(saved.label, static_cast<int32_t>(std::get<int64_t>(saved.value)));
    case Gff::FieldType::Word:
        return Gff::Field::newWord(saved.label, static_cast<uint32_t>(std::get<uint64_t>(saved.value)));
    case Gff::FieldType::Short:
        return Gff::Field::newShort(saved.label, static_cast<int32_t>(std::get<int64_t>(saved.value)));
    case Gff::FieldType::Dword:
        return Gff::Field::newDword(saved.label, static_cast<uint32_t>(std::get<uint64_t>(saved.value)));
    case Gff::FieldType::Int:
        return Gff::Field::newInt(saved.label, static_cast<int32_t>(std::get<int64_t>(saved.value)));
    case Gff::FieldType::Dword64:
        return Gff::Field::newDword64(saved.label, std::get<uint64_t>(saved.value));
    case Gff::FieldType::Int64:
        return Gff::Field::newInt64(saved.label, std::get<int64_t>(saved.value));
    case Gff::FieldType::Float:
        return Gff::Field::newFloat(saved.label, static_cast<float>(std::get<double>(saved.value)));
    case Gff::FieldType::Double:
        return Gff::Field::newDouble(saved.label, std::get<double>(saved.value));
    case Gff::FieldType::CExoString:
        return Gff::Field::newCExoString(saved.label, std::get<std::string>(saved.value));
    case Gff::FieldType::ResRef:
        return Gff::Field::newResRef(saved.label, std::get<std::string>(saved.value));
    case Gff::FieldType::CExoLocString: {
        const auto &value = std::get<SavedLocString>(saved.value);
        return Gff::Field::newCExoLocString(saved.label, value.strRef, value.text);
    }
    case Gff::FieldType::Void:
        return Gff::Field::newVoid(saved.label, std::get<ByteBuffer>(saved.value));
    case Gff::FieldType::Orientation:
        return Gff::Field::newOrientation(saved.label, std::get<glm::quat>(saved.value));
    case Gff::FieldType::Vector:
        return Gff::Field::newVector(saved.label, std::get<glm::vec3>(saved.value));
    case Gff::FieldType::StrRef:
        return Gff::Field::newStrRef(saved.label, static_cast<int32_t>(std::get<int64_t>(saved.value)));
    case Gff::FieldType::Struct:
    case Gff::FieldType::List: {
        std::vector<std::shared_ptr<Gff>> children;
        for (const auto &child : std::get<SavedStructChildren>(saved.value)) {
            children.push_back(savedStructToGff(*child));
        }
        return saved.type == Gff::FieldType::Struct
                   ? Gff::Field::newStruct(saved.label, children.empty() ? emptyRecord(0) : children.front())
                   : Gff::Field::newList(saved.label, std::move(children));
    }
    }
    throw ValidationException("unsupported saved GFF field type");
}

std::shared_ptr<Gff> savedStructToGff(const SavedStruct &saved) {
    auto result = emptyRecord(saved.type);
    for (const auto &field : saved.fields) {
        put(*result, savedFieldToGff(field));
    }
    return result;
}

void putUnsupported(Gff &record, const std::vector<SavedField> &fields) {
    for (const auto &field : fields) {
        put(record, savedFieldToGff(field));
    }
}

std::shared_ptr<Gff> writeLocals(const Object &object) {
    std::array<uint32_t, 5> bits {};
    std::array<uint8_t, 32> bytes {};
    for (const auto &[index, value] : object.localBooleans()) {
        if (value && index >= 0 && index < 160) {
            bits[static_cast<size_t>(index / 32)] |= 1u << (index % 32);
        }
    }
    for (const auto &[index, value] : object.localNumbers()) {
        if (index >= 0 && index < 32) {
            bytes[static_cast<size_t>(index)] = static_cast<uint8_t>(
                std::clamp(value, 0, 255));
        }
    }
    std::vector<std::shared_ptr<Gff>> bitList;
    for (uint32_t value : bits) {
        bitList.push_back(Gff::Builder().type(0)
                              .field(Gff::Field::newDword("Variable", value))
                              .build());
    }
    std::vector<std::shared_ptr<Gff>> byteList;
    for (uint8_t value : bytes) {
        byteList.push_back(Gff::Builder().type(0)
                               .field(Gff::Field::newByte("Variable", value))
                               .build());
    }
    return Gff::Builder().type(0)
        .field(Gff::Field::newList("BitArray", std::move(bitList)))
        .field(Gff::Field::newList("ByteArray", std::move(byteList)))
        .build();
}

std::shared_ptr<Gff> effectToGff(
    const EffectInstance &effect,
    const Game *game = nullptr) {
    uint32_t expiryDay = effect.expiryDay;
    uint32_t expiryTime = effect.expiryTime;
    if (effect.durationType() == DurationType::Temporary && effect.remainingDuration) {
        if (!game || game->minutesPerHour() == 0) {
            throw ValidationException("temporary effect lacks a game-time conversion context");
        }
        uint64_t now = static_cast<uint64_t>(game->worldTimeDay()) * kMillisecondsPerDay +
                       game->worldTimeOfDay();
        uint64_t delta = static_cast<uint64_t>(std::llround(
            std::max(0.0f, *effect.remainingDuration) * 60000.0 /
            game->minutesPerHour()));
        uint64_t expiry = now + delta;
        expiryDay = static_cast<uint32_t>(expiry / kMillisecondsPerDay);
        expiryTime = static_cast<uint32_t>(expiry % kMillisecondsPerDay);
    } else if (!effect.hasSerializableTemporalProvenance()) {
        throw ValidationException("temporary effect lacks save-facing expiry provenance");
    }

    uint16_t retailType = effect.retailType;
    if (retailType == 0 && effect.effect) {
        retailType = static_cast<uint16_t>(effect.effect->type());
    }
    auto result = Gff::Builder().type(2)
        .field(Gff::Field::newDword64("Id", effect.id))
        .field(Gff::Field::newWord("Type", retailType))
        .field(Gff::Field::newWord("SubType", effect.subType))
        .field(Gff::Field::newFloat("Duration", effect.duration))
        .field(Gff::Field::newByte("SkipOnLoad", effect.skipOnLoad))
        .field(Gff::Field::newDword("ExpireDay", expiryDay))
        .field(Gff::Field::newDword("ExpireTime", expiryTime))
        .field(Gff::Field::newDword("CreatorId", effect.creatorId))
        .field(Gff::Field::newDword("SpellId", effect.spellId))
        .field(Gff::Field::newInt("IsExposed", effect.exposed))
        .field(Gff::Field::newInt("NumIntegers", static_cast<int32_t>(effect.integerParameters.size())))
        .build();

    std::vector<std::shared_ptr<Gff>> ints;
    for (int32_t value : effect.integerParameters) {
        ints.push_back(Gff::Builder().type(3).field(Gff::Field::newInt("Value", value)).build());
    }
    std::vector<std::shared_ptr<Gff>> floats;
    for (float value : effect.floatParameters) {
        floats.push_back(Gff::Builder().type(4).field(Gff::Field::newFloat("Value", value)).build());
    }
    std::vector<std::shared_ptr<Gff>> strings;
    for (const auto &value : effect.stringParameters) {
        strings.push_back(Gff::Builder().type(5).field(Gff::Field::newCExoString("Value", value)).build());
    }
    std::vector<std::shared_ptr<Gff>> objects;
    for (uint32_t value : effect.objectParameters) {
        objects.push_back(Gff::Builder().type(6).field(Gff::Field::newDword("Value", value)).build());
    }
    put(*result, Gff::Field::newList("IntList", std::move(ints)));
    put(*result, Gff::Field::newList("FloatList", std::move(floats)));
    put(*result, Gff::Field::newList("StringList", std::move(strings)));
    put(*result, Gff::Field::newList("ObjectList", std::move(objects)));
    return result;
}

std::shared_ptr<Gff> scriptEventToGff(const SavedScriptEvent &event) {
    auto result = Gff::Builder().type(0x4444)
        .field(Gff::Field::newWord("EventType", event.type)).build();
    auto makeIntList = [](const std::vector<int32_t> &values) {
        std::vector<std::shared_ptr<Gff>> result;
        for (auto value : values) result.push_back(Gff::Builder().type(0).field(Gff::Field::newInt("Parameter", value)).build());
        return result;
    };
    std::vector<std::shared_ptr<Gff>> floats, strings, objects;
    for (auto value : event.floats) floats.push_back(Gff::Builder().type(0).field(Gff::Field::newFloat("Parameter", value)).build());
    for (const auto &value : event.strings) strings.push_back(Gff::Builder().type(0).field(Gff::Field::newCExoString("Parameter", value)).build());
    for (const auto &value : event.objects) objects.push_back(Gff::Builder().type(0).field(Gff::Field::newDword("Parameter", value.id)).build());
    put(*result, Gff::Field::newList("IntList", makeIntList(event.integers)));
    put(*result, Gff::Field::newList("FloatList", std::move(floats)));
    put(*result, Gff::Field::newList("StringList", std::move(strings)));
    put(*result, Gff::Field::newList("ObjectList", std::move(objects)));
    return result;
}

std::shared_ptr<Gff> situationToGff(
    const SerializedScriptSituation &situation, const Game *game = nullptr) {
    auto result = Gff::Builder().type(0x7777)
        .field(Gff::Field::newInt("CodeSize", situation.codeSize))
        .field(Gff::Field::newVoid("Code", situation.code))
        .field(Gff::Field::newDword("CRC", situation.crc))
        .field(Gff::Field::newInt("InstructionPtr", situation.instructionPointer))
        .field(Gff::Field::newInt("SecondaryPtr", situation.secondaryPointer))
        .field(Gff::Field::newCExoString("Name", situation.scriptName))
        .field(Gff::Field::newInt("StackSize", situation.stackSize))
        .build();
    putUnsupported(*result, situation.unsupportedFields);

    std::vector<std::shared_ptr<Gff>> values;
    for (const auto &saved : situation.stack) {
        auto value = Gff::Builder().type(0)
            .field(Gff::Field::newChar("Type", saved.type)).build();
        switch (static_cast<SavedVmStackType>(saved.type)) {
        case SavedVmStackType::Integer:
            put(*value, Gff::Field::newInt("Value", std::get<int32_t>(saved.payload)));
            break;
        case SavedVmStackType::Float:
            put(*value, Gff::Field::newFloat("Value", std::get<float>(saved.payload)));
            break;
        case SavedVmStackType::String:
            put(*value, Gff::Field::newCExoString("Value", std::get<std::string>(saved.payload)));
            break;
        case SavedVmStackType::Object:
            put(*value, Gff::Field::newDword("Value", std::get<SavedObjectReference>(saved.payload).id));
            break;
        case SavedVmStackType::Effect:
            put(*value, Gff::Field::newStruct("GameDefinedStrct", effectToGff(std::get<EffectInstance>(saved.payload), game)));
            break;
        case SavedVmStackType::Event:
            put(*value, Gff::Field::newStruct("GameDefinedStrct", scriptEventToGff(std::get<SavedScriptEvent>(saved.payload))));
            break;
        case SavedVmStackType::Location: {
            const auto &location = std::get<SavedLocationValue>(saved.payload);
            auto payload = Gff::Builder().type(0)
                .field(Gff::Field::newFloat("PositionX", location.position.x))
                .field(Gff::Field::newFloat("PositionY", location.position.y))
                .field(Gff::Field::newFloat("PositionZ", location.position.z))
                .field(Gff::Field::newFloat("OrientationX", location.orientation.x))
                .field(Gff::Field::newFloat("OrientationY", location.orientation.y))
                .field(Gff::Field::newFloat("OrientationZ", location.orientation.z)).build();
            put(*value, Gff::Field::newStruct("GameDefinedStrct", std::move(payload)));
            break;
        }
        default:
            if (auto unsupported = std::get_if<UnsupportedSavedPayload>(&saved.payload)) {
                value = savedStructToGff(unsupported->data);
            } else {
                throw ValidationException("unsupported live VM stack value");
            }
            break;
        }
        values.push_back(std::move(value));
    }
    auto stack = Gff::Builder().type(0)
        .field(Gff::Field::newInt("BasePointer", situation.basePointer))
        .field(Gff::Field::newInt("StackPointer", situation.stackPointer))
        .field(Gff::Field::newInt("TotalSize", situation.totalSize))
        .field(Gff::Field::newList("Stack", std::move(values))).build();
    put(*result, Gff::Field::newStruct("Stack", std::move(stack)));
    return result;
}

std::shared_ptr<Gff> actionToGff(
    const SavedActionRecord &action, const Game *game = nullptr) {
    auto result = Gff::Builder().type(0)
        .field(Gff::Field::newDword("ActionId", action.actionId))
        .field(Gff::Field::newWord("GroupActionId", action.groupActionId))
        .field(Gff::Field::newWord("NumParams", static_cast<uint16_t>(action.parameters.size())))
        .build();
    putUnsupported(*result, action.unsupportedFields);
    std::vector<std::shared_ptr<Gff>> parameters;
    for (const auto &parameter : action.parameters) {
        auto record = Gff::Builder().type(1)
            .field(Gff::Field::newDword("Type", parameter.type)).build();
        if (auto value = std::get_if<int32_t>(&parameter.payload)) put(*record, Gff::Field::newInt("Value", *value));
        else if (auto value = std::get_if<float>(&parameter.payload)) put(*record, Gff::Field::newFloat("Value", *value));
        else if (auto value = std::get_if<SavedObjectReference>(&parameter.payload)) put(*record, Gff::Field::newDword("Value", value->id));
        else if (auto value = std::get_if<std::string>(&parameter.payload)) put(*record, Gff::Field::newCExoString("Value", *value));
        else if (auto value = std::get_if<SerializedScriptSituation>(&parameter.payload)) put(*record, Gff::Field::newStruct("Value", situationToGff(*value, game)));
        else if (auto value = std::get_if<UnsupportedSavedPayload>(&parameter.payload)) record = savedStructToGff(value->data);
        parameters.push_back(std::move(record));
    }
    put(*result, Gff::Field::newList("Paramaters", std::move(parameters)));
    return result;
}

std::shared_ptr<Gff> eventToGff(
    const SavedEventRecord &event, const Game *game = nullptr) {
    auto result = Gff::Builder().type(0xabcd)
        .field(Gff::Field::newDword("Day", event.day))
        .field(Gff::Field::newDword("Time", event.time))
        .field(Gff::Field::newDword("ObjectId", event.object.id))
        .field(Gff::Field::newDword("CallerId", event.caller.id))
        .field(Gff::Field::newDword("EventId", event.eventId)).build();
    putUnsupported(*result, event.unsupportedFields);
    std::shared_ptr<Gff> data;
    if (auto value = std::get_if<UnsupportedSavedPayload>(&event.payload)) data = savedStructToGff(value->data);
    else if (auto value = std::get_if<SerializedScriptSituation>(&event.payload)) data = situationToGff(*value, game);
    else if (auto value = std::get_if<EffectInstance>(&event.payload)) data = effectToGff(*value, game);
    else if (auto value = std::get_if<SavedBytePayload>(&event.payload)) data = Gff::Builder().type(0x9999).field(Gff::Field::newByte("Value", value->value)).build();
    else if (auto value = std::get_if<SavedIntPayload>(&event.payload)) data = Gff::Builder().type(0x3333).field(Gff::Field::newInt("Value", value->value)).build();
    else if (auto value = std::get_if<SavedDwordPayload>(&event.payload)) data = Gff::Builder().type(0x3333).field(Gff::Field::newDword("Value", value->value)).build();
    else if (auto value = std::get_if<SavedScriptEvent>(&event.payload)) data = scriptEventToGff(*value);
    else if (auto value = std::get_if<SavedBodyBag>(&event.payload)) data = Gff::Builder().type(0x5555)
        .field(Gff::Field::newDword("BodyBagId", value->object.id))
        .field(Gff::Field::newFloat("PositionX", value->position.x))
        .field(Gff::Field::newFloat("PositionY", value->position.y))
        .field(Gff::Field::newFloat("PositionZ", value->position.z)).build();
    else if (auto value = std::get_if<SavedSpellImpact>(&event.payload)) data = Gff::Builder().type(0x6666)
        .field(Gff::Field::newInt("SpellId", value->spellId))
        .field(Gff::Field::newDword("CasterId", value->caster.id))
        .field(Gff::Field::newDword("TargetId", value->target.id))
        .field(Gff::Field::newDword("AreaId", value->area.id))
        .field(Gff::Field::newDword("ItemId", value->item.id))
        .field(Gff::Field::newCExoString("Script", value->script))
        .field(Gff::Field::newFloat("TargetPosX", value->targetPosition.x))
        .field(Gff::Field::newFloat("TargetPosY", value->targetPosition.y))
        .field(Gff::Field::newFloat("TargetPosZ", value->targetPosition.z))
        .field(Gff::Field::newInt("FinalForceCost", value->finalForceCost)).build();
    if (data) put(*result, Gff::Field::newStruct("EventData", std::move(data)));
    return result;
}

float bearing(const Object &object) {
    return object.getFacing();
}

void putTransform(Gff &record, const Object &object, bool xyzLabels = false) {
    const auto &p = object.position();
    put(record, Gff::Field::newFloat(xyzLabels ? "X" : "XPosition", p.x));
    put(record, Gff::Field::newFloat(xyzLabels ? "Y" : "YPosition", p.y));
    put(record, Gff::Field::newFloat(xyzLabels ? "Z" : "ZPosition", p.z));
    if (xyzLabels) put(record, Gff::Field::newFloat("Bearing", bearing(object)));
}

} // namespace

void ModuleObjectIdContext::reserveWorldId(uint32_t id) {
    if (id >= kSavedRuntimeInvalidObjectId) {
        throw ValidationException("ordinary saved object uses a reserved ID");
    }
    if (!_used.insert(id).second) {
        throw ValidationException("saved object ID collides in module namespace");
    }
}

void ModuleObjectIdContext::reservePartyId(uint32_t id) {
    if (id < kSavedRuntimeInvalidObjectId) {
        reserveWorldId(id);
        return;
    }
    if (id == kSavedRuntimeInvalidObjectId || id > 0x7fffffffu ||
        !_reservedPartyIds.insert(id).second) {
        throw ValidationException("saved party creature uses an invalid or duplicate reserved ID");
    }
}

void ModuleObjectIdContext::retainItem(const Item &item) {
    if (_itemIds.count(&item) != 0) return;
    constexpr uint32_t invalid = std::numeric_limits<uint32_t>::max();
    auto preferred = item.originalOwnerLocalObjectId();
    if (!preferred || *preferred >= kSavedRuntimeInvalidObjectId ||
        *preferred == invalid) {
        return;
    }
    if (!_used.insert(*preferred).second) {
        throw ValidationException("retained module item ID collides in saved object namespace");
    }
    _itemIds.emplace(&item, *preferred);
}

void ModuleObjectIdContext::allocateItem(const Item &item) {
    if (_itemIds.count(&item) != 0) return;
    for (uint32_t candidate = 2; candidate < kSavedRuntimeInvalidObjectId; ++candidate) {
        if (_used.insert(candidate).second) {
            _itemIds.emplace(&item, candidate);
            return;
        }
    }
    throw ValidationException("saved object ID namespace is exhausted");
}

uint32_t ModuleObjectIdContext::itemId(const Item &item) const {
    auto found = _itemIds.find(&item);
    if (found == _itemIds.end()) {
        throw ValidationException("module item has no serialized object ID");
    }
    return found->second;
}

uint32_t ModuleObjectIdContext::nextId(uint32_t retainedCursor) const {
    uint32_t next = 2;
    if (!_used.empty()) {
        if (*_used.rbegin() >= kSavedRuntimeInvalidObjectId - 1) {
            throw ValidationException("saved object ID namespace is exhausted");
        }
        next = std::max(next, *_used.rbegin() + 1);
    }
    if (retainedCursor >= 2 && retainedCursor < kSavedRuntimeInvalidObjectId) {
        next = std::max(next, retainedCursor);
    }
    if (next >= kSavedRuntimeInvalidObjectId) {
        throw ValidationException("saved object ID namespace is exhausted");
    }
    return next;
}

ModuleObjectIdContext ModuleSnapshotBuilder::buildObjectIdContext(
    const Module &module, const Area &area) const {
    ModuleObjectIdContext ids;
    std::vector<const Item *> items;
    std::set<const Item *> seenItems;
    auto addItem = [&](const std::shared_ptr<Item> &item) {
        if (item && seenItems.insert(item.get()).second) items.push_back(item.get());
    };
    auto addCreatureItems = [&](const Creature &creature, bool includeInventory) {
        std::set<const Item *> equipped;
        for (const auto &[slot, item] : creature._equipment) {
            (void)slot;
            if (!item) continue;
            equipped.insert(item.get());
            addItem(item);
        }
        if (!includeInventory) return;
        for (const auto &item : creature._items) {
            if (item && equipped.count(item.get()) == 0) addItem(item);
        }
    };

    ids.reserveWorldId(area._id);
    for (const auto &object : area._objects) {
        if (!object || area._objectsToDestroy.count(object->id()) != 0 ||
            _game._party.isMember(*object)) continue;
        ids.reserveWorldId(object->id());
        switch (object->type()) {
        case ObjectType::Creature:
            addCreatureItems(*static_cast<const Creature *>(object.get()), true);
            break;
        case ObjectType::Placeable:
            for (const auto &item : static_cast<const Placeable *>(object.get())->_items) addItem(item);
            break;
        case ObjectType::Store:
            for (const auto &item : static_cast<const Store *>(object.get())->_items) addItem(item);
            break;
        default:
            break;
        }
    }
    auto modulePlayer = _game._party.player();
    if (!modulePlayer) throw ValidationException("module has no controlled player creature");
    ids.reservePartyId(modulePlayer->id());
    addCreatureItems(*modulePlayer, false);
    for (const auto &creature : module._limboCreatures) {
        if (!creature) continue;
        ids.reservePartyId(creature->id());
        addCreatureItems(*creature, true);
    }
    for (const Item *item : items) ids.retainItem(*item);
    for (const Item *item : items) ids.allocateItem(*item);
    return ids;
}

std::optional<SerializedScriptSituation> exportScriptSituation(
    const SavedScriptContinuation &continuation, std::string &error) {
    if (auto original = continuation.originalSavedSituation()) {
        return *original;
    }
    const auto &state = continuation.executionState();
    if (!state.program || state.insOffset < kNcsHeaderSize) {
        error = "live continuation has no serializable program/instruction pointer";
        return std::nullopt;
    }
    ByteBuffer ncs;
    try {
        auto output = std::make_shared<MemoryOutputStream>(ncs);
        script::NcsWriter writer(*state.program);
        writer.save(output);
    } catch (const std::exception &ex) {
        error = ex.what();
        return std::nullopt;
    }
    if (ncs.size() < kNcsHeaderSize) {
        error = "NCS writer returned a truncated program";
        return std::nullopt;
    }

    SerializedScriptSituation result;
    result.code.assign(ncs.begin() + kNcsHeaderSize, ncs.end());
    result.codeSize = static_cast<int32_t>(result.code.size());
    result.instructionPointer = static_cast<int32_t>(state.insOffset - kNcsHeaderSize);
    result.scriptName = continuation.scriptName();
    result.crc = 0;
    result.secondaryPointer = 0;

    auto convert = [&error](const script::Variable &value) -> std::optional<SavedVmStackValue> {
        SavedVmStackValue saved;
        switch (value.type) {
        case script::VariableType::Int:
            saved.type = static_cast<int8_t>(SavedVmStackType::Integer); saved.payload = value.intValue; break;
        case script::VariableType::Float:
            saved.type = static_cast<int8_t>(SavedVmStackType::Float); saved.payload = value.floatValue; break;
        case script::VariableType::String:
            saved.type = static_cast<int8_t>(SavedVmStackType::String); saved.payload = value.strValue; break;
        case script::VariableType::Object:
            saved.type = static_cast<int8_t>(SavedVmStackType::Object); saved.payload = SavedObjectReference(value.objectId); break;
        case script::VariableType::Effect: {
            auto effect = std::dynamic_pointer_cast<SavedEffectValue>(value.engineType);
            if (!effect) { error = "live VM effect lacks a save-facing EffectInstance"; return std::nullopt; }
            saved.type = static_cast<int8_t>(SavedVmStackType::Effect); saved.payload = effect->instance(); break;
        }
        case script::VariableType::Event: {
            auto event = std::dynamic_pointer_cast<Event>(value.engineType);
            if (!event) { error = "live VM event has an unsupported engine value"; return std::nullopt; }
            SavedScriptEvent payload;
            payload.type = static_cast<uint16_t>(event->number());
            payload.integers = event->integers(); payload.floats = event->floats(); payload.strings = event->strings();
            for (auto id : event->objects()) payload.objects.emplace_back(id);
            saved.type = static_cast<int8_t>(SavedVmStackType::Event); saved.payload = std::move(payload); break;
        }
        case script::VariableType::Location: {
            auto location = std::dynamic_pointer_cast<Location>(value.engineType);
            if (!location) { error = "live VM location has an unsupported engine value"; return std::nullopt; }
            saved.type = static_cast<int8_t>(SavedVmStackType::Location);
            saved.payload = SavedLocationValue {location->position(), location->saveOrientation()}; break;
        }
        default:
            error = "live VM stack contains a value without a retail save representation";
            return std::nullopt;
        }
        return saved;
    };
    for (const auto &value : state.globals) {
        auto saved = convert(value); if (!saved) return std::nullopt; result.stack.push_back(std::move(*saved));
    }
    result.basePointer = static_cast<int32_t>(result.stack.size());
    for (const auto &value : state.locals) {
        auto saved = convert(value); if (!saved) return std::nullopt; result.stack.push_back(std::move(*saved));
    }
    result.stackPointer = static_cast<int32_t>(result.stack.size());
    result.stackSize = result.stackPointer;
    result.totalSize = result.stackPointer;
    return result;
}

std::shared_ptr<Gff> ModuleSnapshotBuilder::objectBase(
    const Object &object, ResType templateType, uint32_t structType) const {
    std::shared_ptr<Gff> result;
    if (object._saveRecordProvenance && object._saveRecordProvenance->shadow) {
        result = object._saveRecordProvenance->shadow.cloneForMerge();
    } else if (!object._blueprintResRef.empty()) {
        if (auto blueprint = _game._services.resource.gffs.get(
                object._blueprintResRef, templateType)) {
            result = blueprint->deepCopy();
        }
    }
    if (!result) result = emptyRecord(structType);
    result->setType(structType);
    return result;
}

void ModuleSnapshotBuilder::writeObjectState(
    Gff &record, const Object &object) const {
    put(record, Gff::Field::newDword("ObjectId", object._id));
    put(record, Gff::Field::newCExoString("Tag", object._tag));
    if (!object._blueprintResRef.empty()) {
        put(record, Gff::Field::newResRef("TemplateResRef", object._blueprintResRef));
    }
    put(record, Gff::Field::newByte("Commandable", object._commandable));
    put(record, Gff::Field::newByte("Min1HP", object._minOneHP));
    put(record, Gff::Field::newByte("Plot", object._plot));
    put(record, Gff::Field::newStruct("SWVarTable", writeLocals(object)));

    std::vector<std::shared_ptr<Gff>> effects;
    for (auto effect : object.saveEffectSnapshot()) {
        effects.push_back(effectToGff(
            normalizeEffectReferences(std::move(effect)), &_game));
    }
    put(record, Gff::Field::newList("EffectList", std::move(effects)));

    std::vector<std::shared_ptr<Gff>> actions;
    for (auto action : object.saveActionSnapshot()) {
        normalizeActionReferences(action);
        actions.push_back(actionToGff(action, &_game));
    }
    put(record, Gff::Field::newList("ActionList", std::move(actions)));

    static const std::array<std::string, 9> references {
        "AreaId", "CreatorId", "LastAttacker", "LastDamager",
        "LastHostileActor", "LastPerceived", "MasterID", "OwnerId", "TargetId"};
    for (const auto &field : references) {
        if (object._savedReferenceIds.count(field) != 0 ||
            object._savedReferences.count(field) != 0) {
            auto bound = object.savedReference(field);
            put(record, Gff::Field::newDword(
                field,
                bound && isSerializedWorldObject(*bound)
                    ? bound->id()
                    : kSavedRuntimeInvalidObjectId));
        }
    }
}

bool ModuleSnapshotBuilder::isSerializedWorldObject(
    const Object &object) const {
    if (!_game._module || !_game._module->_area) return false;
    const auto &area = *_game._module->_area;
    if (&object == &area) return true;
    for (const auto &entry : area._objects) {
        if (entry.get() == &object &&
            area._objectsToDestroy.count(object.id()) == 0) return true;
    }
    for (const auto &entry : _game._module->_limboCreatures) {
        if (entry.get() == &object) return true;
    }
    return _game._party.player().get() == &object ||
           _game._party.actualPlayer().get() == &object;
}

uint32_t ModuleSnapshotBuilder::serializedReferenceId(
    const SavedObjectReference &reference) const {
    if (reference.isInvalid()) return kSavedRuntimeInvalidObjectId;
    auto bound = reference.boundObject();
    return bound && isSerializedWorldObject(*bound)
               ? bound->id()
               : kSavedRuntimeInvalidObjectId;
}

EffectInstance ModuleSnapshotBuilder::normalizeEffectReferences(
    EffectInstance effect) const {
    auto normalizeId = [this](uint32_t id) {
        if (id == kSavedRuntimeInvalidObjectId ||
            id == kSavedEffectInvalidObjectId) return id;
        auto found = _game._objectById.find(id);
        return found != _game._objectById.end() &&
                       isSerializedWorldObject(*found->second)
                   ? found->second->id()
                   : kSavedRuntimeInvalidObjectId;
    };
    if (auto creator = effect.boundCreator()) {
        effect.creatorId = isSerializedWorldObject(*creator)
                               ? creator->id()
                               : kSavedRuntimeInvalidObjectId;
    } else {
        effect.creatorId = normalizeId(effect.creatorId);
    }
    for (auto &id : effect.objectParameters) id = normalizeId(id);
    return effect;
}

void ModuleSnapshotBuilder::normalizeSituationReferences(
    SerializedScriptSituation &situation) const {
    for (auto &value : situation.stack) {
        if (auto reference = std::get_if<SavedObjectReference>(&value.payload)) {
            reference->id = serializedReferenceId(*reference);
        } else if (auto effect = std::get_if<EffectInstance>(&value.payload)) {
            *effect = normalizeEffectReferences(std::move(*effect));
        } else if (auto event = std::get_if<SavedScriptEvent>(&value.payload)) {
            for (auto &reference : event->objects) {
                reference.id = serializedReferenceId(reference);
            }
        }
    }
}

void ModuleSnapshotBuilder::normalizeActionReferences(
    SavedActionRecord &action) const {
    for (auto &parameter : action.parameters) {
        if (auto reference = std::get_if<SavedObjectReference>(&parameter.payload)) {
            reference->id = serializedReferenceId(*reference);
        } else if (auto situation = std::get_if<SerializedScriptSituation>(&parameter.payload)) {
            normalizeSituationReferences(*situation);
        }
    }
}

void ModuleSnapshotBuilder::normalizeEventReferences(
    SavedEventRecord &event) const {
    event.object.id = serializedReferenceId(event.object);
    event.caller.id = serializedReferenceId(event.caller);
    if (auto situation = std::get_if<SerializedScriptSituation>(&event.payload)) {
        normalizeSituationReferences(*situation);
    } else if (auto effect = std::get_if<EffectInstance>(&event.payload)) {
        *effect = normalizeEffectReferences(std::move(*effect));
    } else if (auto spell = std::get_if<SavedSpellImpact>(&event.payload)) {
        spell->caster.id = serializedReferenceId(spell->caster);
        spell->target.id = serializedReferenceId(spell->target);
        spell->area.id = serializedReferenceId(spell->area);
        spell->item.id = serializedReferenceId(spell->item);
    } else if (auto scriptEvent = std::get_if<SavedScriptEvent>(&event.payload)) {
        for (auto &reference : scriptEvent->objects) {
            reference.id = serializedReferenceId(reference);
        }
    } else if (auto bag = std::get_if<SavedBodyBag>(&event.payload)) {
        bag->object.id = serializedReferenceId(bag->object);
    }
}

std::shared_ptr<Gff> ModuleSnapshotBuilder::writeItem(
    const Item &item, uint32_t structType,
    std::optional<uint32_t> serializedId) const {
    auto result = objectBase(item, ResType::Uti, structType);
    writeObjectState(*result, item);
    if (serializedId) {
        put(*result, Gff::Field::newDword("ObjectId", *serializedId));
    } else {
        removeSaveField(*result, "ObjectId");
    }
    put(*result, Gff::Field::newInt("BaseItem", item._baseItem));
    put(*result, Gff::Field::newByte("Charges", item._charges));
    put(*result, Gff::Field::newDword("Cost", item._cost));
    put(*result, Gff::Field::newDword("AddCost", item._addCost));
    put(*result, Gff::Field::newByte("Stolen", item._stolen));
    put(*result, Gff::Field::newWord("StackSize", item._stackSize));
    put(*result, Gff::Field::newByte("Identified", item._identified));
    put(*result, Gff::Field::newByte("ModelVariation", item._modelVariation));
    put(*result, Gff::Field::newByte("BodyVariation", item._bodyVariation));
    put(*result, Gff::Field::newByte("TextureVar", item._textureVariation));
    put(*result, Gff::Field::newByte("Dropable", item._dropable));
    std::vector<std::shared_ptr<Gff>> properties;
    for (const auto &property : item._properties) {
        properties.push_back(Gff::Builder().type(0)
            .field(Gff::Field::newByte("ChanceAppear", property.chanceAppear))
            .field(Gff::Field::newByte("CostTable", property.costTable))
            .field(Gff::Field::newWord("CostValue", property.costValue))
            .field(Gff::Field::newByte("Param1", property.paramTable))
            .field(Gff::Field::newByte("Param1Value", property.paramValue))
            .field(Gff::Field::newWord("PropertyName", property.propertyName))
            .field(Gff::Field::newWord("Subtype", property.subtype))
            .field(Gff::Field::newByte("UpgradeType", property.upgradeType)).build());
    }
    put(*result, Gff::Field::newList("PropertiesList", std::move(properties)));
    return result;
}

std::shared_ptr<Gff> ModuleSnapshotBuilder::writeCreature(
    const Creature &creature, uint32_t structType,
    std::optional<uint32_t> serializedId) const {
    ModuleObjectIdContext ids;
    std::set<const Item *> seen;
    std::vector<const Item *> items;
    auto addItem = [&](const std::shared_ptr<Item> &item) {
        if (item && seen.insert(item.get()).second) items.push_back(item.get());
    };
    for (const auto &[slot, item] : creature._equipment) {
        (void)slot;
        addItem(item);
    }
    for (const auto &item : creature._items) addItem(item);
    for (const Item *item : items) ids.retainItem(*item);
    for (const Item *item : items) ids.allocateItem(*item);
    return writeCreature(creature, structType, serializedId, ids, true);
}
std::shared_ptr<Gff> ModuleSnapshotBuilder::writeCreature(
    const Creature &creature, uint32_t structType,
    std::optional<uint32_t> serializedId,
    const ModuleObjectIdContext &ids,
    bool includeInventory) const {
    auto result = objectBase(creature, ResType::Utc, structType);
    writeObjectState(*result, creature);
    if (serializedId) {
        put(*result, Gff::Field::newDword("ObjectId", *serializedId));
    } else {
        removeSaveField(*result, "ObjectId");
    }
    putTransform(*result, creature);
    const auto direction = glm::vec3(
        -std::sin(creature.getFacing()), std::cos(creature.getFacing()), 0.0f);
    put(*result, Gff::Field::newFloat("XOrientation", direction.x));
    put(*result, Gff::Field::newFloat("YOrientation", direction.y));
    put(*result, Gff::Field::newFloat("ZOrientation", direction.z));
    put(*result, Gff::Field::newByte("IsPC", creature._isPC));
    put(*result, Gff::Field::newWord("FactionID", static_cast<uint16_t>(creature._faction)));
    put(*result, Gff::Field::newWord("Appearance_Type", creature._appearance));
    put(*result, Gff::Field::newByte("Gender", static_cast<uint8_t>(creature._gender)));
    put(*result, Gff::Field::newShort("HitPoints", creature._hitPoints));
    put(*result, Gff::Field::newShort("MaxHitPoints", creature._maxHitPoints));
    put(*result, Gff::Field::newShort("CurrentHitPoints", creature._currentHitPoints));
    put(*result, Gff::Field::newByte("Dead", creature._dead));
    put(*result, Gff::Field::newShort("ForcePoints", creature._forcePoints));
    put(*result, Gff::Field::newShort("CurrentForce", creature._currentForce));
    put(*result, Gff::Field::newDword("Experience", creature._xp));
    put(*result, Gff::Field::newByte("GoodEvil", creature._goodEvil));
    put(*result, Gff::Field::newByte("Race", static_cast<uint8_t>(creature._race)));
    put(*result, Gff::Field::newByte("SubraceIndex", static_cast<uint8_t>(creature._subrace)));
    put(*result, Gff::Field::newByte("Disarmable", creature._disarmable));
    put(*result, Gff::Field::newByte("NoPermDeath", creature._noPermDeath));
    put(*result, Gff::Field::newByte("NotReorienting", creature._notReorienting));
    put(*result, Gff::Field::newByte("BodyVariation", creature._bodyVariation));
    put(*result, Gff::Field::newByte("TextureVar", creature._textureVar));
    put(*result, Gff::Field::newByte("PartyInteract", creature._partyInteract));
    put(*result, Gff::Field::newByte(
        "MovementRate", static_cast<uint8_t>(std::clamp(creature._walkRate, 0, 255))));
    put(*result, Gff::Field::newByte("Listening", creature._isListening));
    put(*result, Gff::Field::newByte("NaturalAC", creature._naturalAC));
    put(*result, Gff::Field::newShort("refbonus", creature._refBonus));
    put(*result, Gff::Field::newShort("willbonus", creature._willBonus));
    put(*result, Gff::Field::newShort("fortbonus", creature._fortBonus));
    put(*result, Gff::Field::newFloat("ChallengeRating", creature._challengeRating));
    put(*result, Gff::Field::newWord("SoundSetFile", creature._soundSetId));
    put(*result, Gff::Field::newByte("BodyBag", creature._bodyBagId));
    put(*result, Gff::Field::newByte("PerceptionRange", creature._perceptionId));

    put(*result, Gff::Field::newByte("Str", creature._attributes.strength()));
    put(*result, Gff::Field::newByte("Dex", creature._attributes.dexterity()));
    put(*result, Gff::Field::newByte("Con", creature._attributes.constitution()));
    put(*result, Gff::Field::newByte("Int", creature._attributes.intelligence()));
    put(*result, Gff::Field::newByte("Wis", creature._attributes.wisdom()));
    put(*result, Gff::Field::newByte("Cha", creature._attributes.charisma()));
    auto oldSkills = result->getList("SkillList");
    std::vector<std::shared_ptr<Gff>> skills;
    for (int index = 0; index <= static_cast<int>(SkillType::TreatInjury); ++index) {
        auto skill = static_cast<size_t>(index) < oldSkills.size()
                         ? oldSkills[static_cast<size_t>(index)]->deepCopy()
                         : emptyRecord(0);
        put(*skill, Gff::Field::newByte(
            "Rank", creature._attributes.getSkillRank(
                        static_cast<SkillType>(index))));
        skills.push_back(std::move(skill));
    }
    put(*result, Gff::Field::newList("SkillList", std::move(skills)));
    std::map<uint16_t, std::shared_ptr<Gff>> oldFeats;
    for (const auto &feat : result->getList("FeatList")) {
        oldFeats.emplace(
            static_cast<uint16_t>(feat->getUint("Feat")), feat->deepCopy());
    }
    std::vector<std::shared_ptr<Gff>> feats;
    for (auto feat : creature._attributes.feats()) {
        auto type = static_cast<uint16_t>(feat);
        auto found = oldFeats.find(type);
        auto featRecord = found != oldFeats.end()
                              ? found->second
                              : emptyRecord(1);
        put(*featRecord, Gff::Field::newWord("Feat", type));
        feats.push_back(std::move(featRecord));
    }
    put(*result, Gff::Field::newList("FeatList", std::move(feats)));
    std::map<int32_t, std::shared_ptr<Gff>> oldClasses;
    for (const auto &clazz : result->getList("ClassList")) {
        oldClasses.emplace(clazz->getInt("Class"), clazz->deepCopy());
    }
    std::vector<std::shared_ptr<Gff>> classes;
    bool firstClass = true;
    for (const auto &[clazz, level] : creature._attributes.classLevels()) {
        if (!clazz) continue;
        auto classType = static_cast<int32_t>(clazz->type());
        auto found = oldClasses.find(classType);
        auto classRecord = found != oldClasses.end()
                               ? found->second
                               : emptyRecord(2);
        put(*classRecord, Gff::Field::newInt("Class", classType));
        put(*classRecord, Gff::Field::newShort("ClassLevel", level));
        std::vector<std::shared_ptr<Gff>> spells;
        if (firstClass) {
            for (auto spell : creature._attributes.spells()) {
                spells.push_back(Gff::Builder().type(3)
                    .field(Gff::Field::newWord(
                        "Spell", static_cast<uint16_t>(spell))).build());
            }
        }
        put(*classRecord, Gff::Field::newList("KnownList0", std::move(spells)));
        classes.push_back(std::move(classRecord));
        firstClass = false;
    }
    put(*result, Gff::Field::newList("ClassList", std::move(classes)));

    put(*result, Gff::Field::newResRef("ScriptHeartbeat", creature._onHeartbeat));
    put(*result, Gff::Field::newResRef("ScriptUserDefine", creature._onUserDefined));
    put(*result, Gff::Field::newResRef("ScriptOnNotice", creature._onNotice));
    put(*result, Gff::Field::newResRef("ScriptSpellAt", creature._onSpellAt));
    put(*result, Gff::Field::newResRef("ScriptAttacked", creature._onAttacked));
    put(*result, Gff::Field::newResRef("ScriptDamaged", creature._onDamaged));
    put(*result, Gff::Field::newResRef("ScriptDisturbed", creature._onDisturbed));
    put(*result, Gff::Field::newResRef("ScriptEndRound", creature._onEndRound));
    put(*result, Gff::Field::newResRef("ScriptEndDialogu", creature._onEndDialogue));
    put(*result, Gff::Field::newResRef("ScriptDialogue", creature._onDialogue));
    put(*result, Gff::Field::newResRef("ScriptSpawn", creature._onSpawn));
    put(*result, Gff::Field::newResRef("ScriptDeath", creature._onDeath));
    put(*result, Gff::Field::newResRef("ScriptOnBlocked", creature._onBlocked));

    std::set<const Item *> equipped;
    std::vector<std::shared_ptr<Gff>> equipList;
    for (const auto &[slot, item] : creature._equipment) {
        if (!item) continue;
        equipped.insert(item.get());
        auto saved = writeItem(*item, 1u << slot, ids.itemId(*item));
        saved->setType(1u << slot);
        equipList.push_back(std::move(saved));
    }
    std::vector<std::shared_ptr<Gff>> inventory;
    if (includeInventory) {
        for (const auto &item : creature._items) {
            if (!item || equipped.count(item.get()) != 0) continue;
            inventory.push_back(writeItem(*item, 0, ids.itemId(*item)));
        }
    }
    put(*result, Gff::Field::newList("Equip_ItemList", std::move(equipList)));
    put(*result, Gff::Field::newList("ItemList", std::move(inventory)));

    std::map<uint32_t, std::shared_ptr<Gff>> oldPerceptions;
    for (const auto &perception : result->getList("PerceptionList")) {
        oldPerceptions.emplace(
            perception->getUint("ObjectId", kSavedRuntimeInvalidObjectId),
            perception->deepCopy());
    }
    std::set<uint32_t> perceived;
    perceived.insert(creature._perception.seen.begin(), creature._perception.seen.end());
    perceived.insert(creature._perception.heard.begin(), creature._perception.heard.end());
    std::vector<std::shared_ptr<Gff>> perceptions;
    for (uint32_t id : perceived) {
        auto objectIt = _game._objectById.find(id);
        if (objectIt == _game._objectById.end() ||
            !isSerializedWorldObject(*objectIt->second)) continue;
        auto found = oldPerceptions.find(id);
        auto perception = found != oldPerceptions.end()
                              ? found->second
                              : emptyRecord(0);
        uint8_t data = static_cast<uint8_t>(
            perception->getUint("PerceptionData") & ~0x3u);
        if (creature._perception.seen.count(id) != 0) data |= 0x1;
        if (creature._perception.heard.count(id) != 0) data |= 0x2;
        put(*perception, Gff::Field::newDword("ObjectId", id));
        put(*perception, Gff::Field::newByte("PerceptionData", data));
        perceptions.push_back(std::move(perception));
    }
    put(*result, Gff::Field::newList("PerceptionList", std::move(perceptions)));
    return result;
}

std::shared_ptr<Gff> ModuleSnapshotBuilder::writeDoor(const Door &door) const {
    auto result = objectBase(door, ResType::Utd, 8);
    writeObjectState(*result, door);
    putTransform(*result, door, true);
    put(*result, Gff::Field::newByte("OpenState", static_cast<uint8_t>(door._state)));
    put(*result, Gff::Field::newByte("Locked", door._locked));
    put(*result, Gff::Field::newByte("Lockable", door._lockable));
    put(*result, Gff::Field::newShort("HP", door._hitPoints));
    put(*result, Gff::Field::newShort("CurrentHP", door._currentHitPoints));
    put(*result, Gff::Field::newByte("Dead", door._dead));
    put(*result, Gff::Field::newByte("TrapFlag", door._trapFlag));
    put(*result, Gff::Field::newByte("TrapType", door._trapType));
    put(*result, Gff::Field::newByte("TrapDisarmable", door._trapDisarmable));
    put(*result, Gff::Field::newByte("TrapDetectable", door._trapDetectable));
    put(*result, Gff::Field::newByte("DisarmDC", door._disarmDC));
    put(*result, Gff::Field::newByte("TrapDetectDC", door._trapDetectDC));
    put(*result, Gff::Field::newByte("TrapOneShot", door._trapOneShot));
    put(*result, Gff::Field::newByte("KeyRequired", door._keyRequired));
    put(*result, Gff::Field::newByte("AutoRemoveKey", door._autoRemoveKey));
    put(*result, Gff::Field::newByte("OpenLockDC", door._openLockDC));
    put(*result, Gff::Field::newByte("CloseLockDC", door._closeLockDC));
    return result;
}

std::shared_ptr<Gff> ModuleSnapshotBuilder::writePlaceable(
    const Placeable &placeable, const ModuleObjectIdContext &ids) const {
    auto result = objectBase(placeable, ResType::Utp, 9);
    writeObjectState(*result, placeable);
    putTransform(*result, placeable, true);
    put(*result, Gff::Field::newByte("Open", placeable._open));
    put(*result, Gff::Field::newByte("Locked", placeable._locked));
    put(*result, Gff::Field::newByte("Lockable", placeable._lockable));
    put(*result, Gff::Field::newShort("HP", placeable._hitPoints));
    put(*result, Gff::Field::newShort("CurrentHP", placeable._currentHitPoints));
    put(*result, Gff::Field::newByte("Dead", placeable._dead));
    put(*result, Gff::Field::newByte("Useable", placeable._usable));
    put(*result, Gff::Field::newByte("HasInventory", placeable._hasInventory));
    put(*result, Gff::Field::newByte("TrapFlag", placeable._trapFlag));
    put(*result, Gff::Field::newByte("TrapType", placeable._trapType));
    put(*result, Gff::Field::newByte("TrapDisarmable", placeable._trapDisarmable));
    put(*result, Gff::Field::newByte("TrapDetectable", placeable._trapDetectable));
    put(*result, Gff::Field::newByte("DisarmDC", placeable._disarmDC));
    put(*result, Gff::Field::newByte("TrapDetectDC", placeable._trapDetectDC));
    put(*result, Gff::Field::newByte("TrapOneShot", placeable._trapOneShot));
    std::vector<std::shared_ptr<Gff>> items;
    for (const auto &item : placeable._items) if (item) items.push_back(writeItem(*item, 0, ids.itemId(*item)));
    put(*result, Gff::Field::newList("ItemList", std::move(items)));
    return result;
}

std::shared_ptr<Gff> ModuleSnapshotBuilder::writeTrigger(
    const Trigger &trigger) const {
    auto result = objectBase(trigger, ResType::Utt, 1);
    writeObjectState(*result, trigger);
    putTransform(*result, trigger);
    put(*result, Gff::Field::newResRef("ScriptOnEnter", trigger._onEnter));
    put(*result, Gff::Field::newResRef("ScriptOnExit", trigger._onExit));
    put(*result, Gff::Field::newResRef("OnDisarm", trigger._onDisarm));
    put(*result, Gff::Field::newResRef("OnTrapTriggered", trigger._onTrapTriggered));
    put(*result, Gff::Field::newByte("TrapType", trigger._trapType));
    put(*result, Gff::Field::newByte("TrapOneShot", trigger._trapOneShot));
    put(*result, Gff::Field::newByte("TrapDisarmable", trigger._trapDisarmable));
    put(*result, Gff::Field::newByte("TrapDetectable", trigger._trapDetectable));
    put(*result, Gff::Field::newInt("Type", trigger._triggerType));
    put(*result, Gff::Field::newByte("SetByPlayerParty", trigger._setByPlayerParty));
    std::vector<std::shared_ptr<Gff>> geometry;
    for (const auto &point : trigger._geometry) {
        geometry.push_back(Gff::Builder().type(3)
            .field(Gff::Field::newFloat("PointX", point.x))
            .field(Gff::Field::newFloat("PointY", point.y))
            .field(Gff::Field::newFloat("PointZ", point.z)).build());
    }
    put(*result, Gff::Field::newList("Geometry", std::move(geometry)));
    return result;
}

std::shared_ptr<Gff> ModuleSnapshotBuilder::writeEncounter(
    const Encounter &encounter) const {
    auto result = objectBase(encounter, ResType::Ute, 7);
    writeObjectState(*result, encounter);
    putTransform(*result, encounter);
    put(*result, Gff::Field::newByte("Active", encounter._active));
    put(*result, Gff::Field::newByte("Reset", encounter._reset));
    put(*result, Gff::Field::newInt("ResetTime", encounter._resetTime));
    put(*result, Gff::Field::newInt("Respawns", encounter._respawns));
    put(*result, Gff::Field::newInt("SpawnOption", encounter._spawnOption));
    put(*result, Gff::Field::newInt("MaxCreatures", encounter._maxCreatures));
    put(*result, Gff::Field::newInt("RecCreatures", encounter._recCreatures));
    put(*result, Gff::Field::newByte("PlayerOnly", encounter._playerOnly));
    put(*result, Gff::Field::newInt("DifficultyIndex", encounter._difficultyIndex));
    put(*result, Gff::Field::newInt("Difficulty", encounter._difficulty));

    const auto &state = encounter._savedRuntimeState;
    put(*result, Gff::Field::newInt("AreaListMaxSize", state.areaListMaxSize));
    put(*result, Gff::Field::newInt("AreaListSize", state.areaListSize));
    put(*result, Gff::Field::newFloat("AreaPoints", state.areaPoints));
    put(*result, Gff::Field::newInt("CurrentSpawns", state.currentSpawns));
    put(*result, Gff::Field::newInt("CustomScriptId", state.customScriptId));
    put(*result, Gff::Field::newByte("Exhausted", state.exhausted));
    put(*result, Gff::Field::newDword("HeartbeatDay", state.heartbeatDay));
    put(*result, Gff::Field::newDword("HeartbeatTime", state.heartbeatTime));
    put(*result, Gff::Field::newDword("LastEntered", state.lastEntered));
    put(*result, Gff::Field::newDword("LastLeft", state.lastLeft));
    put(*result, Gff::Field::newDword("LastSpawnDay", state.lastSpawnDay));
    put(*result, Gff::Field::newDword("LastSpawnTime", state.lastSpawnTime));
    put(*result, Gff::Field::newInt("NumberSpawned", state.numberSpawned));
    put(*result, Gff::Field::newFloat("SpawnPoolActive", state.spawnPoolActive));
    put(*result, Gff::Field::newByte("Started", state.started));

    std::vector<std::shared_ptr<Gff>> areaList;
    for (size_t index = 0; index < state.areaObjectIds.size(); ++index) {
        auto object = encounter.savedAreaObject(index);
        areaList.push_back(Gff::Builder().type(0)
            .field(Gff::Field::newDword(
                "AreaObject",
                object && isSerializedWorldObject(*object)
                    ? object->id()
                    : kSavedRuntimeInvalidObjectId)).build());
    }
    put(*result, Gff::Field::newList("AreaList", std::move(areaList)));

    std::vector<std::shared_ptr<Gff>> creatures;
    for (const auto &creature : encounter._creatures) {
        creatures.push_back(Gff::Builder().type(0)
            .field(Gff::Field::newDword("Appearance", creature._appearance))
            .field(Gff::Field::newFloat("CR", creature._cr))
            .field(Gff::Field::newResRef("ResRef", creature._resRef))
            .field(Gff::Field::newByte("SingleSpawn", creature._singleSpawn)).build());
    }
    put(*result, Gff::Field::newList("CreatureList", std::move(creatures)));
    std::vector<std::shared_ptr<Gff>> geometry;
    for (const auto &point : encounter._geometry) {
        geometry.push_back(Gff::Builder().type(0)
            .field(Gff::Field::newFloat("X", point.x))
            .field(Gff::Field::newFloat("Y", point.y))
            .field(Gff::Field::newFloat("Z", point.z)).build());
    }
    put(*result, Gff::Field::newList("Geometry", std::move(geometry)));
    std::vector<std::shared_ptr<Gff>> spawnPoints;
    for (const auto &point : encounter._spawnPoints) {
        spawnPoints.push_back(Gff::Builder().type(0)
            .field(Gff::Field::newFloat("X", point.position.x))
            .field(Gff::Field::newFloat("Y", point.position.y))
            .field(Gff::Field::newFloat("Z", point.position.z))
            .field(Gff::Field::newFloat("Orientation", point.orientation)).build());
    }
    put(*result, Gff::Field::newList("SpawnPointList", std::move(spawnPoints)));
    return result;
}

std::shared_ptr<Gff> ModuleSnapshotBuilder::writeStore(
    const Store &store, const ModuleObjectIdContext &ids) const {
    auto result = objectBase(store, ResType::Utm, 11);
    writeObjectState(*result, store);
    putTransform(*result, store);
    const auto direction = glm::vec3(-std::sin(store.getFacing()), std::cos(store.getFacing()), 0.0f);
    put(*result, Gff::Field::newFloat("XOrientation", direction.x));
    put(*result, Gff::Field::newFloat("YOrientation", direction.y));
    put(*result, Gff::Field::newInt("MarkDown", store._markDown));
    put(*result, Gff::Field::newInt("MarkUp", store._markUp));
    put(*result, Gff::Field::newResRef("OnOpenStore", store._onOpenStore));
    put(*result, Gff::Field::newByte("BuySellFlag", store._buySellFlag));
    std::vector<std::shared_ptr<Gff>> items;
    for (const auto &item : store._items) if (item) items.push_back(writeItem(*item, 0, ids.itemId(*item)));
    put(*result, Gff::Field::newList("ItemList", std::move(items)));
    return result;
}

std::shared_ptr<Gff> ModuleSnapshotBuilder::writeWaypoint(
    const Waypoint &waypoint) const {
    auto result = objectBase(waypoint, ResType::Utw, 5);
    writeObjectState(*result, waypoint);
    putTransform(*result, waypoint);
    auto direction = glm::vec3(-std::sin(waypoint.getFacing()), std::cos(waypoint.getFacing()), 0.0f);
    put(*result, Gff::Field::newFloat("XOrientation", direction.x));
    put(*result, Gff::Field::newFloat("YOrientation", direction.y));
    put(*result, Gff::Field::newByte("HasMapNote", waypoint._hasMapNote));
    put(*result, Gff::Field::newByte("MapNoteEnabled", waypoint._mapNoteEnabled));
    return result;
}

std::shared_ptr<Gff> ModuleSnapshotBuilder::writeSound(
    const Sound &sound) const {
    auto result = objectBase(sound, ResType::Uts, 6);
    writeObjectState(*result, sound);
    putTransform(*result, sound);
    put(*result, Gff::Field::newByte("Active", sound._active));
    put(*result, Gff::Field::newByte("Positional", sound._positional));
    put(*result, Gff::Field::newByte("Looping", sound._looping));
    put(*result, Gff::Field::newByte("Volume", sound._volume));
    put(*result, Gff::Field::newByte("VolumeVrtn", sound._volumeVrtn));
    put(*result, Gff::Field::newByte("Times", sound._times));
    put(*result, Gff::Field::newFloat("PitchVariation", sound._pitchVariation));
    put(*result, Gff::Field::newDword("Hours", sound._hours));
    put(*result, Gff::Field::newDword("GeneratedType", sound._generatedType));
    put(*result, Gff::Field::newDword("Interval", sound._interval));
    put(*result, Gff::Field::newDword("IntervalVrtn", sound._intervalVrtn));
    put(*result, Gff::Field::newFloat("MinDistance", sound._minDistance));
    put(*result, Gff::Field::newFloat("MaxDistance", sound._maxDistance));
    put(*result, Gff::Field::newByte("Continuous", sound._continuous));
    put(*result, Gff::Field::newByte("Random", sound._random));
    put(*result, Gff::Field::newFloat("FixedVariance", sound._fixedVariance));
    put(*result, Gff::Field::newByte("RandomPosition", sound._randomPosition));
    put(*result, Gff::Field::newFloat("RandomRangeX", sound._randomRangeX));
    put(*result, Gff::Field::newFloat("RandomRangeY", sound._randomRangeY));
    put(*result, Gff::Field::newByte("Priority", sound._priorityId));
    put(*result, Gff::Field::newFloat("Elevation", sound._elevation));
    std::vector<std::shared_ptr<Gff>> sounds;
    for (const auto &resref : sound._sounds) sounds.push_back(
        Gff::Builder().type(0).field(Gff::Field::newResRef("Sound", resref)).build());
    put(*result, Gff::Field::newList("Sounds", std::move(sounds)));
    return result;
}

std::shared_ptr<Gff> ModuleSnapshotBuilder::writeCamera(
    const StaticCamera &camera) const {
    auto result = objectBase(camera, ResType::Invalid, 14);
    auto position = camera.position();
    position.z -= camera.height();
    put(*result, Gff::Field::newInt("CameraID", camera.cameraId()));
    put(*result, Gff::Field::newVector("Position", position));
    put(*result, Gff::Field::newOrientation(
                     "Orientation", camera.staticOrientation()));
    put(*result, Gff::Field::newFloat("Pitch", camera.staticPitch()));
    put(*result, Gff::Field::newFloat("Height", camera.height()));
    put(*result, Gff::Field::newFloat("FieldOfView", camera.fieldOfView()));
    put(*result, Gff::Field::newFloat("MicRange", camera.micRange()));
    return result;
}

std::shared_ptr<Gff> ModuleSnapshotBuilder::buildAre(const Area &area) const {
    std::shared_ptr<Gff> result;
    if (auto shadow = _game._saveResourceShadows.find(
            {SaveResourceKind::AreaAre, area._name})) {
        result = shadow->cloneForMerge();
    } else if (auto authored = _game._services.resource.gffs.get(
                   area._name, ResType::Are)) {
        result = authored->deepCopy();
    } else {
        result = emptyRecord(std::numeric_limits<uint32_t>::max());
    }
    put(*result, Gff::Field::newCExoString("Tag", area._tag));
    put(*result, Gff::Field::newByte("Unescapable", area._unescapable));
    put(*result, Gff::Field::newByte("StealthXPEnabled", area._stealthXPEnabled));
    put(*result, Gff::Field::newDword("StealthXPMax", static_cast<uint32_t>(std::max(0, area._maxStealthXP))));
    put(*result, Gff::Field::newDword("StealthXPCurrent", static_cast<uint32_t>(std::max(0, area._currentStealthXP))));
    put(*result, Gff::Field::newDword("StealthXPLoss", static_cast<uint32_t>(std::max(0, area._stealthXPDecrement))));
    put(*result, Gff::Field::newStruct("SWVarTable", writeLocals(area)));
    return result;
}

std::shared_ptr<Gff> ModuleSnapshotBuilder::buildGit(
    const Module &, const Area &area,
    const ModuleObjectIdContext &ids) const {
    std::shared_ptr<Gff> result;
    if (auto shadow = _game._saveResourceShadows.find(
            {SaveResourceKind::AreaGit, area._name})) {
        result = shadow->cloneForMerge();
    } else if (auto authored = _game._services.resource.gffs.get(
                   area._name, ResType::Git)) {
        result = authored->deepCopy();
    } else {
        result = emptyRecord(std::numeric_limits<uint32_t>::max());
    }

    std::map<std::string, std::vector<std::shared_ptr<Gff>>> lists {
        {"Creature List", {}}, {"Door List", {}}, {"Placeable List", {}},
        {"TriggerList", {}}, {"Encounter List", {}}, {"StoreList", {}},
        {"WaypointList", {}}, {"SoundList", {}}, {"List", {}}};
    lists["CameraList"] = {};
    for (const auto &object : area._objects) {
        if (!object || area._objectsToDestroy.count(object->id()) != 0) continue;
        if (object->type() == ObjectType::Creature && _game._party.isMember(*object)) continue;
        switch (object->type()) {
        case ObjectType::Creature:
            lists["Creature List"].push_back(writeCreature(
                *static_cast<Creature *>(object.get()), 4, object->id(), ids));
            break;
        case ObjectType::Door:
            lists["Door List"].push_back(writeDoor(*static_cast<Door *>(object.get())));
            break;
        case ObjectType::Placeable:
            lists["Placeable List"].push_back(writePlaceable(*static_cast<Placeable *>(object.get()), ids));
            break;
        case ObjectType::Trigger:
            lists["TriggerList"].push_back(writeTrigger(*static_cast<Trigger *>(object.get())));
            break;
        case ObjectType::Encounter:
            lists["Encounter List"].push_back(writeEncounter(*static_cast<Encounter *>(object.get())));
            break;
        case ObjectType::Store:
            lists["StoreList"].push_back(writeStore(*static_cast<Store *>(object.get()), ids));
            break;
        case ObjectType::Waypoint:
            lists["WaypointList"].push_back(writeWaypoint(*static_cast<Waypoint *>(object.get())));
            break;
        case ObjectType::Sound:
            lists["SoundList"].push_back(writeSound(*static_cast<Sound *>(object.get())));
            break;
        case ObjectType::Camera:
            lists["CameraList"].push_back(writeCamera(
                *static_cast<StaticCamera *>(object.get())));
            break;
        case ObjectType::Item: {
            auto item = writeItem(*static_cast<Item *>(object.get()), 0, object->id());
            putTransform(*item, *object, true);
            lists["List"].push_back(std::move(item));
            break;
        }
        case ObjectType::AreaOfEffect:
            throw ValidationException("live area-of-effect objects have no E3d save representation");
        default:
            throw ValidationException("active area contains an unsupported save object type");
        }
    }
    for (auto &[label, list] : lists) {
        put(*result, Gff::Field::newList(label, std::move(list)));
    }
    // E2 has no published runtime AreaEffectList representation. Keeping an
    // authored/shadow list here would resurrect state absent from the graph.
    removeSaveField(*result, "AreaEffectList");
    return result;
}

std::shared_ptr<Gff> ModuleSnapshotBuilder::buildIfo(
    const Module &module, const ModuleObjectIdContext &ids) const {
    std::shared_ptr<Gff> result;
    if (auto shadow = _game._saveResourceShadows.find(
            {SaveResourceKind::ModuleIfo, module._name})) {
        result = shadow->cloneForMerge();
    } else if (auto authored = _game._services.resource.gffs.get(
                   "module", ResType::Ifo)) {
        result = authored->deepCopy();
    } else {
        result = emptyRecord(std::numeric_limits<uint32_t>::max());
    }
    if (!module._area) throw ValidationException("module has no active area");
    const Area &area = *module._area;
    put(*result, Gff::Field::newByte("Mod_IsSaveGame", 1));
    put(*result, Gff::Field::newResRef("Mod_ResRef", module._name));
    put(*result, Gff::Field::newCExoString("Mod_Tag", module._tag.empty() ? module._name : module._tag));
    put(*result, Gff::Field::newResRef("Mod_Entry_Area", area._name));
    put(*result, Gff::Field::newDword("Mod_Area", area._id));
    put(*result, Gff::Field::newFloat("Mod_Entry_X", module._info.entryPosition.x));
    put(*result, Gff::Field::newFloat("Mod_Entry_Y", module._info.entryPosition.y));
    put(*result, Gff::Field::newFloat("Mod_Entry_Z", module._info.entryPosition.z));
    put(*result, Gff::Field::newFloat("Mod_Entry_Dir_X", -std::sin(module._info.entryFacing)));
    put(*result, Gff::Field::newFloat("Mod_Entry_Dir_Y", std::cos(module._info.entryFacing)));
    put(*result, Gff::Field::newDword("Mod_CalendarDay", _game._worldTimeDay));
    put(*result, Gff::Field::newDword("Mod_TimeOfDay", _game._worldTimeOfDay));
    put(*result, Gff::Field::newByte("Mod_MinPerHour", _game._minutesPerHour));
    put(*result, Gff::Field::newDword64("Mod_Effect_NxtId", _game._effectIds.nextId()));
    put(*result, Gff::Field::newStruct("SWVarTable", writeLocals(module)));

    auto areaEntry = Gff::Builder().type(6)
        .field(Gff::Field::newResRef("Area_Name", area._name))
        .field(Gff::Field::newDword("ObjectId", area._id)).build();
    put(*result, Gff::Field::newList("Mod_Area_list", {areaEntry}));

    uint32_t shadowNext = result->getUint("Mod_NextObjId0", 2);
    put(*result, Gff::Field::newDword(
        "Mod_NextObjId0", ids.nextId(shadowNext)));

    std::vector<std::shared_ptr<Gff>> tokens;
    for (const auto &[number, value] : _game._customTokens) {
        if (number < 0) continue;
        tokens.push_back(Gff::Builder().type(7)
            .field(Gff::Field::newDword("Mod_TokensNumber", static_cast<uint32_t>(number)))
            .field(Gff::Field::newCExoString("Mod_TokensValue", value)).build());
    }
    put(*result, Gff::Field::newList("Mod_Tokens", std::move(tokens)));

    std::vector<std::shared_ptr<Gff>> events;
    for (auto event : module.saveEventSnapshot()) {
        normalizeEventReferences(event);
        events.push_back(eventToGff(event, &_game));
    }
    put(*result, Gff::Field::newList("EventQueue", std::move(events)));

    auto modulePlayer = _game._party.player();
    if (!modulePlayer) throw ValidationException("module has no controlled player creature");
    auto player = writeCreature(*modulePlayer, 4, modulePlayer->id(), ids, false);
    put(*player, Gff::Field::newByte(
        "Mod_IsPrimaryPlr", modulePlayer == _game._party.actualPlayer()));
    put(*result, Gff::Field::newList("Mod_PlayerList", {player}));

    std::vector<std::shared_ptr<Gff>> limbo;
    for (const auto &creature : module._limboCreatures) {
        if (creature) {
            limbo.push_back(writeCreature(*creature, 4, creature->id(), ids));
        }
    }
    put(*result, Gff::Field::newList("Creature List", std::move(limbo)));
    return result;
}

ModuleSnapshotResult ModuleSnapshotBuilder::build() const noexcept {
    ModuleSnapshotResult result;
    if (!_game._module || !_game._runtimeSessionPlayable) {
        result.error = ModuleSnapshotError::NoPlayableModule;
        result.message = "there is no stable playable module to snapshot";
        return result;
    }
    if (_saveGroup.empty()) {
        result.error = ModuleSnapshotError::InvalidRuntimeGraph;
        result.message = "save-group resource name is empty";
        return result;
    }
    try {
        SavedModuleSnapshot snapshot;
        snapshot.target = resource::ResourceId(_saveGroup, ResType::Sav);
        auto ids = buildObjectIdContext(*_game._module, *_game._module->_area);
        snapshot.ifo = buildIfo(*_game._module, ids);
        snapshot.are = buildAre(*_game._module->_area);
        snapshot.git = buildGit(*_game._module, *_game._module->_area, ids);
        snapshot.ifoBytes = resource::GffWriter(
            resource::GffFileFormat::v32("IFO "), *snapshot.ifo).toBytes();
        snapshot.areBytes = resource::GffWriter(
            resource::GffFileFormat::v32("ARE "), *snapshot.are).toBytes();
        snapshot.gitBytes = resource::GffWriter(
            resource::GffFileFormat::v32("GIT "), *snapshot.git).toBytes();

        resource::ErfWriter archive;
        archive.add({"module", ResType::Ifo, snapshot.ifoBytes});
        archive.add({_game._module->_area->_name, ResType::Are, snapshot.areBytes});
        archive.add({_game._module->_area->_name, ResType::Git, snapshot.gitBytes});
        snapshot.archiveBytes = archive.toBytes(resource::ErfWriter::FileType::MOD);
        try {
            validate(snapshot);
        } catch (const std::exception &ex) {
            result.error = ModuleSnapshotError::ValidationFailure;
            result.message = ex.what();
            return result;
        }
        result.snapshot = std::move(snapshot);
        return result;
    } catch (const ValidationException &ex) {
        result.error = ModuleSnapshotError::UnsupportedLiveState;
        result.message = ex.what();
    } catch (const std::exception &ex) {
        result.error = ModuleSnapshotError::EncodingFailure;
        result.message = ex.what();
    }
    return result;
}

void ModuleSnapshotBuilder::validate(const SavedModuleSnapshot &snapshot) const {
    if (snapshot.archiveBytes.empty()) throw ValidationException("empty saved module archive");
    ByteBuffer archiveBytes(snapshot.archiveBytes);
    MemoryInputStream archiveStream(archiveBytes);
    resource::ErfReader archive(archiveStream);
    archive.load();
    if (archive.signature() != "MOD V1.0") throw ValidationException("saved module is not MOD V1.0");
    if (archive.keys().size() != 3 || archive.resources().size() != 3) {
        throw ValidationException("saved module must contain exactly IFO/ARE/GIT resources");
    }
    const std::set<resource::ResourceId> expected {
        {"module", ResType::Ifo},
        {_game._module->_area->_name, ResType::Are},
        {_game._module->_area->_name, ResType::Git}};
    std::set<resource::ResourceId> actual;
    for (const auto &key : archive.keys()) actual.insert(key.resId);
    if (actual != expected) throw ValidationException("saved module resource keys are inconsistent");

    auto roundTrip = [](ByteBuffer bytes, const std::string &signature) {
        MemoryInputStream stream(bytes);
        resource::GffReader reader(stream);
        reader.load();
        auto root = reader.root();
        if (!root || !root->signature() || *root->signature() != signature + "V3.2") {
            throw ValidationException("GFF signature mismatch for " + signature);
        }
        return root;
    };
    auto ifo = roundTrip(snapshot.ifoBytes, "IFO ");
    auto are = roundTrip(snapshot.areBytes, "ARE ");
    auto git = roundTrip(snapshot.gitBytes, "GIT ");
    if (ifo->getString("Mod_Entry_Area") != _game._module->_area->_name ||
        ifo->getUint("Mod_Area") != _game._module->_area->_id) {
        throw ValidationException("IFO active-area identity is inconsistent");
    }
    uint32_t nextId = ifo->getUint("Mod_NextObjId0");
    if (nextId < 2 || nextId >= kSavedRuntimeInvalidObjectId ||
        ifo->getUint64("Mod_Effect_NxtId") == kUnassignedEffectId) {
        throw ValidationException("IFO runtime cursors are invalid");
    }
    std::set<uint32_t> ordinaryIds;
    std::set<uint32_t> reservedPartyIds;
    auto addOrdinaryId = [&ordinaryIds](uint32_t id, const char *kind) {
        if (id >= kSavedRuntimeInvalidObjectId || !ordinaryIds.insert(id).second) {
            throw ValidationException(std::string(kind) +
                                      " has an invalid or duplicate saved object ID");
        }
    };
    auto addPartyId = [&addOrdinaryId, &reservedPartyIds](uint32_t id,
                                                         const char *kind) {
        if (id < kSavedRuntimeInvalidObjectId) {
            addOrdinaryId(id, kind);
        } else if (id == kSavedRuntimeInvalidObjectId || id > 0x7fffffffu ||
                   !reservedPartyIds.insert(id).second) {
            throw ValidationException(std::string(kind) +
                                      " has an invalid or duplicate reserved ID");
        }
    };
    auto addItems = [&addOrdinaryId](const std::shared_ptr<Gff> &owner,
                                     bool includeInventory) {
        for (const auto &item : owner->getList("Equip_ItemList")) {
            addOrdinaryId(item->getUint("ObjectId", kSavedRuntimeInvalidObjectId),
                          "equipped item");
        }
        if (!includeInventory) return;
        for (const auto &item : owner->getList("ItemList")) {
            addOrdinaryId(item->getUint("ObjectId", kSavedRuntimeInvalidObjectId),
                          "contained item");
        }
    };

    addOrdinaryId(ifo->getUint("Mod_Area", kSavedRuntimeInvalidObjectId),
                  "saved area");
    static const std::array<const char *, 9> authoritativeLists {
        "Creature List", "Door List", "Placeable List", "TriggerList",
        "Encounter List", "StoreList", "WaypointList", "SoundList", "List"};
    for (const char *label : authoritativeLists) {
        for (const auto &object : git->getList(label)) {
            addOrdinaryId(object->getUint("ObjectId", kSavedRuntimeInvalidObjectId),
                          "GIT object");
            if (std::string(label) == "Creature List") addItems(object, true);
            if (std::string(label) == "Placeable List" ||
                std::string(label) == "StoreList") {
                for (const auto &item : object->getList("ItemList")) {
                    addOrdinaryId(item->getUint(
                        "ObjectId", kSavedRuntimeInvalidObjectId), "contained item");
                }
            }
        }
    }
    for (const auto &player : ifo->getList("Mod_PlayerList")) {
        uint32_t id = player->getUint("ObjectId", kSavedRuntimeInvalidObjectId);
        addPartyId(id, "module player");
        if (!player->getList("ItemList").empty()) {
            throw ValidationException("module player duplicates shared party inventory");
        }
        addItems(player, false);
    }
    for (const auto &creature : ifo->getList("Creature List")) {
        addPartyId(creature->getUint("ObjectId", kSavedRuntimeInvalidObjectId),
                   "limbo creature");
        addItems(creature, true);
    }
    if (!ordinaryIds.empty() && nextId <= *ordinaryIds.rbegin()) {
        throw ValidationException(
            "Mod_NextObjId0 does not advance beyond the ordinary saved object graph");
    }
    (void)are;
}

} // namespace game
} // namespace reone
