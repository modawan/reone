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
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/placeable.h"
#include "reone/game/object/store.h"
#include "reone/game/party.h"
#include "reone/game/script/routines.h"
#include "reone/game/storetrade.h"
#include "reone/game/types.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include "reone/script/executioncontext.h"
#include "reone/script/routine.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;
using namespace testing;

namespace {

constexpr int kCreditsBaseItem = 0;
constexpr int kPlainBaseItem = 1;

std::shared_ptr<TwoDA> makeBaseItemsTable() {
    TwoDA::Builder builder;
    builder.columns({"itemclass", "itemtype"});
    builder.row({"I_Credits", "23"});
    builder.row({"I_Medpac", ""});
    return std::shared_ptr<TwoDA>(builder.build());
}

void expectItemTables(TestEngine &engine) {
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeBaseItemsTable()));
    EXPECT_CALL(engine.resourceModule().textures(), get(_, _)).Times(AnyNumber());
}

// An item record with its cost: Item::cost is the added cost scaled by the
// base item's multiplier, one by default.
std::shared_ptr<Gff> itemRecord(const std::string &tag, int addCost, int stackSize, int baseItem = kPlainBaseItem) {
    return Gff::Builder()
        .field(Gff::Field::newCExoString("Tag", tag))
        .field(Gff::Field::newInt("BaseItem", baseItem))
        .field(Gff::Field::newWord("StackSize", stackSize))
        .field(Gff::Field::newDword("AddCost", addCost))
        .build();
}

std::shared_ptr<Gff> storeRecord(int markUp, int markDown, int buySellFlag) {
    return Gff::Builder()
        .field(Gff::Field::newCExoString("Tag", "store"))
        .field(Gff::Field::newInt("MarkUp", markUp))
        .field(Gff::Field::newInt("MarkDown", markDown))
        .field(Gff::Field::newByte("BuySellFlag", buySellFlag))
        .build();
}

struct StoreFixture {
    TestEngine &engine;
    StubConsole console;
    Game game;
    std::shared_ptr<Creature> player;

    StoreFixture() :
        engine(testEngine()),
        game(GameID::KotOR, "", engine.options(), engine.services(), console) {
        expectItemTables(engine);
        player = game.newCreature();
        game.party().addMember(kNpcPlayer, player);
        game.party().setPlayer(player);
        game.party().setActualPlayer(player);
    }

    std::shared_ptr<Item> makeItem(const std::string &tag, int addCost, int stackSize = 1) {
        auto item = game.newItem();
        item->deserialize(*itemRecord(tag, addCost, stackSize), SerializedIdentityContext::templateResource());
        return item;
    }

    std::shared_ptr<Store> makeStore(int markUp = 110, int markDown = 40, int buySellFlag = 3) {
        auto store = game.newStore();
        store->deserialize(*storeRecord(markUp, markDown, buySellFlag), SerializedIdentityContext::templateResource());
        return store;
    }

    std::shared_ptr<Item> stock(Store &store, const std::string &tag, int addCost, int stackSize, bool infinite = false) {
        auto item = makeItem(tag, addCost, stackSize);
        item->setInfinite(infinite);
        return store.addItem(item);
    }

    std::shared_ptr<Item> carry(const std::string &tag, int addCost, int stackSize = 1) {
        return player->addItem(makeItem(tag, addCost, stackSize));
    }

    std::shared_ptr<Item> held(const Object &owner, const std::string &tag) const {
        for (const auto &item : owner.items()) {
            if (item->tag() == tag) {
                return item;
            }
        }
        return nullptr;
    }
};

} // namespace

// Pricing

TEST(StorePrice, marks_up_and_down_by_percent) {
    EXPECT_EQ(storeTradePrice(100, 110), 110);
    EXPECT_EQ(storeTradePrice(100, 40), 40);
    EXPECT_EQ(storeTradePrice(250, 100), 250);
}

TEST(StorePrice, truncates_toward_zero) {
    // 16.5, 2.8 and 99.99
    EXPECT_EQ(storeTradePrice(15, 110), 16);
    EXPECT_EQ(storeTradePrice(7, 40), 2);
    EXPECT_EQ(storeTradePrice(909, 11), 99);
}

