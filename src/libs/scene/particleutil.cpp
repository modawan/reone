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

#include "reone/scene/particleutil.h"

#include <cmath>
#include <limits>

#include "reone/graphics/lumautil.h"

namespace reone {

namespace scene {

namespace particleutil {

static constexpr float kMinVectorLength = 1e-6f;
static constexpr float kMotionBlurTrailTime = 0.25f;

MotionBlurBasis buildMotionBlurBasis(
    const glm::mat4 &emitterTransform,
    const glm::vec3 &localVelocity,
    const glm::vec3 &toCamera,
    const glm::vec3 &cameraRight,
    const glm::vec3 &cameraUp,
    float particleLength,
    float blurLength) {

    glm::vec3 worldVelocity(emitterTransform * glm::vec4(localVelocity, 0.0f));
    float speed = glm::length(worldVelocity);
    if (speed <= kMinVectorLength || particleLength <= kMinVectorLength) {
        return {cameraRight, cameraUp, 1.0f};
    }

    glm::vec3 up(worldVelocity / speed);
    glm::vec3 right(glm::cross(up, toCamera));
    float rightLength = glm::length(right);
    if (rightLength <= kMinVectorLength) {
        return {cameraRight, cameraUp, 1.0f};
    }
    right /= rightLength;

    float trailLength = kMotionBlurTrailTime * speed * glm::max(blurLength, 0.0f);
    return {right, up, 1.0f + trailLength / particleLength};
}

int advanceSpawnAccumulator(float birthrate, float dt, float &accumulator) {
    if (!std::isfinite(birthrate) ||
        !std::isfinite(dt) ||
        !std::isfinite(accumulator) ||
        birthrate <= 0.0f) {
        accumulator = 0.0f;
        return 0;
    }
    if (dt <= 0.0f) {
        return 0;
    }
    if (dt > kMaxContinuousParticleDelta) {
        accumulator = 0.0f;
        return 0;
    }

    accumulator += birthrate * dt;
    if (!std::isfinite(accumulator)) {
        accumulator = 0.0f;
        return 0;
    }

    float wholeParticles = glm::floor(accumulator);
    accumulator -= wholeParticles;
    if (wholeParticles >= static_cast<float>(kMaxSpawnParticlesPerUpdate)) {
        return kMaxSpawnParticlesPerUpdate;
    }
    return static_cast<int>(wholeParticles);
}

AtlasFrameBounds atlasFrameBounds(
    const glm::ivec2 &textureSize,
    const glm::ivec2 &gridSize,
    int frame) {

    glm::ivec2 safeTextureSize(glm::max(textureSize, glm::ivec2(1)));
    glm::ivec2 safeGridSize(glm::clamp(gridSize, glm::ivec2(1), safeTextureSize));
    int frameCount = safeGridSize.x * safeGridSize.y;
    int safeFrame = glm::clamp(frame, 0, frameCount - 1);
    glm::ivec2 frameCoord(
        safeFrame % safeGridSize.x,
        safeFrame / safeGridSize.x);

    glm::vec2 cellMin = glm::vec2(frameCoord) / glm::vec2(safeGridSize);
    glm::vec2 cellMax = glm::vec2(frameCoord + glm::ivec2(1)) / glm::vec2(safeGridSize);
    glm::vec2 cellCenter = 0.5f * (cellMin + cellMax);
    glm::vec2 halfTexel = 0.5f / glm::vec2(safeTextureSize);

    AtlasFrameBounds result;
    result.minUV = glm::min(cellMin + halfTexel, cellCenter);
    result.maxUV = glm::max(cellMax - halfTexel, cellCenter);
    return result;
}

glm::vec2 clampAtlasUV(const AtlasFrameBounds &bounds, const glm::vec2 &uv) {
    return glm::clamp(uv, bounds.minUV, bounds.maxUV);
}

std::array<float, 4> cubicReconstructionWeights(float fraction) {
    float t = std::isfinite(fraction) ? glm::clamp(fraction, 0.0f, 1.0f) : 0.0f;
    float t2 = t * t;
    float t3 = t2 * t;
    return {
        -0.5f * t + t2 - 0.5f * t3,
        1.0f - 2.5f * t2 + 1.5f * t3,
        0.5f * t + 2.0f * t2 - 1.5f * t3,
        -0.5f * t2 + 0.5f * t3};
}

DecodedParticleSample decodeParticleSample(
    const glm::vec4 &sample,
    ParticleAlphaMode alphaMode,
    bool lightenBlend,
    float alphaExponent) {

    DecodedParticleSample result;
    result.color = glm::vec3(sample);
    float storedAlpha = glm::clamp(sample.a, 0.0f, 1.0f);
    float luminance = glm::clamp(graphics::rgbToLuma(result.color), 0.0f, 1.0f);

    if (!lightenBlend) {
        result.alpha = storedAlpha;
        return result;
    }

    switch (alphaMode) {
    case ParticleAlphaMode::Texture:
        result.alpha = storedAlpha;
        break;
    case ParticleAlphaMode::Luminance:
        result.alpha = luminance;
        result.color *= 1.0f / glm::max(0.0001f, luminance);
        break;
    case ParticleAlphaMode::AlphaAndLuminance:
        result.alpha = glm::min(storedAlpha, luminance);
        if (luminance <= storedAlpha) {
            result.color *= 1.0f / glm::max(0.0001f, luminance);
        }
        break;
    case ParticleAlphaMode::Legacy:
    default:
        result.alpha = luminance;
        result.color *= 1.0f / glm::max(0.0001f, luminance);
        break;
    }

    float exponent = std::isfinite(alphaExponent) && alphaExponent > 0.0f
                         ? alphaExponent
                         : 1.0f;
    result.alpha = std::pow(glm::clamp(result.alpha, 0.0f, 1.0f), exponent);
    return result;
}

float enhanceParticleCoverage(float alpha, float contrast) {
    float safeAlpha = std::isfinite(alpha)
                          ? glm::clamp(alpha, 0.0f, 1.0f)
                          : 0.0f;
    float safeContrast = std::isfinite(contrast)
                             ? glm::clamp(contrast, 0.0f, 1.0f)
                             : 0.0f;
    float detail = glm::max(
        safeAlpha,
        glm::smoothstep(0.02f, 0.28f, safeAlpha));
    return glm::mix(safeAlpha, detail, safeContrast);
}

float analyticParticleCoreEnvelope(
    const glm::vec2 &localUV,
    bool motionBlur,
    float intensity) {

    if (!std::isfinite(localUV.x) ||
        !std::isfinite(localUV.y) ||
        !std::isfinite(intensity) ||
        intensity <= 0.0f) {
        return 0.0f;
    }

    glm::vec2 centered = 2.0f * localUV - 1.0f;
    float safeIntensity = glm::clamp(intensity, 0.0f, 1.0f);
    if (!motionBlur) {
        float radialCore =
            1.0f - glm::smoothstep(0.0f, 0.65f, glm::length(centered));
        return safeIntensity * radialCore * radialCore;
    }

    glm::vec2 inside = glm::max(glm::vec2(1.0f) - glm::abs(centered), glm::vec2(0.0f));
    float crossSection = inside.x * inside.x;
    crossSection *= crossSection;
    float endTaper = inside.y * inside.y;
    return safeIntensity * crossSection * endTaper;
}

float clampMotionTrailLength(
    float authoredLength,
    float basisScale,
    float profileScale,
    float maximumLength) {

    if (!std::isfinite(authoredLength) || authoredLength <= 0.0f) {
        return 0.0f;
    }

    float safeBasisScale = std::isfinite(basisScale) ? glm::max(basisScale, 0.0f) : 1.0f;
    float safeProfileScale = std::isfinite(profileScale) ? glm::max(profileScale, 0.0f) : 1.0f;
    float safeMaximum = std::isfinite(maximumLength)
                            ? glm::max(maximumLength, 0.0f)
                            : std::numeric_limits<float>::max();
    double scaled = static_cast<double>(authoredLength) *
                    static_cast<double>(safeBasisScale) *
                    static_cast<double>(safeProfileScale);
    return static_cast<float>(glm::min(scaled, static_cast<double>(safeMaximum)));
}

} // namespace particleutil

} // namespace scene

} // namespace reone
