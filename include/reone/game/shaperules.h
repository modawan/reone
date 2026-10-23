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

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace reone {

namespace game {

namespace shape {

/** The cosine of the half-angle of a spell cone, thirty degrees. */
constexpr float kSpellConeCosine = 0.866f;

/** The unit vector along \p vector; a vector shorter than 1e-9 becomes +X. */
inline glm::vec3 normalize(const glm::vec3 &vector) {
    float length = std::sqrt(glm::dot(vector, vector));
    if (static_cast<double>(length) < 1.0e-9) {
        return glm::vec3 {1.0f, 0.0f, 0.0f};
    }
    return vector * (1.0f / length);
}

/**
 * The point of the line from \p origin through \p target nearest to
 * \p position; \p origin itself when the two coincide.
 */
inline glm::vec3 project(const glm::vec3 &origin,
                         const glm::vec3 &target,
                         const glm::vec3 &position) {
    glm::vec3 axis = target - origin;
    float length2 = glm::dot(axis, axis);
    float fraction = length2 == 0.0f
                         ? 0.0f
                         : glm::dot(axis, position - origin) / length2;
    return origin + axis * fraction;
}

/**
 * Whether \p position lies in the cone from \p origin toward \p target, in
 * front of \p origin. The cone's radius at a point is \p size per unit of
 * the axis length, times the distance of the point's projection on the axis
 * from the world origin, not from \p origin.
 */
inline bool matchCone(const glm::vec3 &position,
                      const glm::vec3 &target,
                      const glm::vec3 &origin,
                      float size) {
    glm::vec3 axis = target - origin;
    if (!(glm::dot(normalize(axis), normalize(position - origin)) > 0.0f)) {
        return false;
    }
    glm::vec3 projected = project(origin, target, position);
    glm::vec3 perpendicular = position - projected;
    float radius = (size / std::sqrt(glm::dot(axis, axis))) *
                   std::sqrt(glm::dot(projected, projected));
    return radius * radius >= glm::dot(perpendicular, perpendicular);
}

/**
 * Whether \p position lies in the spell cone from \p origin toward
 * \p target: within thirty degrees of the axis, and projecting onto the axis
 * no farther than \p size from \p origin. A point at \p origin is tested
 * like any other.
 */
inline bool matchSpellCone(const glm::vec3 &position,
                           const glm::vec3 &target,
                           const glm::vec3 &origin,
                           float size) {
    if (!(glm::dot(normalize(target - origin),
                   normalize(position - origin)) >= kSpellConeCosine)) {
        return false;
    }
    glm::vec3 along = project(origin, target, position) - origin;
    return size * size >= glm::dot(along, along);
}

} // namespace shape

} // namespace game

} // namespace reone
