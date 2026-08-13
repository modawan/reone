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
#include <list>
#include <optional>

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
 * std::optional<ResourceSourceBucket> bucket, and a ResourceMountToken
 * sequence this list assigns. Everything else about an entry, including how
 * the source is actually read, belongs to the backend: what is shared here is
 * which source wins and when a source goes away, which is exactly what the two
 * backends have to agree on.
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
        _entries.clear();
    }

    /// Drop every source of one owner, in every bucket. Ownership answers when
    /// a source goes away; it never answers where a source is searched.
    void clearOwner(ResourceOwner owner) {
        _entries.remove_if([owner](const Entry &entry) {
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
        _entries.remove_if([token](const Entry &entry) {
            return entry.sequence >= token;
        });
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
};

} // namespace resource

} // namespace reone
