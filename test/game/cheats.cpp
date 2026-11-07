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

#include "reone/game/d20/class.h"
#include "reone/game/d20/classes.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/placeable.h"
#include "reone/game/party.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"

#include "../fixtures/engine.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

void reone::game::TestGameModule::addLevelStats(Creature &creature, int hitDie, int forcePoints) {
    creature._levelStats.push_back({hitDie, forcePoints});
}

namespace {

// A Jedi Guardian class whose tables carry only what loading a class reads.
struct JediClass {
    NiceMock<MockStrings> strings;
    NiceMock<MockTwoDAs> twoDas;
    Classes classes {strings, twoDas};
    std::shared_ptr<CreatureClass> clazz;

    JediClass() {
        auto empty = std::shared_ptr<TwoDA>(TwoDA::Builder().build());
        TwoDA::Builder saves;
        saves.columns({"level", "fortsave", "refsave", "willsave"}).row({"1", "0", "0", "0"});
        TwoDA::Builder attacks;
        attacks.columns({"bab"}).row({"0"});
        TwoDA::Builder defenses;
        defenses.columns({"def"}).row({"2"}).row({"2"});
        ON_CALL(twoDas, get("skills")).WillByDefault(Return(empty));
        ON_CALL(twoDas, get("save")).WillByDefault(Return(std::shared_ptr<TwoDA>(saves.build())));
        ON_CALL(twoDas, get("attack")).WillByDefault(Return(std::shared_ptr<TwoDA>(attacks.build())));
        ON_CALL(twoDas, get("acbonus")).WillByDefault(Return(std::shared_ptr<TwoDA>(defenses.build())));

        TwoDA::Builder table;
        table.columns({"name", "description", "hitdie", "skillpointbase",
                       "str", "dex", "con", "int", "wis", "cha",
                       "skillstable", "savingthrowtable", "attackbonustable",
                       "armorclasscolumn", "featstable", "featgain", "spellgaintable"})
            .row({"0", "0", "12", "0",
                  "10", "10", "10", "10", "10", "10",
                  "unused", "save", "attack", "def", "", "", ""});
        clazz = std::make_shared<CreatureClass>(ClassType::JediGuardian, classes, strings, twoDas);
        clazz->load(*table.build(), 0);
    }
};

struct CheatFixture : Test {
    void SetUp() override {
        ON_CALL(engine.sceneModule().graphs(), get(_))
            .WillByDefault(ReturnRef(sceneGraph));
        game = std::make_unique<Game>(GameID::KotOR, "", engine.options(), engine.services(), console);
        // A leader change runs a script on the new leader.
        game->initLocalServices();
        player = game->newCreature();
        TestGameModule::configureModuleSnapshot(*game, game->newArea(), player, "end_m01aa", "end_m01aa");
        companion = game->newCreature();
        game->party().addAvailableMember(0, companion);
        game->party().addMember(0, companion);
        TestGameModule::initConsole(*game);
    }

    // A creature's maximums come from its level history, so the heal tests
    // record one Jedi level.
    void giveJediLevel(Creature &creature) {
        creature.attributes().addClassLevels(jedi.clazz.get(), 1);
        TestGameModule::addLevelStats(creature, 12, 8);
    }

    // An item template, and the base item table that loading one reads.
    void expectItem(const std::string &resRef) {
        TwoDA::Builder baseItems;
        baseItems.columns({"maxattackrange", "crithitmult", "critthreat", "damageflags", "dietoroll",
                           "equipableslots", "itemclass", "numdice", "weapontype", "weaponwield",
                           "ammunitiontype", "bodyvar", "itemtype"})
            .row({"", "", "", "", "", "", "w_dblsbr", "", "", "", "-1", "", ""});
        EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
            .WillRepeatedly(Return(std::shared_ptr<TwoDA>(baseItems.build())));
        EXPECT_CALL(engine.resourceModule().textures(), get(_, _)).Times(AnyNumber());
        EXPECT_CALL(engine.resourceModule().gffs(), get(resRef, ResType::Uti))
            .WillRepeatedly(Return(std::shared_ptr<Gff>(Gff::Builder()
                                                            .field(Gff::Field::newCExoString("Tag", resRef))
                                                            .build())));
    }

    TestEngine &engine = testEngine();
    JediClass jedi;
    NiceMock<scene::MockSceneGraph> sceneGraph;
    StubConsole console;
    std::unique_ptr<Game> game;
    std::shared_ptr<Creature> player;
    std::shared_ptr<Creature> companion;
};

} // namespace

TEST_F(CheatFixture, additem_gives_the_item_to_the_selected_object) {
    expectItem("g1_w_dblsbr001");
    auto chest = game->newPlaceable();
    game->module()->area()->selectObject(chest, true);

    console.execute("additem", {"g1_w_dblsbr001"});

    ASSERT_EQ(1u, chest->items().size());
    EXPECT_TRUE(player->items().empty());
}

TEST_F(CheatFixture, additem_adds_nothing_for_an_unknown_code) {
    EXPECT_CALL(engine.resourceModule().gffs(), get("no_such_item", ResType::Uti))
        .WillRepeatedly(Return(nullptr));

    EXPECT_THROW(console.execute("additem", {"no_such_item"}), std::runtime_error);

    EXPECT_TRUE(player->items().empty());
}

TEST_F(CheatFixture, giveitem_gives_the_item_to_the_party_even_with_another_object_selected) {
    expectItem("g1_w_dblsbr001");
    auto chest = game->newPlaceable();
    game->module()->area()->selectObject(chest, true);

    console.execute("giveitem", {"g1_w_dblsbr001"});

    ASSERT_EQ(1u, player->items().size());
    EXPECT_EQ("g1_w_dblsbr001", player->items()[0]->tag());
    EXPECT_TRUE(chest->items().empty());
}

