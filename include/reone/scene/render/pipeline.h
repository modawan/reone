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

#include "reone/graphics/framebuffer.h"
#include "reone/graphics/renderbuffer.h"
#include "reone/graphics/texture.h"

#include "pass.h"

template <>
struct std::hash<glm::ivec2> {
    size_t operator()(const glm::ivec2 &dim) const {
        size_t hash = 0;
        boost::hash_combine(hash, dim.x);
        boost::hash_combine(hash, dim.y);
        return hash;
    }
};

namespace reone {

namespace graphics {

class IStatistic;

class Context;
class MeshRegistry;
class PBRTextures;
class ShaderRegistry;
class TextureRegistry;
class Uniforms;

struct GraphicsOptions;

} // namespace graphics

namespace scene {

enum class RendererType {
    Retro,
    PBR
};

/**
 * A treatment of the whole finished frame: scan noise added from a noise
 * texture, then desaturation toward luminance with a per-channel modulation.
 * Force Sight also changes how the scene itself is drawn. The clairvoyance and
 * fury overlays each lay their texture over a distorted low-resolution copy of
 * the frame.
 */
struct VideoEffect {
    std::shared_ptr<graphics::Texture> scanNoise;
    bool saturation {false};
    glm::vec3 modulation {1.0f};
    float saturationAmount {1.0f};
    bool forceSight {false};
    std::shared_ptr<graphics::Texture> distortion;
    std::shared_ptr<graphics::Texture> clairvoyance;
    std::shared_ptr<graphics::Texture> fury;
};

/** The treatments laid over the whole finished frame. */
struct FrameEffects {
    /**
     * The speed blur: each frame is blended with a 128x128 history of the
     * earlier output, weighted by speedBlurRatio.
     */
    bool speedBlur {false};
    float speedBlurRatio {0.75f};
    std::optional<VideoEffect> videoEffect;
};

class IRenderPipeline {
public:
    virtual ~IRenderPipeline() = default;

    virtual void init() = 0;

    virtual void reset() = 0;
    virtual void inRenderPass(RenderPassName name, std::function<void(IRenderPass &)> block) = 0;

    /** Renders the frame and lays \p effects over it. */
    virtual graphics::Texture &render(const FrameEffects &effects) = 0;
};

class IRenderPipelineFactory {
public:
    virtual ~IRenderPipelineFactory() = default;

    virtual std::unique_ptr<IRenderPipeline> create(RendererType type, glm::ivec2 targetSize) = 0;
};

class RenderPipelineBase : public IRenderPipeline, boost::noncopyable {
public:
    using RenderPassCallback = std::function<void(IRenderPass &)>;

    void reset() override {
        _passCallbacks.clear();
    }

    void inRenderPass(RenderPassName name, RenderPassCallback callback) override {
        _passCallbacks[name] = std::move(callback);
    }

protected:
    struct GaussianBlurParams {
        bool vertical {false};
        bool strong {false};
    };

    glm::ivec2 _targetSize;
    graphics::GraphicsOptions &_options;
    graphics::Context &_context;
    graphics::MeshRegistry &_meshRegistry;
    graphics::ShaderRegistry &_shaderRegistry;
    graphics::IStatistic &_statistic;
    graphics::TextureRegistry &_textureRegistry;
    graphics::Uniforms &_uniforms;

    bool _inited {false};

    glm::mat4 _shadowLightSpace[graphics::kNumShadowLightSpace] {glm::mat4(1.0f)};
    glm::vec4 _shadowCascadeFarPlanes {glm::vec4(0.0f)};

    RenderPassName _passName {RenderPassName::None};
    std::map<RenderPassName, RenderPassCallback> _passCallbacks;

    bool _previousFrameWasSpeed {false};
    std::shared_ptr<graphics::Texture> _speedBlurHistoryColor;
    std::unique_ptr<graphics::Framebuffer> _speedBlurHistory;

