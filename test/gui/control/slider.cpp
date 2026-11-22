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
#include "reone/gui/control/track.h"
#include "reone/input/event.h"

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

TEST_F(SliderTest, track_presses_and_arrow_keys_step_one_value_at_a_time) {
    EXPECT_TRUE(slider->handleMouseDown(1, 5));
    slider->handleMouseUp(1, 5, true, 1);
    EXPECT_EQ(4, slider->value());
    EXPECT_TRUE(slider->handleMouseDown(108, 5));
    slider->handleMouseUp(108, 5, true, 1);
    EXPECT_EQ(5, slider->value());

    slider->setSelected(true);
    EXPECT_TRUE(slider->handleKeyDown(input::KeyCode::Right));
    EXPECT_EQ(6, slider->value());
    EXPECT_TRUE(slider->handleKeyDown(input::KeyCode::Left));
    EXPECT_EQ(5, slider->value());
    EXPECT_EQ((std::vector<int> {4, 5, 6, 5}), changes);
}

TEST_F(SliderTest, the_thumb_drags_across_the_range_and_the_release_reports_it) {
    EXPECT_TRUE(slider->handleMouseDown(55, 5));
    slider->handleMouseDrag(110, 5);
    EXPECT_EQ(10, slider->value());
    slider->handleMouseDrag(-10, 5);
    EXPECT_EQ(0, slider->value());
    EXPECT_TRUE(changes.empty());

    slider->handleMouseUp(-10, 5, false, 1);
    EXPECT_EQ((std::vector<int> {0}), changes);
}

TEST_F(SliderTest, steps_announce_only_a_value_that_moves) {
    int steps = 0;
    slider->setOnStep([&steps]() { ++steps; });
    slider->step(1);
    EXPECT_EQ(6, slider->value());
    slider->setValue(10);
    slider->step(1);
    EXPECT_EQ(10, slider->value());

    EXPECT_EQ(1, steps);
    EXPECT_EQ((std::vector<int> {6}), changes);
}

TEST(TrackDrag, a_dragged_thumb_keeps_its_grip_and_its_range) {
    // Pressed at 50 with the value 5 of 10, over a travel of 100.
    EXPECT_EQ(6, draggedValue(5, 50, 60, 100, 10));
    EXPECT_EQ(4, draggedValue(5, 50, 41, 100, 10));
    EXPECT_EQ(10, draggedValue(5, 50, 1000, 100, 10));
    EXPECT_EQ(0, draggedValue(5, 50, -1000, 100, 10));
    // A thumb with no travel keeps the value it was pressed at.
    EXPECT_EQ(5, draggedValue(5, 50, 90, 0, 10));
}

TEST(WheelSteps, fractions_of_a_turn_gather_into_whole_steps) {
    input::WheelSteps wheel;
    auto turn = [&wheel](float y) {
        return wheel.take(input::MouseWheelEvent(0.0f, y, input::MouseWheelDirection::Normal)).y;
    };
    EXPECT_EQ(0, turn(0.4f));
    EXPECT_EQ(0, turn(0.4f));
    EXPECT_EQ(1, turn(0.4f));
    EXPECT_EQ(-1, turn(-1.5f));
    EXPECT_EQ(0, turn(0.3f));
    EXPECT_EQ(2, turn(2.0f));
}

} // namespace