TEST(StorePrice, anything_with_a_cost_trades_for_at_least_one_credit) {
    EXPECT_EQ(storeTradePrice(1, 40), 1);
    EXPECT_EQ(storeTradePrice(2, 10), 1);
    EXPECT_EQ(storeTradePrice(5, 0), 1);
    // A bonus that takes the percentage below none counts as none.
    EXPECT_EQ(storeTradePrice(100, -50), 1);
}

TEST(StorePrice, an_item_without_a_cost_is_free) {
    EXPECT_EQ(storeTradePrice(0, 110), 0);
    EXPECT_EQ(storeTradePrice(0, 40), 0);
}

TEST(StorePrice, large_costs_do_not_overflow) {
    EXPECT_EQ(storeTradePrice(100000000, 200), 200000000);
    EXPECT_EQ(storeTradePrice(std::numeric_limits<int>::max(), 1000), std::numeric_limits<int>::max());
}

TEST(StorePrice, buy_price_adds_the_bonus_mark_up) {
    StoreFixture fixture;
    auto store = fixture.makeStore(110, 40);
    auto item = fixture.makeItem("blaster", 200);

    EXPECT_EQ(storeBuyPrice(fixture.game, *store, *item, 0), 220);
    EXPECT_EQ(storeBuyPrice(fixture.game, *store, *item, 15), 250);
    EXPECT_EQ(storeBuyPrice(fixture.game, *store, *item, -20), 180);
}

TEST(StorePrice, sell_price_adds_the_bonus_mark_down) {
    StoreFixture fixture;
    auto store = fixture.makeStore(110, 40);
    auto item = fixture.makeItem("blaster", 200);

    EXPECT_EQ(storeSellPrice(fixture.game, *store, *item, 0), 80);
    EXPECT_EQ(storeSellPrice(fixture.game, *store, *item, 10), 100);
}

TEST(StorePrice, prices_follow_the_base_item_cost_multiplier) {
    StoreFixture fixture;
    auto store = fixture.makeStore(100, 50);
    auto item = fixture.makeItem("blaster", 200);
    fixture.game.setBaseItemCostMultiplier(kPlainBaseItem, 1.5f);

    EXPECT_EQ(storeBuyPrice(fixture.game, *store, *item, 0), 300);
    EXPECT_EQ(storeSellPrice(fixture.game, *store, *item, 0), 150);
}

TEST(StorePrice, a_plot_item_costs_nothing) {
    StoreFixture fixture;
    auto store = fixture.makeStore();
    auto item = fixture.makeItem("plot", 200);
    item->setPlotFlag(true);

    EXPECT_EQ(storeBuyPrice(fixture.game, *store, *item, 0), 0);
}

// What can be sold

TEST(StoreSellable, an_ordinary_item_can_be_sold) {
    StoreFixture fixture;
    EXPECT_TRUE(isSellableToStore(*fixture.makeItem("medpac", 10)));
}

TEST(StoreSellable, plot_equipped_and_credit_items_cannot_be_sold) {
    StoreFixture fixture;
    auto plot = fixture.makeItem("plot", 10);
    plot->setPlotFlag(true);
    auto equipped = fixture.makeItem("armor", 10);
    equipped->setEquipped(true);
    auto credits = fixture.game.newItem();
    credits->deserialize(*itemRecord("credits", 1, 50, kCreditsBaseItem), SerializedIdentityContext::templateResource());
    ASSERT_TRUE(credits->isCredits());

    EXPECT_FALSE(isSellableToStore(*plot));
    EXPECT_FALSE(isSellableToStore(*equipped));
    EXPECT_FALSE(isSellableToStore(*credits));
}

// Buying

TEST(StoreBuy, buys_one_unit_of_a_stack) {
    StoreFixture fixture;
    auto store = fixture.makeStore(110, 40, Store::kSellsToPlayer);
    auto stack = fixture.stock(*store, "medpac", 100, 3);
    fixture.game.party().giveGold(500);

    ASSERT_TRUE(buyFromStore(fixture.game, *store, stack, 0));

    EXPECT_EQ(fixture.game.party().gold(), 390);
    EXPECT_EQ(stack->stackSize(), 2);
    EXPECT_EQ(stack->owner(), store->id());
    auto bought = fixture.held(*fixture.player, "medpac");
    ASSERT_TRUE(bought);
    EXPECT_EQ(bought->stackSize(), 1);
    EXPECT_NE(bought, stack);
}

