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
#include "reone/game/d20/class.h"
#include "reone/game/di/services.h"
#include "reone/game/effect.h"
#include "reone/game/event.h"
#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/object/area.h"
#include "reone/game/object/areaofeffect.h"
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
#include "reone/game/projectiles.h"
#include "reone/game/saveprovenance.h"
#include "reone/game/script/savedsituation.h"
#include "reone/game/talent.h"
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

template <class... Visitors>
struct Overloaded : Visitors... {
    using Visitors::operator()...;
};

template <class... Visitors>
Overloaded(Visitors...) -> Overloaded<Visitors...>;

// The struct types of a combat round and of its scheduled entries, and of a
// creature's special ability.
constexpr uint32_t kCombatRoundStructType = 0xcada;
constexpr uint32_t kScheduledActionStructType = 0xdaea;
constexpr uint32_t kSpecialAbilityStructType = 4;
// The struct type of an area's AreaProperties.
constexpr uint32_t kAreaPropertiesStructType = 100;
// The struct types of a queued event's data: an effect applied or removed, a
// script event signalled, a creature summoned, an area transition and a
// controller rumble.
constexpr uint32_t kEffectEventStructType = 0x1111;
constexpr uint32_t kSignalEventStructType = 0x4444;
constexpr uint32_t kSummonCreatureEventStructType = 0xdddd;
constexpr uint32_t kAreaTransitionEventStructType = 0xeeee;
constexpr uint32_t kControllerRumbleEventStructType = 0xffff;
// The struct type of each parameter in a script event's lists.
constexpr uint32_t kScriptEventParameterStructType = 0x69;
// The struct type of a script situation an action takes as a parameter.
constexpr uint32_t kActionSituationStructType = 2;

// The pose a saved creature was last given (its Animation): one of the three
// dead poses, a damage flinch, ready in combat, or the pause, injured or not.
constexpr int kDeadAnimation = 10006;
constexpr int kDead1Animation = 10008;
constexpr int kDead3Animation = 10156;
constexpr int kDamageFlinchAnimation = 10302;
constexpr int kReadyAnimation = 10001;
constexpr int kPauseAnimation = 10000;

// The bits of a perception record's PerceptionData that say the object is seen
// and heard.
constexpr uint32_t kPerceptionSeenBit = 0x1;
constexpr uint32_t kPerceptionHeardBit = 0x2;

std::shared_ptr<Gff> emptyRecord(uint32_t type) {
    return Gff::Builder().type(type).build();
}

/**
 * Whether an area object owns an identity in the module's saved object
 * namespace.
 *
 * The game keeps placeable cameras out of the server object table, so they never
 * receive a server ObjectId: a GIT CameraList entry carries CameraID,
 * Position, Orientation, Pitch, Height, FieldOfView and MicRange and nothing
 * else, in both games, and no script routine can resolve a camera as an
 * object. They are presentation records rebuilt from module data, addressed
 * by CameraID, so they must not consume identities that creatures, doors,
 * placeables, triggers, stores, waypoints, sounds, encounters and items
 * legitimately own.
 */
bool ownsSavedObjectIdentity(ObjectType type) {
    return type != ObjectType::Camera;
}

void put(Gff &record, Gff::Field field) {
    replaceSaveField(record, std::move(field));
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
    if (effect.durationType() == DurationType::Temporary && effect.remainingDuration &&
        effect.expiryOrigin == EffectExpiryOrigin::RuntimeCountdown) {
        // Only a pending relative lifetime is anchored here. Retained and
        // loaded absolute deadlines already belong to the canonical clock;
        // the conversion uses its configured day length.
        if (!game) {
            throw ValidationException("temporary effect lacks a game-time conversion context");
        }
        uint64_t expiry = getEffectExpiryMilliseconds(game->worldTimeMilliseconds(),
                                                      *effect.remainingDuration, static_cast<uint32_t>(game->millisecondsPerWorldDay()));
        uint64_t millisecondsPerDay = game->millisecondsPerWorldDay();
        expiryDay = static_cast<uint32_t>(expiry / millisecondsPerDay);
        expiryTime = static_cast<uint32_t>(expiry % millisecondsPerDay);
    } else if (!effect.hasSerializableTemporalProvenance()) {
        throw ValidationException("temporary effect lacks save-facing expiry provenance");
    }

    auto record = effect;
    record.expiryDay = expiryDay;
    record.expiryTime = expiryTime;
    return record.toGff();
}

std::shared_ptr<Gff> scriptEventToGff(const SavedScriptEvent &event) {
    auto result = Gff::Builder().type(kSignalEventStructType).field(Gff::Field::newWord("EventType", event.type)).build();
    auto makeIntList = [](const std::vector<int32_t> &values) {
        std::vector<std::shared_ptr<Gff>> result;
        for (auto value : values)
            result.push_back(Gff::Builder().type(kScriptEventParameterStructType).field(Gff::Field::newInt("Parameter", value)).build());
        return result;
    };
    std::vector<std::shared_ptr<Gff>> floats, strings, objects;
    for (auto value : event.floats)
        floats.push_back(Gff::Builder().type(kScriptEventParameterStructType).field(Gff::Field::newFloat("Parameter", value)).build());
    for (const auto &value : event.strings)
        strings.push_back(Gff::Builder().type(kScriptEventParameterStructType).field(Gff::Field::newCExoString("Parameter", value)).build());
    for (const auto &value : event.objects)
        objects.push_back(Gff::Builder().type(kScriptEventParameterStructType).field(Gff::Field::newDword("Parameter", value.id)).build());
    put(*result, Gff::Field::newList("IntList", makeIntList(event.integers)));
    put(*result, Gff::Field::newList("FloatList", std::move(floats)));
    put(*result, Gff::Field::newList("StringList", std::move(strings)));
    put(*result, Gff::Field::newList("ObjectList", std::move(objects)));
    return result;
}

std::shared_ptr<Gff> talentToGff(const SavedTalentValue &talent, const Game *game) {
    auto result = Gff::Builder().type(3)
        .field(Gff::Field::newByte("MultiClass", talent.multiClass))
        .field(Gff::Field::newDword("Item", talent.item.id))
        .field(Gff::Field::newByte("CasterLevel", talent.casterLevel))
        .field(Gff::Field::newByte("MetaType", talent.metaType))
        .build();
    if (game && !game->isTSL()) {
        put(*result, Gff::Field::newDword("ID", static_cast<uint32_t>(talent.id)));
        put(*result, Gff::Field::newDword("Type", static_cast<uint32_t>(talent.type)));
        put(*result, Gff::Field::newDword(
            "ItemPropertyInde", static_cast<uint32_t>(talent.itemPropertyIndex)));
    } else {
        put(*result, Gff::Field::newInt("ID", talent.id));
        put(*result, Gff::Field::newInt("Type", talent.type));
        put(*result, Gff::Field::newInt("ItemPropertyInde", talent.itemPropertyIndex));
    }
    return result;
}

std::shared_ptr<Gff> situationToGff(
    const SerializedScriptSituation &situation, const Game *game = nullptr) {
    auto result = Gff::Builder().type(0x7777).field(Gff::Field::newInt("CodeSize", situation.codeSize)).field(Gff::Field::newVoid("Code", situation.code)).field(Gff::Field::newDword("CRC", situation.crc)).field(Gff::Field::newInt("InstructionPtr", situation.instructionPointer)).field(Gff::Field::newInt("SecondaryPtr", situation.secondaryPointer)).field(Gff::Field::newCExoString("Name", situation.scriptName)).field(Gff::Field::newInt("StackSize", situation.stackSize)).build();

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
        case SavedVmStackType::Talent:
            put(*value, Gff::Field::newStruct(
                "GameDefinedStrct", talentToGff(std::get<SavedTalentValue>(saved.payload), game)));
            break;
        default:
            throw ValidationException("unsupported live VM stack value");
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
    auto result = Gff::Builder().type(0).field(Gff::Field::newDword("ActionId", action.actionId)).field(Gff::Field::newWord("GroupActionId", static_cast<uint16_t>(action.groupActionId))).field(Gff::Field::newWord("NumParams", static_cast<uint16_t>(action.parameters.size()))).build();
    if (action.cast) {
        put(*result, Gff::Field::newStruct("CastState", action.cast->toGff()));
    }
    if (action.scheduled) {
        put(*result, Gff::Field::newByte("Scheduled", 1));
    }
    if (action.round) {
        put(*result, Gff::Field::newStruct("RoundState", action.round->toGff()));
    }
    if (action.physical) {
        put(*result, Gff::Field::newStruct("PhysicalState", action.physical->toGff()));
    }
    std::vector<std::shared_ptr<Gff>> parameters;
    for (const auto &parameter : action.parameters) {
        auto record = Gff::Builder().type(1)
            .field(Gff::Field::newDword("Type", parameter.type)).build();
        if (auto value = std::get_if<int32_t>(&parameter.payload)) put(*record, Gff::Field::newInt("Value", *value));
        else if (auto value = std::get_if<float>(&parameter.payload)) put(*record, Gff::Field::newFloat("Value", *value));
        else if (auto value = std::get_if<SavedObjectReference>(&parameter.payload)) put(*record, Gff::Field::newDword("Value", value->id));
        else if (auto value = std::get_if<std::string>(&parameter.payload)) put(*record, Gff::Field::newCExoString("Value", *value));
        else if (auto value = std::get_if<SerializedScriptSituation>(&parameter.payload)) {
            auto situation = situationToGff(*value, game);
            situation->setType(kActionSituationStructType);
            put(*record, Gff::Field::newStruct("Value", std::move(situation)));
        }
        parameters.push_back(std::move(record));
    }
    // An action without parameters has no parameter list.
    if (!parameters.empty()) {
        put(*result, Gff::Field::newList("Paramaters", std::move(parameters)));
    }
    return result;
}

