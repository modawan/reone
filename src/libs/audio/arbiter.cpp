/*
 * Copyright (c) 2020-2023 The reone project contributors
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

#include "reone/audio/arbiter.h"

namespace reone {

namespace audio {

PriorityGroup SourceArbiter::priorityGroup(int group) const {
    return group >= 0 && group < static_cast<int>(_groups.size()) ? _groups[group] : PriorityGroup {};
}

void SourceArbiter::setVoiceCounts(int voices2D, int voices3D) {
    _voices2D = std::clamp(voices2D, kMinVoices2D, kMaxVoices);
    _voices3D = std::clamp(voices3D, kMinVoices3D, kMaxVoices);
}

SourceArbiter::Entry *SourceArbiter::find(uint32_t id) {
    auto it = std::find_if(_entries.begin(), _entries.end(), [id](const Entry &entry) { return entry.id == id; });
    return it != _entries.end() ? &*it : nullptr;
}

const SourceArbiter::Entry *SourceArbiter::find(uint32_t id) const {
    auto it = std::find_if(_entries.begin(), _entries.end(), [id](const Entry &entry) { return entry.id == id; });
    return it != _entries.end() ? &*it : nullptr;
}

std::optional<SourceSlot> SourceArbiter::slot(uint32_t id) const {
    const Entry *entry = find(id);
    return entry ? std::optional<SourceSlot>(entry->slot) : std::nullopt;
}

SourceArbiter::Pool SourceArbiter::pool(const ArbitedSource &source) const {
    if (source.streamed) {
        return Pool::Streamed;
    }
    return source.positional ? Pool::Positional : Pool::Flat;
}

// The 2D voices are halved: the first half plays unstreamed sources, the
// other half streams.
int SourceArbiter::capacity(Pool pool) const {
    switch (pool) {
    case Pool::Positional:
        return _voices3D;
    case Pool::Flat:
        return _voices2D / 2;
    default:
        return _voices2D - _voices2D / 2;
    }
}

int SourceArbiter::playingInGroup(int group) const {
    return static_cast<int>(std::count_if(_entries.begin(), _entries.end(), [group](const Entry &entry) {
        return entry.slot == SourceSlot::Playing && entry.source.group == group;
    }));
}

int SourceArbiter::playingInPool(Pool p) const {
    return static_cast<int>(std::count_if(_entries.begin(), _entries.end(), [this, p](const Entry &entry) {
        return entry.slot == SourceSlot::Playing && pool(entry.source) == p;
    }));
}

Admission SourceArbiter::admit(const ArbitedSource &source, bool inRange) {
    Admission admission;
    if (!inRange && !source.looping) {
        return admission;
    }
    Entry entry;
    entry.id = _nextId++;
    entry.source = source;
    if (!inRange) {
        entry.slot = SourceSlot::Away;
        _entries.push_back(entry);
        admission.id = entry.id;
        admission.slot = SourceSlot::Away;
        return admission;
    }
    if (tryStart(entry, false, admission.changes)) {
        _entries.push_back(entry);
        admission.id = entry.id;
        admission.slot = SourceSlot::Playing;
        return admission;
    }
    if (!source.looping) {
        return admission;
    }
    wait(entry);
    _entries.push_back(entry);
    admission.id = entry.id;
    admission.slot = SourceSlot::Waiting;
    return admission;
}

void SourceArbiter::remove(uint32_t id) {
    _entries.remove_if([id](const Entry &entry) { return entry.id == id; });
}

void SourceArbiter::setOwned(uint32_t id, bool owned) {
    if (Entry *entry = find(id)) {
        entry->owned = owned;
    }
}

void SourceArbiter::leaveRange(uint32_t id) {
    Entry *entry = find(id);
    if (!entry) {
        return;
    }
    if (!entry->source.looping) {
        remove(id);
        return;
    }
    entry->slot = SourceSlot::Away;
}

void SourceArbiter::enterRange(uint32_t id) {
    Entry *entry = find(id);
    if (entry && entry->slot == SourceSlot::Away) {
        wait(*entry);
    }
}

std::vector<SlotChange> SourceArbiter::update() {
    std::vector<SlotChange> changes;
    // The loops waiting now, the one that began waiting last first. A loop
    // made to wait during this pass tries again on the next.
    std::vector<std::pair<uint64_t, uint32_t>> waiting;
    for (const Entry &entry : _entries) {
        if (entry.slot == SourceSlot::Waiting) {
            waiting.emplace_back(entry.waitOrder, entry.id);
        }
    }
    std::sort(waiting.begin(), waiting.end(), [](const auto &left, const auto &right) { return left.first > right.first; });
    for (const auto &[order, id] : waiting) {
        Entry *entry = find(id);
        if (!entry || entry->slot != SourceSlot::Waiting || entry->waitOrder != order) {
            continue;
        }
        if (tryStart(*entry, true, changes)) {
            changes.push_back(SlotChange {id, SlotChange::Kind::Start});
        }
    }
    return changes;
}

bool SourceArbiter::tryStart(Entry &entry, bool retry, std::vector<SlotChange> &changes) {
    const PriorityGroup group = priorityGroup(entry.source.group);
    // A group that plays none plays nothing, interrupting or not.
    if (group.maxPlaying <= 0) {
        return false;
    }
    if (playingInGroup(entry.source.group) >= group.maxPlaying) {
        if (retry || !group.interrupt) {
            return false;
        }
        interrupt(entry, changes);
    }
    if (playingInPool(pool(entry.source)) >= capacity(pool(entry.source)) && !stealVoice(entry, changes)) {
        return false;
    }
    entry.slot = SourceSlot::Playing;
    entry.playOrder = ++_order;
    return true;
}

// Only the group's newest source of the same kind is weighed. A
// fire-and-forget one-shot plays out, and nothing else is stopped for it.
void SourceArbiter::interrupt(const Entry &entry, std::vector<SlotChange> &changes) {
    Entry *newest = nullptr;
    for (Entry &other : _entries) {
        if (other.slot != SourceSlot::Playing || other.source.group != entry.source.group ||
            other.source.streamed != entry.source.streamed) {
            continue;
        }
        if (!newest || other.playOrder > newest->playOrder) {
            newest = &other;
        }
    }
    if (!newest) {
        return;
    }
    const bool firedAndForgotten = !newest->source.looping && !newest->source.streamed && !newest->owned;
    if (!firedAndForgotten) {
        displace(*newest, changes);
    }
}

// The voice of the source whose group comes last by priority, past the new
// source's own; between equals, the one playing longest.
bool SourceArbiter::stealVoice(const Entry &entry, std::vector<SlotChange> &changes) {
    const Pool p = pool(entry.source);
    const int priority = priorityGroup(entry.source.group).priority;
    Entry *victim = nullptr;
    int victimPriority = priority;
    for (Entry &other : _entries) {
        if (other.slot != SourceSlot::Playing || pool(other.source) != p) {
            continue;
        }
        if (p == Pool::Streamed && !other.source.looping) {
            continue;
        }
        const int otherPriority = priorityGroup(other.source.group).priority;
        if (otherPriority > victimPriority || (victim && otherPriority == victimPriority && other.playOrder < victim->playOrder)) {
            victim = &other;
            victimPriority = otherPriority;
        }
    }
    if (!victim) {
        return false;
    }
    displace(*victim, changes);
    return true;
}

void SourceArbiter::displace(Entry &victim, std::vector<SlotChange> &changes) {
    const uint32_t id = victim.id;
    if (victim.source.looping) {
        wait(victim);
        changes.push_back(SlotChange {id, SlotChange::Kind::Wait});
        return;
    }
    remove(id);
    changes.push_back(SlotChange {id, SlotChange::Kind::Stop});
}

void SourceArbiter::wait(Entry &entry) {
    entry.slot = SourceSlot::Waiting;
    entry.waitOrder = ++_order;
}

} // namespace audio

} // namespace reone
