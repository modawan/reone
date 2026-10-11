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

namespace graphics {

inline glm::vec3 calculateTriangleNormal(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c) {
    return glm::cross(b - a, c - a);
}

inline float calculateTriangleArea(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c) {
    float ab = glm::distance(a, b);
    float ac = glm::distance(a, c);
    float bc = glm::distance(b, c);
    return 0.25f * glm::sqrt((ab + ac + bc) * (-ab + ac + bc) * (ab - ac + bc) * (ab + ac - bc));
}

} // namespace graphics

} // namespace reone
