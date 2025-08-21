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

namespace graphics {

/// OrthographicCamera maps a 3D scene on a 2D screen plane without
/// taking perspective into account. It is defined by width (left and
/// right position on the X axis), height (bottom and top), and
/// boundaries on the Z-axis (zNear and zFar). Ever vertex with Z <
/// zNear or Z > zFar is clipped out.
///
/// See https://learnopengl.com/Getting-Started/Coordinate-Systems
/// for more details.
class OrthographicCamera : public Camera {
public:
    OrthographicCamera() :
        Camera(CameraType::Orthographic) {
    }

    void setProjection(float left, float right, float bottom, float top, float zNear, float zFar) {
        _zNear = zNear;
        _zFar = zFar;

        Camera::setProjection(glm::ortho(left, right, bottom, top, zNear, zFar));
    }
};

} // namespace graphics

} // namespace reone
