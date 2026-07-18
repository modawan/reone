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

#include "reone/game/action/unlockobject.h"
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
                     "ammunitiontype", "bodyvar"});
    builder.row({"", "", "", "", "", "", "I_Credits", "", "", "", "", ""});
    builder.row({"", "", "", "", "", "", "I_Datapad", "", "", "", "", ""});
    builder.row({"", "", "", "", "", "2", "I_Disguise", "", "", "", "-1", ""});
    return std::shared_ptr<TwoDA>(builder.build());
}

std::shared_ptr<TwoDA> makeAppearanceTable() {
    TwoDA::Builder builder;
    builder.columns({"modeltype", "walkdist", "rundist", "footsteptype", "envmap", "race", "racetex"});
    builder.row({"S", "1", "1", "-1", "", "", ""});
    builder.row({"S", "1", "1", "-1", "", "", ""});
    builder.row({"S", "1", "1", "-1", "", "", ""});
    return std::shared_ptr<TwoDA>(builder.build());
}

std::shared_ptr<Gff> makeDisguiseItemGff(int appearance) {
    auto property = Gff::Builder()
                        .field(Gff::Field::newWord("PropertyName", static_cast<int>(ItemProperty::Disguise)))
                        .field(Gff::Field::newWord("Subtype", appearance))
                        .build();
    return Gff::Builder()
        .field(Gff::Field::newInt("BaseItem", 2))
        .field(Gff::Field::newList("PropertiesList", {std::move(property)}))
        .build();
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

TEST(UnlockObjectAction, should_unlock_plot_nonlockable_door_without_opening_it) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto door = game.newDoor();
    door->setLocked(true);
    door->setPlotFlag(true);
    auto action = game.newAction<UnlockObjectAction>(door);

    action->execute(action, *door, 0.0f);

    EXPECT_FALSE(door->isLocked());
    EXPECT_FALSE(door->isOpen());
    EXPECT_TRUE(action->isCompleted());
}

TEST(UnlockObjectAction, should_unlock_plot_nonlockable_placeable_without_opening_it) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto placeable = game.newPlaceable();
    placeable->setLocked(true);
    placeable->setPlotFlag(true);
    auto action = game.newAction<UnlockObjectAction>(placeable);

    action->execute(action, *placeable, 0.0f);

    EXPECT_FALSE(placeable->isLocked());
    EXPECT_FALSE(placeable->isOpen());
    EXPECT_TRUE(action->isCompleted());
}

TEST(UnlockObjectAction, should_be_idempotent_for_unlocked_supported_targets) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto door = game.newDoor();
    auto placeable = game.newPlaceable();
    auto doorAction = game.newAction<UnlockObjectAction>(door);
    auto placeableAction = game.newAction<UnlockObjectAction>(placeable);

    doorAction->execute(doorAction, *door, 0.0f);
    placeableAction->execute(placeableAction, *placeable, 0.0f);

    EXPECT_FALSE(door->isLocked());
    EXPECT_FALSE(door->isOpen());
    EXPECT_TRUE(doorAction->isCompleted());
    EXPECT_FALSE(placeable->isLocked());
    EXPECT_FALSE(placeable->isOpen());
    EXPECT_TRUE(placeableAction->isCompleted());
}

TEST(UnlockObjectAction, should_complete_safely_for_missing_destroyed_or_unsupported_target) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto actor = game.newCreature();
    auto destroyed = game.newPlaceable();
    destroyed->damage(std::numeric_limits<int>::max(), actor->id());
    destroyed->setLocked(true);

    auto missingAction = game.newAction<UnlockObjectAction>(std::shared_ptr<Object>());
    auto destroyedAction = game.newAction<UnlockObjectAction>(destroyed);
    auto unsupportedAction = game.newAction<UnlockObjectAction>(actor);

    EXPECT_NO_THROW(missingAction->execute(missingAction, *actor, 0.0f));
    EXPECT_NO_THROW(destroyedAction->execute(destroyedAction, *actor, 0.0f));
    EXPECT_NO_THROW(unsupportedAction->execute(unsupportedAction, *actor, 0.0f));

    EXPECT_TRUE(missingAction->isCompleted());
    EXPECT_TRUE(destroyedAction->isCompleted());
    EXPECT_TRUE(destroyed->isLocked());
    EXPECT_TRUE(unsupportedAction->isCompleted());
}

