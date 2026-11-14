/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <gtest/gtest.h>

#include "../../fixtures/graphics.h"
#include "../../fixtures/gui.h"
#include "../../fixtures/resource.h"
#include "../../fixtures/scene.h"
#include "reone/graphics/texture.h"
#include "reone/gui/control/slider.h"

using namespace reone;
using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;
using namespace testing;

namespace {

class SliderTest : public Test {
protected:
    void SetUp() override {
        graphics.init();
        resources.init();
        sceneModule.init();
        thumb = std::make_shared<Texture>("thumb", TextureType::TwoDim, Texture::Properties {});
        thumb->setPixels(10, 10, PixelFormat::RGBA8, Texture::Layer {std::make_shared<ByteBuffer>(400, 0)});
        EXPECT_CALL(resources.textures(), get("thumb", TextureUsage::GUI)).WillOnce(Return(thumb));

        resource::generated::GUI_CONTROLS definition;
        definition.CONTROLTYPE = static_cast<int>(ControlType::Slider);
        definition.EXTENT.LEFT = 0;
        definition.EXTENT.TOP = 0;
        definition.EXTENT.WIDTH = 110;
        definition.EXTENT.HEIGHT = 10;
        definition.CURVALUE = 5;
        definition.MAXVALUE = 10;
        definition.THUMB.emplace();
        definition.THUMB->IMAGE = "thumb";
        slider = std::make_unique<Slider>(gui, sceneModule.graphs(), graphics.services(), resources.services());
        slider->load(definition, false);
        slider->setOnValueChanged([this](int value) { changes.push_back(value); });
    }

    NiceMock<MockGUI> gui;
    TestGraphicsModule graphics;
    TestResourceModule resources;
    scene::TestSceneModule sceneModule;
    std::shared_ptr<Texture> thumb;
    std::unique_ptr<Slider> slider;
    std::vector<int> changes;
};

TEST_F(SliderTest, clicks_and_arrow_keys_step_one_value_at_a_time) {
    EXPECT_TRUE(slider->handleClick(0, 5));
    EXPECT_EQ(4, slider->value());
    EXPECT_TRUE(slider->handleClick(109, 5));
    EXPECT_EQ(5, slider->value());

    slider->setSelected(true);
    EXPECT_TRUE(slider->handleKeyDown(input::KeyCode::Right));
    EXPECT_EQ(6, slider->value());
    EXPECT_TRUE(slider->handleKeyDown(input::KeyCode::Left));
    EXPECT_EQ(5, slider->value());
    EXPECT_EQ((std::vector<int> {4, 5, 6, 5}), changes);
}

TEST_F(SliderTest, only_the_thumb_starts_a_drag_and_dragging_covers_the_range) {
    EXPECT_TRUE(slider->handleMouseDown(55, 5));
    EXPECT_FALSE(slider->handleMouseDown(5, 5));

    slider->handleMouseDrag(110, 5);
    EXPECT_EQ(10, slider->value());
    slider->handleMouseDrag(-10, 5);
    EXPECT_EQ(0, slider->value());
    EXPECT_EQ((std::vector<int> {10, 0}), changes);
}

} // namespace