TEST(StoreBuy, buying_the_last_unit_moves_the_item) {
    StoreFixture fixture;
    auto store = fixture.makeStore();
    auto item = fixture.stock(*store, "blaster", 100, 1);
    fixture.game.party().giveGold(110);

    ASSERT_TRUE(buyFromStore(fixture.game, *store, item, 0));

    EXPECT_EQ(fixture.game.party().gold(), 0);
    EXPECT_TRUE(store->items().empty());
    EXPECT_EQ(item->owner(), fixture.player->id());
    EXPECT_EQ(fixture.held(*fixture.player, "blaster"), item);
}

TEST(StoreBuy, infinite_stock_never_runs_out) {
    StoreFixture fixture;
    auto store = fixture.makeStore();
    auto item = fixture.stock(*store, "grenade", 100, 1, true);
    fixture.game.party().giveGold(1000);

    ASSERT_TRUE(buyFromStore(fixture.game, *store, item, 0));
    ASSERT_TRUE(buyFromStore(fixture.game, *store, item, 0));

    EXPECT_EQ(fixture.game.party().gold(), 780);
    ASSERT_EQ(store->items().size(), 1u);
    EXPECT_EQ(item->stackSize(), 1);
    EXPECT_TRUE(item->isInfinite());
    auto bought = fixture.held(*fixture.player, "grenade");
    ASSERT_TRUE(bought);
    EXPECT_EQ(bought->stackSize(), 2);
    // What the party buys is an ordinary item.
    EXPECT_FALSE(bought->isInfinite());
}

TEST(StoreBuy, not_enough_credits_buys_nothing) {
    StoreFixture fixture;
    auto store = fixture.makeStore();
    auto item = fixture.stock(*store, "blaster", 100, 2);
    fixture.game.party().giveGold(109);

    EXPECT_FALSE(buyFromStore(fixture.game, *store, item, 0));

    EXPECT_EQ(fixture.game.party().gold(), 109);
    EXPECT_EQ(item->stackSize(), 2);
    EXPECT_FALSE(fixture.held(*fixture.player, "blaster"));
}

TEST(StoreBuy, the_bonus_mark_up_raises_what_is_paid) {
    StoreFixture fixture;
    auto store = fixture.makeStore(110, 40);
    auto item = fixture.stock(*store, "blaster", 100, 2);
    fixture.game.party().giveGold(1000);

    ASSERT_TRUE(buyFromStore(fixture.game, *store, item, 40));

    EXPECT_EQ(fixture.game.party().gold(), 850);
}

TEST(StoreBuy, a_store_that_only_buys_sells_nothing) {
    StoreFixture fixture;
    auto store = fixture.makeStore(110, 40, Store::kBuysFromPlayer);
    auto item = fixture.stock(*store, "blaster", 100, 1);
    fixture.game.party().giveGold(1000);

    EXPECT_FALSE(buyFromStore(fixture.game, *store, item, 0));

    EXPECT_EQ(fixture.game.party().gold(), 1000);
    EXPECT_EQ(item->owner(), store->id());
}

TEST(StoreBuy, an_item_the_store_does_not_hold_cannot_be_bought) {
    StoreFixture fixture;
    auto store = fixture.makeStore();
    auto other = fixture.makeStore();
    auto item = fixture.stock(*other, "blaster", 100, 1);
    fixture.game.party().giveGold(1000);

    EXPECT_FALSE(buyFromStore(fixture.game, *store, item, 0));
    EXPECT_EQ(fixture.game.party().gold(), 1000);
}

// Selling

