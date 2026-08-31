/* Copyright (c) 2026 The reone project contributors */

#pragma once

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

#include "reone/resource/id.h"
#include "reone/game/saveprovenance.h"
#include "reone/system/types.h"

namespace reone {
namespace resource {
class Gff;
}
namespace game {

class Game;
class Item;
class Object;
class Module;
class Area;
class Creature;
class Door;
class Placeable;
class Trigger;
class Encounter;
class Store;
class Waypoint;
class Sound;
class StaticCamera;
class SavedScriptContinuation;
struct EffectInstance;
struct SavedActionRecord;
struct SavedEventRecord;
struct SavedObjectReference;
struct SerializedScriptSituation;

enum class ModuleSnapshotError {
    None,
    NoPlayableModule,
    InvalidRuntimeGraph,
    UnsupportedLiveState,
    EncodingFailure,
    ValidationFailure,
};

struct SavedModuleSnapshot {
    resource::ResourceId target;
    std::shared_ptr<const resource::Gff> ifo;
    std::shared_ptr<const resource::Gff> are;
    std::shared_ptr<const resource::Gff> git;
    ByteBuffer ifoBytes;
    ByteBuffer areBytes;
    ByteBuffer gitBytes;
    ByteBuffer archiveBytes;
};

struct ModuleSnapshotResult {
    ModuleSnapshotError error {ModuleSnapshotError::None};
    std::string message;
    std::optional<SavedModuleSnapshot> snapshot;

    explicit operator bool() const { return snapshot.has_value(); }
};

/** Ordinary saved-object identity namespace shared by one module snapshot. */
class ModuleObjectIdContext {
public:
    explicit ModuleObjectIdContext(SerializedIdentityContext outputIdentityContext) :
        _outputIdentityContext(std::move(outputIdentityContext)) {
    }

    void assignContextObject(const Object &object, uint32_t id);
    void retainObject(const Object &object, bool partyIdentity = false);
    void allocateObject(const Object &object, bool partyIdentity = false);
    uint32_t objectId(const Object &object) const;
    bool contains(const Object &object) const;
    uint32_t nextId(uint32_t retainedCursor) const;

private:
    SerializedIdentityContext _outputIdentityContext;
    std::set<uint32_t> _used;
    std::set<uint32_t> _reservedPartyIds;
    std::map<const Object *, uint32_t> _objectIds;
};

/**
 * Pure synchronous snapshot of one stable active runtime graph.
 *
 * E3g must invoke this at a stable main-thread frame boundary. The builder
 * does not mutate Game, Module, SaveWorkingState, or a candidate.
 */
class ModuleSnapshotBuilder {
public:
    ModuleSnapshotBuilder(const Game &game, std::string saveGroup) :
        _game(game),
        _saveGroup(std::move(saveGroup)) {
    }

    ModuleSnapshotResult build() const noexcept;

private:
    friend class SaveWideSnapshotBuilder;

    const Game &_game;
    std::string _saveGroup;

    ModuleObjectIdContext buildObjectIdContext(
        const Module &module, const Area &area) const;
    std::shared_ptr<resource::Gff> buildIfo(
        const Module &module, const ModuleObjectIdContext &ids) const;
    std::shared_ptr<resource::Gff> buildAre(const Area &area) const;
    std::shared_ptr<resource::Gff> buildGit(
        const Module &module, const Area &area,
        const ModuleObjectIdContext &ids) const;
    std::shared_ptr<resource::Gff> objectBase(
        const Object &object, resource::ResType templateType, uint32_t structType) const;
    void writeObjectState(
        resource::Gff &record,
        const Object &object,
        const ModuleObjectIdContext &ids) const;
    std::shared_ptr<resource::Gff> writeCreature(
        const Creature &creature, uint32_t structType,
        std::optional<uint32_t> serializedId,
        const SerializedIdentityContext &outputIdentityContext) const;
    std::shared_ptr<resource::Gff> writeCreature(
        const Creature &creature, uint32_t structType,
        std::optional<uint32_t> serializedId,
        const ModuleObjectIdContext &ids,
        bool includeInventory = true) const;
    std::shared_ptr<resource::Gff> writeDoor(
        const Door &door, const ModuleObjectIdContext &ids) const;
    std::shared_ptr<resource::Gff> writePlaceable(
        const Placeable &placeable, const ModuleObjectIdContext &ids) const;
    std::shared_ptr<resource::Gff> writeItem(
        const Item &item,
        uint32_t structType,
        std::optional<uint32_t> serializedId,
        const ModuleObjectIdContext *ids = nullptr) const;
    std::shared_ptr<resource::Gff> writeTrigger(
        const Trigger &trigger, const ModuleObjectIdContext &ids) const;
    std::shared_ptr<resource::Gff> writeEncounter(
        const Encounter &encounter, const ModuleObjectIdContext &ids) const;
    std::shared_ptr<resource::Gff> writeStore(
        const Store &store, const ModuleObjectIdContext &ids) const;
    std::shared_ptr<resource::Gff> writeWaypoint(
        const Waypoint &waypoint, const ModuleObjectIdContext &ids) const;
    std::shared_ptr<resource::Gff> writeSound(
        const Sound &sound, const ModuleObjectIdContext &ids) const;
    std::shared_ptr<resource::Gff> writeCamera(const StaticCamera &camera) const;
    uint32_t serializedReferenceId(
        const SavedObjectReference &reference,
        const ModuleObjectIdContext &ids) const;
    EffectInstance normalizeEffectReferences(
        EffectInstance effect, const ModuleObjectIdContext &ids) const;
    void normalizeSituationReferences(
        SerializedScriptSituation &situation,
        const ModuleObjectIdContext &ids) const;
    void normalizeActionReferences(
        SavedActionRecord &action, const ModuleObjectIdContext &ids) const;
    void normalizeEventReferences(
        SavedEventRecord &event, const ModuleObjectIdContext &ids) const;
    void appendRuntimeDelayedEvents(
        std::vector<SavedEventRecord> &events,
        const Object &owner,
        const ModuleObjectIdContext &ids) const;
    void validate(const SavedModuleSnapshot &snapshot) const;
};

std::optional<SerializedScriptSituation> exportScriptSituation(
    const SavedScriptContinuation &continuation,
    std::string &error);

} // namespace game
} // namespace reone
