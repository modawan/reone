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

#include "options.h"
#include "types.h"

namespace reone {

namespace graphics {

class Framebuffer;
class ShaderProgram;
class Texture;
class UniformBuffer;

enum class FramebufferBlitFilter {
    Nearest,
    Linear
};

struct FramebufferBlitFlags {
    static constexpr int color = 1;
    static constexpr int depth = 2;
    static constexpr int stencil = 3;

    static constexpr int colorDepth = color | depth;
};

class IContext {
public:
    virtual ~IContext() = default;

    virtual void clearColor(glm::vec4 color = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f)) = 0;
    virtual void clearDepth() = 0;
    virtual void clearColorDepth(glm::vec4 color = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f)) = 0;

    virtual void resetProgram() = 0;
    virtual void useProgram(ShaderProgram &program) = 0;

    virtual void resetDrawFramebuffer() = 0;
    virtual void resetReadFramebuffer() = 0;
    virtual void bindDrawFramebuffer(Framebuffer &buffer, std::vector<int> colorIndices = std::vector<int>()) = 0;
    virtual void bindReadFramebuffer(Framebuffer &buffer, std::optional<int> colorIdx = std::nullopt) = 0;
    virtual void blitFramebuffer(Framebuffer &source,
                                 Framebuffer &destination,
                                 const glm::ivec4 &srcRect,
                                 const glm::ivec4 &dstRect,
                                 int srcColorIdx = 0,
                                 int dstColorIdx = 0,
                                 int mask = FramebufferBlitFlags::color,
                                 FramebufferBlitFilter filter = FramebufferBlitFilter::Nearest) = 0;

    /** Reads the back buffer before presentation for automated screenshots. */
    virtual std::shared_ptr<Texture> captureScreen(int width, int height) = 0;
    /** Copies the back buffer, from its lower left corner, into the texture at its size. */
    virtual void copyBackBuffer(Texture &destination) = 0;

    virtual void bindUniformBuffer(UniformBuffer &buffer, int index) = 0;
    virtual void bindTexture(Texture &texture, int unit = TextureUnits::mainTex) = 0;

    virtual const glm::ivec4 &viewport() const = 0;
    /**
     * The window's drawable is \p pixelSize pixels, which on a high-density
     * display is more than the window's size. The screen's viewport covers it
     * when stretched, or else the largest centred part of it with the
     * screen's shape. Taken between frames, when the screen's is the only
     * viewport.
     */
    virtual void setScreenPixelSize(glm::ivec2 pixelSize) = 0;
    virtual DepthTestMode depthTestMode() const = 0;
    virtual bool depthMask() const = 0;
    virtual PolygonMode polygonMode() const = 0;
    virtual FaceCullMode faceCullMode() const = 0;
    virtual BlendMode blendMode() const = 0;

    virtual void pushViewport(glm::ivec4 viewport) = 0;
    virtual void pushDepthTestMode(DepthTestMode mode) = 0;
    virtual void pushDepthMask(bool enabled) = 0;
    virtual void pushPolygonMode(PolygonMode mode) = 0;
    virtual void pushFaceCullMode(FaceCullMode mode) = 0;
    virtual void pushBlendMode(BlendMode mode) = 0;

    virtual void popViewport() = 0;
    virtual void popDepthTestMode() = 0;
    virtual void popDepthMask() = 0;
    virtual void popPolygonMode() = 0;
    virtual void popFaceCullMode() = 0;
    virtual void popBlendMode() = 0;

    virtual void withViewport(glm::ivec4 viewport, const std::function<void()> &block) = 0;
    /**
     * Runs \p block with drawing clipped to \p bounds (left, bottom, width,
     * height, from the framebuffer's bottom left), first clearing the colour
     * there. A clip inside another keeps to both, and the outer one is back
     * after \p block.
     */
    virtual void withScissorTest(const glm::ivec4 &bounds, const std::function<void()> &block) = 0;
    /** As withScissorTest, without clearing. */
    virtual void withScissorTestNoClear(const glm::ivec4 &bounds, const std::function<void()> &block) = 0;
    virtual void withDepthTestMode(DepthTestMode mode, const std::function<void()> &block) = 0;
    virtual void withDepthMask(bool enabled, const std::function<void()> &block) = 0;
    virtual void withPolygonMode(PolygonMode mode, const std::function<void()> &block) = 0;
    virtual void withFaceCullMode(FaceCullMode mode, const std::function<void()> &block) = 0;
    virtual void withBlendMode(BlendMode mode, const std::function<void()> &block) = 0;
};

/**
 * Runs \p block in \p context with drawing clipped to \p rect, given as
 * left, top, width and height in screen units counted down from the top of a
 * screen \p screenSize large (withScissorTestNoClear, or withScissorTest when
 * \p clear). The screen covers the context's viewport, which on a
 * high-density display holds more pixels than the screen has units; the clip
 * covers every pixel the rect touches.
 */
