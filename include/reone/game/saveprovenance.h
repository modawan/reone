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
