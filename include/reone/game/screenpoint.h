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

#include <algorithm>
#include <limits>
#include <optional>
#include <vector>

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

/**
 * Window points over a box drawn on screen, to click it where nothing covers
 * it: a \p grid by \p grid spread over the window rectangle of the box's
 * projected corners, inset by a tenth, nearest the projected centre first.
 * \p min and \p max bound the box in model space and \p model places it in
 * the world. Corners behind the camera are left out; with none in front,
 * there are no points.
 */
inline std::vector<glm::ivec2> boxWindowPoints(
    const glm::vec3 &min,
    const glm::vec3 &max,
    const glm::mat4 &model,
    const glm::mat4 &view,
    const glm::mat4 &projection,
    int width,
    int height,
    int grid = 5) {

    static const glm::vec4 kUnitViewport(0.0f, 0.0f, 1.0f, 1.0f);
    glm::vec2 low(std::numeric_limits<float>::max());
    glm::vec2 high(std::numeric_limits<float>::lowest());
    bool any = false;
    for (int corner = 0; corner < 8; ++corner) {
        glm::vec3 local((corner & 1) ? max.x : min.x, (corner & 2) ? max.y : min.y, (corner & 4) ? max.z : min.z);
        glm::vec3 world = model * glm::vec4(local, 1.0f);
        glm::vec4 clip = projection * view * glm::vec4(world, 1.0f);
        if (clip.w <= 0.0f) {
            continue;
        }
        glm::vec3 screen = glm::project(world, view, projection, kUnitViewport);
        glm::vec2 window(width * screen.x, height * (1.0f - screen.y));
        low = glm::min(low, window);
        high = glm::max(high, window);
        any = true;
    }
    if (!any) {
        return {};
    }
    glm::vec2 inset = (high - low) * 0.1f;
    low += inset;
    high -= inset;
    glm::vec2 center = (low + high) * 0.5f;
    std::vector<glm::ivec2> points;
    for (int row = 0; row < grid; ++row) {
        for (int column = 0; column < grid; ++column) {
            glm::vec2 t = grid > 1 ? glm::vec2(column, row) / static_cast<float>(grid - 1) : glm::vec2(0.5f);
            glm::ivec2 point(low + (high - low) * t);
            if (point.x >= 0 && point.y >= 0 && point.x < width && point.y < height) {
                points.push_back(point);
            }
        }
    }
    std::stable_sort(points.begin(), points.end(), [&center](const glm::ivec2 &a, const glm::ivec2 &b) {
        return glm::length(glm::vec2(a) - center) < glm::length(glm::vec2(b) - center);
    });
    return points;
}

} // namespace game

} // namespace reone
