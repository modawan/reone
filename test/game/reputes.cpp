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
#include "reone/game/reputes.h"
#include "reone/game/script/routines.h"
#include "reone/resource/2da.h"
#include "reone/script/executioncontext.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

// Synthetic factions. The relationships below are deliberately asymmetric so a
// query that reverses source and target reads a different cell.
constexpr Faction kBeta = static_cast<Faction>(1);
constexpr Faction kGamma = static_cast<Faction>(2);

// beta regards gamma as hostile, while gamma regards beta as friendly.
std::shared_ptr<TwoDA> makeReputeTable() {
    std::vector<std::string> columns {"label", "alpha", "beta", "gamma", "delta"};
    std::vector<TwoDA::Row> rows {
        TwoDA::newRow("0", {"alpha", "50", "50", "50", "50"}),
        TwoDA::newRow("1", {"beta", "50", "100", "0", "50"}),
        TwoDA::newRow("2", {"gamma", "50", "100", "100", "50"}),
        TwoDA::newRow("3", {"delta", "50", "50", "50", "50"})};
    return std::make_shared<TwoDA>(std::move(columns), std::move(rows));
}

std::unique_ptr<Reputes> makeReputes(TestEngine &engine) {
    EXPECT_CALL(engine.resourceModule().twoDas(), get("repute"))
        .WillOnce(Return(makeReputeTable()));
    auto reputes = std::make_unique<Reputes>(engine.services().resource.twoDas);
    reputes->init();
    return reputes;
}

} // namespace

TEST(Reputes, authored_directed_values_are_retained_in_both_directions) {
    TestEngine &engine = testEngine();

    auto reputes = makeReputes(engine);

    EXPECT_EQ(0, reputes->getReputation(kBeta, kGamma));
    EXPECT_EQ(100, reputes->getReputation(kGamma, kBeta));
}

TEST(Reputes, adjustment_changes_only_the_source_view_of_the_target) {
    TestEngine &engine = testEngine();
    auto reputes = makeReputes(engine);

    reputes->adjustReputation(kBeta, kGamma, 50);

    EXPECT_EQ(50, reputes->getReputation(kBeta, kGamma));
    EXPECT_EQ(100, reputes->getReputation(kGamma, kBeta));
}

TEST(Reputes, repeated_adjustments_accumulate) {
    TestEngine &engine = testEngine();
    auto reputes = makeReputes(engine);

    reputes->adjustReputation(kBeta, kGamma, 20);
    reputes->adjustReputation(kBeta, kGamma, 20);

    EXPECT_EQ(40, reputes->getReputation(kBeta, kGamma));
}

TEST(Reputes, adjustments_clamp_to_the_representable_range) {
    TestEngine &engine = testEngine();
    auto reputes = makeReputes(engine);

    reputes->adjustReputation(kBeta, kGamma, 1000);
    EXPECT_EQ(100, reputes->getReputation(kBeta, kGamma));

    reputes->adjustReputation(kBeta, kGamma, -1000);
    EXPECT_EQ(0, reputes->getReputation(kBeta, kGamma));
}

TEST(Reputes, a_positive_adjustment_lifts_hostility_and_a_negative_one_restores_it) {
    TestEngine &engine = testEngine();
    auto reputes = makeReputes(engine);

    // This is the disguise shape: the source faction stops treating the target
    // as an enemy, then treats it as one again once the disguise is dropped.
    EXPECT_TRUE(reputes->getIsEnemy(kBeta, kGamma));

    reputes->adjustReputation(kBeta, kGamma, 50);
    EXPECT_FALSE(reputes->getIsEnemy(kBeta, kGamma));

    reputes->adjustReputation(kBeta, kGamma, -50);
    EXPECT_TRUE(reputes->getIsEnemy(kBeta, kGamma));
}

TEST(Reputes, hostility_is_read_from_the_source_view_not_the_target_view) {
    TestEngine &engine = testEngine();
    auto reputes = makeReputes(engine);

    EXPECT_TRUE(reputes->getIsEnemy(kBeta, kGamma));
    EXPECT_FALSE(reputes->getIsEnemy(kGamma, kBeta));
}

