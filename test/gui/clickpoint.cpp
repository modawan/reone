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

#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "reone/gui/control/button.h"
#include "reone/gui/control/listbox.h"
#include "reone/gui/gui.h"
#include "reone/resource/gff.h"

using namespace reone;
using namespace reone::game;
using namespace reone::gui;
using namespace reone::resource;
using namespace testing;

namespace {

std::shared_ptr<Gff> extent(int left, int top, int width, int height) {
    return Gff::Builder()
        .field(Gff::Field::newInt("LEFT", left))
        .field(Gff::Field::newInt("TOP", top))
        .field(Gff::Field::newInt("WIDTH", width))
        .field(Gff::Field::newInt("HEIGHT", height))
        .build();
}

std::shared_ptr<Gff> control(ControlType type, const std::string &tag, const std::string &parent, std::shared_ptr<Gff> ext) {
    return Gff::Builder()
        .field(Gff::Field::newInt("CONTROLTYPE", static_cast<int>(type)))
        .field(Gff::Field::newCExoString("TAG", tag))
        .field(Gff::Field::newCExoString("Obj_Parent", parent))
        .field(Gff::Field::newStruct("EXTENT", std::move(ext)))
        .build();
}

/**
 * An authored 640x480 layout in a 1024x768 window: a button on the root, and
 * a second button inside a panel that is itself a child of the root.
 */
struct ClickLayout {
    graphics::GraphicsOptions options;
    PulsingAlpha pulsing;
    std::unique_ptr<GUI> gui;

    ClickLayout(TestEngine &engine, GUI::ScalingMode scaling) {
        options.width = 1024;
        options.height = 768;
        gui = std::make_unique<GUI>(options, engine.services().scene.graphs, engine.services().graphics, engine.services().resource, pulsing);
        gui->setResolution(640, 480);
        gui->setScaling(scaling);
        auto root = Gff::Builder()
                        .field(Gff::Field::newInt("CONTROLTYPE", static_cast<int>(ControlType::Panel)))
                        .field(Gff::Field::newCExoString("TAG", "ROOT"))
                        .field(Gff::Field::newStruct("EXTENT", extent(0, 0, 640, 480)))
                        .field(Gff::Field::newList("CONTROLS", {
                            control(ControlType::Button, "BTN_PLAY", "ROOT", extent(100, 200, 80, 20)),
                            control(ControlType::Panel, "PNL_INNER", "ROOT", extent(300, 100, 200, 200)),
                            control(ControlType::Button, "BTN_INNER", "PNL_INNER", extent(350, 150, 60, 30)),
                        }))
                        .build();
        gui->load(*root);
    }

    /** Moves the pointer to \p point and clicks; true if \p tag's handler ran. */
    bool clickReaches(const std::string &tag, glm::ivec2 point) {
        auto target = gui->findControl(tag);
        bool clicked = false;
        target->setOnClick([&clicked]() { clicked = true; });
        gui->handle(input::Event::newMouseMotion({point.x, point.y, 0.0f, 0.0f}));
        gui->handle(input::Event::newMouseButtonDown({input::MouseButton::Left, true, 1, point.x, point.y}));
        gui->handle(input::Event::newMouseButtonUp({input::MouseButton::Left, false, 1, point.x, point.y}));
        return clicked;
    }
};

class GUIClickPointByScaling : public TestWithParam<GUI::ScalingMode> {};

} // namespace

TEST_P(GUIClickPointByScaling, a_click_at_the_point_reaches_the_control) {
    ClickLayout layout(testEngine(), GetParam());
    for (const char *tag : {"BTN_PLAY", "BTN_INNER"}) {
        auto point = layout.gui->clickPoint(tag, -1);
        ASSERT_TRUE(point.has_value()) << tag;
        EXPECT_TRUE(layout.clickReaches(tag, *point)) << tag;
    }
}

INSTANTIATE_TEST_SUITE_P(
    AllModes,
    GUIClickPointByScaling,
    Values(GUI::ScalingMode::Center,
           GUI::ScalingMode::CenterHorizontal,
           GUI::ScalingMode::PositionRelativeToCenter,
           GUI::ScalingMode::ScaledRelativeToCenter,
           GUI::ScalingMode::ScaledTopCenter,
           GUI::ScalingMode::Scaled));

TEST(GUIClickPoint, should_add_the_centring_offset_to_the_authored_centre) {
    ClickLayout layout(testEngine(), GUI::ScalingMode::Center);

    // Authored centre (140, 210), moved by the centring offset (192, 144).
    EXPECT_EQ(layout.gui->clickPoint("BTN_PLAY", -1), glm::ivec2(332, 354));
}

