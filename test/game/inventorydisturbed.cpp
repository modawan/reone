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

#include <map>
#include <optional>

#include "../fixtures/engine.h"

#include "reone/game/action/giveitem.h"
#include "reone/game/action/takeitem.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/placeable.h"
#include "reone/game/script/routines.h"
#include "reone/script/program.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

// Locals the probe script writes what it saw to, on OBJECT_SELF.
constexpr int kLocalDisturber = 40;
constexpr int kLocalItem = 41;
constexpr int kLocalType = 42;

// Probe runs, keyed by resref. Static so the stub registered on the shared
// scripts mock stays valid past the test that installed it.
std::map<std::string, int> &probeRuns() {
    static std::map<std::string, int> runs;
    return runs;
}

// A script that stores, in locals on OBJECT_SELF, whether GetLastDisturbed is
// \p disturber, whether GetInventoryDisturbItem is \p item, and what
// GetInventoryDisturbType returns.
std::shared_ptr<script::ScriptProgram> makeProbe(const Routines &routines, const std::string &resRef, uint32_t disturber, uint32_t item) {
    auto program = std::make_shared<script::ScriptProgram>(resRef);
    const int setLocalNumber = routines.getIndexByName("SetLocalNumber");
    const auto observe = [&](const std::string &routine, std::optional<uint32_t> equals, int local) {
        program->add(script::Instruction::newACTION(routines.getIndexByName(routine), 0));
        if (equals) {
            program->add(script::Instruction::newCONSTO(static_cast<int>(*equals)));
            program->add(script::Instruction(script::InstructionType::EQUALOO));
        }
        // SetLocalNumber(OBJECT_SELF, local, value): the first argument on top.
        program->add(script::Instruction::newCONSTI(local));
        program->add(script::Instruction::newCONSTO(static_cast<int>(script::kObjectSelf)));
        program->add(script::Instruction::newACTION(setLocalNumber, 3));
    };
    observe("GetLastDisturbed", disturber, kLocalDisturber);
    observe("GetInventoryDisturbItem", item, kLocalItem);
    observe("GetInventoryDisturbType", std::nullopt, kLocalType);
    program->add(script::Instruction(script::InstructionType::RETN));
    return program;
}

struct DisturbedFixture {
    TestEngine &engine;
    StubConsole console;
    Game game;
    Routines routines;
    std::shared_ptr<Area> area;

    DisturbedFixture() :
        engine(testEngine()),
        game(GameID::KotOR, "", engine.options(), engine.services(), console),
        routines(GameID::KotOR, &game, &engine.services()) {

        game.initLocalServices();
        routines.init();
        area = game.newArea();
        TestGameModule::setActiveModuleArea(game, area);
    }

    std::shared_ptr<Creature> addCreature() {
        auto creature = game.newCreature();
        creature->setPosition(glm::vec3(0.0f));
        return creature;
    }

    std::shared_ptr<Item> addItemTo(Object &owner) {
        auto item = game.newItem();
        item->setTag("disturbed_item");
        owner.addItem(item);
        return item;
    }

    // \p resRef runs the probe; its runs are counted.
    void probe(const std::string &resRef, uint32_t disturber, uint32_t item) {
        probeRuns()[resRef] = 0;
        auto program = makeProbe(routines, resRef, disturber, item);
        EXPECT_CALL(engine.resourceModule().scripts(), get(resRef))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([program](const std::string &key) {
                ++probeRuns()[key];
                return program;
            }));
    }

    // \p resRef resolves to nothing; its runs are counted.
    void count(const std::string &resRef) {
        probeRuns()[resRef] = 0;
        EXPECT_CALL(engine.resourceModule().scripts(), get(resRef))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([](const std::string &key) {
                ++probeRuns()[key];
                return std::shared_ptr<script::ScriptProgram>();
            }));
    }
};

} // namespace

TEST(InventoryDisturbed, give_item_runs_the_receiving_creatures_disturbed_script_as_added) {
    DisturbedFixture fixture;
    // A placeable gives, so the give needs no walk to the receiver.
    auto giver = fixture.game.newPlaceable();
    auto receiver = fixture.addCreature();
    auto item = fixture.addItemTo(*giver);
    receiver->setOnDisturbed("disturbed_give");
    fixture.probe("disturbed_give", giver->id(), item->id());

    auto action = fixture.game.newAction<GiveItemAction>(item, receiver, GiveItemAction::kWholeStack, false);
    action->execute(action, *giver, 0.0f);

    ASSERT_EQ(receiver->id(), item->owner());
    EXPECT_EQ(1, probeRuns()["disturbed_give"]);
    EXPECT_EQ(1, receiver->getLocalNumber(kLocalDisturber));
    EXPECT_EQ(1, receiver->getLocalNumber(kLocalItem));
    EXPECT_EQ(static_cast<int>(InventoryDisturbType::Added), receiver->getLocalNumber(kLocalType));
}

TEST(InventoryDisturbed, a_creature_giving_to_itself_is_not_disturbed) {
    DisturbedFixture fixture;
    auto owner = fixture.addCreature();
    auto item = fixture.addItemTo(*owner);
    owner->setOnDisturbed("disturbed_self");
    fixture.count("disturbed_self");

    auto action = fixture.game.newAction<GiveItemAction>(item, owner, GiveItemAction::kWholeStack, false);
    action->execute(action, *fixture.game.newPlaceable(), 0.0f);

    EXPECT_EQ(owner->id(), item->owner());
    EXPECT_EQ(0, probeRuns()["disturbed_self"]);
}

TEST(InventoryDisturbed, take_item_runs_the_creature_taken_froms_disturbed_script_as_removed) {
    DisturbedFixture fixture;
    auto owner = fixture.addCreature();
    auto taker = fixture.addCreature();
    auto item = fixture.addItemTo(*owner);
    owner->setOnDisturbed("disturbed_take");
    fixture.probe("disturbed_take", taker->id(), item->id());

    auto action = fixture.game.newAction<TakeItemAction>(item, owner);
    action->execute(action, *taker, 0.0f);

    ASSERT_EQ(taker->id(), item->owner());
    EXPECT_EQ(1, probeRuns()["disturbed_take"]);
    EXPECT_EQ(1, owner->getLocalNumber(kLocalDisturber));
    EXPECT_EQ(1, owner->getLocalNumber(kLocalItem));
    EXPECT_EQ(static_cast<int>(InventoryDisturbType::Removed), owner->getLocalNumber(kLocalType));
}
