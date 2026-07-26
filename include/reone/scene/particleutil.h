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

#include <array>

#include "glm/glm.hpp"

#include "reone/scene/render/pass.h"

namespace reone {

namespace scene {

namespace particleutil {

constexpr float kMaxContinuousParticleDelta = 0.25f;
constexpr int kMaxSpawnParticlesPerUpdate = 256;

struct MotionBlurBasis {
    glm::vec3 right {0.0f};
    glm::vec3 up {0.0f};
    float lengthScale {1.0f};
};

struct AtlasFrameBounds {
    glm::vec2 minUV {0.5f};
    glm::vec2 maxUV {0.5f};
};

struct DecodedParticleSample {
    glm::vec3 color {0.0f};
    float alpha {0.0f};
};

MotionBlurBasis buildMotionBlurBasis(
    const glm::mat4 &emitterTransform,
    const glm::vec3 &localVelocity,
    const glm::vec3 &toCamera,
    const glm::vec3 &cameraRight,
    const glm::vec3 &cameraUp,
    float particleLength,
    float blurLength);

int advanceSpawnAccumulator(float birthrate, float dt, float &accumulator);

AtlasFrameBounds atlasFrameBounds(
    const glm::ivec2 &textureSize,
    const glm::ivec2 &gridSize,
    int frame);

glm::vec2 clampAtlasUV(const AtlasFrameBounds &bounds, const glm::vec2 &uv);

std::array<float, 4> cubicReconstructionWeights(float fraction);

DecodedParticleSample decodeParticleSample(
    const glm::vec4 &sample,
    ParticleAlphaMode alphaMode,
    bool lightenBlend,
    float alphaExponent);

float enhanceParticleCoverage(float alpha, float contrast);

float analyticParticleCoreEnvelope(
    const glm::vec2 &localUV,
    bool motionBlur,
    float intensity);

float clampMotionTrailLength(
    float authoredLength,
    float basisScale,
    float profileScale,
    float maximumLength);

} // namespace particleutil

} // namespace scene

} // namespace reone
