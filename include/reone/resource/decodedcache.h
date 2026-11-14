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

#include "resources.h"

namespace reone {

namespace resource {

/**
 * The lookups a decoded value was made from, each with the source that
 * answered it. A lookup nothing answered is recorded too: a resource that
 * appears later changes the value as surely as one that is replaced.
 *
 * A value is current while every one of its lookups would still be answered
 * the same way, because decoding it again would then read the same bytes.
 */
class ResourceReads {
public:
    /// Look id up and record which source answered.
    std::optional<Resource> find(IResources &resources, const ResourceId &id) {
        auto resource = resources.find(id);
        _reads.push_back(Read {&resources, id, resource ? std::make_optional(resource->source) : std::nullopt});
        return resource;
    }

    /// Take on the lookups of a value this one is built from.
    void add(const ResourceReads &other) {
        _reads.insert(_reads.end(), other._reads.begin(), other._reads.end());
    }

    /// Whether every lookup would still be answered by the source that answered it.
    bool current() const {
        return std::all_of(_reads.begin(), _reads.end(), [](const Read &read) {
            return read.resources->locate(read.id) == read.source;
        });
    }

    /// Call visit with the resources of every lookup, repeats included.
    template <class Visit>
    void visitResources(Visit visit) const {
        for (const auto &read : _reads) {
            visit(*read.resources);
        }
    }

private:
    struct Read {
        IResources *resources;
        ResourceId id;
        std::optional<ResourceSourceRef> source;
    };

    std::vector<Read> _reads;
};

/**
 * Decoded values by key, each served only while it is current.
 *
 * A value outlives module transitions, save loads and replacement changes for
 * as long as every lookup it was decoded from would still be answered by the
 * same source, so a value read from a global source is decoded once. A value
 * stops being served once a source it was read from is retired, a source
 * mounted since would answer one of its lookups instead, or a replacement is
 * set or removed for one of its ids; the next request decodes it again.
 *
 * Every time a resource collection the cache has read through changes
 * generation, all entries are checked before anything is served and stale
 * ones are dropped, so the cache holds nothing from a retired source.
 * Dropping an entry only stops the cache serving it: whoever holds the value
 * keeps it.
 */
template <class Key, class Value, class Hash = std::hash<Key>>
class DecodedCache : boost::noncopyable {
public:
    struct Entry {
        std::shared_ptr<Value> value;
        ResourceReads reads;
    };

    void clear() {
        _entries.clear();
        _watched.clear();
    }

    /**
     * The current entry under key. When there is none, decode(reads) makes the
     * value, recording into reads every lookup it makes.
     */
    template <class Decode>
    const Entry &get(const Key &key, Decode decode) {
        dropStale();
        auto it = _entries.find(key);
        if (it != _entries.end()) {
            return it->second;
        }
        Entry entry;
        entry.value = decode(entry.reads);
        watch(entry.reads);
        return _entries.emplace(key, std::move(entry)).first->second;
    }

private:
    struct Watched {
        const IResources *resources;
        std::uint64_t generation;
    };

    std::unordered_map<Key, Entry, Hash> _entries;
    std::vector<Watched> _watched;

    void dropStale() {
        bool changed = false;
        for (auto &watched : _watched) {
            auto generation = watched.resources->generation();
            if (generation != watched.generation) {
                watched.generation = generation;
                changed = true;
            }
        }
        if (!changed) {
            return;
        }
        for (auto it = _entries.begin(); it != _entries.end();) {
            if (it->second.reads.current()) {
                ++it;
            } else {
                it = _entries.erase(it);
            }
        }
    }

    void watch(const ResourceReads &reads) {
        reads.visitResources([this](const IResources &resources) {
            auto known = std::any_of(_watched.begin(), _watched.end(), [&resources](const Watched &watched) {
                return watched.resources == &resources;
            });
            if (!known) {
                _watched.push_back(Watched {&resources, resources.generation()});
            }
        });
    }
};

} // namespace resource

} // namespace reone
