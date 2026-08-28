/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "reone/resource/gff.h"

namespace reone {

namespace game {

/** Deep-owned immutable semantic GFF state, without writer behavior. */
class SaveGffShadow {
public:
    SaveGffShadow() = default;
    static SaveGffShadow capture(const resource::Gff &source);

    explicit operator bool() const { return static_cast<bool>(_record); }
    const resource::Gff &record() const { return *_record; }
    std::shared_ptr<resource::Gff> cloneForMerge() const;

private:
    explicit SaveGffShadow(std::shared_ptr<const resource::Gff> record) :
        _record(std::move(record)) {
    }
    std::shared_ptr<const resource::Gff> _record;
};

void replaceSaveField(resource::Gff &record, resource::Gff::Field field);
void removeSaveField(resource::Gff &record, std::string_view label);

/**
 * Authority carried by ObjectId fields in one materialization context.
 *
 * The GFF field type alone does not answer this question. Saved module graphs
 * own an authoritative object namespace, detached records carry only local
 * serialization identities, and templates carry no serialized instance
 * identity at all.
 */
enum class SerializedIdentityDomain {
    ModuleGraph,
    DetachedRecord,
    Template,
};

struct SerializedIdentityContext {
    SerializedIdentityDomain domain {SerializedIdentityDomain::Template};
    std::string identityNamespace;

    static SerializedIdentityContext moduleGraph(std::string identityNamespace) {
        return {SerializedIdentityDomain::ModuleGraph, std::move(identityNamespace)};
    }
    static SerializedIdentityContext detachedRecord(std::string identityNamespace) {
        return {SerializedIdentityDomain::DetachedRecord, std::move(identityNamespace)};
    }
    static SerializedIdentityContext templateResource(std::string identityNamespace = {}) {
        return {SerializedIdentityDomain::Template, std::move(identityNamespace)};
    }

    bool isSerializedState() const {
        return domain != SerializedIdentityDomain::Template;
    }
    bool hasAuthoritativeObjectIds() const {
        return domain == SerializedIdentityDomain::ModuleGraph;
    }

    bool operator==(const SerializedIdentityContext &rhs) const {
        return domain == rhs.domain && identityNamespace == rhs.identityNamespace;
    }
};

struct SerializedObjectIdentity {
    SerializedIdentityContext context;
    uint32_t id {0};

    bool operator==(const SerializedObjectIdentity &rhs) const {
        return context == rhs.context && id == rhs.id;
    }
};

enum class SerializedGraphRoot {
    ModuleIfo,
    AreaGit,
};

struct SerializedObjectIdClaim {
    uint32_t id {0};
    std::string path;
};

/** Collect only owned identities, never ObjectId-shaped reference fields. */
std::vector<SerializedObjectIdClaim> collectSerializedObjectIdClaims(
    const resource::Gff &root,
    const SerializedIdentityContext &context,
    SerializedGraphRoot graphRoot);

enum class SaveRecordOriginKind {
    ActiveGitObject,
    ModulePlayer,
    ModuleLimboCreature,
    AvailableNpc,
    AvailablePuppet,
    PrimaryPlayerUtc,
    ContainedItem,
    EquippedItem,
    PartyInventoryItem,
    StoreItem,
    PlaceableItem,
};

struct SaveRecordOrigin {
    SaveRecordOriginKind kind {SaveRecordOriginKind::ActiveGitObject};
    std::string owner;

    bool operator==(const SaveRecordOrigin &rhs) const {
        return kind == rhs.kind && owner == rhs.owner;
    }
};

struct SaveRecordProvenance {
    SaveGffShadow shadow;
    SaveRecordOrigin origin;
    std::optional<SerializedObjectIdentity> identity;
};

enum class SaveResourceKind {
    Nfo,
    GlobalVars,
    PartyTable,
    FactionTable,
    Inventory,
    ModuleIfo,
    AreaAre,
    AreaGit,
};

struct SaveResourceKey {
    SaveResourceKind kind {SaveResourceKind::Nfo};
    std::string name;

    bool operator<(const SaveResourceKey &rhs) const {
        return std::tie(kind, name) < std::tie(rhs.kind, rhs.name);
    }
};

class SaveResourceShadows {
public:
    void capture(SaveResourceKey key, const resource::Gff &source);
    const SaveGffShadow *find(const SaveResourceKey &key) const;
    void clear() { _shadows.clear(); }
    size_t size() const { return _shadows.size(); }

private:
    std::map<SaveResourceKey, SaveGffShadow> _shadows;
};

} // namespace game
} // namespace reone
