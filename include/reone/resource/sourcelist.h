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

#include "modulepolicy.h"

#include "reone/system/exception/validation.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <list>
#include <optional>
#include <set>
#include <vector>

namespace reone {

namespace resource {

/**
 * A point in the mount sequence.
 *
 * Every mount takes the next value, so a token read before an operation names
 * exactly the sources that operation went on to add. This is what makes a
 * failed operation removable without knowing what it tried to mount.
 */
using ResourceMountToken = std::uint64_t;

/// What a source reads its resources from.
enum class ResourceSourceForm {
    KeyBif,
    Erf,
    Rim,
    Folder,
    Executable,
    /// Resources a caller placed in memory one by one.
    Memory,
    /// The runtime replacement store, which answers above every source.
    Replacement,
};

/**
 * The source that answered a lookup, and why it was the one to answer.
 *
 * The mount sequence identifies a mounted source. A list never reuses one, so
 * a reference names a single mount of a single source even when the same file
 * is mounted again later. The rest describes that mount: what it reads from
 * (the path is empty for an archive mounted from memory), the bucket that
 * placed it in the lookup order, and the owner whose retirement removes it.
 *
 * The replacement revision is the looked-up id's revision in the runtime
 * replacement store at the time of the lookup, whichever source answered. A
 * replacement itself has no mount, bucket or path, and nothing but its own
 * removal retires it, which is why its owner is Global.
 *
 * Two lookups of one id that report equal references were answered from the
 * same mount with the same replacement revision, so they read the same bytes.
 */
struct ResourceSourceRef {
    ResourceSourceForm form {ResourceSourceForm::Replacement};
    std::filesystem::path path;
    std::optional<ResourceSourceBucket> bucket;
    ResourceOwner owner {ResourceOwner::Global};
    ResourceMountToken sequence {0};
    std::uint64_t replacementRevision {0};

    bool operator==(const ResourceSourceRef &other) const {
        return form == other.form &&
               path == other.path &&
               bucket == other.bucket &&
               owner == other.owner &&
               sequence == other.sequence &&
               replacementRevision == other.replacementRevision;
    }

    bool operator!=(const ResourceSourceRef &other) const {
        return !(*this == other);
    }
};

/**
 * How a source list orders what it holds.
 *
 * A list has no mode until something is mounted, takes its mode from the first
 * source registered, and releases it once it is empty again. There is no third
 * mode: bucketed and unbucketed sources never coexist.
 */
enum class ResourceSourceOrder {
    Empty,
    Insertion,
    Bucketed,
};

/// Position of a bucket in the raw lookup order. Derived from
/// kRawResourceLookupOrder rather than restated, so the two cannot come to
/// disagree. Only a bucketed source has a rank; an unbucketed one has none,
/// which is why the two cannot be ordered against each other.
constexpr std::size_t bucketRank(ResourceSourceBucket bucket) {
    std::size_t rank = 0;
    for (auto candidate : kRawResourceLookupOrder) {
        if (candidate == bucket) {
            break;
        }
        ++rank;
    }
    return rank;
}

/**
 * Ordered list of mounted resource sources, shared by every IResources
 * backend. Entries are held in lookup order, so a backend resolves an id by
 * walking the list and taking the first hit.
 *
 * Entry must expose a ResourceOwner owner, an
 * std::optional<ResourceSourceBucket> bucket, a ResourceSourceForm form, a
 * std::filesystem::path path, and a ResourceMountToken sequence this list
 * assigns. Everything else about an entry, including how the source is
 * actually read, belongs to the backend: what is shared here is which source
 * wins and when a source goes away, which is exactly what the two backends
 * have to agree on. Resolving a lookup is therefore done here too, given only
 * whether each source holds the resource.
 *
 * Owner and bucket answer different questions and are never derived from each
 * other. The bucket says where a source is searched; the owner says what makes
 * it go away. A source is not searched earlier for being shorter-lived, and it
 * does not live longer for being searched first.
 *
 * A list is homogeneous. In insertion mode, which is what every caller uses
 * today, the source added last wins and the list behaves exactly like the
 * insertion-ordered list it replaces. In bucketed mode sources follow
 * kRawResourceLookupOrder, and within one bucket the source added last wins.
 *
 * The two modes are never mixed. Ranking an unbucketed source against a
 * bucketed one would require inventing a position for it in the raw lookup
 * order, and any position invented would be wrong: a source that has not been
 * placed has no place. Mounting across modes is therefore rejected rather than
 * resolved, so that migrating a call site is a complete change or no change.
 */
template <class Entry>
class ResourceSourceList {
public:
    using Entries = std::list<Entry>;
    using iterator = typename Entries::iterator;
    using const_iterator = typename Entries::const_iterator;