TEST(GUIClickPoint, should_not_offer_a_tag_the_layout_lacks) {
    ClickLayout layout(testEngine(), GUI::ScalingMode::Scaled);

    EXPECT_FALSE(layout.gui->clickPoint("BTN_MISSING", -1).has_value());
}

TEST(GUIClickPoint, should_not_offer_a_hidden_control) {
    ClickLayout layout(testEngine(), GUI::ScalingMode::Scaled);
    layout.gui->findControl("BTN_PLAY")->setVisible(false);

    EXPECT_FALSE(layout.gui->clickPoint("BTN_PLAY", -1).has_value());
}

TEST(GUIClickPoint, should_not_offer_a_disabled_control) {
    ClickLayout layout(testEngine(), GUI::ScalingMode::Scaled);
    layout.gui->findControl("BTN_PLAY")->setDisabled(true);

    EXPECT_FALSE(layout.gui->clickPoint("BTN_PLAY", -1).has_value());
}

namespace {

resource::generated::GUI_EXTENT extentOf(int left, int top, int width, int height) {
    resource::generated::GUI_EXTENT extent;
    extent.LEFT = left;
    extent.TOP = top;
    extent.WIDTH = width;
    extent.HEIGHT = height;
    return extent;
}

/** A five-row list, rows "a" to "e", 40 high, in the layout's front. */
std::shared_ptr<ListBox> addList(TestEngine &engine, GUI &gui) {
    resource::generated::GUI_CONTROLS definition;
    definition.CONTROLTYPE = static_cast<int>(ControlType::ListBox);
    definition.EXTENT = extentOf(600, 400, 300, 200);
    resource::generated::GUI_CONTROLS_PROTOITEM proto;
    proto.CONTROLTYPE = static_cast<int>(ControlType::Button);
    proto.EXTENT = extentOf(600, 400, 300, 40);
    definition.PROTOITEM = std::move(proto);
    auto list = std::make_shared<ListBox>(gui, engine.services().scene.graphs, engine.services().graphics, engine.services().resource);
    list->load(definition, false);
    list->setTag("LB_ITEMS");
    for (const char *tag : {"a", "b", "c", "d", "e"}) {
        ListBox::Item item;
        item.tag = tag;
        list->addItem(std::move(item));
    }
    gui.addControlToFront(list, IGUI::ControlCoordinates::Screen);
    return list;
}

} // namespace

TEST(GUIClickPoint, a_click_at_a_row_point_reaches_that_row_of_a_list) {
    TestEngine &engine = testEngine();
    ClickLayout layout(engine, GUI::ScalingMode::Center);
    auto list = addList(engine, *layout.gui);
    std::string clicked;
    list->setOnItemClick([&clicked](const std::string &tag) { clicked = tag; });

    for (auto [row, tag] : {std::pair {0, "a"}, std::pair {1, "b"}, std::pair {4, "e"}}) {
        auto point = layout.gui->clickPoint("LB_ITEMS", row);
        ASSERT_TRUE(point.has_value()) << row;
        layout.gui->handle(input::Event::newMouseMotion({point->x, point->y, 0.0f, 0.0f}));
        layout.gui->handle(input::Event::newMouseButtonDown({input::MouseButton::Left, true, 1, point->x, point->y}));
        layout.gui->handle(input::Event::newMouseButtonUp({input::MouseButton::Left, false, 1, point->x, point->y}));
        EXPECT_EQ(clicked, tag) << row;
    }
}

TEST(GUIClickPoint, should_not_offer_a_row_a_list_does_not_show) {
    TestEngine &engine = testEngine();
    ClickLayout layout(engine, GUI::ScalingMode::Center);
    addList(engine, *layout.gui);

    EXPECT_FALSE(layout.gui->clickPoint("LB_ITEMS", 5).has_value());
    // A row of something that is not a list.
    EXPECT_FALSE(layout.gui->clickPoint("BTN_PLAY", 0).has_value());
}

TEST(GUIClickPoint, should_not_offer_a_control_another_one_covers) {
    TestEngine &engine = testEngine();
    ClickLayout layout(engine, GUI::ScalingMode::Center);
    auto before = layout.gui->clickPoint("BTN_PLAY", -1);
    ASSERT_TRUE(before.has_value());
    auto cover = std::make_shared<Button>(*layout.gui, engine.services().scene.graphs, engine.services().graphics, engine.services().resource);
    cover->setTag("BTN_COVER");
    cover->setExtent(layout.gui->findControl("BTN_PLAY")->extent());
    layout.gui->addControlToFront(cover, IGUI::ControlCoordinates::Screen);

    // Whichever of the two the hit test picks, the other cannot be clicked.
    bool playClickable = layout.gui->clickPoint("BTN_PLAY", -1).has_value();
    bool coverClickable = layout.gui->clickPoint("BTN_COVER", -1).has_value();
    EXPECT_NE(playClickable, coverClickable);
}
