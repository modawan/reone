/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include "reone/resource/gff.h"

#include "effect.h"

namespace reone {

namespace game {

class Action;
class Game;
class Object;
class SavedScriptSituationImporter;

constexpr uint32_t kSavedRuntimeInvalidObjectId = 0x7f000000;

/** A saved object identity which is deliberately unbound while B is built. */
struct SavedObjectReference {
    uint32_t id {kSavedRuntimeInvalidObjectId};

    SavedObjectReference() = default;
    explicit SavedObjectReference(uint32_t id) : id(id) {}

    bool isInvalid() const { return id == kSavedRuntimeInvalidObjectId; }
    std::shared_ptr<Object> boundObject() const { return _object.lock(); }

private:
    friend class Game;
    std::weak_ptr<Object> _object;
    std::optional<uint64_t> _runtimeSession;
};

struct SavedLocString {
    int32_t strRef {0};
    std::string text;
};

struct SavedStruct;
using SavedStructChildren = std::vector<std::shared_ptr<SavedStruct>>;
using SavedFieldValue = std::variant<
    int64_t,
    uint64_t,
    double,
    std::string,
    ByteBuffer,
    glm::vec3,
    glm::quat,
    SavedLocString,
    SavedStructChildren>;

/**
 * Semantic GFF field fallback for a genuinely unsupported typed payload.
 * Known action/event/VM payloads use their concrete models below.
 */
struct SavedField {
    resource::Gff::FieldType type {resource::Gff::FieldType::Int};
    std::string label;
    SavedFieldValue value {int64_t {0}};
};

struct SavedStruct {
    uint32_t type {0};
    std::vector<SavedField> fields;

    static SavedStruct fromGff(const resource::Gff &gff);
};

struct UnsupportedSavedPayload {
    SavedStruct data;
};

struct SavedLocationValue {
    glm::vec3 position {0.0f};
    glm::vec3 orientation {0.0f};
};

struct SavedScriptEvent {
    uint16_t type {0};
    std::vector<int32_t> integers;
    std::vector<float> floats;
    std::vector<std::string> strings;
    std::vector<SavedObjectReference> objects;
};

/** Retail CScriptTalent game-defined VM structure (engine structure 3). */
struct SavedTalentValue {
    int32_t id {-1};
    int32_t type {-1};
    uint8_t multiClass {0};
    SavedObjectReference item;
    int32_t itemPropertyIndex {-1};
    uint8_t casterLevel {0xff};
    uint8_t metaType {0xff};
};

enum class SavedVmStackType : int8_t {
    Integer = 3,
    Float = 4,
    String = 5,
    Object = 6,
    Effect = 16,
    Event = 17,
    Location = 18,
    Talent = 19,
};

using SavedVmStackPayload = std::variant<
    UnsupportedSavedPayload,
    int32_t,
    float,
    std::string,
    SavedObjectReference,
    EffectInstance,
    SavedScriptEvent,
    SavedLocationValue,
    SavedTalentValue>;

struct SavedVmStackValue {
    int8_t type {0};
    SavedVmStackPayload payload {UnsupportedSavedPayload {}};
};

enum class ScriptSituationResumeSupport {
    UnsupportedRetailSnapshot,
    ValidatedImport,
};

/** Retail STORE_STATE continuation, kept separate from live ExecutionState. */
struct SerializedScriptSituation {
    int32_t codeSize {0};
    ByteBuffer code;
    uint32_t crc {0};
    int32_t instructionPointer {0};
    int32_t secondaryPointer {0};
    std::string scriptName;
    int32_t stackSize {0};
    int32_t basePointer {0};
    int32_t stackPointer {0};
    int32_t totalSize {0};
    std::vector<SavedVmStackValue> stack;
    std::vector<SavedField> unsupportedFields;

