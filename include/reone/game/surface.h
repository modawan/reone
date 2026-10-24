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

namespace reone {

namespace game {

struct Surface {
    std::string label;
    /**
     * The walk cell. A room's walkable faces are those of surfaces whose cell
     * is 1; a position stands on walkable ground when its surface's cell is
     * not zero.
     */
    int walk {0};
    bool walkcheck {false};
    bool lineOfSight {false};
    bool grass {false};
    /** Names the footstep sounds of the surface; a surface without a name is silent underfoot. */
    std::string name;
};

} // namespace game

} // namespace reone