    RenderPipelineBase(glm::ivec2 targetSize,
                       graphics::GraphicsOptions &options,
                       graphics::Context &context,
                       graphics::MeshRegistry &meshRegistry,
                       graphics::ShaderRegistry &shaderRegistry,
                       graphics::IStatistic &statistic,
                       graphics::TextureRegistry &textureRegistry,
                       graphics::Uniforms &uniforms) :
        _targetSize(std::move(targetSize)),
        _options(options),
        _context(context),
        _meshRegistry(meshRegistry),
        _shaderRegistry(shaderRegistry),
        _statistic(statistic),
        _textureRegistry(textureRegistry),
        _uniforms(uniforms) {
    }

    void applyBoxBlur(graphics::Texture &tex, graphics::Framebuffer &dst, const glm::ivec2 &size);
    void applyGaussianBlur(graphics::Texture &tex, graphics::Framebuffer &dst, const glm::ivec2 &size, const GaussianBlurParams &params);
    void applyMedianFilter(graphics::Texture &tex, graphics::Framebuffer &dst, const glm::ivec2 &size, bool strong = false);
    void applyFXAA(graphics::Texture &tex, graphics::Framebuffer &dst, const glm::ivec2 &size);
    void applySharpen(graphics::Texture &tex, graphics::Framebuffer &dst, const glm::ivec2 &size, float amount);
    /**
     * When \p effects has the speed blur, blends \p output with the history
     * if the previous frame was blurred too, using \p scratch, then shrinks
     * \p output into the history.
     */
    void applySpeedBlur(const FrameEffects &effects, graphics::Framebuffer &output, graphics::Texture &outputColor, graphics::Framebuffer &scratch);
    /** The 128x128 copy of earlier output the speed blur and the overlays read. */
    void initSpeedBlurHistory();
    /** Lays \p overlay over \p output, showing the history distorted by \p distortion through it. */
    void drawDistortionOverlay(graphics::Texture &distortion, graphics::Texture &overlay, graphics::Framebuffer &output, graphics::Texture &outputColor, graphics::Framebuffer &scratch);
    /** Lays the video effect of \p effects, if any, over \p output through \p scratch. */
    void applyVideoEffect(const FrameEffects &effects, graphics::Framebuffer &output, graphics::Texture &outputColor, graphics::Framebuffer &scratch);
    /**
     * Lays the clairvoyance and then the fury overlay of the video effect of
     * \p effects, if any, over \p output through \p scratch.
     */
    void applyDistortionOverlays(const FrameEffects &effects, graphics::Framebuffer &output, graphics::Texture &outputColor, graphics::Framebuffer &scratch);
};

class RenderPipelineFactory : public IRenderPipelineFactory, boost::noncopyable {
public:
    RenderPipelineFactory(graphics::GraphicsOptions &options,
                          graphics::Context &context,
                          graphics::MeshRegistry &meshRegistry,
                          graphics::PBRTextures &pbrTextures,
                          graphics::ShaderRegistry &shaderRegistry,
                          graphics::IStatistic &statistic,
                          graphics::TextureRegistry &textureRegistry,
                          graphics::Uniforms &uniforms) :
        _options(options),
        _context(context),
        _meshRegistry(meshRegistry),
        _pbrTextures(pbrTextures),
        _shaderRegistry(shaderRegistry),
        _statistic(statistic),
        _textureRegistry(textureRegistry),
        _uniforms(uniforms) {
    }

    std::unique_ptr<IRenderPipeline> create(RendererType type, glm::ivec2 targetSize) override;

private:
    graphics::GraphicsOptions &_options;
    graphics::Context &_context;
    graphics::MeshRegistry &_meshRegistry;
    graphics::PBRTextures &_pbrTextures;
    graphics::ShaderRegistry &_shaderRegistry;
    graphics::IStatistic &_statistic;
    graphics::TextureRegistry &_textureRegistry;
    graphics::Uniforms &_uniforms;
};

} // namespace scene

} // namespace reone
