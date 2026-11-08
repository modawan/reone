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

#include "../fixtures/engine.h"
#include "../fixtures/game.h"
#include "../fixtures/scene.h"

#include "reone/game/game.h"
#include "reone/game/gui/hud.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/gui/control/button.h"
#include "reone/gui/gui.h"
#include "reone/input/event.h"
#include "reone/resource/gff.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

HUD &reone::game::TestGameModule::installHUD(Game &game, std::shared_ptr<gui::IGUI> gui) {
    game._hud = std::make_unique<HUD>(game, game._services);
    game._hud->_gui = std::move(gui);
    return *game._hud;
}

void reone::game::TestGameModule::showHUDTargetMenu(HUD &hud, std::shared_ptr<Object> target) {
    auto &select = hud._select;
    select._selectedObject = std::move(target);
    select._selectedScreenCoords = glm::vec3(0.5f, 0.5f, 0.5f);
    select._actionSlots.clear();
    for (int i = 0; i < SelectionOverlay::kNumActionSlots; ++i) {
        ActionSlot slot;
        slot.actions.emplace_back(ActionType::AttackObject);
        select._actionSlots.push_back(std::move(slot));
    }
}

glm::ivec2 reone::game::TestGameModule::hudTargetMenuColumn(const HUD &hud, int column) {
    float x = 0.0f;
    float y = 0.0f;
    hud._select.getActionScreenCoords(column, x, y);
    return {static_cast<int>(std::ceil(x)), static_cast<int>(std::ceil(y))};
}

int reone::game::TestGameModule::hudTargetMenuHover(const HUD &hud) {
    return hud._select._selectedActionSlot;
}