std::shared_ptr<Gff> eventToGff(
    const SavedEventRecord &event, const Game *game = nullptr) {
    auto result = Gff::Builder().type(0xabcd).field(Gff::Field::newDword("Day", event.day)).field(Gff::Field::newDword("Time", event.time)).field(Gff::Field::newDword("ObjectId", event.object.id)).field(Gff::Field::newDword("CallerId", event.caller.id)).field(Gff::Field::newDword("EventId", event.eventId)).build();
    std::shared_ptr<Gff> data;
    if (auto value = std::get_if<SerializedScriptSituation>(&event.payload)) {
        data = situationToGff(*value, game);
    } else if (auto value = std::get_if<EffectInstance>(&event.payload)) {
        data = value->toGff();
        data->setType(kEffectEventStructType);
    } else if (auto value = std::get_if<SavedBytePayload>(&event.payload))
        data = Gff::Builder().type(0x9999).field(Gff::Field::newByte("Value", value->value)).build();
    else if (auto value = std::get_if<SavedIntPayload>(&event.payload)) {
        const bool rumble = event.eventId == static_cast<uint32_t>(SavedEventType::ControllerRumble);
        data = Gff::Builder().type(rumble ? kControllerRumbleEventStructType : 0x3333).field(Gff::Field::newInt("Value", value->value)).build();
    } else if (auto value = std::get_if<SavedScriptEvent>(&event.payload)) {
        data = scriptEventToGff(*value);
        switch (static_cast<SavedEventType>(event.eventId)) {
        case SavedEventType::SummonCreature:
            data->setType(kSummonCreatureEventStructType);
            break;
        case SavedEventType::AreaTransition:
            data->setType(kAreaTransitionEventStructType);
            break;
        default:
            break;
        }
    } else if (auto value = std::get_if<SavedBodyBag>(&event.payload))
        data = Gff::Builder().type(0x5555).field(Gff::Field::newDword("BodyBagId", value->object.id)).field(Gff::Field::newFloat("PositionX", value->position.x)).field(Gff::Field::newFloat("PositionY", value->position.y)).field(Gff::Field::newFloat("PositionZ", value->position.z)).build();
    else if (auto value = std::get_if<SavedBroadcastAoo>(&event.payload)) data = Gff::Builder().type(0x3333)
        .field(Gff::Field::newDword("Value", value->target.id)).build();
    else if (auto value = std::get_if<SavedCombatAttack>(&event.payload)) {
        data = value->toGff();
    } else if (auto value = std::get_if<CombatFeedbackMessage>(&event.payload)) {
        data = value->toGff(event.object.id, event.caller.id);
    } else if (auto value = std::get_if<SavedWeaponImpact>(&event.payload)) {
        data = value->toGff();
    } else if (auto value = std::get_if<SavedSpellImpact>(&event.payload)) {
        data = value->toGff();
    }
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

static bool sameModuleIdentityNamespace(
    const SerializedIdentityContext &lhs,
    const SerializedIdentityContext &rhs) {
    return lhs.hasAuthoritativeObjectIds() &&
           rhs.hasAuthoritativeObjectIds() &&
           boost::iequals(lhs.identityNamespace, rhs.identityNamespace);
}

void ModuleObjectIdContext::assignContextObject(
    const Object &object, uint32_t id) {
    if (_objectIds.count(&object) != 0) {
        return;
    }
    if (id >= kSavedRuntimeInvalidObjectId || !_used.insert(id).second) {
        throw ValidationException(
            "contextual object ID collides in module namespace");
    }
    _objectIds.emplace(&object, id);
}

void ModuleObjectIdContext::retainObject(
    const Object &object, bool partyIdentity) {
    if (_objectIds.count(&object) != 0) return;
    auto identity = object.serializedObjectIdentity();
    if (!identity ||
        !sameModuleIdentityNamespace(identity->context, _outputIdentityContext)) {
        return;
    }
    uint32_t id = identity->id;
    if (partyIdentity && id >= kSavedRuntimeInvalidObjectId) {
        if (id > kSavedRuntimeMaxCharacterObjectId || !_characterIds.insert(id).second) {
            throw ValidationException(
                "saved party creature uses an invalid or duplicate reserved ID");
        }
    } else {
        if (id >= kSavedRuntimeInvalidObjectId) {
            throw ValidationException("ordinary saved object uses a reserved ID");
        }
        if (!_used.insert(id).second) {
            throw ValidationException(
                "authoritative saved object ID collides in module namespace");
        }
    }
    _objectIds.emplace(&object, id);
}

void ModuleObjectIdContext::allocateObject(
    const Object &object, bool partyIdentity) {
    if (_objectIds.count(&object) != 0) return;
    const uint32_t preferred = object.id();
    if (partyIdentity && preferred > kSavedRuntimeInvalidObjectId &&
        preferred <= kSavedRuntimeMaxCharacterObjectId &&
        _characterIds.insert(preferred).second) {
        _objectIds.emplace(&object, preferred);
        return;
    }
    if (preferred < kSavedRuntimeInvalidObjectId &&
        _used.insert(preferred).second) {
        _objectIds.emplace(&object, preferred);
        return;
    }
    for (uint32_t candidate = 2; candidate < kSavedRuntimeInvalidObjectId; ++candidate) {
        if (_used.insert(candidate).second) {
            _objectIds.emplace(&object, candidate);
            return;
        }
    }
    throw ValidationException("saved object ID namespace is exhausted");
}

uint32_t ModuleObjectIdContext::objectId(const Object &object) const {
    auto found = _objectIds.find(&object);
    if (found == _objectIds.end()) {
        throw ValidationException("module graph object has no serialized identity");
    }
    return found->second;
}

bool ModuleObjectIdContext::contains(const Object &object) const {
    return _objectIds.count(&object) != 0;
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

uint32_t ModuleObjectIdContext::nextCharacterId(uint32_t retainedCursor) const {
    // The character cursor descends: it stays below every character identity
    // held by the graph, as placing a saved character lowers it on load.
    if (_characterIds.empty()) {
        return retainedCursor;
    }
    return std::min(retainedCursor, *_characterIds.begin() - 1);
}

ModuleObjectIdContext ModuleSnapshotBuilder::buildObjectIdContext(
    const Module &module, const Area &area) const {
    ModuleObjectIdContext ids(
        SerializedIdentityContext::moduleGraph(_saveGroup));
    // The game creates the structural Module before loading the owned IFO/GIT
    // graph. Slot 0 is consequently a contextual reference target, not an
    // ObjectId persisted by an owned Module record.
    ids.assignContextObject(module, kSavedRuntimeModuleObjectId);
    std::vector<std::pair<const Object *, bool>> objects;
    std::set<const Object *> seen;
    auto addObject = [&](const Object &object, bool partyIdentity = false) {
        if (seen.insert(&object).second) {
            objects.emplace_back(&object, partyIdentity);
        }
    };
    auto addItem = [&](const std::shared_ptr<Item> &item) {
        if (item) addObject(*item);
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

    addObject(module);
    addObject(area);
    for (const auto &object : area._objects) {
        if (!object || area._objectsToDestroy.count(object->id()) != 0) continue;
        if (object->type() == ObjectType::Creature &&
            _game._party.isRetainedRuntimeRepresentation(
                *static_cast<const Creature *>(object.get()))) continue;
        if (ownsSavedObjectIdentity(object->type())) {
            addObject(*object);
        }
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
    addObject(*modulePlayer, true);
    addCreatureItems(*modulePlayer, false);
    for (const auto &creature : module._limboCreatures) {
        if (!creature) continue;
        addObject(*creature, true);
        addCreatureItems(*creature, true);
    }
    // Reserve every identity already authoritative in this exact destination
    // graph before importing anything. Detached/local numbers never enter the
    // namespace; allocation is keyed by the live object, not by that number.
    for (const auto &[object, partyIdentity] : objects) {
        ids.retainObject(*object, partyIdentity);
    }
    for (const auto &[object, partyIdentity] : objects) {
        ids.allocateObject(*object, partyIdentity);
    }
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

    auto convert = [&error](
                       const script::Variable &value,
                       const char *scope,
                       size_t index) -> std::optional<SavedVmStackValue> {
        SavedVmStackValue saved;
        switch (value.type) {
        case script::VariableType::Int:
            saved.type = static_cast<int8_t>(SavedVmStackType::Integer); saved.payload = value.intValue; break;
        case script::VariableType::Float:
            saved.type = static_cast<int8_t>(SavedVmStackType::Float); saved.payload = value.floatValue; break;
        case script::VariableType::String:
            saved.type = static_cast<int8_t>(SavedVmStackType::String); saved.payload = value.strValue; break;
        case script::VariableType::Object:
            saved.type = static_cast<int8_t>(SavedVmStackType::Object); saved.payload = SavedObjectReference::fromRuntimeId(value.objectId); break;
        case script::VariableType::Effect: {
            auto effect = std::dynamic_pointer_cast<SavedEffectValue>(value.engineType);
            auto liveEffect = std::dynamic_pointer_cast<Effect>(value.engineType);
            if (!liveEffect) {
                error = "live VM effect has an unsupported engine value";
                return std::nullopt;
            }
            saved.type = static_cast<int8_t>(SavedVmStackType::Effect);
            saved.payload = effect ? effect->instance()
                                   : liveEffect->saveFacingInstance();
            break;
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
            if (!value.engineType) {
                // K1 normalizes an uninitialized VM location to a
                // discriminator-18 structure whose six components are zero.
                saved.type = static_cast<int8_t>(SavedVmStackType::Location);
                saved.payload = SavedLocationValue {};
                break;
            }
            if (!location) {
                error = str(boost::format(
                    "live VM %s[%d] type=%d location has engine=%s, expected=%s") %
                    scope % index % static_cast<int>(value.type) %
                    (value.engineType ? typeid(*value.engineType).name() : "none") %
                    typeid(Location).name());
                return std::nullopt;
            }
            saved.type = static_cast<int8_t>(SavedVmStackType::Location);
            saved.payload = SavedLocationValue {location->position(), location->saveOrientation()}; break;
        }
        case script::VariableType::Talent: {
            auto talent = std::dynamic_pointer_cast<Talent>(value.engineType);
            if (!talent) { error = "live VM talent has an unsupported engine value"; return std::nullopt; }
            saved.type = static_cast<int8_t>(SavedVmStackType::Talent);
            saved.payload = SavedTalentValue {
                talent->value(),
                static_cast<int32_t>(talent->type()),
                // The saved talent keeps its class, caster level and meta type as bytes.
                static_cast<uint8_t>(talent->castingClass()),
                SavedObjectReference::fromRuntimeId(talent->item()),
                talent->itemPropertyIndex(),
                static_cast<uint8_t>(talent->casterLevel()),
                static_cast<uint8_t>(talent->metaType())};
            break;
        }
        default:
            error = str(boost::format(
                "live VM %s[%d] contains unsupported type=%d engine=%s") %
                scope % index % static_cast<int>(value.type) %
                (value.engineType ? typeid(*value.engineType).name() : "none"));
            return std::nullopt;
        }
        return saved;
    };
    for (size_t i = 0; i < state.globals.size(); ++i) {
        auto saved = convert(state.globals[i], "globals", i);
        if (!saved) return std::nullopt;
        result.stack.push_back(std::move(*saved));
    }
    result.basePointer = static_cast<int32_t>(result.stack.size());
    for (size_t i = 0; i < state.locals.size(); ++i) {
        auto saved = convert(state.locals[i], "locals", i);
        if (!saved) return std::nullopt;
        result.stack.push_back(std::move(*saved));
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
    Gff &record,
    const Object &object,
    const ModuleObjectIdContext &ids) const {
    if (ids.contains(object)) {
        put(record, Gff::Field::newDword("ObjectId", ids.objectId(object)));
    } else {
        removeSaveField(record, "ObjectId");
    }
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
            normalizeEffectReferences(std::move(effect), ids), &_game));
    }
    put(record, Gff::Field::newList("EffectList", std::move(effects)));

    std::vector<std::shared_ptr<Gff>> actions;
    for (auto action : object.saveActionSnapshot()) {
        normalizeActionReferences(action, ids);
        actions.push_back(actionToGff(action, &_game));
    }
    put(record, Gff::Field::newList("ActionList", std::move(actions)));

    writeCombatRound(record, object, ids);

    if (auto spell = object._spellScriptContext.save([&](const Object &referenced) {
            return ids.contains(referenced) ? ids.objectId(referenced) : kSavedRuntimeInvalidObjectId;
        })) {
        put(record, Gff::Field::newStruct("SpellContext", std::move(spell)));
    }

    static const std::array<std::string, 8> references {
        "AreaId", "CreatorId", "LastAttacker",
        "LastHostileActor", "LastPerceived", "MasterID", "OwnerId", "TargetId"};
    for (const auto &field : references) {
        if (object._savedReferenceIds.count(field) != 0 ||
            object._savedReferences.count(field) != 0) {
            auto bound = object.savedReference(field);
            put(record, Gff::Field::newDword(
                field,
                bound && ids.contains(*bound)
                    ? ids.objectId(*bound)
                    : kSavedRuntimeInvalidObjectId));
        }
    }
    if (object._savedLastDamagerId || !object._lastDamager.empty()) {
        auto bound = object._lastDamager.resolve();
        put(record, Gff::Field::newDword(
            "LastDamager",
            bound && ids.contains(*bound)
                ? ids.objectId(*bound)
                : kSavedRuntimeInvalidObjectId));
    }

    // PerceptionList is part of the same reference-bearing save shadow as the
    // flat fields above. Rewrite every known ObjectId before a subclass either
    // publishes live perception state or preserves the record as shadow data;
    // otherwise graph renumbering could leave a stale numeric alias behind.
    std::vector<std::shared_ptr<Gff>> perceptions;
    size_t perceptionIndex = 0;
    for (const auto &savedPerception : record.getList("PerceptionList")) {
        auto perception = savedPerception->deepCopy();
        auto binding = object.savedReference(
            "Perception/" + std::to_string(perceptionIndex));
        put(*perception, Gff::Field::newDword(
                             "ObjectId",
                             binding && ids.contains(*binding)
                                 ? ids.objectId(*binding)
                                 : kSavedRuntimeInvalidObjectId));
        perceptions.push_back(std::move(perception));
        ++perceptionIndex;
    }
    if (record.has("PerceptionList")) {
        put(record, Gff::Field::newList(
                        "PerceptionList", std::move(perceptions)));
    }
}

void ModuleSnapshotBuilder::writeCombatRound(
    Gff &record, const Object &object, const ModuleObjectIdContext &ids) const {
    auto scheduled = _game.combat().saveScheduled(object);
    const auto creature = Creature::asCreature(object);
    const bool weaponIneffectiveReported = creature && creature->get().weaponIneffectiveReported();
    // The hold an engagement put on a creature owning no round is its round's pause.
    const auto hold = _game.combat().savedOwnerHold(object);
    const float scheduledTime = _game.combat().scheduledTime(object);
    // The creature's rounds that no queued command carries are kept here.
    auto rounds = _game.combat().saveRounds(object);
    if (scheduled.empty() && !weaponIneffectiveReported && !hold && scheduledTime == 0.0f && rounds.empty()) {
        removeSaveField(record, "CombatRoundData");
        return;
    }
    std::vector<std::shared_ptr<Gff>> entries;
    for (auto &action : scheduled) {
        auto entry = Gff::Builder().type(kScheduledActionStructType).field(Gff::Field::newInt("ActionTimer", action.timer)).field(Gff::Field::newWord("Animation", action.animation)).field(Gff::Field::newInt("AnimationTime", action.animationTime)).field(Gff::Field::newInt("NumAttacks", action.numAttacks)).field(Gff::Field::newByte("ActionType", action.type)).field(Gff::Field::newDword("Target", serializedReferenceId(action.target, ids))).field(Gff::Field::newByte("Retargettable", action.retargettable)).field(Gff::Field::newDword("InventorySlot", action.inventorySlot)).field(Gff::Field::newDword("TargetRepository", serializedReferenceId(action.repository, ids))).field(Gff::Field::newByte("Applied", action.applied)).field(Gff::Field::newFloat("PauseRemaining", action.remainingPause)).build();
        if (action.command) {
            normalizeActionReferences(*action.command, ids);
            put(*entry, Gff::Field::newStruct("Command", actionToGff(*action.command, &_game)));
        }
        entries.push_back(std::move(entry));
    }
    const auto holder = hold ? hold->pausedBy : nullptr;
    auto round = Gff::Builder().type(kCombatRoundStructType).field(Gff::Field::newInt("Timer", static_cast<int>(scheduledTime * 1000.0f))).field(Gff::Field::newList("SchedActionList", std::move(entries))).build();
    if (creature) {
        put(*round, Gff::Field::newByte("WeaponSucks", weaponIneffectiveReported ? 1 : 0));
    }
    put(*round, Gff::Field::newByte("RoundPaused", hold ? 1 : 0));
    put(*round, Gff::Field::newDword("RoundPausedBy",
                                     holder && ids.contains(*holder) ? ids.objectId(*holder) : kSavedRuntimeInvalidObjectId));
    put(*round, Gff::Field::newInt("PauseTimer", hold ? static_cast<int>(std::lround(hold->remaining * 1000.0f)) : 0));
    std::vector<std::shared_ptr<Gff>> roundEntries;
    for (auto &saved : rounds) {
        saved.clock.pauseOwner.id = serializedReferenceId(saved.clock.pauseOwner, ids);
        saved.clock.master.id = serializedReferenceId(saved.clock.master, ids);
        saved.clock.engaged.id = serializedReferenceId(saved.clock.engaged, ids);
        auto entry = saved.clock.toGff();
        put(*entry, Gff::Field::newByte("SpellCastRound", saved.spellRound ? 1 : 0));
        roundEntries.push_back(std::move(entry));
    }
    put(*round, Gff::Field::newList("RoundList", std::move(roundEntries)));
    put(record, Gff::Field::newStruct("CombatRoundData", std::move(round)));
}

uint32_t ModuleSnapshotBuilder::serializedReferenceId(
    const SavedObjectReference &reference,
    const ModuleObjectIdContext &ids) const {
    if (reference.isInvalid()) return kSavedRuntimeInvalidObjectId;
    if (reference.isRawValue()) {
        return reference.id;
    }
    auto bound = reference.boundObject();
    if (bound) {
        return ids.contains(*bound)
                   ? ids.objectId(*bound)
                   : kSavedRuntimeInvalidObjectId;
    }
    if (reference.isSerializedIdentity()) {
        return kSavedRuntimeInvalidObjectId;
    }
    auto found = _game._objectById.find(reference.id);
    return found != _game._objectById.end() && ids.contains(*found->second)
               ? ids.objectId(*found->second)
               : kSavedRuntimeInvalidObjectId;
}

EffectInstance ModuleSnapshotBuilder::normalizeEffectReferences(
    EffectInstance effect, const ModuleObjectIdContext &ids) const {
    auto normalizeId = [this, &ids, serialized = effect.hasSerializedObjectReferences()](uint32_t id) {
        if (id == kSavedRuntimeInvalidObjectId ||
            id == kSavedEffectInvalidObjectId) return id;
        if (serialized) return kSavedRuntimeInvalidObjectId;
        auto found = _game._objectById.find(id);
        return found != _game._objectById.end() && ids.contains(*found->second)
                   ? ids.objectId(*found->second)
                   : kSavedRuntimeInvalidObjectId;
    };
    if (auto creator = effect.boundCreator()) {
        effect.creatorId = ids.contains(*creator)
                               ? ids.objectId(*creator)
                               : kSavedRuntimeInvalidObjectId;
    } else {
        effect.creatorId = normalizeId(effect.creatorId);
    }
    for (size_t index = 0; index < effect.objectParameters.size(); ++index) {
        if (auto object = effect.boundObjectParameter(index)) {
            effect.objectParameters[index] = ids.contains(*object)
                                                   ? ids.objectId(*object)
                                                   : kSavedRuntimeInvalidObjectId;
        } else {
            effect.objectParameters[index] = normalizeId(
                effect.objectParameters[index]);
        }
    }
    return effect;
}

void ModuleSnapshotBuilder::normalizeSituationReferences(
    SerializedScriptSituation &situation,
    const ModuleObjectIdContext &ids) const {
    for (auto &value : situation.stack) {
        std::visit(
            Overloaded {
                [](std::monostate &) {},
                [](int32_t &) {},
                [](float &) {},
                [](std::string &) {},
                [this, &ids](SavedObjectReference &reference) {
                    reference.id = serializedReferenceId(reference, ids);
                },
                [this, &ids](EffectInstance &effect) {
                    effect = normalizeEffectReferences(std::move(effect), ids);
                },
                [this, &ids](SavedScriptEvent &event) {
                    for (auto &reference : event.objects) {
                        reference.id = serializedReferenceId(reference, ids);
                    }
                },
                [](SavedLocationValue &) {},
                [this, &ids](SavedTalentValue &talent) {
                    talent.item.id = serializedReferenceId(talent.item, ids);
                },
            },
            value.payload);
    }
}

void ModuleSnapshotBuilder::normalizeActionReferences(
    SavedActionRecord &action,
    const ModuleObjectIdContext &ids) const {
    if (action.cast && action.cast->itemUse) {
        auto &item = action.cast->itemUse->item;
        item.id = serializedReferenceId(item, ids);
    }
    if (action.round) {
        action.round->pauseOwner.id = serializedReferenceId(action.round->pauseOwner, ids);
        action.round->master.id = serializedReferenceId(action.round->master, ids);
        action.round->engaged.id = serializedReferenceId(action.round->engaged, ids);
    }
    if (action.physical) {
        for (auto &source : action.physical->sources) {
            source.id = serializedReferenceId(source, ids);
        }
        for (auto &list : action.physical->targetEffects)
            for (auto &effect : list) {
                effect = normalizeEffectReferences(std::move(effect), ids);
            }
        for (auto &history : action.physical->histories) {
            history.reactionObject.id = serializedReferenceId(history.reactionObject, ids);
            history.ammoItem.id = serializedReferenceId(history.ammoItem, ids);
        }
        if (auto &record = action.physical->roundRecord) {
            record->reactionObject.id = serializedReferenceId(record->reactionObject, ids);
            record->ammoItem.id = serializedReferenceId(record->ammoItem, ids);
        }
    }
    for (size_t index = 0; index < action.parameters.size(); ++index) {
        auto &parameter = action.parameters[index];
        // A PlayAnimation keeps its animation, not an object, in its object slot.
        if (action.type() == SavedActionType::PlayAnimation && index == 0) {
            continue;
        }
        if (auto reference = std::get_if<SavedObjectReference>(&parameter.payload)) {
            reference->id = serializedReferenceId(*reference, ids);
        } else if (auto situation = std::get_if<SerializedScriptSituation>(&parameter.payload)) {
            normalizeSituationReferences(*situation, ids);
        }
    }
}

void ModuleSnapshotBuilder::normalizeEventReferences(
    SavedEventRecord &event,
    const ModuleObjectIdContext &ids) const {
    event.object.id = serializedReferenceId(event.object, ids);
    event.caller.id = serializedReferenceId(event.caller, ids);
    if (auto situation = std::get_if<SerializedScriptSituation>(&event.payload)) {
        normalizeSituationReferences(*situation, ids);
    } else if (auto effect = std::get_if<EffectInstance>(&event.payload)) {
        *effect = normalizeEffectReferences(std::move(*effect), ids);
    } else if (auto hit = std::get_if<SavedWeaponImpact>(&event.payload)) {
        hit->source.id = serializedReferenceId(hit->source, ids);
    } else if (auto spell = std::get_if<SavedSpellImpact>(&event.payload)) {
        spell->caster.id = serializedReferenceId(spell->caster, ids);
        spell->target.id = serializedReferenceId(spell->target, ids);
        spell->area.id = serializedReferenceId(spell->area, ids);
        spell->item.id = serializedReferenceId(spell->item, ids);
    } else if (auto scriptEvent = std::get_if<SavedScriptEvent>(&event.payload)) {
        for (auto &reference : scriptEvent->objects) {
            reference.id = serializedReferenceId(reference, ids);
        }
    } else if (auto bag = std::get_if<SavedBodyBag>(&event.payload)) {
        bag->object.id = serializedReferenceId(bag->object, ids);
    } else if (auto broadcast = std::get_if<SavedBroadcastAoo>(&event.payload)) {
        broadcast->target.id = serializedReferenceId(broadcast->target, ids);
    } else if (auto combat = std::get_if<SavedCombatAttack>(&event.payload)) {
        combat->reactionObject.id = serializedReferenceId(
            combat->reactionObject, ids);
        combat->ammoItem.id = serializedReferenceId(combat->ammoItem, ids);
    }
}

std::shared_ptr<Gff> ModuleSnapshotBuilder::writeItem(
    const Item &item, uint32_t structType,
    std::optional<uint32_t> serializedId,
    const ModuleObjectIdContext *ids) const {
    auto result = objectBase(item, ResType::Uti, structType);
    if (ids) {
        writeObjectState(*result, item, *ids);
    } else {
        ModuleObjectIdContext local(
            SerializedIdentityContext::detachedRecord("standalone-item"));
        local.allocateObject(item);
        writeObjectState(*result, item, local);
    }
    if (serializedId) {
        put(*result, Gff::Field::newDword("ObjectId", *serializedId));
    } else {
        removeSaveField(*result, "ObjectId");
    }
    put(*result, Gff::Field::newInt("BaseItem", item._baseItem));
    put(*result, Gff::Field::newCExoLocString("LocalizedName", item._localizedName.id(), item._localizedName.str()));
    put(*result, Gff::Field::newCExoLocString("Description", item._description.id(), item._description.str()));
    put(*result, Gff::Field::newCExoLocString("DescIdentified", item._descIdentified.id(), item._descIdentified.str()));
    put(*result, Gff::Field::newByte("Charges", item._charges));
    put(*result, Gff::Field::newByte("MaxCharges", item._maxCharges));
    put(*result, Gff::Field::newDword("Cost", item.cost(_game.baseItemCostMultiplier(item.baseItemType()))));
    put(*result, Gff::Field::newDword("AddCost", item._addCost));
    put(*result, Gff::Field::newByte("Stolen", item._stolen));
    put(*result, Gff::Field::newWord("StackSize", item._stackSize));
    put(*result, Gff::Field::newDword("Upgrades", item._upgrades));
    put(*result, Gff::Field::newByte("Identified", item._identified));
    put(*result, Gff::Field::newByte("ModelVariation", item._modelVariation));
    put(*result, Gff::Field::newByte("BodyVariation", item._bodyVariation));
    put(*result, Gff::Field::newByte("TextureVar", item._textureVariation));
    put(*result, Gff::Field::newByte("Dropable", item._dropable));
    put(*result, Gff::Field::newByte("NonEquippable", item._nonEquippable));
    std::vector<std::shared_ptr<Gff>> properties;
    for (const auto &property : item._properties) {
        properties.push_back(Gff::Builder().type(0).field(Gff::Field::newByte("ChanceAppear", property.chanceAppear)).field(Gff::Field::newByte("CostTable", property.costTable)).field(Gff::Field::newWord("CostValue", property.costValue)).field(Gff::Field::newByte("Param1", property.paramTable)).field(Gff::Field::newByte("Param1Value", property.paramValue)).field(Gff::Field::newWord("PropertyName", property.propertyName)).field(Gff::Field::newWord("Subtype", property.subtype)).field(Gff::Field::newByte("UpgradeType", property.upgradeType)).field(Gff::Field::newByte("UsesPerDay", property.usesPerDay.value_or(Item::PropertyEntry::kUnrecordedUsesPerDay))).field(Gff::Field::newByte("Useable", property.usable)).field(Gff::Field::newDword("UsedDay", property.usedDay)).field(Gff::Field::newDword("UsedTime", property.usedTime)).build());
    }
    put(*result, Gff::Field::newList("PropertiesList", std::move(properties)));
    return result;
}

std::shared_ptr<Gff> ModuleSnapshotBuilder::writeCreature(
    const Creature &creature, uint32_t structType,
    std::optional<uint32_t> serializedId,
    const SerializedIdentityContext &outputIdentityContext) const {
    ModuleObjectIdContext ids(outputIdentityContext);
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
    ids.retainObject(creature);
    for (const Item *item : items) ids.retainObject(*item);
    ids.allocateObject(creature);
    for (const Item *item : items) ids.allocateObject(*item);
    return writeCreature(creature, structType, serializedId, ids, true);
}
std::shared_ptr<Gff> ModuleSnapshotBuilder::writeCreature(
    const Creature &creature, uint32_t structType,
    std::optional<uint32_t> serializedId,
    const ModuleObjectIdContext &ids,
    bool includeInventory) const {
    auto result = objectBase(creature, ResType::Utc, structType);
    writeObjectState(*result, creature, ids);
    if (serializedId) {
        put(*result, Gff::Field::newDword("ObjectId", *serializedId));
    } else {
        removeSaveField(*result, "ObjectId");
    }
    writeCreatureState(*result, creature);
    writeCreatureAttributes(*result, creature);
    writeCreatureScripts(*result, creature);
    writeCreatureItems(*result, creature, ids, includeInventory);
    writeCreaturePerception(*result, creature, ids);
    return result;
}

void ModuleSnapshotBuilder::writeCreatureState(Gff &record, const Creature &creature) const {
    putTransform(record, creature);
    const auto direction = glm::vec3(
        -std::sin(creature.getFacing()), std::cos(creature.getFacing()), 0.0f);
    put(record, Gff::Field::newFloat("XOrientation", direction.x));
    put(record, Gff::Field::newFloat("YOrientation", direction.y));
    put(record, Gff::Field::newFloat("ZOrientation", direction.z));
    put(record, Gff::Field::newByte("IsPC", creature._isPC));
    put(record, Gff::Field::newInt("PlayerCreated", creature._playerCreated));
    put(record, Gff::Field::newByte("StealthMode", creature._stealthMode));
    if (_game.isTSL()) {
        put(record, Gff::Field::newInt(
                        "AssignedPup", creature._assignedPuppet));
    }
    put(record, Gff::Field::newWord("FactionID", static_cast<uint16_t>(creature._faction)));
    put(record, Gff::Field::newWord("Appearance_Type", creature._appearance));
    put(record, Gff::Field::newByte("PM_IsDisguised", creature._disguised));
    if (creature._disguised) {
        put(record, Gff::Field::newWord("PM_Appearance", creature._appearanceBeforeDisguise));
    }
    put(record, Gff::Field::newByte("Gender", static_cast<uint8_t>(creature._gender)));
    put(record, Gff::Field::newShort("HitPoints", creature._hitPoints));
    put(record, Gff::Field::newShort("MaxHitPoints", creature.maxHitPoints()));
    put(record, Gff::Field::newShort(
                    "CurrentHitPoints", creature.serializedCurrentHitPoints()));
    put(record, Gff::Field::newByte("Dead", creature._dead));
    put(record, Gff::Field::newShort("ForcePoints", creature._forcePoints));
    put(record, Gff::Field::newShort("CurrentForce", creature.serializedCurrentForce()));
    put(record, Gff::Field::newShort("MaxForcePoints", creature.maxForcePoints()));
    if (_game.isTSL()) {
        put(record, Gff::Field::newChar("FuryDamageBonus", creature._furyDamageBonus));
    }
    // The level history is kept for a player character while no companion is
    // controlled in the player character's place. The player character
    // waiting for control back keeps the history it had when it gave control
    // up.
    if (creature._isPC &&
        (_game.party().controlledNpc() == kNpcPlayer || &creature == _game.party().actualPlayer().get())) {
        std::vector<std::shared_ptr<Gff>> levels;
        for (const auto &level : creature._levelStats) {
            levels.push_back(Gff::Builder().type(0).field(Gff::Field::newByte("LvlStatHitDie", level.hitDie)).field(Gff::Field::newByte("LvlStatForce", level.forcePoints)).build());
        }
        put(record, Gff::Field::newList("LvlStatList", std::move(levels)));
    } else {
        removeSaveField(record, "LvlStatList");
    }
    put(record, Gff::Field::newInt("TemporaryHP", creature._temporaryHitPoints));
    put(record, Gff::Field::newInt("TemporaryFP", creature._temporaryForcePoints));
    if (_game.isTSL()) {
        put(record, Gff::Field::newInt("BonusForcePoints", creature._bonusForcePoints));
    }
    put(record, Gff::Field::newDword("Experience", creature._xp));
    put(record, Gff::Field::newInt("JoiningXP", creature._joiningXP));
    put(record, Gff::Field::newByte("GoodEvil", creature._goodEvil));
    put(record, Gff::Field::newByte("Race", static_cast<uint8_t>(creature._race)));
    put(record, Gff::Field::newByte("SubraceIndex", static_cast<uint8_t>(creature._subrace)));
    put(record, Gff::Field::newByte("Disarmable", creature._disarmable));
    put(record, Gff::Field::newByte("NoPermDeath", creature._noPermDeath));
    put(record, Gff::Field::newByte("IsDestroyable", creature._destroyable));
    put(record, Gff::Field::newByte("IsRaiseable", creature._raiseable));
    put(record, Gff::Field::newByte("DeadSelectable", creature._selectableWhenDead));
    put(record, Gff::Field::newByte("NotReorienting", creature._notReorienting));
    put(record, Gff::Field::newByte("BodyVariation", creature._bodyVariation));
    put(record, Gff::Field::newByte("TextureVar", creature._textureVar));
    put(record, Gff::Field::newByte("PartyInteract", creature._partyInteract));
    // The ambient state and the pose the creature was last given.
    put(record, Gff::Field::newByte("AmbientAnimState", static_cast<uint8_t>(creature._effectAmbientState)));
    const auto deadAnimation = [&creature]() {
        switch (creature._deathPose.value_or(Creature::DeathPose::Dead)) {
        case Creature::DeathPose::Dead1:
            return kDead1Animation;
        case Creature::DeathPose::Dead3:
            return kDead3Animation;
        default:
            return kDeadAnimation;
        }
    };
    put(record, Gff::Field::newInt("Animation",
                                   creature._dead                 ? deadAnimation()
                                   : creature._damageFlinchHeld   ? kDamageFlinchAnimation
                                   : creature._combatState.active ? kReadyAnimation
                                                                  : kPauseAnimation));
    put(record, Gff::Field::newByte(
                    "CreatnScrptFird", creature._spawnScriptFired));
    if (_game.isTSL()) {
        put(record, Gff::Field::newDword(
                        "CurrentForm", static_cast<uint32_t>(creature._currentForm)));
        put(record, Gff::Field::newByte(
                        "MultiplierSet", creature._autoBalanceContext.multiplierSet));
        if (creature._autoBalancePlayerLevelAtSpawnSet) {
            put(record, Gff::Field::newByte(
                            "PCLevelAtSpawn", creature._autoBalanceContext.playerLevelAtSpawn));
        } else {
            removeSaveField(record, "PCLevelAtSpawn");
        }
    }
    put(record, Gff::Field::newByte(
                    "MovementRate", static_cast<uint8_t>(std::clamp(creature._walkRate, 0, 255))));
    put(record, Gff::Field::newByte("Listening", creature._isListening));
    put(record, Gff::Field::newByte("NaturalAC", creature._naturalAC));
    put(record, Gff::Field::newShort("refbonus", creature._refBonus));
    put(record, Gff::Field::newShort("willbonus", creature._willBonus));
    put(record, Gff::Field::newShort("fortbonus", creature._fortBonus));
    put(record, Gff::Field::newFloat("ChallengeRating", creature._challengeRating));
    put(record, Gff::Field::newInt("AIState", static_cast<int>(creature._aiStyle)));
    put(record, Gff::Field::newWord("SoundSetFile", creature._soundSetId));
    put(record, Gff::Field::newByte("BodyBag", creature._bodyBagId));
}

void ModuleSnapshotBuilder::writeCreatureAttributes(Gff &record, const Creature &creature) const {
    put(record, Gff::Field::newByte("Str", creature._attributes.strength()));
    put(record, Gff::Field::newByte("Dex", creature._attributes.dexterity()));
    put(record, Gff::Field::newByte("Con", creature._attributes.constitution()));
    put(record, Gff::Field::newByte("Int", creature._attributes.intelligence()));
    put(record, Gff::Field::newByte("Wis", creature._attributes.wisdom()));
    put(record, Gff::Field::newByte("Cha", creature._attributes.charisma()));
    auto oldSkills = record.getList("SkillList");
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
    put(record, Gff::Field::newList("SkillList", std::move(skills)));
    std::map<uint16_t, std::shared_ptr<Gff>> oldFeats;
    for (const auto &feat : record.getList("FeatList")) {
        oldFeats.emplace(
            static_cast<uint16_t>(feat->getUint("Feat")), feat->deepCopy());
    }
    std::vector<std::shared_ptr<Gff>> feats;
    for (auto feat : creature._attributes.featOrder()) {
        auto type = static_cast<uint16_t>(feat);
        auto found = oldFeats.find(type);
        auto featRecord = found != oldFeats.end()
                              ? found->second
                              : emptyRecord(1);
        put(*featRecord, Gff::Field::newWord("Feat", type));
        feats.push_back(std::move(featRecord));
    }
    put(record, Gff::Field::newList("FeatList", std::move(feats)));
    std::map<int32_t, std::shared_ptr<Gff>> oldClasses;
    for (const auto &clazz : record.getList("ClassList")) {
        oldClasses.emplace(clazz->getInt("Class"), clazz->deepCopy());
    }
    std::vector<std::shared_ptr<Gff>> classes;
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
        for (auto spell : creature._attributes.spellsForClass(clazz->type())) {
            spells.push_back(Gff::Builder().type(3).field(Gff::Field::newWord(
                                                              "Spell", static_cast<uint16_t>(spell)))
                                 .build());
        }
        put(*classRecord, Gff::Field::newList("KnownList0", std::move(spells)));
        classes.push_back(std::move(classRecord));
    }
    put(record, Gff::Field::newList("ClassList", std::move(classes)));
    std::vector<std::shared_ptr<Gff>> abilities;
    for (const auto &ability : creature._spellLikeAbilities) {
        abilities.push_back(Gff::Builder().type(kSpecialAbilityStructType).field(Gff::Field::newWord("Spell", ability.spell)).field(Gff::Field::newByte("SpellFlags", ability.flags)).field(Gff::Field::newByte("SpellCasterLevel", ability.casterLevel)).build());
    }
    put(record, Gff::Field::newList("SpecAbilityList", std::move(abilities)));
}

void ModuleSnapshotBuilder::writeCreatureScripts(Gff &record, const Creature &creature) const {
    put(record, Gff::Field::newResRef("ScriptHeartbeat", creature._onHeartbeat));
    put(record, Gff::Field::newResRef("ScriptUserDefine", creature._onUserDefined));
    put(record, Gff::Field::newResRef("ScriptOnNotice", creature._onNotice));
    put(record, Gff::Field::newResRef("ScriptSpellAt", creature._onSpellAt));
    put(record, Gff::Field::newResRef("ScriptAttacked", creature._onAttacked));
    put(record, Gff::Field::newResRef("ScriptDamaged", creature._onDamaged));
    put(record, Gff::Field::newResRef("ScriptDisturbed", creature._onDisturbed));
    put(record, Gff::Field::newResRef("ScriptEndRound", creature._onEndRound));
    put(record, Gff::Field::newResRef("ScriptEndDialogu", creature._onEndDialogue));
    put(record, Gff::Field::newResRef("ScriptDialogue", creature._onDialogue));
    put(record, Gff::Field::newResRef("ScriptSpawn", creature._onSpawn));
    put(record, Gff::Field::newResRef("ScriptDeath", creature._onDeath));
    put(record, Gff::Field::newResRef("ScriptOnBlocked", creature._onBlocked));
}

void ModuleSnapshotBuilder::writeCreatureItems(
    Gff &record, const Creature &creature, const ModuleObjectIdContext &ids, bool includeInventory) const {
    std::set<const Item *> equipped;
    std::vector<std::shared_ptr<Gff>> equipList;
    for (const auto &[slot, item] : creature._equipment) {
        if (!item) continue;
        equipped.insert(item.get());
        auto saved = writeItem(
            *item, 1u << slot, ids.objectId(*item), &ids);
        saved->setType(1u << slot);
        equipList.push_back(std::move(saved));
    }
    std::vector<std::shared_ptr<Gff>> inventory;
    if (includeInventory) {
        for (const auto &item : creature._items) {
            if (!item || equipped.count(item.get()) != 0) continue;
            inventory.push_back(writeItem(
                *item, 0, ids.objectId(*item), &ids));
        }
    }
    put(record, Gff::Field::newList("Equip_ItemList", std::move(equipList)));
    put(record, Gff::Field::newList("ItemList", std::move(inventory)));
}

void ModuleSnapshotBuilder::writeCreaturePerception(
    Gff &record, const Creature &creature, const ModuleObjectIdContext &ids) const {
    std::map<uint32_t, std::shared_ptr<Gff>> oldPerceptions;
    for (const auto &perception : record.getList("PerceptionList")) {
        oldPerceptions.emplace(
            perception->getUint("ObjectId", kSavedRuntimeInvalidObjectId),
            perception->deepCopy());
    }
    std::set<uint32_t> perceived;
    for (const auto &[id, reference] : creature._perception.seen) {
        if (reference.resolve()) perceived.insert(id);
    }
    for (const auto &[id, reference] : creature._perception.heard) {
        if (reference.resolve()) perceived.insert(id);
    }
    for (const auto &[id, reference] : creature._perception.invisible) {
        if (reference.resolve()) {
            perceived.insert(id);
        }
    }
    std::vector<std::shared_ptr<Gff>> perceptions;
    for (uint32_t id : perceived) {
        auto objectIt = _game._objectById.find(id);
        if (objectIt == _game._objectById.end() ||
            !ids.contains(*objectIt->second)) continue;
        uint32_t savedId = ids.objectId(*objectIt->second);
        auto found = oldPerceptions.find(savedId);
        auto perception = found != oldPerceptions.end()
                              ? found->second
                              : emptyRecord(0);
        uint8_t data = static_cast<uint8_t>(
            perception->getUint("PerceptionData") & ~(kPerceptionSeenBit | kPerceptionHeardBit));
        if (creature._perception.sees(id)) {
            data |= kPerceptionSeenBit;
        }
        // The invisible mark is written into the heard bit.
        if (creature._perception.hears(id) || creature._perception.isInvisible(id)) {
            data |= kPerceptionHeardBit;
        }
        put(*perception, Gff::Field::newDword("ObjectId", savedId));
        put(*perception, Gff::Field::newByte("PerceptionData", data));
        perceptions.push_back(std::move(perception));
    }
    put(record, Gff::Field::newList("PerceptionList", std::move(perceptions)));
}

std::shared_ptr<Gff> ModuleSnapshotBuilder::writeDoor(
    const Door &door, const ModuleObjectIdContext &ids) const {
    auto result = objectBase(door, ResType::Utd, 8);
    // Write current Plot state. Remove an input-only Invulnerable value from the
    // resource shadow so it cannot override Plot on the next load.
    removeSaveField(*result, "Invulnerable");
    writeObjectState(*result, door, ids);
    put(*result, Gff::Field::newByte("Static", door._static));
    putTransform(*result, door, true);
    put(*result, Gff::Field::newDword("Appearance", door._appearance));
    put(*result, Gff::Field::newByte("GenericType", door._genericType));
    put(*result, Gff::Field::newDword("Faction", static_cast<uint32_t>(door._faction)));
    put(*result, Gff::Field::newByte("OpenState", static_cast<uint8_t>(door._state)));
    put(*result, Gff::Field::newByte("Locked", door._locked));
    put(*result, Gff::Field::newByte("Lockable", door._lockable));
    put(*result, Gff::Field::newShort("HP", door._hitPoints));
    put(*result, Gff::Field::newShort("CurrentHP", door._currentHitPoints));
    put(*result, Gff::Field::newByte("Dead", door.isDead()));
    put(*result, Gff::Field::newByte("TrapFlag", door.isTrapped()));
    put(*result, Gff::Field::newByte("TrapType", door.trapBaseType()));
    put(*result, Gff::Field::newByte("TrapDisarmable", door.trapDisarmable()));
    put(*result, Gff::Field::newByte("TrapDetectable", door.trapDetectable()));
    // Saved DCs are the difficulty-shifted values, so they shift again on every save.
    put(*result, Gff::Field::newByte("DisarmDC", static_cast<uint8_t>(door.trapDisarmDC())));
    put(*result, Gff::Field::newByte("TrapDetectDC", static_cast<uint8_t>(door.trapDetectDC())));
    put(*result, Gff::Field::newByte("TrapOneShot", door.trapOneShot()));
    if (_game.isTSL()) {
        put(*result, Gff::Field::newInt("OwnerDemolitions", door.ownerDemolitionsSkill()));
    }
    put(*result, Gff::Field::newByte("KeyRequired", door._keyRequired));
    put(*result, Gff::Field::newByte("AutoRemoveKey", door._autoRemoveKey));
    put(*result, Gff::Field::newByte("OpenLockDC", door._openLockDC));
    put(*result, Gff::Field::newByte("CloseLockDC", door._closeLockDC));
    put(*result, Gff::Field::newByte("LinkedToFlags", door._linkedToFlags));
    put(*result, Gff::Field::newCExoString("LinkedTo", door._linkedTo));
    put(*result, Gff::Field::newResRef("LinkedToModule", door._linkedToModule));
    put(*result, Gff::Field::newCExoLocString(
        "TransitionDestin", -1, door._transitionDestin.str()));
    return result;
}

std::shared_ptr<Gff> ModuleSnapshotBuilder::writePlaceable(
    const Placeable &placeable, const ModuleObjectIdContext &ids) const {
    auto result = objectBase(placeable, ResType::Utp, 9);
    writeObjectState(*result, placeable, ids);
    putTransform(*result, placeable, true);
    put(*result, Gff::Field::newInt("Animation", placeable._animation));
    put(*result, Gff::Field::newByte("Open", placeable._open));
    put(*result, Gff::Field::newDword("Faction", static_cast<uint32_t>(placeable._faction)));
    put(*result, Gff::Field::newByte("Locked", placeable._locked));
    put(*result, Gff::Field::newByte("Lockable", placeable._lockable));
    put(*result, Gff::Field::newShort("HP", placeable._hitPoints));
    put(*result, Gff::Field::newShort("CurrentHP", placeable._currentHitPoints));
    put(*result, Gff::Field::newByte("Dead", placeable.isDead()));
    put(*result, Gff::Field::newByte("Useable", placeable._usable));
    put(*result, Gff::Field::newByte("HasInventory", placeable._hasInventory));
    put(*result, Gff::Field::newByte("TrapFlag", placeable.isTrapped()));
    put(*result, Gff::Field::newByte("TrapType", placeable.trapBaseType()));
    put(*result, Gff::Field::newByte("TrapDisarmable", placeable.trapDisarmable()));
    put(*result, Gff::Field::newByte("TrapDetectable", placeable.trapDetectable()));
    // Saved DCs are the difficulty-shifted values, so they shift again on every save.
    put(*result, Gff::Field::newByte("DisarmDC", static_cast<uint8_t>(placeable.trapDisarmDC())));
    put(*result, Gff::Field::newByte("TrapDetectDC", static_cast<uint8_t>(placeable.trapDetectDC())));
    put(*result, Gff::Field::newByte("TrapOneShot", placeable.trapOneShot()));
    if (_game.isTSL()) {
        put(*result, Gff::Field::newInt("OwnerDemolitions", placeable.ownerDemolitionsSkill()));
    }
    // A portrait ID naming a row is saved in place of the portrait resref.
    if (placeable._portraitId != Placeable::kNoPortraitId) {
        put(*result, Gff::Field::newWord("PortraitId", placeable._portraitId));
    } else {
        put(*result, Gff::Field::newResRef("Portrait", placeable._portrait));
    }
    // A body bag is made at runtime with no blueprint, so it carries all of
    // its own fields.
    if (placeable._isBodyBag) {
        put(*result, Gff::Field::newDword("Appearance", placeable._appearance));
        put(*result, Gff::Field::newCExoLocString("LocName", placeable._locName.id(), placeable._locName.str()));
        put(*result, Gff::Field::newCExoLocString("Description", placeable._description.id(), placeable._description.str()));
        put(*result, Gff::Field::newByte("OpenLockDC", placeable._openLockDC));
        put(*result, Gff::Field::newByte("Hardness", placeable._hardness));
        put(*result, Gff::Field::newByte("Fort", placeable._fort));
        put(*result, Gff::Field::newByte("Will", placeable._will));
        put(*result, Gff::Field::newByte("Ref", placeable._ref));
        put(*result, Gff::Field::newByte("Static", placeable._static));
        put(*result, Gff::Field::newByte("PartyInteract", placeable._partyInteract));
        put(*result, Gff::Field::newByte("BodyBag", placeable._bodyBagId));
        put(*result, Gff::Field::newByte("DieWhenEmpty", placeable._dieWhenEmpty));
        put(*result, Gff::Field::newByte("IsBodyBag", placeable._isBodyBag));
        put(*result, Gff::Field::newByte("IsBodyBagVisible", placeable._isBodyBagVisible));
        put(*result, Gff::Field::newByte("IsCorpse", placeable._isCorpse));
    }
    std::vector<std::shared_ptr<Gff>> items;
    for (const auto &item : placeable._items) if (item) items.push_back(
        writeItem(*item, 0, ids.objectId(*item), &ids));
    put(*result, Gff::Field::newList("ItemList", std::move(items)));
    return result;
}

std::shared_ptr<Gff> ModuleSnapshotBuilder::writeTrigger(
    const Trigger &trigger, const ModuleObjectIdContext &ids) const {
    auto result = objectBase(trigger, ResType::Utt, 1);
    writeObjectState(*result, trigger, ids);
    putTransform(*result, trigger);
    put(*result, Gff::Field::newResRef("ScriptOnEnter", trigger._onEnter));
    put(*result, Gff::Field::newResRef("ScriptOnExit", trigger._onExit));
    put(*result, Gff::Field::newResRef("OnDisarm", trigger.trap().disarmScript()));
    put(*result, Gff::Field::newResRef("OnTrapTriggered", trigger.trap().triggeredScript()));
    put(*result, Gff::Field::newByte("TrapType", trigger.trapBaseType()));
    put(*result, Gff::Field::newByte("TrapOneShot", trigger.trapOneShot()));
    put(*result, Gff::Field::newByte("TrapDisarmable", trigger.trapDisarmable()));
    put(*result, Gff::Field::newByte("TrapDetectable", trigger.trapDetectable()));
    put(*result, Gff::Field::newInt("Type", trigger._triggerType));
    put(*result, Gff::Field::newDword("Faction", static_cast<uint32_t>(trigger._faction)));
    put(*result, Gff::Field::newByte("SetByPlayerParty", trigger._setByPlayerParty));
    put(*result, Gff::Field::newDword("CreatorId", serializedReferenceId(trigger._creator, ids)));
    if (_game.isTSL()) {
        put(*result, Gff::Field::newInt("OwnerDemolitions", trigger.ownerDemolitionsSkill()));
    }
    std::vector<std::shared_ptr<Gff>> geometry;
    for (const auto &point : trigger._geometry) {
        geometry.push_back(Gff::Builder().type(3)
            .field(Gff::Field::newFloat("PointX", point.x))
            .field(Gff::Field::newFloat("PointY", point.y))
            .field(Gff::Field::newFloat("PointZ", point.z)).build());
    }
    put(*result, Gff::Field::newList("Geometry", std::move(geometry)));
    put(*result, Gff::Field::newByte("LinkedToFlags", trigger._linkedToFlags));
    put(*result, Gff::Field::newCExoString("LinkedTo", trigger._linkedTo));
    put(*result, Gff::Field::newResRef("LinkedToModule", trigger._linkedToModule));
    put(*result, Gff::Field::newCExoLocString(
                     "TransitionDestin", trigger._transitionDestin.id(), trigger._transitionDestin.str()));
    return result;
}

std::shared_ptr<Gff> ModuleSnapshotBuilder::writeEncounter(
    const Encounter &encounter, const ModuleObjectIdContext &ids) const {
    auto result = objectBase(encounter, ResType::Ute, 7);
    writeObjectState(*result, encounter, ids);
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
    auto savedId = [&](const std::shared_ptr<Object> &object) {
        return object && ids.contains(*object) ? ids.objectId(*object) : kSavedRuntimeInvalidObjectId;
    };
    put(*result, Gff::Field::newDword("LastEntered", savedId(encounter.lastEntered())));
    put(*result, Gff::Field::newDword("LastLeft", savedId(encounter.lastLeft())));
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
                object && ids.contains(*object)
                    ? ids.objectId(*object)
                    : kSavedRuntimeInvalidObjectId)).build());
    }
    put(*result, Gff::Field::newList("AreaList", std::move(areaList)));

    std::vector<std::shared_ptr<Gff>> creatures;
    for (const auto &creature : encounter._creatures) {
        creatures.push_back(Gff::Builder().type(0).field(Gff::Field::newDword("Appearance", creature._appearance)).field(Gff::Field::newFloat("CR", creature._cr)).field(Gff::Field::newResRef("ResRef", creature._resRef)).field(Gff::Field::newByte("SingleSpawn", creature._singleSpawn)).field(Gff::Field::newInt("GuaranteedCount", creature._guaranteedCount)).build());
    }
    put(*result, Gff::Field::newList("CreatureList", std::move(creatures)));
    std::vector<std::shared_ptr<Gff>> pendingSpawns;
    for (const auto &entry : encounter._pendingSpawns) {
        pendingSpawns.push_back(Gff::Builder().type(0).field(Gff::Field::newResRef("SpawnResRef", entry.resRef)).field(Gff::Field::newFloat("SpawnCR", entry.challengeRating)).build());
    }
    put(*result, Gff::Field::newList("SpawnList", std::move(pendingSpawns)));
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

