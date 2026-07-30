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

void MessageLog::add(uint32_t type, Style style, std::string text) {
    if (text.empty()) {
        return;
    }
    while (_entries.size() >= kMaxEntries) {
        _entries.pop_front();
    }
    _entries.push_back({type, style, std::move(text)});
}

} // namespace game

} // namespace reone
