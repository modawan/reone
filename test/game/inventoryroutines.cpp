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

#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/placeable.h"
#include "reone/game/object/store.h"
#include "reone/game/object/waypoint.h"
#include "reone/game/script/routines.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;

namespace {

class InventoryRoutines : boost::noncopyable {
public:
    InventoryRoutines() :
        _game(GameID::KotOR, "", testEngine().options(), testEngine().services(), _console),
        _routines(GameID::KotOR, &_game, &testEngine().services()) {

        _routines.init();
    }

    int callInt(const std::string &name, std::vector<Variable> args = {}) {
        Routine &routine = _routines.get(_routines.getIndexByName(name));
        ExecutionContext execution;
        execution.routines = &_routines;
        return routine.invoke(args, execution).intValue;
    }

    Game &game() { return _game; }

private:
    StubConsole _console;
    Game _game;
    Routines _routines;
};

} // namespace

// 570: TRUE for creatures and stores; for items and placeables, whether they
// are containers; FALSE otherwise.
TEST(InventoryRoutines, has_inventory_for_creatures_and_stores) {
    InventoryRoutines routines;
    auto creature = routines.game().newCreature();
    auto store = routines.game().newStore();

    EXPECT_EQ(1, routines.callInt("GetHasInventory", {Variable::ofObject(creature->id())}));
    EXPECT_EQ(1, routines.callInt("GetHasInventory", {Variable::ofObject(store->id())}));
}

TEST(InventoryRoutines, has_inventory_not_for_plain_items_placeables_or_waypoints) {
    InventoryRoutines routines;
    auto item = routines.game().newItem();
    auto placeable = routines.game().newPlaceable();
    auto waypoint = routines.game().newWaypoint();
    ASSERT_FALSE(item->isContainer());
    ASSERT_FALSE(placeable->hasInventory());

    EXPECT_EQ(0, routines.callInt("GetHasInventory", {Variable::ofObject(item->id())}));
    EXPECT_EQ(0, routines.callInt("GetHasInventory", {Variable::ofObject(placeable->id())}));
    EXPECT_EQ(0, routines.callInt("GetHasInventory", {Variable::ofObject(waypoint->id())}));
    EXPECT_EQ(0, routines.callInt("GetHasInventory", {Variable::ofObject(kObjectInvalid)}));
}
