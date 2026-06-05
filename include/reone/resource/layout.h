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

namespace resource {

struct Layout {
    struct Room {
        std::string name;
        glm::vec3 position {0.0f};
    };

    // Minigame track placement (LYT "trackcount" section). Position only; the
    // LYT track format carries no rotation.
    struct Track {
        std::string name;
        glm::vec3 position {0.0f};
    };

    std::vector<Room> rooms;
    std::vector<Track> tracks;

    std::optional<std::reference_wrapper<const Room>> findByName(const std::string &name) const {
        for (auto &room : rooms) {
            if (room.name == name) {
                return room;
            }
        }
        return std::nullopt;
    }

    std::optional<std::reference_wrapper<const Track>> findTrackByName(const std::string &name) const {
        std::string lower(boost::to_lower_copy(name));
        for (auto &track : tracks) {
            if (track.name == lower) {
                return track;
            }
        }
        return std::nullopt;
    }
};

} // namespace resource

} // namespace reone