    static SerializedScriptSituation fromGff(const resource::Gff &gff);
    ScriptSituationResumeSupport resumeSupport() const {
        return ScriptSituationResumeSupport::ValidatedImport;
    }
    bool bindObjectReferences(const Game &game);
    bool isBoundToCurrentRuntimeSession(const Game &game) const;

private:
    friend class SavedScriptSituationImporter;
    std::optional<uint64_t> _runtimeSession;
};

enum class SavedActionParameterType : uint32_t {
    Unsupported = 0,
    Integer = 1,
    Float = 2,
    Object = 3,
    String = 4,
    ScriptSituation = 5,
};

using SavedActionParameterPayload = std::variant<
    UnsupportedSavedPayload,
    int32_t,
    float,
    SavedObjectReference,
    std::string,
    SerializedScriptSituation>;

struct SavedActionParameter {
    uint32_t type {0};
    SavedActionParameterPayload payload {UnsupportedSavedPayload {}};

    static SavedActionParameter fromGff(const resource::Gff &gff);
    bool bindObjectReferences(const Game &game);
};

enum class SavedExecutionSupport {
    Executable,
    RepresentableButUnsupported,
    RetailDiscards,
};

struct SavedActionRecord {
    uint32_t actionId {0};
    uint16_t groupActionId {0};
    uint16_t declaredParameterCount {0};
    std::vector<SavedActionParameter> parameters;
    std::vector<SavedField> unsupportedFields;

    static SavedActionRecord fromGff(const resource::Gff &gff);
    SavedExecutionSupport executionSupport() const;
    std::shared_ptr<Action> toRuntimeAction(
        Game &game,
        const SavedScriptSituationImporter *importer = nullptr) const;
    bool bindObjectReferences(const Game &game);
};

/** Parsed per-object queue; publication waits until B object registration. */
struct SavedActionQueue {
    std::vector<SavedActionRecord> actions;

    static SavedActionQueue fromGff(const resource::Gff &gff, const std::string &label = "ActionList");
};

enum class SavedEventType : uint32_t {
    Timed = 1,
    EnteredTrigger = 2,
    LeftTrigger = 3,
    RemoveFromArea = 4,
    ApplyEffect = 5,
    CloseObject = 6,
    OpenObject = 7,
    SpellImpact = 8,
    PlayAnimation = 9,
    SignalEvent = 10,
    DestroyObject = 11,
    Unlock = 12,
    Lock = 13,
    RemoveEffect = 14,
    OnMeleeAttacked = 15,
    DecrementStackSize = 16,
    SpawnBodyBag = 17,
    ForcedAction = 18,
    ItemOnHitSpellImpact = 19,
    BroadcastAoo = 20,
    BroadcastSafeProjectile = 21,
    FeedbackMessage = 22,
    AbilityEffectApplied = 23,
    SummonCreature = 24,
    AcquireItem = 25,
    AreaTransition = 26,
    ControllerRumble = 27,
};

struct SavedBytePayload {
    uint8_t value {0};
};
struct SavedIntPayload {
    int32_t value {0};
};
struct SavedDwordPayload {
    uint32_t value {0};
};

struct SavedSpellImpact {
    int32_t spellId {0};
    SavedObjectReference caster;
    SavedObjectReference target;
    SavedObjectReference area;
    SavedObjectReference item;
    std::string script;
    glm::vec3 targetPosition {0.0f};
    int32_t finalForceCost {0};
};

struct SavedBodyBag {
    SavedObjectReference object;
    glm::vec3 position {0.0f};
};

using SavedEventPayload = std::variant<
    std::monostate,
    UnsupportedSavedPayload,
    SerializedScriptSituation,
    EffectInstance,
    SavedBytePayload,
    SavedIntPayload,
    SavedDwordPayload,
    SavedSpellImpact,
    SavedScriptEvent,
    SavedBodyBag>;

struct SavedEventRecord {
    /** Absolute world/game timestamp, never a wall-clock or derived delay. */
    uint32_t day {0};
    uint32_t time {0};
    SavedObjectReference object;
    SavedObjectReference caller;
    uint32_t eventId {0};
    SavedEventPayload payload;
    std::vector<SavedField> unsupportedFields;

    static SavedEventRecord fromGff(const resource::Gff &gff);
    SavedExecutionSupport executionSupport() const;
    bool shouldRestore() const;
    bool bindObjectReferences(const Game &game);
};

/** Parsed module/server queue; list order is the authoritative queue order. */
struct SavedEventQueue {
    std::vector<SavedEventRecord> events;

    static SavedEventQueue fromGff(const resource::Gff &gff, const std::string &label = "EventQueue");
};

} // namespace game

} // namespace reone