TEST_F(CheatFixture, giveitem_adds_nothing_for_an_unknown_code) {
    EXPECT_CALL(engine.resourceModule().gffs(), get("no_such_item", ResType::Uti))
        .WillRepeatedly(Return(nullptr));

    EXPECT_THROW(console.execute("giveitem", {"no_such_item"}), std::runtime_error);

    EXPECT_TRUE(player->items().empty());
}

TEST_F(CheatFixture, giveitem_gives_to_the_shared_inventory_under_another_leader) {
    expectItem("g1_w_dblsbr001");
    ASSERT_TRUE(game->party().setPartyLeader(0));

    console.execute("giveitem", {"g1_w_dblsbr001"});

    ASSERT_EQ(1u, player->items().size());
    EXPECT_TRUE(companion->items().empty());
}

TEST_F(CheatFixture, heal_restores_the_leaders_hit_points) {
    giveJediLevel(*player);
    player->setCurrentHitPoints(5);

    console.execute("heal");

    EXPECT_GT(player->maxHitPoints(), 5);
    EXPECT_EQ(player->maxHitPoints(), player->currentHitPoints());
}

TEST_F(CheatFixture, heal_revives_a_dead_leader) {
    giveJediLevel(*player);
    // A loaded creature is raiseable unless its record says otherwise.
    player->setRaiseable(true);
    player->setCurrentHitPoints(-5);

    console.execute("heal");

    EXPECT_EQ(player->maxHitPoints(), player->currentHitPoints());
}

TEST_F(CheatFixture, heal_leaves_a_dead_leader_that_cannot_be_raised_dead) {
    giveJediLevel(*player);
    player->setCurrentHitPoints(-5);

    console.execute("heal");

    EXPECT_EQ(-5, player->currentHitPoints());
}

TEST_F(CheatFixture, heal_restores_the_leaders_force_points) {
    giveJediLevel(*player);
    player->setCurrentForcePoints(0);

    console.execute("heal");

    EXPECT_GT(player->maxForcePoints(), 0);
    EXPECT_EQ(player->maxForcePoints(), player->currentForce());
}

TEST_F(CheatFixture, heal_leaves_the_rest_of_the_party_hurt) {
    giveJediLevel(*companion);
    companion->setCurrentHitPoints(5);

    console.execute("heal");

    EXPECT_EQ(5, companion->currentHitPoints());
}

TEST_F(CheatFixture, heal_heals_the_new_leader_after_a_leader_change) {
    giveJediLevel(*player);
    giveJediLevel(*companion);
    player->setCurrentHitPoints(5);
    companion->setCurrentHitPoints(5);
    ASSERT_TRUE(game->party().setPartyLeader(0));

    console.execute("heal");

    EXPECT_EQ(companion->maxHitPoints(), companion->currentHitPoints());
    EXPECT_EQ(5, player->currentHitPoints());
}

TEST_F(CheatFixture, invulnerability_turns_on_the_plot_flag_for_the_whole_party) {
    console.execute("invulnerability");

    EXPECT_TRUE(player->plotFlag());
    EXPECT_TRUE(companion->plotFlag());
    EXPECT_EQ("invulnerability on", console.lines.back());
}

TEST_F(CheatFixture, invulnerability_toggles_back_off) {
    console.execute("invulnerability");
    console.execute("invulnerability");

    EXPECT_FALSE(player->plotFlag());
    EXPECT_FALSE(companion->plotFlag());
    EXPECT_EQ("invulnerability off", console.lines.back());
}

TEST_F(CheatFixture, invulnerability_gives_the_party_the_opposite_of_the_leaders_flag) {
    player->setPlotFlag(true);

    console.execute("invulnerability");

    EXPECT_FALSE(player->plotFlag());
    EXPECT_FALSE(companion->plotFlag());
}

TEST_F(CheatFixture, invulnerability_leaves_companions_outside_the_party_alone) {
    auto waiting = game->newCreature();
    game->party().addAvailableMember(1, waiting);

    console.execute("invulnerability");

    EXPECT_FALSE(waiting->plotFlag());
}

TEST_F(CheatFixture, invulnerability_reads_the_new_leaders_flag_after_a_leader_change) {
    companion->setPlotFlag(true);
    ASSERT_TRUE(game->party().setPartyLeader(0));

    console.execute("invulnerability");

    EXPECT_FALSE(companion->plotFlag());
    EXPECT_FALSE(player->plotFlag());
}

TEST_F(CheatFixture, whereami_prints_the_module_and_the_leaders_position) {
    player->setPosition(glm::vec3(1.5f, -2.25f, 0.0f));

    console.execute("whereami");

    ASSERT_EQ(2u, console.lines.size());
    EXPECT_EQ("module: end_m01aa", console.lines[0]);
    EXPECT_EQ("position: (1.50, -2.25, 0.00)", console.lines[1]);
}

TEST_F(CheatFixture, whereami_prints_only_the_position_without_a_module) {
    TestGameModule::setActiveModule(*game, false);

    console.execute("whereami");

    ASSERT_EQ(1u, console.lines.size());
    EXPECT_EQ("position: (0.00, 0.00, 0.00)", console.lines[0]);
}

TEST_F(CheatFixture, whereami_reports_the_new_leader_after_a_leader_change) {
    companion->setPosition(glm::vec3(4.0f, 5.0f, 6.0f));
    ASSERT_TRUE(game->party().setPartyLeader(0));

    console.execute("whereami");

    EXPECT_EQ("position: (4.00, 5.00, 6.00)", console.lines.back());
}
