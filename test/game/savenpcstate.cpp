/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"

#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/party.h"
#include "reone/game/script/routines.h"
#include "reone/resource/format/gffreader.h"
#include "reone/resource/saveworkingstate.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"
#include "reone/system/stream/memoryinput.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;
using namespace testing;

namespace {

/**
 * SaveNPCState persists one roster companion over its availnpc record. It is a
 * write and nothing else: retail bounds the roster, treats an empty slot as
 * nothing to save, and leaves membership, availability, control and placement
 * untouched. The record it writes is the same one a save writes for that slot,
 * so a later spawn reads back the companion as it is now rather than as its
 * blueprint describes it.
 */
struct SaveNpcStateFixture : TestWithParam<GameID> {
    void SetUp() override {
        game = std::make_unique<Game>(
            GetParam(), "", engine.options(), engine.services(), console);
        routines = std::make_unique<Routines>(
            GetParam(), game.get(), &engine.services());
        routines->init();

        auto &director = engine.resourceModule().director();
        EXPECT_CALL(director, committedSaveWorkingState())
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([this]() { return committed; }));
        EXPECT_CALL(director, adoptSaveWorkingState(_))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([this](auto state) {
                ++adoptions;
                committed = std::move(state);
            }));
    }

    /** Put a companion in roster slot `npc`, as party selection would. */
    std::shared_ptr<Creature> rosterCompanion(int npc) {
        auto creature = game->newCreature();
        game->party().addAvailableMember(npc, creature);
        return creature;
    }

    /** Call the routine the way a compiled script calls it. */
    void callSaveNpcState(int npc) {
        Routine &routine = routines->get(routines->getIndexByName("SaveNPCState"));
        ExecutionContext execution;
        execution.routines = routines.get();
        routine.invoke({Variable::ofInt(npc)}, execution);
    }

    bool hasRecord(int npc) const {
        return committed->contains(
            {"availnpc" + std::to_string(npc), ResType::Utc});
    }

    std::shared_ptr<Gff> record(int npc) const {
        auto resource = committed->find(
            {"availnpc" + std::to_string(npc), ResType::Utc});
        EXPECT_TRUE(resource) << "no availnpc" << npc << " record was written";
        auto stream = MemoryInputStream(resource->data);
        auto reader = GffReader(stream);
        reader.load();
        return reader.root();
    }

    TestEngine &engine {testEngine()};
    StubConsole console;
    std::unique_ptr<Game> game;
    std::unique_ptr<Routines> routines;
    std::shared_ptr<const SaveWorkingState> committed {
        std::make_shared<const SaveWorkingState>()};
    int adoptions {0};
};

} // namespace

TEST_P(SaveNpcStateFixture, routine_is_registered_with_the_retail_signature) {
    int index = routines->getIndexByName("SaveNPCState");
    EXPECT_EQ(index, 734);
    Routine &routine = routines->get(index);
    EXPECT_EQ(routine.getArgumentCount(), 1);
}

TEST_P(SaveNpcStateFixture, saving_a_present_companion_writes_its_roster_record) {
    rosterCompanion(3);

    callSaveNpcState(3);

    EXPECT_TRUE(hasRecord(3));
    EXPECT_EQ(record(3)->signature(), std::optional<std::string>("UTC V3.2"));
}

// The point of the routine: what lands is the companion as it stands now, not
// what its blueprint says.
TEST_P(SaveNpcStateFixture, the_record_holds_current_state_not_template_state) {
    auto companion = rosterCompanion(2);
    companion->setMaxHitPoints(40);
    companion->setCurrentHitPoints(17);
    companion->setXP(4321);

    callSaveNpcState(2);

    auto utc = record(2);
    EXPECT_EQ(utc->getInt("CurrentHitPoints"), 17);
    EXPECT_EQ(utc->getInt("MaxHitPoints"), 40);
    EXPECT_EQ(utc->getInt("Experience"), 4321);
}

// Saving again after the companion changes must replace the record, not keep
// the first one.
TEST_P(SaveNpcStateFixture, saving_again_replaces_the_earlier_record) {
    auto companion = rosterCompanion(1);
    companion->setMaxHitPoints(40);
    companion->setCurrentHitPoints(30);
    callSaveNpcState(1);
    ASSERT_EQ(record(1)->getInt("CurrentHitPoints"), 30);

    companion->setCurrentHitPoints(9);
    callSaveNpcState(1);

    EXPECT_EQ(record(1)->getInt("CurrentHitPoints"), 9);
}

TEST_P(SaveNpcStateFixture, an_empty_roster_slot_is_silently_nothing_to_save) {
    EXPECT_NO_THROW(callSaveNpcState(4));

    EXPECT_FALSE(hasRecord(4));
    EXPECT_EQ(adoptions, 0) << "an empty slot must not touch the working state";
}

TEST_P(SaveNpcStateFixture, an_out_of_range_index_is_silently_ignored) {
    rosterCompanion(0);

    EXPECT_NO_THROW(callSaveNpcState(-1));
    EXPECT_NO_THROW(callSaveNpcState(12));
    EXPECT_NO_THROW(callSaveNpcState(9999));

    EXPECT_EQ(adoptions, 0);
    EXPECT_FALSE(hasRecord(0));
}

TEST_P(SaveNpcStateFixture, each_roster_slot_owns_a_distinct_record) {
    auto first = rosterCompanion(0);
    first->setMaxHitPoints(40);
    first->setCurrentHitPoints(11);
    auto second = rosterCompanion(5);
    second->setMaxHitPoints(40);
    second->setCurrentHitPoints(22);

    callSaveNpcState(0);
    callSaveNpcState(5);

    EXPECT_EQ(record(0)->getInt("CurrentHitPoints"), 11);
    EXPECT_EQ(record(5)->getInt("CurrentHitPoints"), 22);
}

// It persists; it does not rearrange the party.
TEST_P(SaveNpcStateFixture, it_changes_nothing_about_the_party) {
    auto player = game->newCreature();
    game->party().addMember(kNpcPlayer, player);
    game->party().setPlayer(player);
    auto companion = rosterCompanion(0);
    game->party().addMember(0, companion);

    auto sizeBefore = game->party().getSize();
    auto leaderBefore = game->party().getLeader();
    auto availableBefore = game->party().isMemberAvailable(0);

    callSaveNpcState(0);

    EXPECT_EQ(game->party().getSize(), sizeBefore);
    EXPECT_EQ(game->party().getLeader(), leaderBefore);
    EXPECT_EQ(game->party().isMemberAvailable(0), availableBefore);
    EXPECT_EQ(game->party().getAvailableMember(0), companion)
        << "the roster creature itself must be left alone";
}

INSTANTIATE_TEST_SUITE_P(
    BothGames,
    SaveNpcStateFixture,
    ::testing::Values(GameID::KotOR, GameID::TSL),
    [](const ::testing::TestParamInfo<GameID> &info) {
        return info.param == GameID::TSL ? "TSL" : "KotOR";
    });
