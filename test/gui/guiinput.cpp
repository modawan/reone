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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/graphics.h"
#include "../fixtures/resource.h"
#include "../fixtures/scene.h"
#include "reone/graphics/options.h"
#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"
#include "reone/gui/gui.h"
#include "reone/input/event.h"
#include "reone/resource/gff.h"

using namespace reone;
using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;
using namespace reone::scene;
using namespace testing;

namespace {

// Modelled on modawan/reone#263: a press on a GUI control must not also fall
// through to whatever is behind the GUI.
class GUIInputTest : public Test {
protected:
    void SetUp() override {
        _graphicsModule.init();
        _resourceModule.init();
        _sceneModule.init();
        _options.width = 800;
        _options.height = 600;

        _gui = std::make_unique<GUI>(
            _options, _sceneModule.graphs(), _graphicsModule.services(), _resourceModule.services(), _pulsingAlpha);
        _gui->setResolution(800, 600);
        auto extent = Gff::Builder()
                          .field(Gff::Field::newInt("WIDTH", 800))
                          .field(Gff::Field::newInt("HEIGHT", 600))
                          .build();
        auto root = Gff::Builder()
                        .field(Gff::Field::newInt("CONTROLTYPE", static_cast<int>(ControlType::Panel)))
                        .field(Gff::Field::newCExoString("TAG", "ROOT"))
                        .field(Gff::Field::newStruct("EXTENT", std::move(extent)))
                        .field(Gff::Field::newList("CONTROLS", {}))
                        .build();
        _gui->load(*root);

        _button = std::make_shared<Button>(
            *_gui, _sceneModule.graphs(), _graphicsModule.services(), _resourceModule.services());
        _button->setTag("BTN_TEST");
        _button->setExtent({300, 500, 100, 50});
        _button->setOnClick([this]() { ++_clicks; });
        _gui->addControlToFront(_button, IGUI::ControlCoordinates::Screen);

        _label = std::make_shared<Label>(
            *_gui, _sceneModule.graphs(), _graphicsModule.services(), _resourceModule.services());
        _label->setTag("LBL_TEST");
        _label->setExtent({500, 500, 100, 50});
        _gui->addControlToFront(_label, IGUI::ControlCoordinates::Screen);
    }

    static input::Event press(int x, int y, input::MouseButton button = input::MouseButton::Left) {
        return input::Event::newMouseButtonDown({button, true, 1, x, y});
    }

    static input::Event release(int x, int y) {
        return input::Event::newMouseButtonUp({input::MouseButton::Left, false, 1, x, y});
    }

    TestGraphicsModule _graphicsModule;
    TestResourceModule _resourceModule;
    TestSceneModule _sceneModule;
    GraphicsOptions _options;
    PulsingAlpha _pulsingAlpha;
    std::unique_ptr<GUI> _gui;
    std::shared_ptr<Button> _button;
    std::shared_ptr<Label> _label;
    int _clicks {0};
};

} // namespace

TEST_F(GUIInputTest, a_left_press_is_consumed_only_on_a_selectable_control) {
    EXPECT_TRUE(_gui->handle(press(350, 525)));

    EXPECT_FALSE(_gui->handle(press(100, 100)));
    // A label takes no clicks, so the press is not the GUI's.
    EXPECT_FALSE(_gui->handle(press(550, 525)));
    // Only the left button clicks controls.
    EXPECT_FALSE(_gui->handle(press(350, 525, input::MouseButton::Right)));

    _button->setVisible(false);
    EXPECT_FALSE(_gui->handle(press(350, 525)));
    _button->setVisible(true);
    _button->setDisabled(true);
    EXPECT_FALSE(_gui->handle(press(350, 525)));
}

TEST_F(GUIInputTest, a_consumed_press_still_clicks_its_control_on_release) {
    _gui->handle(input::Event::newMouseMotion({350, 525, 0.0f, 0.0f}));
    EXPECT_TRUE(_button->isSelected());

    ASSERT_TRUE(_gui->handle(press(350, 525)));
    EXPECT_EQ(0, _clicks);
    _gui->handle(release(350, 525));
    EXPECT_EQ(1, _clicks);
}

TEST_F(GUIInputTest, reports_whether_a_selectable_control_lies_under_a_point) {
    EXPECT_TRUE(_gui->hasSelectableControlAt(300, 500));
    EXPECT_TRUE(_gui->hasSelectableControlAt(400, 550));
    EXPECT_FALSE(_gui->hasSelectableControlAt(401, 525));
    EXPECT_FALSE(_gui->hasSelectableControlAt(550, 525));
    EXPECT_FALSE(_gui->hasSelectableControlAt(100, 100));
}
