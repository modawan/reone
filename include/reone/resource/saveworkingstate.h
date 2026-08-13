/*
 * Copyright (c) 2026 The reone project contributors
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

#pragma once

#include "container/erf.h"
#include "container/folder.h"
#include "resource.h"

namespace reone {

namespace resource {

/**
 * Durable identity of one save slot.
 *
 * Loose metadata and the packed working-state archive are deliberately named
 * separately. The archive is not a fallback source for metadata merely because
 * both happen to live in one durable slot.
 */
struct SaveSlotDescriptor {
    std::filesystem::path directory;
    std::filesystem::path archive;
};

/**
 * Complete logical contents represented by SAVEGAME.sav.
 *
 * The archive is indexed at construction and payloads are read lazily by exact
 * identity. Later mutation/writer work can extend this boundary with an
 * overlay and tombstones without making the durable archive the live registry.
 */
class SaveWorkingState : boost::noncopyable {
public:
    explicit SaveWorkingState(const std::filesystem::path &archivePath);

    std::optional<Resource> find(const ResourceId &id);
    bool contains(const ResourceId &id) const;

    const std::unordered_set<ResourceId> &resourceIds() const {
        return _archive.resourceIds();
    }

private:
    ErfResourceContainer _archive;
};

/**
 * One coherent save session ready to become active.
 *
 * A caller constructs this without publishing it, then commits the whole
 * object with one pointer replacement. Candidate sources are never mounted.
 */
class SaveSessionState : boost::noncopyable {
public:
    explicit SaveSessionState(SaveSlotDescriptor descriptor);

    const SaveSlotDescriptor &slot() const { return _slot; }
    SaveWorkingState &workingState() { return _workingState; }
    const SaveWorkingState &workingState() const { return _workingState; }

    std::optional<Resource> findMetadata(const ResourceId &id);
    std::optional<Resource> findWorking(const ResourceId &id);

private:
    SaveSlotDescriptor _slot;
    FolderResourceContainer _metadata;
    SaveWorkingState _workingState;
};

} // namespace resource

} // namespace reone
