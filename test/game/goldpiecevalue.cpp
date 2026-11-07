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
#include "reone/game/script/routines.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;

using testing::Return;

namespace {

constexpr int kLightsaber = 8;

class GoldPieceValue : public testing::Test {
protected:
    void SetUp() override {
        _engine.init();
        TwoDA::Builder baseItems;
        baseItems.columns({"ammunitiontype"});
        for (int row = 0; row <= kLightsaber; ++row) {
            baseItems.row({"0"});
        }
        ON_CALL(_engine.resourceModule().twoDas(), get(std::string("baseitems")))
            .WillByDefault(Return(std::shared_ptr<TwoDA>(baseItems.build())));
        _game = std::make_unique<Game>(GameID::KotOR, "", _engine.options(), _engine.services(), _console);
        _routines = std::make_unique<Routines>(GameID::KotOR, _game.get(), &_engine.services());
        _routines->init();
    }

    std::shared_ptr<Item> makeItem(uint32_t addCost, int stackSize = 1, bool plot = false) {
        auto uti = Gff::Builder()
                       .field(Gff::Field::newInt("BaseItem", kLightsaber))
                       .field(Gff::Field::newDword("AddCost", addCost))
                       .field(Gff::Field::newWord("StackSize", stackSize))
                       .field(Gff::Field::newByte("Plot", plot ? 1 : 0))
                       .build();
        return _game->newItem(*uti, SerializedIdentityContext::templateResource("g1_w_lghtsbr01"));
    }

    Variable call(const std::string &name, std::vector<Variable> args) {
        Routine &routine = _routines->get(_routines->getIndexByName(name));
        ExecutionContext execution;
        execution.routines = _routines.get();
        return routine.invoke(args, execution);
    }

    int value(const Object &object) {
        return call("GetGoldPieceValue", {Variable::ofObject(object.id())}).intValue;
    }

    TestEngine _engine;
    StubConsole _console;
    std::unique_ptr<Game> _game;
    std::unique_ptr<Routines> _routines;
};

} // namespace

// 311: an object that is not a valid item is worth nothing.
TEST_F(GoldPieceValue, only_an_item_has_a_value) {
    auto creature = _game->newCreature();
    auto item = makeItem(900);

    EXPECT_EQ(0, call("GetGoldPieceValue", {Variable::ofObject(kObjectInvalid)}).intValue);
    EXPECT_EQ(0, value(*creature));
    EXPECT_EQ(900, value(*item));
}

TEST_F(GoldPieceValue, plot_item_is_worth_nothing) {
    auto item = makeItem(900, 1, true);
    EXPECT_EQ(0, value(*item));
    item->setPlotFlag(false);
    EXPECT_EQ(900, value(*item));
}

TEST_F(GoldPieceValue, item_without_added_cost_is_worth_one) {
    EXPECT_EQ(1, value(*makeItem(0)));
}

TEST_F(GoldPieceValue, stack_is_worth_every_item_in_it) {
    EXPECT_EQ(250, value(*makeItem(25, 10)));
}

TEST_F(GoldPieceValue, changed_item_cost_scales_the_value) {
    auto item = makeItem(900);
    std::shared_ptr<Gff> uti(Gff::Builder().field(Gff::Field::newInt("BaseItem", kLightsaber)).build());
    ON_CALL(_engine.resourceModule().gffs(), get("g1_w_lghtsbr02", ResType::Uti)).WillByDefault(Return(uti));
    call("ChangeItemCost", {Variable::ofString("g1_w_lghtsbr02"), Variable::ofFloat(0.5f)});

    EXPECT_EQ(450, value(*item));
}

// The value a script reads is the one the item is saved with.
TEST_F(GoldPieceValue, single_item_value_is_its_saved_cost) {
    auto item = makeItem(1234);
    _game->setBaseItemCostMultiplier(kLightsaber, 1.5f);
    EXPECT_EQ(1851, value(*item));
    EXPECT_EQ(item->cost(_game->baseItemCostMultiplier(kLightsaber)), value(*item));
}

TEST_F(GoldPieceValue, value_beyond_a_script_integer_is_capped) {
    EXPECT_EQ(std::numeric_limits<int>::max(), value(*makeItem(2000000000u, 100)));
    EXPECT_EQ(std::numeric_limits<int>::max(), value(*makeItem(3000000000u)));
}

TEST_F(GoldPieceValue, multiplier_beyond_a_script_integer_is_capped) {
    auto item = makeItem(900);
    _game->setBaseItemCostMultiplier(kLightsaber, 1e10f);
    EXPECT_EQ(std::numeric_limits<int>::max(), value(*item));
    _game->setBaseItemCostMultiplier(kLightsaber, std::numeric_limits<float>::infinity());
    EXPECT_EQ(std::numeric_limits<int>::max(), value(*item));
}

TEST_F(GoldPieceValue, unusable_multiplier_leaves_the_least_value) {
    auto item = makeItem(900);
    _game->setBaseItemCostMultiplier(kLightsaber, std::numeric_limits<float>::quiet_NaN());
    EXPECT_EQ(1, value(*item));
    _game->setBaseItemCostMultiplier(kLightsaber, -2.0f);
    EXPECT_EQ(1, value(*item));
}