std::shared_ptr<Gff> ModuleSnapshotBuilder::writeAreaOfEffect(
    const AreaOfEffect &areaOfEffect, const ModuleObjectIdContext &ids) const {
    auto result = objectBase(areaOfEffect, ResType::Invalid, AreaOfEffect::kSaveStructType);
    writeObjectState(*result, areaOfEffect, ids);
    auto savedId = [&](const std::shared_ptr<Object> &object) {
        return object && ids.contains(*object) ? ids.objectId(*object) : kSavedRuntimeInvalidObjectId;
    };
    put(*result, Gff::Field::newInt("AreaEffectId", areaOfEffect._areaEffectId));
    put(*result, Gff::Field::newDword("SpellId", areaOfEffect.effectSpellId()));
    put(*result, Gff::Field::newByte("Shape", static_cast<uint8_t>(areaOfEffect._shape)));
    put(*result, Gff::Field::newByte("MetaMagicType", static_cast<uint8_t>(areaOfEffect._metaMagic)));
    put(*result, Gff::Field::newInt("SpellSaveDC", areaOfEffect._spellSaveDC));
    put(*result, Gff::Field::newInt("SpellLevel", areaOfEffect._spellLevel));
    if (areaOfEffect._shape == AreaOfEffect::Shape::Rectangle) {
        put(*result, Gff::Field::newFloat("Length", areaOfEffect._length));
        put(*result, Gff::Field::newFloat("Width", areaOfEffect._width));
    } else {
        put(*result, Gff::Field::newFloat("Radius", areaOfEffect._radius));
    }
    put(*result, Gff::Field::newDword("LinkedToObject", savedId(areaOfEffect.carrier())));
    put(*result, Gff::Field::newDword("LastEntered", savedId(areaOfEffect.lastEntered())));
    put(*result, Gff::Field::newDword("LastLeft", savedId(areaOfEffect.lastLeft())));
    put(*result, Gff::Field::newDword("Duration",
                                      static_cast<uint32_t>(std::max(0.0f, areaOfEffect._remainingDuration) * 1000.0f)));
    put(*result, Gff::Field::newByte("DurationType", static_cast<uint8_t>(areaOfEffect._durationType)));
    put(*result, Gff::Field::newDword("LastHrtbtDay", areaOfEffect._heartbeatDay));
    put(*result, Gff::Field::newDword("LastHrtbtTime", areaOfEffect._heartbeatTime));
    put(*result, Gff::Field::newCExoString("OnHeartbeat", areaOfEffect.getOnHeartbeat()));
    put(*result, Gff::Field::newCExoString("OnUserDefined", areaOfEffect.getOnUserDefined()));
    put(*result, Gff::Field::newCExoString("OnObjEnter", areaOfEffect._onEnter));
    put(*result, Gff::Field::newCExoString("OnObjExit", areaOfEffect._onExit));
    const auto &position = areaOfEffect.position();
    put(*result, Gff::Field::newFloat("PositionX", position.x));
    put(*result, Gff::Field::newFloat("PositionY", position.y));
    put(*result, Gff::Field::newFloat("PositionZ", position.z));
    const float facing = scriptFacingFromObject(areaOfEffect.getFacing());
    put(*result, Gff::Field::newFloat("OrientationX", std::cos(facing)));
    put(*result, Gff::Field::newFloat("OrientationY", std::sin(facing)));
    put(*result, Gff::Field::newFloat("OrientationZ", 0.0f));
    return result;
}

