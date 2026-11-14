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

#pragma once

#include "types.h"

namespace reone {

namespace audio {

/** How a source stands with its priority group and the voices. */
enum class SourceSlot {
    /** It takes room in its group and a voice. */
    Playing,
    /** A loop waiting for room in its group or for a voice. */
    Waiting,
    /** A loop out of range of the listener, which takes neither. */
    Away
};

/** What the arbiter weighs a source by. */
struct ArbitedSource {
    /** The source's priority group. */
    int group {0};
    bool looping {false};
    /** It plays on a streamed voice: music, ambience and speech. */
    bool streamed {false};
    /** It plays on a positional voice; one that isn't, on a 2D voice. */
    bool positional {false};
};

/** What the arbiter does to a source it has admitted before. */
struct SlotChange {
    enum class Kind {
        /** The source is stopped, and the arbiter has let it go. */
        Stop,
        /** The loop waits, paused, for room or a voice. */
        Wait,
        /** The waiting loop plays. */
        Start
    };

    uint32_t id {0};
    Kind kind {Kind::Stop};
};

/** The arbiter's answer to a new source. */
struct Admission {
    /** The source's id; none when it is not played: a one-shot without room, a voice or range. */
    std::optional<uint32_t> id;
    SourceSlot slot {SourceSlot::Playing};
    /** What making room did to the sources already admitted. */
    std::vector<SlotChange> changes;
};

/**
 * Shares the priority groups' room and the voices among the sources, as the
 * game does. A group lets as many sources play at once as its maximum. Three
 * pools of voices take them: positional voices, 2D voices, which are half the
 * 2D count, and streamed voices, the other half.
 *
 * A new source in a full group that interrupts stops the group's newest
 * source of its own kind, streamed or not, unless that is a fire-and-forget
 * one-shot, which plays out; the new source plays either way. In a full group
 * that doesn't interrupt a one-shot isn't played and a loop waits.
 *
 * A source with no free voice takes the voice of the source in its pool whose
 * group has the highest priority number, if that is higher than its own; only
 * a looping stream gives up a streamed voice. With no voice to take, a
 * one-shot isn't played and a loop waits.
 *
 * A stopped loop waits again. Waiting loops try again on each update, the
 * one that began waiting last first; they may take voices, but never
 * interrupt.
 */
class SourceArbiter {
public:
    /** The game's least and greatest voice counts. */
    static constexpr int kMinVoices2D = 24;
    static constexpr int kMinVoices3D = 16;
    static constexpr int kMaxVoices = 64;

    /** Sets the priority groups' rows, by group. */
    void setPriorityGroups(std::vector<PriorityGroup> groups) { _groups = std::move(groups); }
    /** The row of \p group; a group past the table takes a blank row. */
    PriorityGroup priorityGroup(int group) const;
    /** Sets the voice counts, each kept within the game's bounds. */
    void setVoiceCounts(int voices2D, int voices3D);

    /**
     * Admits a new source: it plays, waits or is not played.
     * @param inRange the source is within its distance of the listener; a
     *        one-shot that is not is not played, and a loop stays away
     */
    Admission admit(const ArbitedSource &source, bool inRange);
    /** Lets a source go: it has ended or been stopped. */
    void remove(uint32_t id);
    /**
     * Marks a non-looping, unstreamed source as its owner's, which keeps it
     * and plays it again: an interrupt stops it rather than letting it play
     * out.
     */
    void setOwned(uint32_t id, bool owned);
    /** A loop out of range gives up its room and voice; a one-shot is let go. */
    void leaveRange(uint32_t id);
    /** A loop back in range waits for room and a voice. */
    void enterRange(uint32_t id);
    /** Lets the waiting loops try again; the one that began waiting last first. */
    std::vector<SlotChange> update();

    /** The source's slot; none for a source the arbiter doesn't hold. */
    std::optional<SourceSlot> slot(uint32_t id) const;

private:
    enum class Pool {
        Positional,
        Flat,
        Streamed
    };

    struct Entry {
        uint32_t id {0};
        ArbitedSource source;
        bool owned {false};
        SourceSlot slot {SourceSlot::Playing};
        /** When it last began playing; the newest has the highest. */
        uint64_t playOrder {0};
        /** When it last began waiting; the latest has the highest. */
        uint64_t waitOrder {0};
    };

    std::vector<PriorityGroup> _groups;
    int _voices2D {kMinVoices2D};
    int _voices3D {kMinVoices3D};
    /** A list, so that letting one source go leaves the others where they are. */
    std::list<Entry> _entries;
    uint32_t _nextId {1};
    uint64_t _order {0};

    Entry *find(uint32_t id);
    const Entry *find(uint32_t id) const;
    Pool pool(const ArbitedSource &source) const;
    int capacity(Pool pool) const;
    int playingInGroup(int group) const;
    int playingInPool(Pool pool) const;

    /**
     * Gives \p entry room and a voice if it can have them. A new source
     * interrupts; a retry doesn't.
     */
    bool tryStart(Entry &entry, bool retry, std::vector<SlotChange> &changes);
    /** Makes room in a full group that interrupts. */
    void interrupt(const Entry &entry, std::vector<SlotChange> &changes);
    /** Takes a voice in a full pool; false when none may be taken. */
    bool stealVoice(const Entry &entry, std::vector<SlotChange> &changes);
    /** A source losing its room or voice: a loop waits, any other stops. */
    void displace(Entry &victim, std::vector<SlotChange> &changes);
    void wait(Entry &entry);
};

} // namespace audio

} // namespace reone
