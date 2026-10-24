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

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>

namespace reone {

namespace game {

/**
 * Rolling in-game feedback and conversation history. It lasts as long as the
 * program: loading a game appends the saved lists, and neither a load, a
 * return to the main menu nor a new character clears it.
 */
class MessageLog {
public:
    /** The highlight flag a line is written with: Combat is the highlighted colour. */
    enum class Style : uint8_t {
        Normal = 0,
        Combat = 1,
    };

    /** The list a line is written to. KotOR has only the message list. */
    enum class Buffer {
        Messages = 0,
        Combat = 1,
    };

    /** A line of the message or combat list. */
    struct Entry {
        uint32_t type;
        Style style;
        std::string text;
        /** The list the line is in. */
        Buffer buffer {Buffer::Messages};
    };

    /** A conversation line: who spoke it and what was said. */
    struct DialogEntry {
        std::string speaker;
        std::string text;
    };

    /** Each list keeps this many lines and drops its oldest one when full. */
    static constexpr std::size_t kMaxEntries = 64;
    static constexpr uint32_t kFeedbackMessageType = 0x80;

    /**
     * Writes a line to a list. An empty line is not written. With the combat
     * list disabled, it goes to the message list. A full list drops its oldest
     * line.
     */
    void add(uint32_t type, Style style, std::string text, Buffer buffer = Buffer::Messages);
    /** A conversation line goes to the dialog list; one without text is not kept. */
    void addDialog(std::string speaker, std::string text);

    /** TSL enables the combat list; with it disabled, its lines go to the message list. */
    void setCombatBufferEnabled(bool enabled) { _combatBufferEnabled = enabled; }

    const std::deque<Entry> &entries() const { return _entries; }
    const std::deque<DialogEntry> &dialogEntries() const { return _dialogEntries; }

private:
    std::deque<Entry> _entries;
    std::deque<DialogEntry> _dialogEntries;
    bool _combatBufferEnabled {false};
};

} // namespace game

} // namespace reone
