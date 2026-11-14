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
    PulsingAlpha pulsingAlpha;
    GUI gui(options, engine.sceneModule().graphs(), engine.graphicsModule().services(), engine.resourceModule().services(), pulsingAlpha);
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

} // namespace