TEST(StoreSell, sells_one_unit_of_a_stack) {
    StoreFixture fixture;
    auto store = fixture.makeStore(110, 40, Store::kBuysFromPlayer);
    auto stack = fixture.carry("medpac", 100, 3);

    ASSERT_TRUE(sellToStore(fixture.game, *store, stack, 0));

    EXPECT_EQ(fixture.game.party().gold(), 40);
    EXPECT_EQ(stack->stackSize(), 2);
    auto sold = fixture.held(*store, "medpac");
    ASSERT_TRUE(sold);
    EXPECT_EQ(sold->stackSize(), 1);
}

TEST(StoreSell, selling_the_last_unit_moves_the_item) {
    StoreFixture fixture;
    auto store = fixture.makeStore(110, 40);
    auto item = fixture.carry("blaster", 100);

    ASSERT_TRUE(sellToStore(fixture.game, *store, item, 15));

    EXPECT_EQ(fixture.game.party().gold(), 55);
    EXPECT_FALSE(fixture.held(*fixture.player, "blaster"));
    EXPECT_EQ(item->owner(), store->id());
}

TEST(StoreSell, a_sold_unit_joins_the_stores_stack) {
    StoreFixture fixture;
    auto store = fixture.makeStore();
    auto stocked = fixture.stock(*store, "medpac", 100, 2);
    auto carried = fixture.carry("medpac", 100);

    ASSERT_TRUE(sellToStore(fixture.game, *store, carried, 0));

    ASSERT_EQ(store->items().size(), 1u);
    EXPECT_EQ(stocked->stackSize(), 3);
}

TEST(StoreSell, a_sold_unit_stays_apart_from_infinite_stock) {
    StoreFixture fixture;
    auto store = fixture.makeStore();
    auto stocked = fixture.stock(*store, "medpac", 100, 1, true);
    auto carried = fixture.carry("medpac", 100);

    ASSERT_TRUE(sellToStore(fixture.game, *store, carried, 0));

    ASSERT_EQ(store->items().size(), 2u);
    EXPECT_EQ(stocked->stackSize(), 1);
    EXPECT_FALSE(carried->isInfinite());
}

TEST(StoreSell, a_store_that_only_sells_buys_nothing) {
    StoreFixture fixture;
    auto store = fixture.makeStore(110, 40, Store::kSellsToPlayer);
    auto item = fixture.carry("blaster", 100);

    EXPECT_FALSE(sellToStore(fixture.game, *store, item, 0));

    EXPECT_EQ(fixture.game.party().gold(), 0);
    EXPECT_EQ(item->owner(), fixture.player->id());
}

TEST(StoreSell, a_plot_item_cannot_be_sold) {
    StoreFixture fixture;
    auto store = fixture.makeStore();
    auto item = fixture.carry("plot", 100);
    item->setPlotFlag(true);

    EXPECT_FALSE(sellToStore(fixture.game, *store, item, 0));
    EXPECT_EQ(item->owner(), fixture.player->id());
}

TEST(StoreSell, an_item_the_party_does_not_carry_cannot_be_sold) {
    StoreFixture fixture;
    auto store = fixture.makeStore();
    auto footlocker = fixture.game.newPlaceable();
    auto item = footlocker->addItem(fixture.makeItem("blaster", 100));

    EXPECT_FALSE(sellToStore(fixture.game, *store, item, 0));
    EXPECT_EQ(fixture.game.party().gold(), 0);
}

// Loading

TEST(StoreLoad, reads_the_name_from_loc_name) {
    StoreFixture fixture;
    auto store = fixture.game.newStore();
    auto record = Gff::Builder()
                      .field(Gff::Field::newCExoLocString("LocName", -1, "Larrim's Store"))
                      .build();

    store->deserialize(*record, SerializedIdentityContext::templateResource());

    EXPECT_EQ(store->name(), "Larrim's Store");
}

TEST(StoreLoad, falls_back_to_localized_name) {
    StoreFixture fixture;
    auto store = fixture.game.newStore();
    auto record = Gff::Builder()
                      .field(Gff::Field::newCExoLocString("LocalizedName", -1, "Janice's Store"))
                      .build();

    store->deserialize(*record, SerializedIdentityContext::templateResource());

    EXPECT_EQ(store->name(), "Janice's Store");
}

