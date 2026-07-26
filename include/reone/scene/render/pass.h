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

#include "reone/graphics/types.h"
#include "reone/graphics/uniforms.h"

namespace reone {

namespace graphics {

class IContext;
class IMeshRegistry;
class IPBRTextures;
class IShaderRegistry;
class IStatistic;
class ITextureRegistry;
class IUniforms;
class Mesh;
class Texture;

struct GraphicsOptions;
struct LocalUniforms;
struct Material;

} // namespace graphics

namespace scene {

enum class RenderPassName {
    None,
    DirLightShadowsPass,
    PointLightShadows,
    OpaqueGeometry,
    TransparentGeometry,
    PostProcessing,
    Debug
};

enum class ParticleReconstruction {
    Legacy = 0,
    Cubic = 1
};

enum class ParticleAlphaMode {
    Legacy = 0,
    Texture = 1,
    Luminance = 2,
    AlphaAndLuminance = 3
};

enum class ParticleTrailMode {
    Legacy = 0,
    AnalyticCore = 1
};

enum class ParticleDiagnosticMode {
    Composite = 0,
    TextureOnly = 1,
    AlphaOnly = 2,
    SolidColor = 3
};

/**
 * Shader-facing particle presentation options.
 *
 * All zero-valued modes select the original renderer path. This is deliberate:
 * ordinary emitters remain one-sample and retain Odyssey Lighten decoding.
 */
struct ParticleRenderPolicy {
    ParticleReconstruction reconstruction {ParticleReconstruction::Legacy};
    ParticleAlphaMode alpha {ParticleAlphaMode::Legacy};
    ParticleTrailMode trail {ParticleTrailMode::Legacy};
    ParticleDiagnosticMode diagnostic {ParticleDiagnosticMode::Composite};
    float reconstructionStrength {0.0f};
    float alphaExponent {1.0f};
    float trailCoreIntensity {0.0f};
};

struct ParticleInstance {
    int frame {0};
    glm::vec3 position {0.0f};
    glm::vec2 size {0.0f};
    glm::vec4 color {1.0f};
    glm::vec3 right {0.0f};
    glm::vec3 up {0.0f};
};

inline void populateParticleUniforms(
    graphics::ParticleUniforms &uniforms,
    const ParticleRenderPolicy &policy,
    bool motionBlur,
    const glm::ivec2 &gridSize,
    const std::vector<ParticleInstance> &particles) {

    uniforms.gridSize = gridSize;
    uniforms.reconstructionMode = static_cast<int>(policy.reconstruction);
    uniforms.alphaMode = static_cast<int>(policy.alpha);
    uniforms.trailMode = static_cast<int>(policy.trail);
    uniforms.motionBlur = motionBlur ? 1 : 0;
    uniforms.reconstructionStrength = policy.reconstructionStrength;
    uniforms.alphaExponent = policy.alphaExponent;
    uniforms.trailCoreIntensity = policy.trailCoreIntensity;
    uniforms.diagnosticMode = static_cast<int>(policy.diagnostic);
    for (size_t i = 0; i < particles.size(); ++i) {
        const auto &particle = particles[i];
        uniforms.particles[i].positionFrame = glm::vec4(particle.position, static_cast<float>(particle.frame));
        uniforms.particles[i].size = particle.size;
        uniforms.particles[i].color = particle.color;
        uniforms.particles[i].right = glm::vec4(particle.right, 0.0f);
        uniforms.particles[i].up = glm::vec4(particle.up, 0.0f);
    }
}

struct GrassInstance {
    int variant {0};
    glm::vec3 position {0.0f};
    glm::vec2 lightmapUV {0.0f};
};

class IRenderPass {
public:
    virtual ~IRenderPass() = default;

    virtual void draw(graphics::Mesh &mesh,
                      graphics::Material &material,
                      const glm::mat4 &transform,
                      const glm::mat4 &transformInv) = 0;

    virtual void drawSkinned(graphics::Mesh &mesh,
                             graphics::Material &material,
                             const glm::mat4 &transform,
                             const glm::mat4 &transformInv,
                             const std::vector<glm::mat4> &bones) = 0;

    virtual void drawDangly(graphics::Mesh &mesh,
                            graphics::Material &material,
                            const glm::mat4 &transform,
                            const glm::mat4 &transformInv,
                            const std::vector<glm::vec4> &positions) = 0;

    virtual void drawSaber(graphics::Mesh &mesh,
                           graphics::Material &material,
                           const glm::mat4 &transform,
                           const glm::mat4 &transformInv,
                           const glm::vec4 &displacement) = 0;

    virtual void drawBillboard(graphics::Texture &texture,
                               const glm::vec4 &color,
                               const glm::mat4 &transform,
                               const glm::mat4 &transformInv,
                               std::optional<float> size) = 0;

    virtual void drawParticles(graphics::Texture &texture,
                               graphics::FaceCullMode faceCulling,
                               bool premultipliedAlpha,
                               bool motionBlur,
                               const ParticleRenderPolicy &policy,
                               const glm::ivec2 &gridSize,
                               const std::vector<ParticleInstance> &particles) = 0;

    virtual void drawGrass(float radius,
                           float quadSize,
                           graphics::Texture &texture,
                           std::optional<std::reference_wrapper<graphics::Texture>> &lightmap,
                           const std::vector<GrassInstance> &instances) = 0;

    virtual void drawAABB(const std::vector<glm::vec4> &corners) = 0;

    virtual void drawImage(graphics::Texture &texture,
                           const glm::ivec2 &position,
                           const glm::ivec2 &scale,
                           glm::vec4 color = glm::vec4(1.0f),
                           glm::mat3x4 uv = glm::mat3x4(1.0f)) = 0;
};

} // namespace scene

} // namespace reone
