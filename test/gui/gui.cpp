/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "reone/gui/gui.h"
#include "reone/gui/tooltip.h"
#include "reone/resource/gff.h"

using namespace reone;
using namespace reone::gui;
using namespace reone::resource;

namespace {

std::shared_ptr<Gff> extent(int left, int top, int width, int height) {
    return Gff::Builder()
        .field(Gff::Field::newInt("LEFT", left))
        .field(Gff::Field::newInt("TOP", top))
        .field(Gff::Field::newInt("WIDTH", width))
        .field(Gff::Field::newInt("HEIGHT", height))
        .build();
}

std::shared_ptr<Gff> button(std::string tag, std::string parent, int left, int top, int width, int height, int id) {
    return Gff::Builder()
        .field(Gff::Field::newInt("CONTROLTYPE", static_cast<int>(ControlType::Button)))
        .field(Gff::Field::newCExoString("TAG", std::move(tag)))
        .field(Gff::Field::newCExoString("Obj_Parent", std::move(parent)))
        .field(Gff::Field::newInt("ID", id))
        .field(Gff::Field::newStruct("EXTENT", extent(left, top, width, height)))
        .field(Gff::Field::newStruct("BORDER", Gff::Builder().build()))
        .build();
}

class ClickListener : public IGUIEventListener {
public:
    void onClick(const std::string &control) override { clicked = control; }
    void onSelectionChanged(const std::string &, bool) override {}
    void onEntered(const std::string &) override {}

    std::string clicked;
};

TEST(GUI, a_child_drawn_over_its_parent_takes_the_pointer) {
    TestEngine engine;
    engine.init();
    graphics::GraphicsOptions options;
    options.width = 200;
    options.height = 200;
    GUIClock clock;
    Tooltip tooltip(options);
    GUI gui(options, engine.sceneModule().graphs(), engine.graphicsModule().services(), engine.resourceModule().services(), clock, tooltip);
    gui.setResolution(200, 200);
    auto root = Gff::Builder()
                    .field(Gff::Field::newInt("CONTROLTYPE", static_cast<int>(ControlType::Panel)))
                    .field(Gff::Field::newCExoString("TAG", "root"))
                    .field(Gff::Field::newStruct("EXTENT", extent(0, 0, 200, 200)))
                    .field(Gff::Field::newStruct("BORDER", Gff::Builder().build()))
                    .field(Gff::Field::newList("CONTROLS", {
                                                               button("row", "root", 0, 0, 100, 100, 1),
                                                               button("arrow", "row", 10, 10, 20, 20, 2),
                                                           }))
                    .build();
    gui.load(*root);
    ClickListener listener;
    gui.setEventListener(listener);

    gui.handle(input::Event::newMouseButtonDown({input::MouseButton::Left, true, 1, 15, 15}));
    gui.handle(input::Event::newMouseButtonUp({input::MouseButton::Left, false, 1, 15, 15}));

    EXPECT_EQ("arrow", listener.clicked);
}

TEST(GUIClock, the_hilight_pulse_falls_to_its_low_and_rises_back) {
    GUIClock clock;
    EXPECT_FLOAT_EQ(1.0f, clock.value());
    clock.update(0.375f);
    EXPECT_NEAR(0.7f, clock.value(), 1e-4f);
    clock.update(0.375f);
    EXPECT_NEAR(0.4f, clock.value(), 1e-4f);
    clock.update(0.75f);
    EXPECT_NEAR(1.0f, clock.value(), 1e-4f);
}

TEST(GUIClock, one_long_frame_lands_where_many_short_ones_do) {
    GUIClock longFrame;
    longFrame.update(2.0f);
    GUIClock shortFrames;
    for (int i = 0; i < 200; ++i) {
        shortFrames.update(0.01f);
    }
    EXPECT_NEAR(longFrame.value(), shortFrames.value(), 1e-3f);
    EXPECT_NEAR(longFrame.textureTime(), shortFrames.textureTime(), 1e-4);
}

TEST(GUI, the_arrow_keys_pass_over_a_disabled_control_but_stop_on_a_hidden_one) {
    TestEngine engine;
    engine.init();
    graphics::GraphicsOptions options;
    options.width = 200;
    options.height = 200;
    GUIClock clock;
    Tooltip tooltip(options);
    GUI gui(options, engine.sceneModule().graphs(), engine.graphicsModule().services(), engine.resourceModule().services(), clock, tooltip);
    gui.setResolution(200, 200);
    auto root = Gff::Builder()
                    .field(Gff::Field::newInt("CONTROLTYPE", static_cast<int>(ControlType::Panel)))
                    .field(Gff::Field::newCExoString("TAG", "root"))
                    .field(Gff::Field::newStruct("EXTENT", extent(0, 0, 200, 200)))
                    .field(Gff::Field::newStruct("BORDER", Gff::Builder().build()))
                    .field(Gff::Field::newList("CONTROLS", {
                                                               button("a", "root", 0, 0, 100, 20, 1),
                                                               button("b", "root", 0, 30, 100, 20, 2),
                                                               button("c", "root", 0, 60, 100, 20, 3),
                                                           }))
                    .build();
    gui.load(*root);
    auto a = gui.findControl("a");
    auto b = gui.findControl("b");
    auto c = gui.findControl("c");
    ASSERT_TRUE(a && b && c);
    a->setMoveTo(Control::MoveDirection::Down, 2);
    b->setMoveTo(Control::MoveDirection::Down, 3);

    b->setDisabled(true);
    auto target = gui.moveTarget(*a, Control::MoveDirection::Down);
    ASSERT_TRUE(target);
    EXPECT_EQ("c", target->get().tag());

    b->setDisabled(false);
    b->setVisible(false);
    target = gui.moveTarget(*a, Control::MoveDirection::Down);
    ASSERT_TRUE(target);
    EXPECT_EQ("b", target->get().tag());
}

} // namespace