TEST(StoreLoad, reads_mark_up_mark_down_and_buy_sell_flag) {
    StoreFixture fixture;
    auto store = fixture.makeStore(110, 40, 3);

    EXPECT_EQ(store->markUp(), 110);
    EXPECT_EQ(store->markDown(), 40);
    EXPECT_TRUE(store->sellsToPlayer());
    EXPECT_TRUE(store->buysFromPlayer());

    auto sellsOnly = fixture.makeStore(100, 100, 1);
    EXPECT_TRUE(sellsOnly->sellsToPlayer());
    EXPECT_FALSE(sellsOnly->buysFromPlayer());
}

TEST(StoreLoad, reads_infinite_stock_per_item_list_entry) {
    StoreFixture fixture;
    EXPECT_CALL(fixture.engine.resourceModule().gffs(), get("g_i_medeqpmnt01", ResType::Uti))
        .Times(AnyNumber())
        .WillRepeatedly(Return(itemRecord("medpac", 10, 1)));
    EXPECT_CALL(fixture.engine.resourceModule().gffs(), get("g_w_blstrpstl001", ResType::Uti))
        .Times(AnyNumber())
        .WillRepeatedly(Return(itemRecord("blaster", 100, 1)));
    auto infinite = Gff::Builder()
                        .field(Gff::Field::newResRef("InventoryRes", "g_i_medeqpmnt01"))
                        .field(Gff::Field::newByte("Infinite", 1))
                        .build();
    auto finite = Gff::Builder()
                      .field(Gff::Field::newResRef("InventoryRes", "g_w_blstrpstl001"))
                      .build();
    auto record = Gff::Builder()
                      .field(Gff::Field::newList("ItemList", {infinite, finite}))
                      .build();
    auto store = fixture.game.newStore();

    store->deserialize(*record, SerializedIdentityContext::templateResource());

    auto medpac = fixture.held(*store, "medpac");
    auto blaster = fixture.held(*store, "blaster");
    ASSERT_TRUE(medpac);
    ASSERT_TRUE(blaster);
    EXPECT_TRUE(medpac->isInfinite());
    EXPECT_FALSE(blaster->isInfinite());
}

TEST(StoreLoad, a_saved_item_record_keeps_infinite_stock) {
    StoreFixture fixture;
    auto record = Gff::Builder()
                      .field(Gff::Field::newCExoString("Tag", "medpac"))
                      .field(Gff::Field::newWord("StackSize", 1))
                      .field(Gff::Field::newByte("Infinite", 1))
                      .build();
    auto item = fixture.game.newItem();

    item->deserialize(*record, SerializedIdentityContext::templateResource());

    EXPECT_TRUE(item->isInfinite());
}

// The script routine

TEST(StoreRoutine, open_store_on_an_object_that_is_not_a_store_does_nothing) {
    StoreFixture fixture;
    Routines routines(GameID::KotOR, &fixture.game, &fixture.engine.services());
    routines.init();
    auto placeable = fixture.game.newPlaceable();
    Routine &routine = routines.get(routines.getIndexByName("OpenStore"));
    ExecutionContext execution;
    execution.routines = &routines;

    EXPECT_NO_THROW(routine.invoke(
        {Variable::ofObject(placeable->id()), Variable::ofObject(fixture.player->id()), Variable::ofInt(0), Variable::ofInt(0)},
        execution));
    EXPECT_EQ(fixture.game.currentScreen(), Game::Screen::None);
}

TEST(StoreRoutine, open_store_on_a_store_reaches_the_game) {
    StoreFixture fixture;
    Routines routines(GameID::KotOR, &fixture.game, &fixture.engine.services());
    routines.init();
    auto store = fixture.makeStore();
    Routine &routine = routines.get(routines.getIndexByName("OpenStore"));
    ExecutionContext execution;
    execution.routines = &routines;

    // Without a loaded store GUI there is no screen to open; the routine is
    // implemented and the game declines.
    EXPECT_NO_THROW(routine.invoke(
        {Variable::ofObject(store->id()), Variable::ofObject(fixture.player->id()), Variable::ofInt(10), Variable::ofInt(5)},
        execution));
    EXPECT_FALSE(fixture.game.openStore(store, 0, 0));
    EXPECT_EQ(fixture.game.currentScreen(), Game::Screen::None);
}