namespace {

// modawan/reone#263: party members walk behind the leader, often low on the
// screen under the HUD, and the HUD's buttons sit over the world. Pointing at
// or clicking a HUD button must neither hilight nor click what lies behind it.
class HUDInputTest : public Test {
protected:
    void SetUp() override {
        _engine.init();
        EXPECT_CALL(_engine.sceneModule().graphs(), get(_)).Times(AnyNumber()).WillRepeatedly(ReturnRef(_sceneGraph));
        _game = std::make_unique<Game>(GameID::KotOR, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
        _area = _game->newArea();
        TestGameModule::setActiveModuleArea(*_game, _area);

        _leader = _game->newCreature();
        _game->party().addMember(kNpcPlayer, _leader);
        _game->party().setPlayer(_leader);
        _game->party().setActualPlayer(_leader);
        _companion = _game->newCreature();
        _area->add(_companion);

        auto &options = _engine.options().graphics;
        _gui = std::make_shared<gui::GUI>(
            options, _engine.services().scene.graphs, _engine.services().graphics, _engine.services().resource, _pulsingAlpha);
        _gui->setResolution(options.width, options.height);
        auto extent = Gff::Builder()
                          .field(Gff::Field::newInt("WIDTH", options.width))
                          .field(Gff::Field::newInt("HEIGHT", options.height))
                          .build();
        auto root = Gff::Builder()
                        .field(Gff::Field::newInt("CONTROLTYPE", static_cast<int>(gui::ControlType::Panel)))
                        .field(Gff::Field::newCExoString("TAG", "TGuiPanel"))
                        .field(Gff::Field::newStruct("EXTENT", std::move(extent)))
                        .field(Gff::Field::newList("CONTROLS", {}))
                        .build();
        _gui->load(*root);
        _button = std::make_shared<gui::Button>(
            *_gui, _engine.services().scene.graphs, _engine.services().graphics, _engine.services().resource);
        _button->setTag("BTN_EQU");
        placeButton({10, 10});
        _button->setOnClick([this]() { ++_clicks; });
        _gui->addControlToFront(_button, gui::IGUI::ControlCoordinates::Screen);

        _hud = &TestGameModule::installHUD(*_game, _gui);
        TestGameModule::setCurrentScreen(*_game, static_cast<int>(Game::Screen::InGame));
    }

    // The button spans 40 by 40 pixels from 20 above and left of the point.
    void placeButton(glm::ivec2 point) {
        _button->setExtent({point.x - 20, point.y - 20, 40, 40});
    }

    static input::Event motion(glm::ivec2 point) {
        return input::Event::newMouseMotion({point.x, point.y, 0.0f, 0.0f});
    }

    static input::Event press(glm::ivec2 point) {
        return input::Event::newMouseButtonDown({input::MouseButton::Left, true, 1, point.x, point.y});
    }

    static input::Event release(glm::ivec2 point) {
        return input::Event::newMouseButtonUp({input::MouseButton::Left, false, 1, point.x, point.y});
    }

    TestEngine _engine;
    StubConsole _console;
    NiceMock<scene::MockSceneGraph> _sceneGraph;
    std::unique_ptr<Game> _game;
    std::shared_ptr<Area> _area;
    std::shared_ptr<Creature> _leader;
    std::shared_ptr<Creature> _companion;
    gui::PulsingAlpha _pulsingAlpha;
    std::shared_ptr<gui::GUI> _gui;
    std::shared_ptr<gui::Button> _button;
    HUD *_hud {nullptr};
    int _clicks {0};
};

} // namespace

TEST_F(HUDInputTest, a_press_on_a_hud_control_does_not_reach_the_world) {
    const glm::ivec2 onButton {10, 10};
    EXPECT_CALL(_sceneGraph, pickModelAt(_, _, _)).Times(0);

    EXPECT_TRUE(_game->handle(press(onButton)));
    _game->handle(release(onButton));
    EXPECT_EQ(1, _clicks);
    Mock::VerifyAndClearExpectations(&_sceneGraph);

    // Away from the HUD the world still takes the press.
    const glm::ivec2 offButton {200, 200};
    EXPECT_CALL(_sceneGraph, pickModelAt(offButton.x, offButton.y, _leader.get())).WillOnce(Return(nullptr));
    _game->handle(press(offButton));
}

TEST_F(HUDInputTest, pointing_at_a_hud_control_hilights_nothing_in_the_world) {
    const glm::ivec2 onButton {10, 10};
    _area->hilightObject(_companion);
    EXPECT_CALL(_sceneGraph, pickModelAt(_, _, _)).Times(0);

    _game->handle(motion(onButton));

    EXPECT_TRUE(_button->isSelected());
    EXPECT_EQ(nullptr, _area->hilightedObject());
    EXPECT_TRUE(_game->isPointerOnHUD(onButton.x, onButton.y));
    Mock::VerifyAndClearExpectations(&_sceneGraph);

    const glm::ivec2 offButton {200, 200};
    EXPECT_CALL(_sceneGraph, pickModelAt(offButton.x, offButton.y, _leader.get())).WillOnce(Return(nullptr));
    _game->handle(motion(offButton));
    EXPECT_FALSE(_button->isSelected());
    EXPECT_FALSE(_game->isPointerOnHUD(offButton.x, offButton.y));
}

TEST_F(HUDInputTest, a_hud_control_under_the_target_menu_takes_the_pointer_and_the_click) {
    TestGameModule::showHUDTargetMenu(*_hud, _companion);
    // Two pixels into the middle column's top arrow, which would otherwise
    // cycle the column's entry.
    const glm::ivec2 column = TestGameModule::hudTargetMenuColumn(*_hud, 1);
    const glm::ivec2 point = column + glm::ivec2(2, 2);
    EXPECT_CALL(_sceneGraph, pickModelAt(_, _, _)).Times(0);

    // Clear of the HUD's controls the menu still takes the pointer.
    _game->handle(motion(point));
    EXPECT_EQ(1, TestGameModule::hudTargetMenuHover(*_hud));
    EXPECT_FALSE(_button->isSelected());

    placeButton(point);
    _game->handle(motion(point));
    EXPECT_TRUE(_button->isSelected());
    EXPECT_EQ(-1, TestGameModule::hudTargetMenuHover(*_hud));

    EXPECT_TRUE(_game->handle(press(point)));
    _game->handle(release(point));
    EXPECT_EQ(1, _clicks);
}
