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
#include "reone/game/d20/class.h"
#include "reone/game/d20/classes.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/party.h"
#include "reone/game/script/routines.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;

using testing::NiceMock;

void reone::game::TestGameModule::setLevelUpGuiLoads(Game &game, bool loads, int *loadCount) {
    game._levelUpGuiLoadOverride = [loads, loadCount]() {
        if (loadCount) {
            ++*loadCount;
        }
        return loads;
    };
}

namespace {

class ShowLevelUpGUI : public testing::Test {
protected:
    void SetUp() override {
        _engine.init();
        _game = std::make_unique<Game>(GameID::KotOR, "", _engine.options(), _engine.services(), _console);
        _routines = std::make_unique<Routines>(GameID::KotOR, _game.get(), &_engine.services());
        _routines->init();
        _classes = std::make_unique<Classes>(_classStrings, _classTwoDas);
        _soldier = std::make_shared<CreatureClass>(ClassType::Soldier, *_classes, _classStrings, _classTwoDas);
        _guardian = std::make_shared<CreatureClass>(ClassType::JediGuardian, *_classes, _classStrings, _classTwoDas);
        TestGameModule::setLevelUpGuiLoads(*_game, true);
        TestGameModule::setActiveModule(*_game, true);
    }

    std::shared_ptr<Creature> makeCreature(int soldierLevels, int xp) {
        auto creature = _game->newCreature();
        creature->attributes().addClassLevels(_soldier.get(), soldierLevels);
        creature->setMaxHitPoints(20);
        creature->setCurrentHitPoints(20);
        creature->setXP(xp);
        return creature;
    }

    std::shared_ptr<Creature> makeLeader(int soldierLevels, int xp) {
        auto leader = makeCreature(soldierLevels, xp);
        _game->party().addMember(kNpcPlayer, leader);
        _game->party().setPlayer(leader);
        return leader;
    }

    int showLevelUpGUI() {
        Routine &routine = _routines->get(_routines->getIndexByName("ShowLevelUpGUI"));
        ExecutionContext execution;
        execution.routines = _routines.get();
        return routine.invoke({}, execution).intValue;
    }

    TestEngine _engine;
    StubConsole _console;
    std::unique_ptr<Game> _game;
    std::unique_ptr<Routines> _routines;
    NiceMock<MockStrings> _classStrings;
    NiceMock<MockTwoDAs> _classTwoDas;
    std::unique_ptr<Classes> _classes;
    std::shared_ptr<CreatureClass> _soldier;
    std::shared_ptr<CreatureClass> _guardian;
};

} // namespace

// 265: the screen comes up only for a player with the experience to level up.
TEST_F(ShowLevelUpGUI, screen_comes_up_at_the_next_level_experience) {
    auto leader = makeLeader(1, 999);

    EXPECT_EQ(0, showLevelUpGUI());
    EXPECT_EQ(Game::Screen::None, _game->currentScreen());
    leader->setXP(1000);
    EXPECT_EQ(1, showLevelUpGUI());
    EXPECT_EQ(Game::Screen::CharacterGeneration, _game->currentScreen());
}

TEST_F(ShowLevelUpGUI, leader_at_the_level_cap_gets_no_screen) {
    auto leader = makeLeader(19, 1000000);
    EXPECT_EQ(1, showLevelUpGUI());

    leader->attributes().addClassLevels(_soldier.get(), 1);
    EXPECT_EQ(0, showLevelUpGUI());
}

TEST_F(ShowLevelUpGUI, dead_leader_gets_no_screen) {
    auto leader = makeLeader(1, 1000);
    leader->setCurrentHitPoints(0);
    EXPECT_EQ(0, showLevelUpGUI());
    EXPECT_EQ(Game::Screen::None, _game->currentScreen());

    leader->setCurrentHitPoints(20);
    EXPECT_EQ(1, showLevelUpGUI());
}

TEST_F(ShowLevelUpGUI, no_leader_or_no_module_gets_no_screen) {
    EXPECT_EQ(0, showLevelUpGUI());

    makeLeader(1, 1000);
    TestGameModule::setActiveModule(*_game, false);
    EXPECT_EQ(0, showLevelUpGUI());
    EXPECT_EQ(Game::Screen::None, _game->currentScreen());

    TestGameModule::setActiveModule(*_game, true);
    EXPECT_EQ(1, showLevelUpGUI());
}

TEST_F(ShowLevelUpGUI, screen_that_fails_to_load_is_not_up) {
    makeLeader(1, 1000);
    TestGameModule::setLevelUpGuiLoads(*_game, false);
    EXPECT_EQ(0, showLevelUpGUI());
    EXPECT_EQ(Game::Screen::None, _game->currentScreen());

    TestGameModule::setLevelUpGuiLoads(*_game, true);
    EXPECT_EQ(1, showLevelUpGUI());
}

// The Dantooine council adds a Jedi class at level 0 before showing the
// screen; the level the screen takes is that class's.
TEST_F(ShowLevelUpGUI, level_after_a_new_jedi_class_goes_to_that_class) {
    auto leader = makeLeader(1, 1000);
    leader->attributes().addClassLevels(_guardian.get(), 0);

    EXPECT_EQ(1, showLevelUpGUI());
    EXPECT_EQ(ClassType::JediGuardian, leader->attributes().getEffectiveClass());
}

// Called again while the screen is up, the routine reports it up and leaves
// the level the player is taking alone.
TEST_F(ShowLevelUpGUI, screen_already_up_is_not_opened_again) {
    int loads = 0;
    TestGameModule::setLevelUpGuiLoads(*_game, true, &loads);
    makeLeader(1, 1000);

    EXPECT_EQ(1, showLevelUpGUI());
    EXPECT_EQ(1, showLevelUpGUI());
    EXPECT_EQ(1, loads);
}

// The screen levels the party leader, so it is the leader's experience that
// counts, not the player character's.
TEST_F(ShowLevelUpGUI, leader_who_is_not_the_player_is_the_one_levelled) {
    constexpr int kNpcCompanion = 4;
    auto pc = makeCreature(1, 0);
    auto companion = makeCreature(1, 1000);
    _game->party().addAvailableMember(kNpcCompanion, companion);
    _game->party().addMember(kNpcCompanion, companion);
    _game->party().addMember(kNpcPlayer, pc);
    _game->party().setPlayer(pc);
    ASSERT_EQ(companion, _game->party().getLeader());

    EXPECT_EQ(1, showLevelUpGUI());

    TestGameModule::setCurrentScreen(*_game, static_cast<int>(Game::Screen::InGame));
    companion->setXP(0);
    pc->setXP(1000);
    EXPECT_EQ(0, showLevelUpGUI());
}