std::shared_ptr<Gff> ModuleSnapshotBuilder::writeStore(
    const Store &store, const ModuleObjectIdContext &ids) const {
    auto result = objectBase(store, ResType::Utm, 11);
    writeObjectState(*result, store, ids);
    putTransform(*result, store);
    const auto direction = glm::vec3(-std::sin(store.getFacing()), std::cos(store.getFacing()), 0.0f);
    put(*result, Gff::Field::newFloat("XOrientation", direction.x));
    put(*result, Gff::Field::newFloat("YOrientation", direction.y));
    put(*result, Gff::Field::newInt("MarkDown", store._markDown));
    put(*result, Gff::Field::newInt("MarkUp", store._markUp));
    put(*result, Gff::Field::newResRef("OnOpenStore", store._onOpenStore));
    put(*result, Gff::Field::newByte("BuySellFlag", store._buySellFlag));
    std::vector<std::shared_ptr<Gff>> items;
    for (const auto &item : store._items) if (item) items.push_back(
        writeItem(*item, 0, ids.objectId(*item), &ids));
    put(*result, Gff::Field::newList("ItemList", std::move(items)));
    return result;
}

std::shared_ptr<Gff> ModuleSnapshotBuilder::writeWaypoint(
    const Waypoint &waypoint, const ModuleObjectIdContext &ids) const {
    auto result = objectBase(waypoint, ResType::Utw, 5);
    writeObjectState(*result, waypoint, ids);
    putTransform(*result, waypoint);
    auto direction = glm::vec3(-std::sin(waypoint.getFacing()), std::cos(waypoint.getFacing()), 0.0f);
    put(*result, Gff::Field::newFloat("XOrientation", direction.x));
    put(*result, Gff::Field::newFloat("YOrientation", direction.y));
    put(*result, Gff::Field::newByte("HasMapNote", waypoint._hasMapNote));
    put(*result, Gff::Field::newByte("MapNoteEnabled", waypoint._mapNoteEnabled));
    return result;
}

