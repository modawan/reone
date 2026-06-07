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

#include "reone/resource/format/lytreader.h"

#include "reone/system/stream/fileinput.h"
#include "reone/system/textreader.h"

namespace reone {

namespace resource {

void LytReader::load(IInputStream &in) {
    auto reader = TextReader(in);
    while (auto line = reader.readLine()) {
        if (!line) {
            break;
        }
        processLine(*line);
    }
}

void LytReader::processLine(const std::string &line) {
    std::vector<std::string> tokens;
    std::string trimLine(boost::trim_copy(line));
    boost::split(tokens, trimLine, boost::is_space(), boost::token_compress_on);

    const std::string &first = tokens[0];
    switch (_state) {
    case State::None:
        if (first == "beginlayout") {
            _state = State::Layout;
        }
        break;
    case State::Layout:
        if (first == "donelayout") {
            _state = State::None;
        } else if (first == "roomcount") {
            _roomCount = stoi(tokens[1]);
            if (_roomCount > 0) {
                _layout.rooms.reserve(_roomCount);
                _state = State::Rooms;
            }
        } else if (first == "trackcount") {
            // Minigame track placements (optional; absent in normal modules).
            _trackCount = stoi(tokens[1]);
            if (_trackCount > 0) {
                _layout.tracks.reserve(_trackCount);
                _state = State::Tracks;
            }
        } else if (first == "obstaclecount") {
            // Minigame obstacle placements (optional; absent in normal modules).
            _obstacleCount = stoi(tokens[1]);
            if (_obstacleCount > 0) {
                _layout.obstacles.reserve(_obstacleCount);
                _state = State::Obstacles;
            }
        }
        // Other sections (doorhookcount, ...) and their entries are
        // intentionally ignored here.
        break;
    case State::Rooms:
        appendRoom(tokens);
        if (_layout.rooms.size() == static_cast<size_t>(_roomCount)) {
            _state = State::Layout;
        }
        break;
    case State::Tracks:
        appendTrack(tokens);
        if (_layout.tracks.size() == static_cast<size_t>(_trackCount)) {
            _state = State::Layout;
        }
        break;
    case State::Obstacles:
        appendObstacle(tokens);
        if (_layout.obstacles.size() == static_cast<size_t>(_obstacleCount)) {
            _state = State::Layout;
        }
        break;
    }
}

void LytReader::appendRoom(const std::vector<std::string> &tokens) {
    Layout::Room room;
    room.name = boost::to_lower_copy(tokens[0]);
    room.position = glm::vec3(
        stof(tokens[1]),
        stof(tokens[2]),
        stof(tokens[3]));
    _layout.rooms.push_back(std::move(room));
}

void LytReader::appendTrack(const std::vector<std::string> &tokens) {
    // Track entry format: "<name> <x> <y> <z>" (position only, no rotation).
    if (tokens.size() < 4) {
        return;
    }
    Layout::Track track;
    track.name = boost::to_lower_copy(tokens[0]);
    track.position = glm::vec3(
        stof(tokens[1]),
        stof(tokens[2]),
        stof(tokens[3]));
    _layout.tracks.push_back(std::move(track));
}

void LytReader::appendObstacle(const std::vector<std::string> &tokens) {
    // Obstacle entry format: "<name> <x> <y> <z>" (position only, no rotation).
    if (tokens.size() < 4) {
        return;
    }
    Layout::Obstacle obstacle;
    obstacle.name = boost::to_lower_copy(tokens[0]);
    obstacle.position = glm::vec3(
        stof(tokens[1]),
        stof(tokens[2]),
        stof(tokens[3]));
    _layout.obstacles.push_back(std::move(obstacle));
}

} // namespace resource

} // namespace reone
