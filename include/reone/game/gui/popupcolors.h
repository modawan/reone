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

namespace reone {

namespace game {

/** The colours a message box is drawn in: its frame and button borders, and its message and button text. */
struct PopupColors {
    /** The frame's and the buttons' borders. */
    glm::vec3 border {1.0f};
    /** The message's and the buttons' text. */
    glm::vec3 text {1.0f};
};

} // namespace game

} // namespace reone