std::shared_ptr<Gff> ModuleSnapshotBuilder::writeSound(
    const Sound &sound, const ModuleObjectIdContext &ids) const {
    auto result = objectBase(sound, ResType::Uts, 6);
    writeObjectState(*result, sound, ids);
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

    // A runtime snapshot is an authoritative instance graph even when its
    // shadow originated in an installed template GIT.
    put(*result, Gff::Field::newByte("UseTemplates", 0));

    // The pending transition is recorded at the top level too.
    put(*result, Gff::Field::newByte("TransPending", area._transitionPending));
    put(*result, Gff::Field::newByte("TransPendNextID", area._transitionPendingNextId));
    put(*result, Gff::Field::newByte("TransPendCurrID", area._transitionPendingCurrentId));

    // The area properties carry the music and ambient sound settings, then the
    // area flags and the fog colour; what is playing is not saved.
    const auto &audio = area._ambientAudio;
    auto properties = Gff::Builder().type(kAreaPropertiesStructType).field(Gff::Field::newInt("MusicDelay", audio.musicDelay)).field(Gff::Field::newInt("MusicDay", audio.musicDay)).field(Gff::Field::newInt("MusicNight", audio.musicNight)).field(Gff::Field::newInt("MusicBattle", audio.musicBattle)).field(Gff::Field::newInt("AmbientSndDay", audio.ambientSoundDay)).field(Gff::Field::newInt("AmbientSndNight", audio.ambientSoundNight)).field(Gff::Field::newInt("AmbientSndDayVol", audio.ambientSoundDayVolume)).field(Gff::Field::newInt("AmbientSndNitVol", audio.ambientSoundNightVolume)).build();
    put(*properties, Gff::Field::newByte("Unescapable", area._unescapable));
    if (_game.isTSL()) {
        put(*properties, Gff::Field::newByte("DisableTransit", area._transitDisabled));
    }
    put(*properties, Gff::Field::newByte("RestrictMode", area._playerRestrictMode));
    put(*properties, Gff::Field::newByte("TransPending", area._transitionPending));
    put(*properties, Gff::Field::newByte("TransPendNextID", area._transitionPendingNextId));
    put(*properties, Gff::Field::newByte("TransPendCurrID", area._transitionPendingCurrentId));
    put(*properties, Gff::Field::newDword("SunFogColor", area._sunFogColor));
    put(*result, Gff::Field::newStruct("AreaProperties", std::move(properties)));

    std::map<std::string, std::vector<std::shared_ptr<Gff>>> lists {
        {"Creature List", {}}, {"Door List", {}}, {"Placeable List", {}}, {"TriggerList", {}}, {"Encounter List", {}}, {"StoreList", {}}, {"WaypointList", {}}, {"SoundList", {}}, {"List", {}}, {"AreaEffectList", {}}};
    lists["CameraList"] = {};
    for (const auto &object : area._objects) {
        if (!object || area._objectsToDestroy.count(object->id()) != 0) continue;
        if (object->type() == ObjectType::Creature &&
            _game._party.isRetainedRuntimeRepresentation(
                *static_cast<const Creature *>(object.get()))) continue;
        switch (object->type()) {
        case ObjectType::Creature:
            lists["Creature List"].push_back(writeCreature(
                *static_cast<Creature *>(object.get()), 4,
                ids.objectId(*object), ids));
            break;
        case ObjectType::Door:
            lists["Door List"].push_back(
                writeDoor(*static_cast<Door *>(object.get()), ids));
            break;
        case ObjectType::Placeable:
            lists["Placeable List"].push_back(writePlaceable(*static_cast<Placeable *>(object.get()), ids));
            break;
        case ObjectType::Trigger:
            // Linked-door thresholds are runtime collision helpers derived
            // from the durable door linkage. Persisting them as authored GIT
            // triggers reconstructs stale ordinary triggers on the next load.
            if (!static_cast<Trigger *>(object.get())->isLinkedDoorTransition()) {
                lists["TriggerList"].push_back(
                    writeTrigger(*static_cast<Trigger *>(object.get()), ids));
            }
            break;
        case ObjectType::Encounter:
            lists["Encounter List"].push_back(
                writeEncounter(*static_cast<Encounter *>(object.get()), ids));
            break;
        case ObjectType::Store:
            lists["StoreList"].push_back(writeStore(*static_cast<Store *>(object.get()), ids));
            break;
        case ObjectType::Waypoint:
            lists["WaypointList"].push_back(
                writeWaypoint(*static_cast<Waypoint *>(object.get()), ids));
            break;
        case ObjectType::Sound:
            lists["SoundList"].push_back(
                writeSound(*static_cast<Sound *>(object.get()), ids));
            break;
        case ObjectType::Camera:
            lists["CameraList"].push_back(writeCamera(
                *static_cast<StaticCamera *>(object.get())));
            break;
        case ObjectType::Item: {
            auto item = writeItem(
                *static_cast<Item *>(object.get()), 0,
                ids.objectId(*object), &ids);
            putTransform(*item, *object, true);
            lists["List"].push_back(std::move(item));
            break;
        }
        case ObjectType::AreaOfEffect:
            lists["AreaEffectList"].push_back(
                writeAreaOfEffect(*static_cast<AreaOfEffect *>(object.get()), ids));
            break;
        default:
            throw ValidationException("active area contains an unsupported save object type");
        }
    }
    for (auto &[label, list] : lists) {
        put(*result, Gff::Field::newList(label, std::move(list)));
    }
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
    put(*result, Gff::Field::newDword("Mod_Area", ids.objectId(area)));
    put(*result, Gff::Field::newFloat("Mod_Entry_X", module._info.entryPosition.x));
    put(*result, Gff::Field::newFloat("Mod_Entry_Y", module._info.entryPosition.y));
    put(*result, Gff::Field::newFloat("Mod_Entry_Z", module._info.entryPosition.z));
    put(*result, Gff::Field::newFloat("Mod_Entry_Dir_X", -std::sin(module._info.entryFacing)));
    put(*result, Gff::Field::newFloat("Mod_Entry_Dir_Y", std::cos(module._info.entryFacing)));
    // Split the canonical clock into the day/time pair. Records written here are
    // therefore always normalized: Mod_PauseTime is below one day length.
    put(*result, Gff::Field::newDword("Mod_PauseDay", _game.worldTimeDay()));
    put(*result, Gff::Field::newDword("Mod_PauseTime", _game.worldTimeOfDay()));
    put(*result, Gff::Field::newByte("Mod_MinPerHour", _game._minutesPerHour));
    put(*result, Gff::Field::newDword64("Mod_Effect_NxtId", _game._effectIds.nextId()));
    put(*result, Gff::Field::newStruct("SWVarTable", writeLocals(module)));

    auto areaEntry = Gff::Builder().type(6)
        .field(Gff::Field::newResRef("Area_Name", area._name))
        .field(Gff::Field::newDword("ObjectId", ids.objectId(area))).build();
    put(*result, Gff::Field::newList("Mod_Area_list", {areaEntry}));

    // Four allocation cursors: ordinary identities count up from the bottom of
    // the namespace, character identities count down from its ceiling, each
    // for local and for external objects. An absent retained cursor is the
    // initial value of a module never saved before.
    uint32_t shadowNext = result->getUint("Mod_NextObjId0", 2);
    put(*result, Gff::Field::newDword(
        "Mod_NextObjId0", ids.nextId(shadowNext)));
    uint32_t shadowNextCharacter = result->getUint(
        "Mod_NextCharId0", kSavedRuntimeMaxCharacterObjectId);
    put(*result, Gff::Field::newDword(
                     "Mod_NextCharId0", ids.nextCharacterId(shadowNextCharacter)));
    // External identities name objects owned by another simulation. A saved
    // module graph holds none, so both external cursors keep their initial
    // values.
    put(*result, Gff::Field::newDword("Mod_NextObjId1", 0));
    put(*result, Gff::Field::newDword(
                     "Mod_NextCharId1", kSavedRuntimeMaxCharacterObjectId));

    std::vector<std::shared_ptr<Gff>> tokens;
    for (const auto &[number, value] : _game._customTokens) {
        if (number <= Game::kLastReservedCustomToken) {
            continue;
        }
        tokens.push_back(Gff::Builder().type(7)
            .field(Gff::Field::newDword("Mod_TokensNumber", static_cast<uint32_t>(number)))
            .field(Gff::Field::newCExoString("Mod_TokensValue", value)).build());
    }
    put(*result, Gff::Field::newList("Mod_Tokens", std::move(tokens)));

    std::vector<std::shared_ptr<Gff>> events;
    std::vector<SavedEventRecord> eventSnapshot = module.saveEventSnapshot();
    std::stable_sort(
        eventSnapshot.begin(), eventSnapshot.end(),
        [](const SavedEventRecord &left, const SavedEventRecord &right) {
            return std::tie(left.day, left.time) < std::tie(right.day, right.time);
        });
    for (auto event : eventSnapshot) {
        normalizeEventReferences(event, ids);
        events.push_back(eventToGff(event, &_game));
    }
    put(*result, Gff::Field::newList("EventQueue", std::move(events)));
    std::vector<std::shared_ptr<Gff>> projectiles;
    for (auto state : _game._services.game.projectiles.savePresentations()) {
        state.caster.id = serializedReferenceId(state.caster, ids);
        state.weapon.id = serializedReferenceId(state.weapon, ids);
        for (auto &leg : state.legs) {
            leg.source.id = serializedReferenceId(leg.source, ids);
            leg.target.id = serializedReferenceId(leg.target, ids);
        }
        projectiles.push_back(state.toGff());
    }
    put(*result, Gff::Field::newList("ProjectileState", std::move(projectiles)));

    auto modulePlayer = _game._party.player();
    if (!modulePlayer) throw ValidationException("module has no controlled player creature");
    auto player = writeCreature(
        *modulePlayer, 4, ids.objectId(*modulePlayer), ids, false);
    put(*player, Gff::Field::newByte(
        "Mod_IsPrimaryPlr", modulePlayer == _game._party.actualPlayer()));
    put(*result, Gff::Field::newList("Mod_PlayerList", {player}));

    std::vector<std::shared_ptr<Gff>> limbo;
    for (const auto &creature : module._limboCreatures) {
        if (creature) {
            limbo.push_back(writeCreature(
                *creature, 4, ids.objectId(*creature), ids));
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
    const auto areaEntries = ifo->getList("Mod_Area_list");
    if (ifo->getString("Mod_Entry_Area") != _game._module->_area->_name ||
        areaEntries.size() != 1 ||
        ifo->getUint("Mod_Area") != areaEntries.front()->getUint("ObjectId")) {
        throw ValidationException("IFO active-area identity is inconsistent");
    }
    uint32_t nextId = ifo->getUint("Mod_NextObjId0");
    uint32_t nextCharacterId = ifo->getUint("Mod_NextCharId0");
    if (nextId < 2 || nextId >= kSavedRuntimeInvalidObjectId ||
        nextCharacterId <= kSavedRuntimeInvalidObjectId ||
        nextCharacterId > kSavedRuntimeMaxCharacterObjectId ||
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
        } else if (id == kSavedRuntimeInvalidObjectId ||
                   id > kSavedRuntimeMaxCharacterObjectId ||
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
    static const std::array<const char *, 10> authoritativeLists {
        "Creature List", "Door List", "Placeable List", "TriggerList",
        "Encounter List", "StoreList", "WaypointList", "SoundList", "List",
        "AreaEffectList"};
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
    if (!reservedPartyIds.empty() && nextCharacterId >= *reservedPartyIds.begin()) {
        throw ValidationException(
            "Mod_NextCharId0 does not descend below the character saved object graph");
    }
    (void)are;
}

} // namespace game
} // namespace reone
