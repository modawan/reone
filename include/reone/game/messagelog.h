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
    enum class Kind {
        Feedback,
        Combat,
    };

    struct Entry {
        Kind kind;
        std::string text;
    };

    static constexpr std::size_t kMaxEntries = 64;

    void add(Kind kind, std::string text);
    void addFeedback(std::string text) { add(Kind::Feedback, std::move(text)); }
    void addCombat(std::string text) { add(Kind::Combat, std::move(text)); }
    void reset() { _entries.clear(); }

    const std::deque<Entry> &entries() const { return _entries; }

private:
    std::deque<Entry> _entries;
};

} // namespace game

} // namespace reone