TEST(Party, should_award_xp_to_pool_and_sync_current_members) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto player = game.newCreature();
    auto companion = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);
    game.party().addMember(0, companion);

    game.party().giveXP(100);

    EXPECT_EQ(game.party().xp(), 100);
    EXPECT_EQ(player->xp(), 100);
    EXPECT_EQ(companion->xp(), 100);
}
TEST(Party, should_set_xp_pool_and_sync_current_members) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto player = game.newCreature();
    auto companion = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);
    game.party().addMember(0, companion);

    game.party().setXP(250);

    EXPECT_EQ(game.party().xp(), 250);
    EXPECT_EQ(player->xp(), 250);
    EXPECT_EQ(companion->xp(), 250);
}
TEST(Party, should_sync_member_added_after_xp_gain) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto player = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);

    game.party().giveXP(100);

    auto latecomer = game.newCreature();
    game.party().addMember(0, latecomer);

    EXPECT_EQ(latecomer->xp(), 100);
    EXPECT_EQ(game.party().xp(), 100);
}
TEST(Party, should_keep_non_party_creature_xp_local) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto player = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);

    auto thug = game.newCreature();
    thug->giveXP(50);
    game.party().giveXP(100);

    EXPECT_EQ(thug->xp(), 50);
    EXPECT_EQ(player->xp(), 100);
    EXPECT_EQ(game.party().xp(), 100);
}

TEST(Party, should_route_item_acquired_by_companion_to_shared_player_inventory) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeBaseItemsTable()));
    EXPECT_CALL(engine.resourceModule().textures(), get(_, _)).Times(AnyNumber());

    auto player = game.newCreature();
    auto companion = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);
    game.party().addMember(0, companion);

    auto receiver = game.party().sharedInventoryReceiver(companion);
    ASSERT_EQ(receiver.get(), player.get());
    receiver->addItem(makeItem(game, "g_i_datapad001", 1, 1));

    ASSERT_EQ(player->items().size(), 1);
    EXPECT_EQ(player->items().front()->tag(), "g_i_datapad001");
    EXPECT_TRUE(companion->items().empty());
}

TEST(Party, should_keep_item_acquired_by_player_in_shared_inventory) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto player = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);

    EXPECT_EQ(game.party().sharedInventoryReceiver(player).get(), player.get());
}

TEST(Party, should_keep_item_acquired_by_non_party_creature_local) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeBaseItemsTable()));
    EXPECT_CALL(engine.resourceModule().textures(), get(_, _)).Times(AnyNumber());

    auto player = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);

    auto thug = game.newCreature();
    auto receiver = game.party().sharedInventoryReceiver(thug);
    ASSERT_EQ(receiver.get(), thug.get());
    receiver->addItem(makeItem(game, "g_i_datapad001", 1, 1));

    ASSERT_EQ(thug->items().size(), 1);
    EXPECT_TRUE(player->items().empty());
}

TEST(Party, should_not_share_inventory_of_non_party_placeable) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto player = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);

    auto footlocker = game.newPlaceable();
    EXPECT_EQ(game.party().sharedInventoryReceiver(footlocker).get(), footlocker.get());
}

TEST(Object, should_restore_saved_appearance_after_unequipping_loaded_disguise) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("appearance"))
        .WillRepeatedly(Return(makeAppearanceTable()));
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .WillRepeatedly(Return(makeBaseItemsTable()));
    EXPECT_CALL(engine.resourceModule().textures(), get(_, _))
        .Times(AnyNumber());
    EXPECT_CALL(engine.resourceModule().models(), get(_))
        .Times(AnyNumber());
    EXPECT_CALL(static_cast<MockPortraits &>(engine.services().game.portraits), getTextureByAppearance(_))
        .Times(AnyNumber());

    auto creatureGff = Gff::Builder()
                           .field(Gff::Field::newWord("Appearance_Type", 2))
                           .field(Gff::Field::newByte("PM_IsDisguised", 1))
                           .field(Gff::Field::newWord("PM_Appearance", 1))
                           .field(Gff::Field::newList("Equip_ItemList", {makeDisguiseItemGff(2)}))
                           .build();
    auto creature = game.newCreature();
    creature->deserialize(*creatureGff);

    ASSERT_EQ(creature->appearance(), 2);
    auto disguise = creature->getEquippedItem(InventorySlots::body);
    ASSERT_TRUE(disguise);

    creature->unequip(disguise);

    EXPECT_EQ(creature->appearance(), 1);
}
