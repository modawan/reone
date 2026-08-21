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

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "container/erf.h"
#include "container/folder.h"
#include "resource.h"

namespace reone {

namespace resource {

class SaveWorkingStateCandidate;
class SaveWorkingStateBacking;
class SaveWorkingStateTestAccess;

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
 * Complete immutable logical save working state.
 *
 * A durable state reads SAVEGAME.sav once at construction, closes the source
 * file, and indexes one owned immutable archive backing. Contained payloads
 * remain lazy by exact identity. Slot identity stays in SaveSlotDescriptor and
 * is deliberately independent of this resource backing.
 *
 * A durable publisher must still reopen the newly published SAVEGAME.sav: an
 * old detached state is lifetime-safe, but does not represent new resources.
 * A candidate may also freeze a transition-time immutable state. Every frozen
 * state owns one normalized effective overlay and shares only the stable
 * archive backing used for unchanged payloads; it never retains a predecessor
 * SaveWorkingState.
 */
class SaveWorkingState : boost::noncopyable {
public:
    /** Creates an unsaved logical state over an empty stable backing. */
    SaveWorkingState();
    explicit SaveWorkingState(const std::filesystem::path &archivePath);

    std::optional<Resource> find(const ResourceId &id) const;
    bool contains(const ResourceId &id) const;

    const std::unordered_set<ResourceId> &resourceIds() const {
        return _resourceIds;
    }

private:
    SaveWorkingState(
        std::shared_ptr<const SaveWorkingStateBacking> backing,
        std::map<ResourceId, std::shared_ptr<const ByteBuffer>> replacements,
        std::unordered_set<ResourceId> tombstones);

    std::shared_ptr<const SaveWorkingStateBacking> _backing;
    std::map<ResourceId, std::shared_ptr<const ByteBuffer>> _replacements;
    std::unordered_set<ResourceId> _tombstones;
    std::unordered_set<ResourceId> _resourceIds;

    friend class SaveWorkingStateCandidate;
    friend class SaveWorkingStateTestAccess;
};

enum class SaveResourceOrigin {
    Borrowed,
    Owned,
};

/**
 * Deferred access to one candidate payload.
 *
 * A borrowed view retains the immutable base but does not read its file-backed
 * payload until read() is called. An owned view retains candidate-generated
 * bytes independently of the caller's input buffer.
 */
class SaveResourceView {
public:
    SaveResourceOrigin origin() const { return _origin; }
    std::optional<Resource> read() const;

private:
    SaveResourceView(
        std::shared_ptr<const SaveWorkingState> base,
        ResourceId id);
    explicit SaveResourceView(std::shared_ptr<const ByteBuffer> data);

    SaveResourceOrigin _origin;
    std::shared_ptr<const SaveWorkingState> _base;
    ResourceId _id;
    std::shared_ptr<const ByteBuffer> _data;

    friend class SaveWorkingStateCandidate;
};

struct SaveWorkingStateCandidateValidation {
    std::vector<std::string> errors;

    bool valid() const { return errors.empty(); }
    explicit operator bool() const { return valid(); }
    void addError(std::string error) { errors.push_back(std::move(error)); }
};

/**
 * Transactional mutable overlay over a committed SaveWorkingState.
 *
 * Repeated put() calls have deterministic last-write-wins semantics. erase()
 * installs an exact-ID tombstone. Candidate construction and enumeration do
 * not read unchanged payloads from the base archive.
 *
 * freeze() creates an immutable normalized in-memory overlay anchored directly
 * to the base state's stable backing. The candidate may temporarily retain its
 * immediate logical base, but the frozen result does not. A freeze is suitable
 * for transition-time state, but it is deliberately not a commit of newly
 * published bytes: after replacing a save slot, the publication layer must
 * reopen/re-index the new durable SAVEGAME.sav.
 */
class SaveWorkingStateCandidate {
public:
    using Validator = std::function<void(
        const SaveWorkingStateCandidate &,
        SaveWorkingStateCandidateValidation &)>;

    static SaveWorkingStateCandidate fromCommitted(
        std::shared_ptr<const SaveWorkingState> base);

    SaveWorkingStateCandidate(const SaveWorkingStateCandidate &) = delete;
    SaveWorkingStateCandidate &operator=(const SaveWorkingStateCandidate &) = delete;
    SaveWorkingStateCandidate(SaveWorkingStateCandidate &&) = default;
    SaveWorkingStateCandidate &operator=(SaveWorkingStateCandidate &&) = default;

    void put(ResourceId id, ByteBuffer payload);
    void erase(const ResourceId &id);

    std::optional<SaveResourceView> find(const ResourceId &id) const;
    bool contains(const ResourceId &id) const;
    std::vector<ResourceId> deterministicResourceIds() const;

    /**
     * Replaces an explicitly supplied module .sav and suppresses only the
     * explicitly corresponding .rsv, if supplied. No module name is inferred.
     */
    void replaceModule(
        ResourceId savedArchiveId,
        ByteBuffer payload,
        std::optional<ResourceId> savedResourceImageId = std::nullopt);

    SaveWorkingStateCandidateValidation validate(
        const Validator &additionalValidator = {}) const;

    std::shared_ptr<const SaveWorkingState> freeze() const;

private:
    struct ModuleReplacement {
        ResourceId savedArchiveId;
        std::optional<ResourceId> savedResourceImageId;
    };

    explicit SaveWorkingStateCandidate(std::shared_ptr<const SaveWorkingState> base);

    std::shared_ptr<const SaveWorkingState> _base;
    std::map<ResourceId, std::shared_ptr<const ByteBuffer>> _replacements;
    std::unordered_set<ResourceId> _tombstones;
    std::vector<ModuleReplacement> _moduleReplacements;
};

/**
 * One coherent save session ready to become active.
 *
 * A caller constructs this without publishing it, then commits the whole
 * object with one pointer replacement. Candidate sources are never mounted.
 */
class SaveSessionState : boost::noncopyable {
public:
    SaveSessionState();
    explicit SaveSessionState(SaveSlotDescriptor descriptor);
    SaveSessionState(
        SaveSlotDescriptor descriptor,
        std::shared_ptr<const SaveWorkingState> workingState);

    const SaveSlotDescriptor &slot() const { return _slot.value(); }
    const std::optional<SaveSlotDescriptor> &slotDescriptor() const {
        return _slot;
    }
    const std::shared_ptr<const SaveWorkingState> &workingState() const {
        return _workingState;
    }
    void replaceWorkingState(std::shared_ptr<const SaveWorkingState> state);

    std::optional<Resource> findMetadata(const ResourceId &id);
    std::optional<Resource> findWorking(const ResourceId &id);

private:
    std::optional<SaveSlotDescriptor> _slot;
    std::unique_ptr<FolderResourceContainer> _metadata;
    std::shared_ptr<const SaveWorkingState> _workingState;
};

} // namespace resource

} // namespace reone
