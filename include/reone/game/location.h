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

#include <optional>

#include "reone/script/enginetype.h"

namespace reone {

namespace game {

class Location : public script::EngineType {
public:
    Location(glm::vec3 position, float facing) :
        _position(std::move(position)),
        _facing(facing) {
    }

    Location(glm::vec3 position, glm::vec3 serializedOrientation) :
        _position(std::move(position)),
        _facing(std::atan2(serializedOrientation.y, serializedOrientation.x)),
        _preservedOrientation(std::move(serializedOrientation)) {
    }

    const glm::vec3 &position() const { return _position; }
    float facing() const { return _facing; }
    const std::optional<glm::vec3> &preservedOrientation() const {
        return _preservedOrientation;
    }
    glm::vec3 saveOrientation() const {
        return _preservedOrientation.value_or(
            glm::vec3(std::cos(_facing), std::sin(_facing), 0.0f));
    }

    void setPosition(glm::vec3 position) { _position = std::move(position); }
    void setFacing(float facing) {
        _facing = facing;
        _preservedOrientation.reset();
    }

private:
    glm::vec3 _position;
    float _facing;
    std::optional<glm::vec3> _preservedOrientation;
};

} // namespace game

} // namespace reone
