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

class TestGameModule;

class StaticCamera : public Camera {
public:
    StaticCamera(
        uint32_t id,
        std::string sceneName,
        Game &game,
        ServicesView &services) :
        Camera(
            id,
            std::move(sceneName),
            game,
            services) {
    }

    void deserialize(const resource::Gff &gff);

    const glm::quat &staticOrientation() const { return _staticOrientation; }
    float staticPitch() const { return _staticPitch; }
    float height() const { return _height; }
    float micRange() const { return _micRange; }

private:
    friend class TestGameModule;

    // Serializable

    // Separate orientation and pitch, as opposed to Object::_orientation where
    // they are multiplied.
    glm::quat _staticOrientation;
    float _staticPitch {0.0f};
    float _height {0.0f};
    float _micRange {0.0f};

    // END Serializable

    void updateProjection() override;
};

} // namespace game

} // namespace reone
