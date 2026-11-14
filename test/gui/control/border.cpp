/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../../fixtures/graphics.h"
#include "../../fixtures/gui.h"
#include "../../fixtures/resource.h"
#include "../../fixtures/scene.h"
#include "reone/graphics/shaderprogram.h"
#include "reone/graphics/texture.h"
#include "reone/gui/control/label.h"
#include "reone/scene/render/pass.h"

using namespace reone;
using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;
using namespace reone::scene;
using namespace testing;

namespace {

class RecordingRenderPass : public IRenderPass {
public:
    void draw(Mesh &, Material &, const glm::mat4 &, const glm::mat4 &) override {}
    void drawSkinned(Mesh &, Material &, const glm::mat4 &, const glm::mat4 &, const std::vector<glm::mat4> &) override {}
    void drawDangly(Mesh &, Material &, const glm::mat4 &, const glm::mat4 &, const std::vector<glm::vec4> &) override {}
    void drawSaber(Mesh &, Material &, const glm::mat4 &, const glm::mat4 &, const glm::mat4 &) override {}
    void drawBillboard(Texture &, const glm::vec4 &, const glm::mat4 &, const glm::mat4 &, std::optional<float>) override {}
    void drawParticles(Texture &, FaceCullMode, bool, const glm::ivec2 &, const std::vector<ParticleInstance> &) override {}
    void drawProjectedBeam(Mesh &, const glm::mat4 &, const glm::mat4 &, const glm::vec4 &) override {}
    void drawGrass(float, float, Texture &, std::optional<std::reference_wrapper<Texture>> &, const std::vector<GrassInstance> &) override {}
    void drawAABB(const std::vector<glm::vec4> &) override {}
    void drawImage(Texture &, const glm::ivec2 &position, const glm::ivec2 &size, glm::vec4, glm::mat3x4, ImageAlphaMode, int) override {
        positions.push_back(position);
        sizes.push_back(size);
    }

    std::vector<glm::ivec2> positions;
    std::vector<glm::ivec2> sizes;
};

TEST(ControlBorder, an_oversized_fill_only_border_fills_the_whole_control) {
    NiceMock<MockGUI> gui;
    TestGraphicsModule graphics;
    TestResourceModule resources;
    TestSceneModule scene;
    graphics.init();
    resources.init();
    scene.init();
    ShaderProgram shader(std::vector<std::shared_ptr<Shader>> {});
    EXPECT_CALL(graphics.shaderRegistry(), get(ShaderProgramId::mvpTexture)).WillOnce(ReturnRef(shader));
    EXPECT_CALL(graphics.context(), useProgram(Ref(shader)));
    EXPECT_CALL(graphics.context(), withBlendMode(_, _)).WillOnce(Invoke([](BlendMode, const std::function<void()> &block) { block(); }));

    auto fill = std::make_shared<Texture>("fill", TextureType::TwoDim, Texture::Properties {});
    fill->setPixels(4, 4, PixelFormat::RGBA8, Texture::Layer {std::make_shared<ByteBuffer>(64, 0)});
    Control::Border border;
    border.fill = fill;
    border.dimension = 32;

    Label control(gui, scene.graphs(), graphics.services(), resources.services());
    control.setExtent({10, 20, 24, 28});
    control.setBorder(std::move(border));
    RecordingRenderPass pass;
    control.render({640, 480}, {3, 4}, pass);

    ASSERT_EQ(1u, pass.positions.size());
    EXPECT_EQ(glm::ivec2(13, 24), pass.positions.front());
    EXPECT_EQ(glm::ivec2(24, 28), pass.sizes.front());
}

} // namespace