TEST(Reputes, out_of_range_and_identical_factions_leave_the_matrix_untouched) {
    TestEngine &engine = testEngine();
    auto reputes = makeReputes(engine);

    reputes->adjustReputation(static_cast<Faction>(-1), kGamma, 50);
    reputes->adjustReputation(kBeta, static_cast<Faction>(99), 50);
    reputes->adjustReputation(static_cast<Faction>(99), kGamma, 50);
    reputes->adjustReputation(kBeta, kBeta, -50);

    EXPECT_EQ(0, reputes->getReputation(kBeta, kGamma));
    EXPECT_EQ(100, reputes->getReputation(kGamma, kBeta));
    EXPECT_EQ(100, reputes->getReputation(kBeta, kBeta));
}

TEST(Reputes, unknown_factions_report_the_neutral_default) {
    TestEngine &engine = testEngine();
    auto reputes = makeReputes(engine);

    EXPECT_EQ(50, reputes->getReputation(static_cast<Faction>(99), kGamma));
    EXPECT_EQ(50, reputes->getReputation(kBeta, static_cast<Faction>(99)));
}

TEST(Reputes, reinitialization_restores_the_authored_matrix) {
    TestEngine &engine = testEngine();
    auto reputes = makeReputes(engine);
    reputes->adjustReputation(kBeta, kGamma, 50);
    ASSERT_EQ(50, reputes->getReputation(kBeta, kGamma));

    EXPECT_CALL(engine.resourceModule().twoDas(), get("repute"))
        .WillOnce(Return(makeReputeTable()));
    reputes->init();

    EXPECT_EQ(0, reputes->getReputation(kBeta, kGamma));
}

TEST(AdjustReputationRoutine, adjusts_the_source_faction_view_of_the_target) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto target = game.newCreature();
    target->setFaction(kGamma);
    auto sourceFactionMember = game.newCreature();
    sourceFactionMember->setFaction(kBeta);
    Routines routines(GameID::KotOR, &game, &engine.services());
    routines.init();
    script::ExecutionContext execution;

    EXPECT_CALL(static_cast<MockReputes &>(engine.services().game.reputes),
                adjustReputation(kBeta, kGamma, 50));

    routines.get(209).invoke(
        {script::Variable::ofObject(target->id()),
         script::Variable::ofObject(sourceFactionMember->id()),
         script::Variable::ofInt(50)},
        execution);
}

TEST(AdjustReputationRoutine, a_non_creature_target_is_ignored) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto target = game.newPlaceable();
    auto sourceFactionMember = game.newCreature();
    sourceFactionMember->setFaction(kBeta);
    Routines routines(GameID::KotOR, &game, &engine.services());
    routines.init();
    script::ExecutionContext execution;

    EXPECT_CALL(static_cast<MockReputes &>(engine.services().game.reputes),
                adjustReputation(_, _, _))
        .Times(0);

    routines.get(209).invoke(
        {script::Variable::ofObject(target->id()),
         script::Variable::ofObject(sourceFactionMember->id()),
         script::Variable::ofInt(50)},
        execution);
}

TEST(DirectedDispositionRoutines, query_the_source_view_of_the_target) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto target = game.newCreature();
    target->setFaction(kGamma);
    auto source = game.newCreature();
    source->setFaction(kBeta);
    Routines routines(GameID::KotOR, &game, &engine.services());
    routines.init();
    script::ExecutionContext execution;
    auto &reputes = static_cast<MockReputes &>(engine.services().game.reputes);

    // GetIsEnemy(oTarget, oSource) asks whether oSource's faction regards
    // oTarget as an enemy, so oSource is the source of the query.
    EXPECT_CALL(reputes, getIsEnemy(Ref(*source), Ref(*target))).WillOnce(Return(true));
    EXPECT_CALL(reputes, getIsFriend(Ref(*source), Ref(*target))).WillOnce(Return(false));
    EXPECT_CALL(reputes, getIsNeutral(Ref(*source), Ref(*target))).WillOnce(Return(false));

    std::vector<script::Variable> args {
        script::Variable::ofObject(target->id()),
        script::Variable::ofObject(source->id())};

    EXPECT_EQ(1, routines.get(235).invoke(args, execution).intValue);
    EXPECT_EQ(0, routines.get(236).invoke(args, execution).intValue);
    EXPECT_EQ(0, routines.get(237).invoke(args, execution).intValue);
}
