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

/// Lifetime scope of a mounted source. Independent of the bucket it is
/// searched in.
enum class ContainerKind {
    Global,
    Local,
    Save,
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
 * Entry must expose a ContainerKind kind and an
 * std::optional<ResourceSourceBucket> bucket. Everything else about an entry,
 * including how the source is actually read, belongs to the backend: what is
 * shared here is which source wins and when a source goes away, which is
 * exactly what the two backends have to agree on.
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
        if (current == ResourceSourceOrder::Empty) {
            _entries.push_front(std::move(entry));
            return;
        }
        if (current != required) {
            throw ValidationException(
                required == ResourceSourceOrder::Bucketed
                    ? "Cannot mount a bucketed source into an insertion-ordered resource source list"
                    : "Cannot mount an unbucketed source into a bucketed resource source list");
        }
        if (current == ResourceSourceOrder::Insertion) {
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

    void clear() {
        _entries.clear();
    }

    /// Drop every source of one scope, in every bucket. Scope answers when a
    /// source goes away; it never answers where a source is searched.
    void clearKind(ContainerKind kind) {
        _entries.remove_if([kind](const Entry &entry) {
            return entry.kind == kind;
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
};

} // namespace resource

} // namespace reone
