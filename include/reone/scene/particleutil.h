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

#include "glm/glm.hpp"

namespace reone {

namespace scene {

namespace particleutil {

struct MotionBlurBasis {
    glm::vec3 right {0.0f};
    glm::vec3 up {0.0f};
    float lengthScale {1.0f};
};

MotionBlurBasis buildMotionBlurBasis(
    const glm::mat4 &emitterTransform,
    const glm::vec3 &localVelocity,
    const glm::vec3 &toCamera,
    const glm::vec3 &cameraRight,
    const glm::vec3 &cameraUp,
    float particleLength,
    float blurLength);

} // namespace particleutil

} // namespace scene

} // namespace reone
