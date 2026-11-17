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

#include <optional>
#include <queue>

namespace reone {

namespace game {

/** Replies a conversation picks and entries it skips on its own, queued by the auto-skip console commands. */
struct ConversationAutoSkip {
    std::queue<std::optional<int>> replies;
    std::queue<bool> entries;
    bool enabled {false};

    std::optional<int> trySkipReply();
    std::optional<bool> trySkipEntry();
};

} // namespace game

} // namespace reone
