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

#include <gtest/gtest.h>

#include "../../fixtures/graphics.h"
#include "../../fixtures/gui.h"
#include "../../fixtures/resource.h"
#include "../../fixtures/scene.h"
#include "reone/graphics/shaderprogram.h"
#include "reone/graphics/texture.h"
#include "reone/gui/control/progressbar.h"
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
    struct ImageDraw {
        glm::ivec2 position;
        glm::ivec2 size;
    };

    void draw(Mesh &, Material &, const glm::mat4 &, const glm::mat4 &) override {}
    void drawSkinned(Mesh &, Material &, const glm::mat4 &, const glm::mat4 &, const std::vector<glm::mat4> &) override {}
    void drawDangly(Mesh &, Material &, const glm::mat4 &, const glm::mat4 &, const std::vector<glm::vec4> &) override {}
    void drawSaber(Mesh &, Material &, const glm::mat4 &, const glm::mat4 &, const glm::mat4 &) override {}
    void drawBillboard(Texture &, const glm::vec4 &, const glm::mat4 &, const glm::mat4 &, std::optional<float>) override {}
    void drawParticles(Texture &, FaceCullMode, bool, const glm::ivec2 &, const std::vector<ParticleInstance> &) override {}
    void drawProjectedBeam(Mesh &, const glm::mat4 &, const glm::mat4 &, const glm::vec4 &) override {}
    void drawGrass(float, float, Texture &, std::optional<std::reference_wrapper<Texture>> &, const std::vector<GrassInstance> &) override {}
    void drawAABB(const std::vector<glm::vec4> &) override {}

    void drawImage(
        Texture &,
        const glm::ivec2 &position,
        const glm::ivec2 &size,
        glm::vec4,
        glm::mat3x4,
        ImageAlphaMode,
        int) override {
        imageDraws.push_back({position, size});
    }
    void drawQuad(const glm::ivec2 &, const glm::ivec2 &, glm::vec4) override {}

    std::vector<ImageDraw> imageDraws;
};

class ProgressBarTest : public Test {
protected:
    void SetUp() override {
        _graphicsModule.init();
        _resourceModule.init();
        _sceneModule.init();

        _fill = std::make_shared<Texture>(
            "fill",
            TextureType::TwoDim,
            Texture::Properties {});
        _shader = std::make_unique<ShaderProgram>(std::vector<std::shared_ptr<Shader>> {});
        EXPECT_CALL(_resourceModule.textures(), get("fill", TextureUsage::GUI))
            .WillOnce(Return(_fill));
        EXPECT_CALL(_graphicsModule.shaderRegistry(), get(ShaderProgramId::mvpTexture))
            .Times(AnyNumber())
            .WillRepeatedly(ReturnRef(*_shader));
        EXPECT_CALL(_graphicsModule.context(), useProgram(Ref(*_shader)))
            .Times(AnyNumber());
        EXPECT_CALL(_graphicsModule.context(), withBlendMode(_, _))
            .WillRepeatedly(InvokeArgument<1>());
    }

    std::unique_ptr<ProgressBar> newProgressBar(Control::Extent extent, bool startFromLeft) {
        resource::generated::GUI_CONTROLS control;
        control.CONTROLTYPE = static_cast<int>(ControlType::ProgressBar);
        control.EXTENT.LEFT = extent.left;
        control.EXTENT.TOP = extent.top;
        control.EXTENT.WIDTH = extent.width;
        control.EXTENT.HEIGHT = extent.height;
        control.STARTFROMLEFT = startFromLeft;
        control.MAXVALUE = 100;
        control.PROGRESS.emplace();
        control.PROGRESS->FILL = "fill";
        control.PROGRESS->FILLSTYLE = static_cast<int>(Control::Border::FillStyle::Stretch);

        auto bar = std::make_unique<ProgressBar>(
            _gui,
            _sceneModule.graphs(),
            _graphicsModule.services(),
            _resourceModule.services());
        bar->load(control, false);
        return bar;
    }

    NiceMock<MockGUI> _gui;
    TestGraphicsModule _graphicsModule;
    TestResourceModule _resourceModule;
    TestSceneModule _sceneModule;
    std::shared_ptr<Texture> _fill;
    std::unique_ptr<ShaderProgram> _shader;
};

TEST_F(ProgressBarTest, vertical_fill_keeps_bottom_edge_and_grows_monotonically) {
    auto bar = newProgressBar({10, 100, 7, 19}, false);
    RecordingRenderPass pass;
    int previousHeight = 0;

    for (int value = 0; value <= 100; ++value) {
        pass.imageDraws.clear();
        bar->setCurValue(value);
        bar->render({640, 480}, {3, 0}, pass);

        // The share is truncated to whole pixels of the 19 inside the frame; an empty one draws nothing.
        if (19 * value / 100 == 0) {
            EXPECT_TRUE(pass.imageDraws.empty()) << "value = " << value;
            continue;
        }

        // The art is laid over the share, which starts at the bottom edge.
        ASSERT_EQ(pass.imageDraws.size(), 1u) << "value = " << value;
        const auto &draw = pass.imageDraws.front();
        EXPECT_EQ(draw.position.y + draw.size.y, 119) << "value = " << value;
        EXPECT_GE(draw.size.y, previousHeight) << "value = " << value;
        previousHeight = draw.size.y;
    }
}

TEST_F(ProgressBarTest, right_anchored_horizontal_fill_keeps_right_edge_and_grows_monotonically) {
    auto bar = newProgressBar({100, 10, 19, 7}, false);
    RecordingRenderPass pass;
    int previousWidth = 0;

    for (int value = 0; value <= 100; ++value) {
        pass.imageDraws.clear();
        bar->setCurValue(value);
        bar->render({640, 480}, {5, 2}, pass);

        // The share is truncated to whole pixels of the 19 inside the frame; an empty one draws nothing.
        if (19 * value / 100 == 0) {
            EXPECT_TRUE(pass.imageDraws.empty()) << "value = " << value;
            continue;
        }

        // The art is laid over the share, which starts at the right edge.
        ASSERT_EQ(pass.imageDraws.size(), 1u) << "value = " << value;
        const auto &draw = pass.imageDraws.front();
        EXPECT_EQ(draw.position.x + draw.size.x, 124) << "value = " << value;
        EXPECT_GE(draw.size.x, previousWidth) << "value = " << value;
        previousWidth = draw.size.x;
    }
}

TEST(ProgressBarFill, a_value_fills_its_share_in_range_and_no_maximum_fills_all) {
    EXPECT_FLOAT_EQ(0.25f, ProgressBar::fillFraction(25, 100));
    EXPECT_FLOAT_EQ(0.0f, ProgressBar::fillFraction(-5, 100));
    EXPECT_FLOAT_EQ(1.0f, ProgressBar::fillFraction(150, 100));
    EXPECT_FLOAT_EQ(1.0f, ProgressBar::fillFraction(3, 0));
}

TEST_F(ProgressBarTest, a_bar_with_no_maximum_draws_its_whole_fill) {
    auto bar = newProgressBar({10, 10, 40, 7}, true);
    bar->setMaxValue(0);
    RecordingRenderPass pass;
    bar->render({640, 480}, {0, 0}, pass);

    ASSERT_EQ(1u, pass.imageDraws.size());
    EXPECT_EQ(40, pass.imageDraws.front().size.x);
}

} // namespace
