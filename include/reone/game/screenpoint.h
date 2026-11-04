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

#include <glm/ext/matrix_projection.hpp>
#include <glm/glm.hpp>

namespace reone {

namespace game {

/**
 * The window pixel a world point is drawn at, top-left origin as mouse events
 * have it. Empty when the point is behind the camera, beyond its far plane,
 * or outside a \p width by \p height window: no click there could reach it.
 */
inline std::optional<glm::ivec2> worldToWindowPoint(
    const glm::vec3 &world,
    const glm::mat4 &view,
    const glm::mat4 &projection,
    int width,
    int height) {

    glm::vec4 clip = projection * view * glm::vec4(world, 1.0f);
    if (clip.w <= 0.0f) {
        return std::nullopt;
    }
    static const glm::vec4 kUnitViewport(0.0f, 0.0f, 1.0f, 1.0f);
    glm::vec3 screen = glm::project(world, view, projection, kUnitViewport);
    if (screen.z < 0.0f || screen.z > 1.0f) {
        return std::nullopt;
    }
    glm::ivec2 point(
        static_cast<int>(width * screen.x),
        static_cast<int>(height * (1.0f - screen.y)));
    if (point.x < 0 || point.y < 0 || point.x >= width || point.y >= height) {
        return std::nullopt;
    }
    return point;
}

} // namespace game

} // namespace reone
