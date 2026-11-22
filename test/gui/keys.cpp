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

std::shared_ptr<Gff> control(ControlType type, std::string tag, int top, int id) {
    return Gff::Builder()
        .field(Gff::Field::newInt("CONTROLTYPE", static_cast<int>(type)))
        .field(Gff::Field::newCExoString("TAG", std::move(tag)))
        .field(Gff::Field::newCExoString("Obj_Parent", "root"))
        .field(Gff::Field::newInt("ID", id))
        .field(Gff::Field::newStruct("EXTENT", extent(0, top, 100, 20)))
        .field(Gff::Field::newStruct("BORDER", Gff::Builder().build()))
        .build();
}

class KeyListener : public IGUIEventListener {
public:
    void onClick(const std::string &control) override { clicked.push_back(control); }
    void onSelectionChanged(const std::string &, bool) override {}
    void onEntered(const std::string &control) override { entered.push_back(control); }

    std::vector<std::string> clicked;
    std::vector<std::string> entered;
};

// A GUI with a button "a", a button "b" below it and a label "caption".
class GUIKeysTest : public testing::Test {
protected:
    void SetUp() override {
        engine.init();
        options.width = 200;
        options.height = 200;
        gui = std::make_unique<GUI>(options, engine.sceneModule().graphs(), engine.graphicsModule().services(), engine.resourceModule().services(), clock);
        gui->setResolution(200, 200);
        auto root = Gff::Builder()
                        .field(Gff::Field::newInt("CONTROLTYPE", static_cast<int>(ControlType::Panel)))
                        .field(Gff::Field::newCExoString("TAG", "root"))
                        .field(Gff::Field::newStruct("EXTENT", extent(0, 0, 200, 200)))
                        .field(Gff::Field::newStruct("BORDER", Gff::Builder().build()))
                        .field(Gff::Field::newList("CONTROLS", {
                                                                   control(ControlType::Button, "a", 0, 1),
                                                                   control(ControlType::Button, "b", 30, 2),
                                                                   control(ControlType::Label, "caption", 60, 3),
                                                               }))
                        .build();
        gui->load(*root);
        gui->setEventListener(listener);
    }

    bool press(input::KeyCode code, bool repeat = false) {
        return gui->handle(input::Event::newKeyDown(input::KeyEvent(true, code, 0, repeat)));
    }

    TestEngine engine;
    graphics::GraphicsOptions options;
    GUIClock clock;
    std::unique_ptr<GUI> gui;
    KeyListener listener;
};

TEST_F(GUIKeysTest, return_clicks_the_active_button_once_and_not_on_a_repeat) {
    auto a = gui->findControl("a");
    ASSERT_TRUE(a);
    gui->setActiveControl(*a);

    EXPECT_TRUE(press(input::KeyCode::Return));
    EXPECT_TRUE(press(input::KeyCode::Return, true));

    EXPECT_EQ((std::vector<std::string> {"a"}), listener.clicked);
}

TEST_F(GUIKeysTest, return_clicks_nothing_on_a_control_that_is_not_a_button) {
    auto caption = gui->findControl("caption");
    ASSERT_TRUE(caption);
    gui->setActiveControl(*caption);

    EXPECT_TRUE(press(input::KeyCode::Return));

    EXPECT_TRUE(listener.clicked.empty());
}

TEST_F(GUIKeysTest, an_arrow_moves_the_keyboard_along_its_link_and_enters_the_control) {
    auto a = gui->findControl("a");
    auto b = gui->findControl("b");
    ASSERT_TRUE(a && b);
    a->setMoveTo(Control::MoveDirection::Down, b->id());
    gui->setActiveControl(*a);

    EXPECT_TRUE(press(input::KeyCode::Down));

    EXPECT_EQ((std::vector<std::string> {"b"}), listener.entered);
    ASSERT_TRUE(gui->activeControl());
    EXPECT_EQ("b", gui->activeControl()->get().tag());
}

} // namespace
