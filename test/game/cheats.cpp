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


namespace {

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














