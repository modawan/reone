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

#include "../camera.h"

namespace reone {

namespace game {

class StaticCamera : public Camera {
public:
    StaticCamera(
        uint32_t id,
        float aspect,
        std::string sceneName,
        Game &game,
        ServicesView &services) :
        Camera(
            id,
            std::move(sceneName),
            game,
            services),
        _aspect(aspect) {
    }

    void deserialize(const resource::Gff &gff);

private:
    // Serializable

    // Separate orientation and pitch, as opposed to Object::_orientation where
    // they are multiplied.
    glm::quat _staticOrientation;
    float _staticPitch {0.0f};
    float _height {0.0f};

    // END Serializable

    float _aspect;
};

} // namespace game

} // namespace reone
