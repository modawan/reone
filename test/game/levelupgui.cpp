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
#include "reone/game/d20/class.h"
#include "reone/game/d20/classes.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/party.h"
#include "reone/game/script/routines.h"
#include "reone/game/types.h"
#include "reone/resource/types.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;

using testing::_;
using testing::NiceMock;

namespace {

class LevelUpHarness : boost::noncopyable {
public:
    LevelUpHarness() :
        _game(GameID::KotOR, "", testEngine().options(), testEngine().services(), _console),
        _routines(GameID::KotOR, &_game, &testEngine().services()),
        _classes(_classStrings, _classTwoDas),
        _soldier(ClassType::Soldier, _classes, _classStrings, _classTwoDas) {

        _routines.init();
    }

    ~LevelUpHarness() {
        testing::Mock::VerifyAndClearExpectations(&testEngine().guiModule().guis());
    }

    int showLevelUpGUI() {
        Routine &routine = _routines.get(_routines.getIndexByName("ShowLevelUpGUI"));
        ExecutionContext execution;
        execution.routines = &_routines;
        return routine.invoke({}, execution).intValue;
    }

    /** A level 1 soldier leading the party in a module, with \p xp experience. */
    std::shared_ptr<Creature> enterModuleWithLeader(int xp) {
        TestGameModule::setActiveModuleArea(_game, _game.newArea());
        auto player = _game.newCreature();
        player->attributes().addClassLevels(&_soldier, 1);
        // A member out of hit points is down, and is not offered a level.
        player->setMaxHitPoints(10);
        player->setCurrentHitPoints(10);
        _game.party().addMember(kNpcPlayer, player);
        _game.party().setPlayer(player);
        player->setXP(xp);
        return player;
    }

private:
    StubConsole _console;
    Game _game;
    Routines _routines;
    NiceMock<MockStrings> _classStrings;
    NiceMock<MockTwoDAs> _classTwoDas;
    Classes _classes;
    CreatureClass _soldier;
};

} // namespace

// The level-up screen is the character generation GUI; with its GUI missing
// here, a request to open it is seen as the GUI being loaded.

// 265: k_pdan_makejedi opens the screen once the player has become a Jedi
// and has the experience for the new class's first level.
TEST(LevelUpGUI, opens_the_level_up_screen_for_a_leader_with_a_level_pending) {
    LevelUpHarness harness;
    auto leader = harness.enterModuleWithLeader(1000);
    ASSERT_TRUE(leader->isLevelUpPending());

    EXPECT_CALL(testEngine().guiModule().guis(), get("maincg", _)).Times(1);

    // The screen cannot load here, so it is not reported as shown.
    EXPECT_EQ(0, harness.showLevelUpGUI());
}

TEST(LevelUpGUI, a_leader_without_the_experience_for_a_level_sees_no_screen) {
    LevelUpHarness harness;
    auto leader = harness.enterModuleWithLeader(999);
    ASSERT_FALSE(leader->isLevelUpPending());

    EXPECT_CALL(testEngine().guiModule().guis(), get(_, _)).Times(0);

    EXPECT_EQ(0, harness.showLevelUpGUI());
}

TEST(LevelUpGUI, outside_a_module_there_is_no_screen) {
    LevelUpHarness harness;

    EXPECT_CALL(testEngine().guiModule().guis(), get(_, _)).Times(0);

    EXPECT_EQ(0, harness.showLevelUpGUI());
}
