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

#include "reone/scene/render/pipeline.h"

#include "reone/graphics/context.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/pbrtextures.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/statistic.h"
#include "reone/graphics/textureregistry.h"
#include "reone/graphics/textureutil.h"
#include "reone/graphics/uniforms.h"
#include "reone/scene/render/pipeline/pbr.h"
#include "reone/scene/render/pipeline/retro.h"

using namespace reone::graphics;

namespace reone {

namespace scene {

void RenderPipelineBase::applyBoxBlur(Texture &srcTexture, Framebuffer &dst, const glm::ivec2 &size) {
    _context.useProgram(_shaderRegistry.get(ShaderProgramId::postBoxBlur4));
    _context.bindDrawFramebuffer(dst, {0});
    _context.bindTexture(srcTexture);
    _context.withViewport(glm::ivec4(0, 0, size), [this]() {
        _context.clearColorDepth();
        _meshRegistry.get(MeshName::quadNDC).draw(_statistic);
    });
}

void RenderPipelineBase::applyGaussianBlur(Texture &tex,
                                           Framebuffer &dst,
                                           const glm::ivec2 &size,
                                           const GaussianBlurParams &params) {
    _uniforms.setScreenEffect([&size, &params](auto &se) {
        se.screenResolution = glm::vec2(size);
        se.screenResolutionRcp = 1.0f / se.screenResolution;
        se.blurDirection = params.vertical ? glm::vec2(0.0f, 1.0f) : glm::vec2(1.0f, 0.0f);
    });
    _context.useProgram(_shaderRegistry.get(params.strong
                                                ? ShaderProgramId::postGaussianBlur13
                                                : ShaderProgramId::postGaussianBlur9));
    _context.bindDrawFramebuffer(dst, {0});
    _context.bindTexture(tex);
    _context.withViewport(glm::ivec4(0, 0, size), [this]() {
        _context.clearColorDepth();
        _meshRegistry.get(MeshName::quadNDC).draw(_statistic);
    });
}

void RenderPipelineBase::applyMedianFilter(Texture &tex,
                                           Framebuffer &dst,
                                           const glm::ivec2 &size,
                                           bool strong) {
    _context.useProgram(_shaderRegistry.get(strong
                                                ? ShaderProgramId::postMedianFilter5
                                                : ShaderProgramId::postMedianFilter3));
    _context.bindDrawFramebuffer(dst, {0});
    _context.bindTexture(tex);
    _context.withViewport(glm::ivec4(0, 0, size), [this]() {
        _context.clearColorDepth();
        _meshRegistry.get(MeshName::quadNDC).draw(_statistic);
    });
}

void RenderPipelineBase::applyFXAA(Texture &tex, Framebuffer &dst, const glm::ivec2 &size) {
    _uniforms.setScreenEffect([&size](auto &se) {
        se.screenResolution = glm::vec2(size);
        se.screenResolutionRcp = 1.0f / se.screenResolution;
    });
    _context.useProgram(_shaderRegistry.get(ShaderProgramId::postFXAA));
    _context.bindDrawFramebuffer(dst, {0});
    _context.bindTexture(tex);
    _context.withViewport(glm::ivec4(0, 0, size), [this]() {
        _context.clearColorDepth();
        _meshRegistry.get(MeshName::quadNDC).draw(_statistic);
    });
}

void RenderPipelineBase::applySharpen(Texture &tex,
                                      Framebuffer &dst,
                                      const glm::ivec2 &size,
                                      float amount) {
    _uniforms.setScreenEffect([&size, &amount](auto &se) {
        se.screenResolution = glm::vec2(size);
        se.screenResolutionRcp = 1.0f / se.screenResolution;
        se.sharpenAmount = amount;
    });
    _context.useProgram(_shaderRegistry.get(ShaderProgramId::postSharpen));
    _context.bindDrawFramebuffer(dst, {0});
    _context.bindTexture(tex);
    _context.withViewport(glm::ivec4(0, 0, size), [this]() {
        _context.clearColorDepth();
        _meshRegistry.get(MeshName::quadNDC).draw(_statistic);
    });
}

static constexpr int kSpeedBlurHistorySize = 128;

void RenderPipelineBase::initSpeedBlurHistory() {
    if (_speedBlurHistory) {
        return;
    }
    _speedBlurHistoryColor = std::make_shared<Texture>(
        "speed_blur_history",
        TextureType::TwoDim,
        getTextureProperties(TextureUsage::ColorBuffer));
    _speedBlurHistoryColor->clear(kSpeedBlurHistorySize, kSpeedBlurHistorySize, PixelFormat::RGBA8);
    _speedBlurHistoryColor->init();
    _speedBlurHistory = std::make_unique<Framebuffer>();
    _speedBlurHistory->attachColorDepth(_speedBlurHistoryColor, nullptr);
    _speedBlurHistory->init();
}

void RenderPipelineBase::applySpeedBlur(const FrameEffects &effects, Framebuffer &output, Texture &outputColor, Framebuffer &scratch) {
    if (!effects.speedBlur) {
        _previousFrameWasSpeed = false;
        return;
    }
    initSpeedBlurHistory();
    const glm::ivec4 screenRect {0, 0, _targetSize.x, _targetSize.y};
    // The first blurred frame only fills the history.
    if (_previousFrameWasSpeed) {
        _uniforms.setScreenEffect([this, &effects](auto &se) {
            se.screenResolution = glm::vec2(_targetSize);
            se.screenResolutionRcp = 1.0f / se.screenResolution;
            se.speedBlurRatio = effects.speedBlurRatio;
        });
        _context.useProgram(_shaderRegistry.get(ShaderProgramId::postSpeedBlur));
        _context.bindDrawFramebuffer(scratch, {0});
        _context.bindTexture(outputColor, TextureUnits::mainTex);
        _context.bindTexture(*_speedBlurHistoryColor, TextureUnits::lightmap);
        _context.withViewport(glm::ivec4(0, 0, _targetSize), [this]() {
            _context.clearColorDepth();
            _meshRegistry.get(MeshName::quadNDC).draw(_statistic);
        });
        _context.blitFramebuffer(scratch, output, screenRect, screenRect);
    }
    _context.blitFramebuffer(
        output,
        *_speedBlurHistory,
        screenRect,
        glm::ivec4(0, 0, kSpeedBlurHistorySize, kSpeedBlurHistorySize),
        0, 0,
        FramebufferBlitFlags::color,
        FramebufferBlitFilter::Linear);
    _previousFrameWasSpeed = true;
}

void RenderPipelineBase::applyDistortionOverlays(const FrameEffects &effects, Framebuffer &output, Texture &outputColor, Framebuffer &scratch) {
    if (!effects.videoEffect || !effects.videoEffect->distortion) {
        return;
    }
    const VideoEffect &effect = *effects.videoEffect;
    if (effect.clairvoyance) {
        drawDistortionOverlay(*effect.distortion, *effect.clairvoyance, output, outputColor, scratch);
    }
    if (effect.fury) {
        drawDistortionOverlay(*effect.distortion, *effect.fury, output, outputColor, scratch);
    }
}

// Each overlay first shrinks the frame into the 128x128 history the speed
// blur also keeps, then lays its texture over the frame, showing the shrunken
// frame, distorted, where the texture is not transparent.
void RenderPipelineBase::drawDistortionOverlay(Texture &distortion, Texture &overlay, Framebuffer &output, Texture &outputColor, Framebuffer &scratch) {
    initSpeedBlurHistory();
    const glm::ivec4 screenRect {0, 0, _targetSize.x, _targetSize.y};
    _context.blitFramebuffer(
        output,
        *_speedBlurHistory,
        screenRect,
        glm::ivec4(0, 0, kSpeedBlurHistorySize, kSpeedBlurHistorySize),
        0, 0,
        FramebufferBlitFlags::color,
        FramebufferBlitFilter::Linear);
    _uniforms.setScreenEffect([this](auto &se) {
        se.screenResolution = glm::vec2(_targetSize);
        se.screenResolutionRcp = 1.0f / se.screenResolution;
    });
    _context.useProgram(_shaderRegistry.get(ShaderProgramId::postDistortionOverlay));
    _context.bindDrawFramebuffer(scratch, {0});
    _context.bindTexture(outputColor, TextureUnits::mainTex);
    _context.bindTexture(*_speedBlurHistoryColor, TextureUnits::lightmap);
    _context.bindTexture(distortion, TextureUnits::envMap);
    _context.bindTexture(overlay, TextureUnits::normalMap);
    _context.withViewport(glm::ivec4(0, 0, _targetSize), [this]() {
        _context.clearColorDepth();
        _meshRegistry.get(MeshName::quadNDC).draw(_statistic);
    });
    _context.blitFramebuffer(scratch, output, screenRect, screenRect);
}

void RenderPipelineBase::applyVideoEffect(const FrameEffects &effects, Framebuffer &output, Texture &outputColor, Framebuffer &scratch) {
    if (!effects.videoEffect) {
        return;
    }
    const VideoEffect &effect = *effects.videoEffect;
    _uniforms.setScreenEffect([&effect](auto &se) {
        se.videoModulationR = effect.modulation.r;
        se.videoModulationG = effect.modulation.g;
        se.videoModulationB = effect.modulation.b;
        se.videoSaturation = effect.saturationAmount;
        se.videoSaturationEnabled = effect.saturation ? 1.0f : 0.0f;
        se.videoScanNoiseEnabled = effect.scanNoise ? 1.0f : 0.0f;
    });
    _context.useProgram(_shaderRegistry.get(ShaderProgramId::postVideoEffect));
    _context.bindDrawFramebuffer(scratch, {0});
    _context.bindTexture(outputColor, TextureUnits::mainTex);
    if (effect.scanNoise) {
        _context.bindTexture(*effect.scanNoise, TextureUnits::lightmap);
    }
    _context.withViewport(glm::ivec4(0, 0, _targetSize), [this]() {
        _context.clearColorDepth();
        _meshRegistry.get(MeshName::quadNDC).draw(_statistic);
    });
    const glm::ivec4 screenRect {0, 0, _targetSize.x, _targetSize.y};
    _context.blitFramebuffer(scratch, output, screenRect, screenRect);
}

std::unique_ptr<IRenderPipeline> RenderPipelineFactory::create(RendererType type, glm::ivec2 targetSize) {
    switch (type) {
    case RendererType::Retro:
        return std::make_unique<RetroRenderPipeline>(
            std::move(targetSize),
            _options,
            _context,
            _meshRegistry,
            _shaderRegistry,
            _statistic,
            _textureRegistry,
            _uniforms);
    case RendererType::PBR:
        return std::make_unique<PBRRenderPipeline>(
            std::move(targetSize),
            _options,
            _context,
            _meshRegistry,
            _pbrTextures,
            _shaderRegistry,
            _statistic,
            _textureRegistry,
            _uniforms);
    default:
        throw std::invalid_argument("Unsupported renderer type: " + std::to_string(static_cast<int>(type)));
    }
}

} // namespace scene

} // namespace reone
