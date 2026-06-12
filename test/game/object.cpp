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

#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/placeable.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

class StubConsole : public IConsole, boost::noncopyable {
public:
    void registerCommand(std::string name, std::string description, CommandHandler handler) override {}
    void printLine(const std::string &text) override {}
};

// TestEngine initializes the Logger singleton, which only tolerates a single
// init per process.
TestEngine &testEngine() {
    static TestEngine engine;
    static bool initialized = false;
    if (!initialized) {
        engine.init();
        initialized = true;
    }
    return engine;
}

std::shared_ptr<TwoDA> makeBaseItemsTable() {
    TwoDA::Builder builder;
    builder.columns({"maxattackrange", "crithitmult", "critthreat", "damageflags", "dietoroll",
                     "equipableslots", "itemclass", "numdice", "weapontype", "weaponwield",
                     "ammunitiontype"});
    builder.row({"", "", "", "", "", "", "I_Credits", "", "", "", ""});
    builder.row({"", "", "", "", "", "", "I_Datapad", "", "", "", ""});
    return std::shared_ptr<TwoDA>(builder.build());
}

std::shared_ptr<Item> makeItem(Game &game, std::string tag, int baseItem, int stackSize) {
    auto gff = Gff::Builder()
                   .field(Gff::Field::newCExoString("Tag", std::move(tag)))
                   .field(Gff::Field::newInt("BaseItem", baseItem))
                   .field(Gff::Field::newWord("StackSize", stackSize))
                   .build();
    auto item = game.newItem();
    item->deserialize(*gff);
    item->setDropable(true);
    return item;
}

} // namespace

TEST(Object, should_convert_credits_to_party_gold_when_looted_by_party_member) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeBaseItemsTable()));
    // Item deserialization incidentally looks up inventory icons; no texture
    // is needed.
    EXPECT_CALL(engine.resourceModule().textures(), get(AnyOf("ii_credits_000", "ii_datapad_000"), _))
        .Times(AnyNumber());

    auto player = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);

    auto footlocker = game.newPlaceable();
    footlocker->addItem(makeItem(game, "g_i_credits001", 0, 5));
    footlocker->addItem(makeItem(game, "g_i_credits002", 0, 10));
    footlocker->addItem(makeItem(game, "g_i_datapad001", 1, 1));

    footlocker->moveDropableItemsTo(*player);

    EXPECT_EQ(game.party().gold(), 15);
    ASSERT_EQ(player->items().size(), 1);
    EXPECT_EQ(player->items().front()->tag(), "g_i_datapad001");
    EXPECT_EQ(player->items().front()->stackSize(), 1);
    EXPECT_TRUE(footlocker->items().empty());
}

TEST(Object, should_move_credits_as_items_when_destination_is_not_in_party) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeBaseItemsTable()));
    // Item deserialization incidentally looks up inventory icons; no texture
    // is needed.
    EXPECT_CALL(engine.resourceModule().textures(), get("ii_credits_000", _))
        .Times(AnyNumber());

    auto thug = game.newCreature();

    auto footlocker = game.newPlaceable();
    footlocker->addItem(makeItem(game, "g_i_credits001", 0, 5));

    footlocker->moveDropableItemsTo(*thug);

    EXPECT_EQ(game.party().gold(), 0);
    ASSERT_EQ(thug->items().size(), 1);
    EXPECT_EQ(thug->items().front()->tag(), "g_i_credits001");
    EXPECT_TRUE(footlocker->items().empty());
}
