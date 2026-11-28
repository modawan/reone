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

#include "../fixtures/graphics.h"
#include "../fixtures/gui.h"
#include "../fixtures/resource.h"
#include "../fixtures/scene.h"
#include "reone/graphics/context.h"
#include "reone/graphics/font.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/options.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/statistic.h"
#include "reone/graphics/texture.h"
#include "reone/graphics/uniforms.h"
#include "reone/gui/control/label.h"
#include "reone/gui/tooltip.h"
#include "reone/scene/render/pass.h"

using namespace reone;
using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;
using namespace reone::scene;
using namespace testing;

namespace {

// A tooltip on a 640 by 480 screen, its panel's label 200 wide, and two
// controls with tooltips.
class TooltipTest : public Test {
protected:
    void SetUp() override {
        options.width = 640;
        options.height = 480;
        graphicsModule.init();
        resourceModule.init();
        sceneModule.init();

        auto texture = std::make_shared<Texture>("font", TextureType::TwoDim, Texture::Properties {});
        texture->setPixels(1, 1, PixelFormat::RGBA8, Texture::Layer {std::make_shared<ByteBuffer>(4, 0)});
        Texture::Features features;
        features.fontHeight = 0.12f;
        features.numChars = 256;
        features.upperLeftCoords.assign(256, glm::vec3(0, 1, 0));
        features.lowerRightCoords.assign(256, glm::vec3(0.5f, 0, 0));
        texture->setFeatures(std::move(features));
        font = std::make_shared<Font>(fontContext, meshes, shaders, statistic, uniforms);
        font->load(texture);

        label = std::make_shared<Label>(*panel, sceneModule.graphs(), graphicsModule.services(), resourceModule.services());
        label->setExtent({0, 0, 200, 40});
        Control::Text text;
        text.font = font;
        label->setText(std::move(text));
        ON_CALL(*panel, scale()).WillByDefault(Return(1.0f));
        ON_CALL(*panel, findControl("tooltip")).WillByDefault(Return(label));
        ON_CALL(*panel, withRenderPass(_)).WillByDefault(Invoke([this](const std::function<void(IRenderPass &)> &) { ++draws; }));
        tooltip.setPanel(panel);

        map = std::make_unique<Label>(screen, sceneModule.graphs(), graphicsModule.services(), resourceModule.services());
        map->setTooltip("Open the map");
        journal = std::make_unique<Label>(screen, sceneModule.graphs(), graphicsModule.services(), resourceModule.services());
        journal->setTooltip("Open the journal");
    }

    void moveOnto(Control &control) {
        tooltip.beginPointerMove();
        tooltip.hover(screen, std::optional<std::reference_wrapper<Control>>(control));
        tooltip.endPointerMove();
    }

    bool shows() {
        const int before {draws};
        tooltip.render();
        return draws > before;
    }

    GraphicsOptions options;
    TestGraphicsModule graphicsModule;
    TestResourceModule resourceModule;
    TestSceneModule sceneModule;
    Context fontContext {options};
    Statistic statistic;
    MeshRegistry meshes {statistic};
    ShaderRegistry shaders;
    Uniforms uniforms {fontContext};
    std::shared_ptr<Font> font;
    std::shared_ptr<NiceMock<MockGUI>> panel {std::make_shared<NiceMock<MockGUI>>()};
    NiceMock<MockGUI> screen;
    std::shared_ptr<Label> label;
    std::unique_ptr<Label> map;
    std::unique_ptr<Label> journal;
    Tooltip tooltip {options};
    int draws {0};
};

TEST_F(TooltipTest, shows_once_the_pointer_rests_past_the_delay_below_and_right_of_it) {
    tooltip.setPointer({100, 100});
    moveOnto(*map);

    tooltip.update(0.9f);
    EXPECT_FALSE(shows());
    tooltip.update(0.2f);
    EXPECT_TRUE(shows());

    EXPECT_EQ(115, label->extent().left);
    EXPECT_EQ(115, label->extent().top);
}

TEST_F(TooltipTest, a_press_holds_tooltips_back_until_the_pointer_leaves_the_control) {
    tooltip.setPointer({100, 100});
    moveOnto(*map);
    tooltip.press();
    tooltip.update(2.0f);
    EXPECT_FALSE(shows());

    moveOnto(*journal);
    tooltip.update(1.1f);
    EXPECT_TRUE(shows());
}

TEST_F(TooltipTest, stays_on_the_screen_by_its_right_and_bottom_edges) {
    tooltip.setPointer({630, 470});
    moveOnto(*map);
    tooltip.update(1.1f);
    ASSERT_TRUE(shows());

    const auto &extent = label->extent();
    EXPECT_EQ(638, extent.left + extent.width);
    EXPECT_EQ(478, extent.top + extent.height);
}

} // namespace
