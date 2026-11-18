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
    _game->module()->updateHover();

    EXPECT_TRUE(_button->isSelected());
    EXPECT_EQ(nullptr, _area->hilightedObject());
    EXPECT_TRUE(_game->isPointerOnHUD(onButton.x, onButton.y));
    Mock::VerifyAndClearExpectations(&_sceneGraph);

    const glm::ivec2 offButton {200, 200};
    EXPECT_CALL(_sceneGraph, pickModelAt(offButton.x, offButton.y, _leader.get())).WillOnce(Return(nullptr));
    _game->handle(motion(offButton));
    _game->module()->updateHover();
    EXPECT_FALSE(_button->isSelected());
    EXPECT_FALSE(_game->isPointerOnHUD(offButton.x, offButton.y));
}

// Picking tests room geometry, and a high-polling mouse sends many moves a
// frame: the moves only note where the pointer went, and the frame's hover
// update picks once, where the last of them left it.
TEST_F(HUDInputTest, several_moves_in_a_frame_pick_once_where_the_last_one_ends) {
    EXPECT_CALL(_sceneGraph, pickModelAt(_, _, _)).Times(0);
    for (int x : {200, 210, 220}) {
        _game->handle(motion({x, 200}));
    }
    Mock::VerifyAndClearExpectations(&_sceneGraph);

    EXPECT_CALL(_sceneGraph, pickModelAt(220, 200, _leader.get())).WillOnce(Return(nullptr));
    _game->module()->updateHover();
    Mock::VerifyAndClearExpectations(&_sceneGraph);

    // Nothing moved since: the next frame picks nothing.
    EXPECT_CALL(_sceneGraph, pickModelAt(_, _, _)).Times(0);
    _game->module()->updateHover();
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

TEST_F(HUDInputTest, a_click_in_the_world_does_nothing_while_a_conversation_is_under_way) {
    _game->setDialogUnderWay(true);
    EXPECT_CALL(_sceneGraph, pickModelAt(_, _, _)).Times(0);

    _game->handle(press({200, 200}));
}

TEST_F(HUDInputTest, nothing_in_the_world_hilights_while_a_conversation_is_under_way) {
    _area->hilightObject(_companion);
    _game->handle(motion({200, 200}));
    _game->setDialogUnderWay(true);
    EXPECT_CALL(_sceneGraph, pickModelAt(_, _, _)).Times(0);

    _game->module()->updateHover();

    EXPECT_EQ(nullptr, _area->hilightedObject());
    Mock::VerifyAndClearExpectations(&_sceneGraph);

    // Once it ends, the pointer picks where it stands without another move.
    _game->setDialogUnderWay(false);
    EXPECT_CALL(_sceneGraph, pickModelAt(200, 200, _leader.get())).WillOnce(Return(nullptr));
    _game->module()->updateHover();
}

TEST_F(HUDInputTest, the_pause_control_only_unpauses_while_a_conversation_is_under_way) {
    _game->setDialogUnderWay(true);
    EXPECT_FALSE(_game->togglePlayerPause());
    EXPECT_FALSE(_game->isPausedAtFrameEnd());

    _game->setDialogUnderWay(false);
    EXPECT_TRUE(_game->togglePlayerPause());
    _game->setDialogUnderWay(true);
    EXPECT_FALSE(_game->togglePlayerPause());
    EXPECT_FALSE(_game->isPausedAtFrameEnd());
}

TEST_F(HUDInputTest, no_autopause_while_a_conversation_is_under_way) {
    _game->setDialogUnderWay(true);
    _game->requestAutoPause(AutoPauseReason::EnemySighted);
    EXPECT_FALSE(_game->isPausedAtFrameEnd());

    _game->setDialogUnderWay(false);
    _game->requestAutoPause(AutoPauseReason::EnemySighted);
    EXPECT_TRUE(_game->isPausedAtFrameEnd());
}
