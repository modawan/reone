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

#include "reone/game/messagelog.h"

namespace reone {

namespace game {

void MessageLog::add(uint32_t type, Style style, std::string text, Buffer buffer) {
    // An empty line is not written.
    if (text.empty()) {
        return;
    }
    if (!_combatBufferEnabled) {
        buffer = Buffer::Messages;
    }
    std::size_t count = 0;
    for (const Entry &entry : _entries) {
        if (entry.buffer == buffer) {
            ++count;
        }
    }
    // A full list drops its own oldest line.
    for (auto it = _entries.begin(); count >= kMaxEntries && it != _entries.end();) {
        if (it->buffer == buffer) {
            it = _entries.erase(it);
            --count;
        } else {
            ++it;
        }
    }
    _entries.push_back({type, style, std::move(text), buffer});
}

void MessageLog::addDialog(std::string speaker, std::string text) {
    if (text.empty()) {
        return;
    }
    if (_dialogEntries.size() >= kMaxEntries) {
        _dialogEntries.pop_front();
    }
    _dialogEntries.push_back({std::move(speaker), std::move(text)});
}

} // namespace game

} // namespace reone
