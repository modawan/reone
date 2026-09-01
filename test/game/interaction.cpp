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

#include <limits>

#include "../fixtures/engine.h"

#include "reone/game/console.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/game/reputes.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

void reone::game::TestGameModule::clickCreature(Module &module, const std::shared_ptr<Creature> &creature) {
    module.onCreatureClick(creature);
}

namespace {

class StubConsole : public IConsole, boost::noncopyable {
public:
    void registerCommand(std::string name, std::string description, CommandHandler handler) override {}
    void printLine(const std::string &text) override {}
};

// Two synthetic factions standing in for "the party leader's" and "the other
// creature's". Nothing here depends on which factions they are.
constexpr Faction kLeaderFaction = Faction::Friendly1;
constexpr Faction kOtherFaction = Faction::Hostile1;

// How a creature is interacted with follows the creature's own view of the
// party leader. These tests set the two directions to opposite values, so a
// query that reverses source and target reads the other cell and fails.
class CreatureInteractionTest : public Test {
protected:
    void SetUp() override {
        _engine.init();
        _game = std::make_unique<Game>(GameID::KotOR, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
        _module = _game->newModule();

        _leader = _game->newCreature();
        _leader->setFaction(kLeaderFaction);
        _game->party().addMember(kNpcPlayer, _leader);
        _game->party().setPlayer(_leader);
        _game->party().setActualPlayer(_leader);

        _other = _game->newCreature();
        _other->setFaction(kOtherFaction);
        _other->setConversation("test_conversation");
    }

    // Installs a directed disposition: only `source` regarding `target` as an
    // enemy answers true, so the reverse pairing answers false.
    void hostileOnly(Faction source, Faction target) {
        auto &reputes = static_cast<MockReputes &>(_engine.services().game.reputes);
        EXPECT_CALL(reputes, getIsEnemy(An<const Creature &>(), An<const Creature &>()))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([source, target](const Creature &from, const Creature &to) {
                return from.faction() == source && to.faction() == target;
            }));
    }

    std::vector<ActionType> contextActionTypes() {
        std::vector<ActionType> types;
        for (auto &action : _module->getContextActions(_other)) {
            types.push_back(action.type);
        }
        return types;
    }

    ActionType queuedActionType() {
        TestGameModule::clickCreature(*_module, _other);
        EXPECT_EQ(1ll, _leader->actions().size());
        return _leader->actions().front()->type();
    }

    TestEngine _engine;
    StubConsole _console;
    std::unique_ptr<Game> _game;
    std::shared_ptr<Module> _module;
    std::shared_ptr<Creature> _leader;
    std::shared_ptr<Creature> _other;
};

} // namespace

// The disguise shape: the party leader still regards the creature as an enemy,
// but the creature has stopped regarding the leader as one.
TEST_F(CreatureInteractionTest, creature_that_is_no_longer_hostile_is_presented_and_treated_as_such) {
    hostileOnly(kLeaderFaction, kOtherFaction);

    EXPECT_FALSE(_module->isHostileToPartyLeader(*_other));
    EXPECT_EQ(ActionType::StartConversation, queuedActionType());
    EXPECT_THAT(contextActionTypes(), Not(Contains(ActionType::AttackObject)));
}

// The reverse shape: the creature regards the leader as an enemy even though
// the leader's own faction does not return the sentiment.
TEST_F(CreatureInteractionTest, creature_that_is_hostile_to_the_leader_is_presented_and_treated_as_hostile) {
    hostileOnly(kOtherFaction, kLeaderFaction);

    EXPECT_TRUE(_module->isHostileToPartyLeader(*_other));
    EXPECT_EQ(ActionType::AttackObject, queuedActionType());
    EXPECT_THAT(contextActionTypes(), Contains(ActionType::AttackObject));
}

TEST_F(CreatureInteractionTest, dead_creatures_are_never_hostile_regardless_of_disposition) {
    hostileOnly(kOtherFaction, kLeaderFaction);
    // Dying renames the creature to its authored "remains" string.
    EXPECT_CALL(_engine.resourceModule().strings(), getText(_))
        .Times(AnyNumber())
        .WillRepeatedly(Return(""));
    _other->damage(std::numeric_limits<int>::max(), nullptr);
    ASSERT_TRUE(_other->isDead());

    EXPECT_FALSE(_module->isHostileToPartyLeader(*_other));
    EXPECT_THAT(contextActionTypes(), Not(Contains(ActionType::AttackObject)));
}

TEST_F(CreatureInteractionTest, clicking_a_non_hostile_creature_without_a_conversation_queues_nothing) {
    hostileOnly(kLeaderFaction, kOtherFaction);
    _other->setConversation("");

    TestGameModule::clickCreature(*_module, _other);

    EXPECT_TRUE(_leader->actions().empty());
}