    ResourceSourceOrder order() const {
        if (_entries.empty()) {
            return ResourceSourceOrder::Empty;
        }
        return _entries.front().bucket ? ResourceSourceOrder::Bucketed
                                       : ResourceSourceOrder::Insertion;
    }

    /**
     * Mount a source at the front of its rank, or at the front of the list in
     * insertion mode.
     *
     * Throws ValidationException when the source does not match the mode the
     * list is already in.
     */
    void add(Entry entry) {
        auto required = entry.bucket ? ResourceSourceOrder::Bucketed
                                     : ResourceSourceOrder::Insertion;
        auto current = order();
        // The mode is checked before the sequence is spent, so a rejected mount
        // leaves the list exactly as it was, token included.
        if (current != ResourceSourceOrder::Empty && current != required) {
            throw ValidationException(
                required == ResourceSourceOrder::Bucketed
                    ? "Cannot mount a bucketed source into an insertion-ordered resource source list"
                    : "Cannot mount an unbucketed source into a bucketed resource source list");
        }
        entry.sequence = _nextSequence++;
        ++_generation;
        if (current == ResourceSourceOrder::Empty ||
            current == ResourceSourceOrder::Insertion) {
            _entries.push_front(std::move(entry));
            return;
        }
        // Every entry carries a bucket in this mode, which is what the check
        // above maintains, so neither dereference here needs a guard.
        auto rank = bucketRank(*entry.bucket);
        auto position = std::find_if(_entries.begin(), _entries.end(), [rank](const Entry &other) {
            return bucketRank(*other.bucket) >= rank;
        });
        _entries.insert(position, std::move(entry));
    }

    /**
     * Drop every source.
     *
     * The mount sequence deliberately keeps counting. Reusing a spent token
     * would let a rollback taken before the clear match sources mounted after
     * it, which is the one way a token could name something it never covered.
     */
    void clear() {
        removeIf([](const Entry &) { return true; });
    }

    /// Drop every source of one owner, in every bucket. Ownership answers when
    /// a source goes away; it never answers where a source is searched.
    void clearOwner(ResourceOwner owner) {
        removeIf([owner](const Entry &entry) {
            return entry.owner == owner;
        });
    }

    /// The token a mount would take next. Read it before an operation to be
    /// able to undo exactly that operation.
    ResourceMountToken mountToken() const { return _nextSequence; }

    /**
     * Drop every source mounted at or after the token, whatever its owner.
     *
     * This undoes an operation rather than retiring a lifetime, which is why it
     * is expressed in mount order and not in ownership: a failed operation has
     * to take back what it added, including sources whose owner still has other
     * members that must survive.
     */
    void rollbackTo(ResourceMountToken token) {
        removeIf([token](const Entry &entry) {
            return entry.sequence >= token;
        });
    }

    /**
     * Changes whenever the set of mounted sources does: a mount, or a clear,
     * retirement or rollback that removes something. Until it changes, every
     * lookup is answered by the source that answered it last.
     */
    std::uint64_t generation() const { return _generation; }

    /**
     * The source a lookup resolves to: the first in lookup order that holds the
     * resource, passing over sources of the excluded owners. holds(entry) says
     * whether a source has it; how a source knows is the backend's business.
     */
    template <class Holds>
    const Entry *resolve(Holds holds, const std::set<ResourceOwner> &excludedOwners = {}) const {
        for (const auto &entry : _entries) {
            if (excludedOwners.count(entry.owner) == 0 && holds(entry)) {
                return &entry;
            }
        }
        return nullptr;
    }

    /// Every source that holds the resource, in lookup order. The first is the
    /// one resolve answers with; the rest are what it shadows.
    template <class Holds>
    std::vector<const Entry *> candidates(Holds holds) const {
        std::vector<const Entry *> found;
        for (const auto &entry : _entries) {
            if (holds(entry)) {
                found.push_back(&entry);
            }
        }
        return found;
    }

    /// How a lookup answered by this source reports it.
    static ResourceSourceRef sourceRef(const Entry &entry) {
        ResourceSourceRef ref;
        ref.form = entry.form;
        ref.path = entry.path;
        ref.bucket = entry.bucket;
        ref.owner = entry.owner;
        ref.sequence = entry.sequence;
        return ref;
    }

    iterator begin() { return _entries.begin(); }
    iterator end() { return _entries.end(); }
    const_iterator begin() const { return _entries.begin(); }
    const_iterator end() const { return _entries.end(); }

    bool empty() const { return _entries.empty(); }
    std::size_t size() const { return _entries.size(); }

private:
    Entries _entries;
    ResourceMountToken _nextSequence {0};
    std::uint64_t _generation {0};

    template <class Predicate>
    void removeIf(Predicate predicate) {
        auto before = _entries.size();
        _entries.remove_if(predicate);
        if (_entries.size() != before) {
            ++_generation;
        }
    }
};

} // namespace resource

} // namespace reone