void withScissorRect(IContext &context, const glm::ivec2 &screenSize, const glm::ivec4 &rect, const std::function<void()> &block, bool clear = false);

/**
 * The pixels of \p context's viewport to a unit of a screen \p screenSize
 * large, across and down; 1 along a side either of them has empty.
 */
glm::vec2 screenPixelScale(const IContext &context, const glm::ivec2 &screenSize);

class Context : public IContext, boost::noncopyable {
public:
    Context(GraphicsOptions &options) :
        _options(options) {
    }

    void init();

    void clearColor(glm::vec4 color = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f)) override;
    void clearDepth() override;
    void clearColorDepth(glm::vec4 color = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f)) override;

    void resetProgram() override;
    void useProgram(ShaderProgram &program) override;

    void resetDrawFramebuffer() override;
    void resetReadFramebuffer() override;
    void bindDrawFramebuffer(Framebuffer &buffer, std::vector<int> colorIndices = std::vector<int>()) override;
    void bindReadFramebuffer(Framebuffer &buffer, std::optional<int> colorIdx = std::nullopt) override;
    void blitFramebuffer(Framebuffer &source,
                         Framebuffer &destination,
                         const glm::ivec4 &srcRect,
                         const glm::ivec4 &dstRect,
                         int srcColorIdx = 0,
                         int dstColorIdx = 0,
                         int mask = FramebufferBlitFlags::color,
                         FramebufferBlitFilter filter = FramebufferBlitFilter::Nearest) override;

    std::shared_ptr<Texture> captureScreen(int width, int height) override;
    void copyBackBuffer(Texture &destination) override;

    void bindUniformBuffer(UniformBuffer &buffer, int index) override;
    void bindTexture(Texture &texture, int unit = TextureUnits::mainTex) override;

    const glm::ivec4 &viewport() const override {
        return _viewports.top();
    }
    void setScreenPixelSize(glm::ivec2 pixelSize) override;

    DepthTestMode depthTestMode() const override {
        return _depthTestModes.top();
    }

    bool depthMask() const override {
        return _depthMasks.top();
    }

    PolygonMode polygonMode() const override {
        return _polygonModes.top();
    }

    FaceCullMode faceCullMode() const override {
        return _faceCullModes.top();
    }

    BlendMode blendMode() const override {
        return _blendModes.top();
    }

    void pushViewport(glm::ivec4 viewport) override;
    void pushDepthTestMode(DepthTestMode mode) override;
    void pushDepthMask(bool enabled) override;
    void pushPolygonMode(PolygonMode mode) override;
    void pushFaceCullMode(FaceCullMode mode) override;
    void pushBlendMode(BlendMode mode) override;

    void popViewport() override;
    void popDepthTestMode() override;
    void popDepthMask() override;
    void popPolygonMode() override;
    void popFaceCullMode() override;
    void popBlendMode() override;

    void withViewport(glm::ivec4 viewport, const std::function<void()> &block) override;
    void withScissorTest(const glm::ivec4 &bounds, const std::function<void()> &block) override;
    void withScissorTestNoClear(
        const glm::ivec4 &bounds,
        const std::function<void()> &block) override;
    void withDepthTestMode(DepthTestMode mode, const std::function<void()> &block) override;
    void withDepthMask(bool enabled, const std::function<void()> &block) override;
    void withPolygonMode(PolygonMode mode, const std::function<void()> &block) override;
    void withFaceCullMode(FaceCullMode mode, const std::function<void()> &block) override;
    void withBlendMode(BlendMode mode, const std::function<void()> &block) override;

private:
    GraphicsOptions &_options;

    bool _inited {false};

    std::optional<std::reference_wrapper<ShaderProgram>> _program;
    std::optional<std::reference_wrapper<Framebuffer>> _readFramebuffer;
    std::optional<std::reference_wrapper<Framebuffer>> _drawFramebuffer;
    int _activeTexUnit {0};

    // States

    std::stack<glm::ivec4> _viewports;
    std::stack<DepthTestMode> _depthTestModes;
    std::stack<bool> _depthMasks;
    std::stack<PolygonMode> _polygonModes;
    std::stack<FaceCullMode> _faceCullModes;
    std::stack<BlendMode> _blendModes;
    /** The clips in force, each within the one before. */
    std::stack<glm::ivec4> _scissors;

    // END States

    void withScissor(const glm::ivec4 &bounds, bool clear, const std::function<void()> &block);

    void setViewport(glm::ivec4 viewport);
    void setDepthTestMode(DepthTestMode mode);
    void setDepthMask(bool enabled);
    void setPolygonMode(PolygonMode mode);
    void setFaceCullMode(FaceCullMode mode);
    void setBlendMode(BlendMode mode);
};

} // namespace graphics

} // namespace reone
