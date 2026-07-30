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
 * Rolling in-game feedback history.
 */
class MessageLog {
public:
    enum class Style {
        Blue,
        Red,
    };

    struct Entry {
        uint32_t type;
        Style style;
        std::string text;
    };

    static constexpr std::size_t kMaxEntries = 64;
    static constexpr uint32_t kFeedbackMessageType = 0x80;

    void add(uint32_t type, Style style, std::string text);
    void addFeedback(std::string text) {
        add(kFeedbackMessageType, Style::Blue, std::move(text));
    }
    void addCombat(std::string text) {
        add(kFeedbackMessageType, Style::Red, std::move(text));
    }
    void reset() { _entries.clear(); }

    const std::deque<Entry> &entries() const { return _entries; }

private:
    std::deque<Entry> _entries;
};

} // namespace game

} // namespace reone
