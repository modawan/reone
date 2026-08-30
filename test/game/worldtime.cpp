/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <algorithm>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"

#include "reone/game/action/movetolocation.h"
#include "reone/game/effect.h"
#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/object/creature.h"
#include "reone/resource/gff.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

void reone::game::TestGameModule::advanceWorldTime(Game &game, float dt) {
    game.advanceWorldTime(dt);
}

void reone::game::TestGameModule::prepareWorldTimeFromIfo(Game &game, const Gff &ifo) {
    game.prepareSavedRuntimeNamespace(
        ifo, SerializedIdentityContext::moduleGraph("test-module"));
}

void reone::game::TestGameModule::restoreAutosaveWorldTime(
    Game &game,
    const Gff &moduleIfo,
    uint32_t pauseDay,
    uint32_t pauseTime) {
    game.restoreWorldTime(moduleIfo, pauseDay, pauseTime);
}

namespace {

// The world clock is part of the shared Game contract, so both games must
// agree. Every shipped K2 module and 113 of 117 K1 modules carry
// Mod_MinPerHour = 2, so both were affected identically; the parameters below
// pin that neither game lets Mod_MinPerHour change the rate of the clock.
struct WorldTimeFixture : TestWithParam<GameID> {
    WorldTimeFixture() :
        game(GetParam(), "", engine.options(), engine.services(), console) {
    }

    void advance(float seconds, float step = 1.0f) {
        for (float elapsed = 0.0f; elapsed < seconds; elapsed += step) {
            TestGameModule::advanceWorldTime(game, std::min(step, seconds - elapsed));
        }
    }

    uint64_t absoluteWorldTime() const { return game.worldTimeMilliseconds(); }

    TestEngine &engine {testEngine()};
    StubConsole console;
    Game game;
};

} // namespace

// The canonical runtime clock counts absolute world/simulation milliseconds.
// One second of simulation is a thousand of them. Mod_MinPerHour shortens the
// day; it must not change the rate at which the clock advances. Before this was
// fixed the clock ran at 60 / Mod_MinPerHour times simulation time, so every
// duration measured against it expired 12x-30x early. Day and time of day are
// derived views over that clock, and are split out only when saving.

TEST_P(WorldTimeFixture, advances_one_millisecond_per_millisecond_of_simulation) {
    for (uint8_t minutesPerHour : {1, 2, 5, 60}) {
        TestGameModule::setSnapshotWorldTime(game, 0, 0, minutesPerHour);
        TestGameModule::advanceWorldTime(game, 0.001f);
        EXPECT_EQ(absoluteWorldTime(), 1u)
            << "1 ms of simulation dt must advance the clock by 1 ms at Mod_MinPerHour="
            << static_cast<int>(minutesPerHour);
    }
}

TEST_P(WorldTimeFixture, advances_at_real_time_rate_regardless_of_minutes_per_hour) {
    for (uint8_t minutesPerHour : {1, 2, 5, 60}) {
        TestGameModule::setSnapshotWorldTime(game, 0, 0, minutesPerHour);
        advance(1.0f);
        EXPECT_EQ(absoluteWorldTime(), 1000u)
            << "one real second must be 1000 world milliseconds at Mod_MinPerHour="
            << static_cast<int>(minutesPerHour);
    }
}

TEST_P(WorldTimeFixture, changing_the_day_length_does_not_move_the_canonical_clock) {
    // Retail K1 ships danm14aa at Mod_MinPerHour=1 while 113 other modules use
    // 2, so the calendar scale genuinely differs between modules. Rescaling the
    // calendar must reinterpret the elapsed clock, never displace it.
    TestGameModule::setSnapshotWorldTime(game, 0, 0, 2);
    advance(600.0f, 60.0f);
    const uint64_t elapsed = absoluteWorldTime();
    ASSERT_EQ(elapsed, 600u * 1000u);

    for (uint8_t minutesPerHour : {1, 5, 60}) {
        TestGameModule::setSnapshotMinutesPerHour(game, minutesPerHour);
        EXPECT_EQ(absoluteWorldTime(), elapsed)
            << "absolute world time must survive a day-length change to "
            << static_cast<int>(minutesPerHour);
        // The derived calendar view is free to change, but must stay coherent.
        EXPECT_LT(game.worldTimeOfDay(), game.millisecondsPerWorldDay());
        EXPECT_EQ(static_cast<uint64_t>(game.worldTimeDay()) *
                          game.millisecondsPerWorldDay() +
                      game.worldTimeOfDay(),
                  elapsed);
    }
}

TEST_P(WorldTimeFixture, derives_the_calendar_pair_from_the_canonical_clock) {
    TestGameModule::setSnapshotWorldTime(game, 0, 0, 2);
    const uint32_t millisecondsPerDay = game.millisecondsPerWorldDay();

    // Two and a half days in.
    advance(2.5f * static_cast<float>(millisecondsPerDay) / 1000.0f, 60.0f);
    EXPECT_EQ(game.worldTimeDay(), 2u);
    EXPECT_NEAR(static_cast<double>(game.worldTimeOfDay()),
                millisecondsPerDay / 2.0, 1000.0);
}

TEST_P(WorldTimeFixture, accumulates_sub_millisecond_remainders_without_drift) {
    TestGameModule::setSnapshotWorldTime(game, 0, 0, 2);
    for (int i = 0; i < 1000; ++i) {
        TestGameModule::advanceWorldTime(game, 1.0f / 3000.0f);
    }
    // 1000 steps of 1/3000 s is 1/3 s; allow one millisecond of rounding.
    EXPECT_NEAR(static_cast<double>(absoluteWorldTime()), 333.0, 1.0);
}

TEST_P(WorldTimeFixture, day_length_follows_minutes_per_hour) {
    // CWorldTimer::SetMinutesPerHour: m_nMillisecondsInDay =
    // MinutesPerHour * 60 * MILLISECONDS_IN_SECOND * HOURS_IN_DAY.
    TestGameModule::setSnapshotWorldTime(game, 0, 0, 2);
    EXPECT_EQ(game.millisecondsPerWorldDay(), 2u * 60u * 1000u * 24u);

    TestGameModule::setSnapshotWorldTime(game, 0, 0, 5);
    EXPECT_EQ(game.millisecondsPerWorldDay(), 5u * 60u * 1000u * 24u);
}

TEST_P(WorldTimeFixture, rolls_the_calendar_after_one_game_day_of_real_time) {
    TestGameModule::setSnapshotWorldTime(game, 0, 0, 2);
    // A game day at Mod_MinPerHour=2 is 2 * 60 * 24 = 2880 real seconds.
    advance(2879.0f, 60.0f);
    EXPECT_EQ(game.worldTimeDay(), 0u);

    advance(2.0f);
    EXPECT_EQ(game.worldTimeDay(), 1u);
    EXPECT_LT(game.worldTimeOfDay(), game.millisecondsPerWorldDay());
}

TEST_P(WorldTimeFixture, forced_move_timeout_expires_after_the_scripted_real_duration) {
    TestGameModule::setSnapshotWorldTime(game, 0, 0, 2);
    auto actor = game.newCreature();
    actor->setPosition({0.0f, 0.0f, 0.0f});
    // No module or scene graph in this fixture, so keep the action off the
    // navigation path. The forced-timeout branch runs before navigateTo and is
    // what this test is about.
    actor->setMovementRestricted(true);

    auto destination = std::make_shared<Location>(glm::vec3 {100.0f, 0.0f, 0.0f}, 0.0f);
    auto action = game.newAction<MoveToLocationAction>(destination, false, true, 30.0f);

    // Arm the deadline, then hold the clock just short of the timeout.
    action->execute(action, *actor, 0.0f);
    advance(29.0f);
    action->execute(action, *actor, 0.0f);
    EXPECT_FALSE(action->isCompleted())
        << "a 30 second forced move must not expire after 29 real seconds";
    EXPECT_LT(glm::length(actor->position()), 1.0f);

    advance(2.0f);
    action->execute(action, *actor, 0.0f);
    EXPECT_TRUE(action->isCompleted());
    EXPECT_NEAR(actor->position().x, 100.0f, 0.001f);
}

TEST_P(WorldTimeFixture, temporary_effect_expiry_round_trips_as_a_real_duration) {
    for (uint8_t minutesPerHour : {2, 5}) {
        TestGameModule::setSnapshotWorldTime(game, 0, 0, minutesPerHour);

        EffectInstance instance;
        instance.effect = game.newEffect<Effect>(EffectType::Haste);
        instance.subType = 1; // DurationType::Temporary
        instance.duration = 45.0f;
        instance.remainingDuration = 45.0f;
        instance.expiryDay = 0;
        instance.expiryTime = static_cast<uint32_t>(45.0f * 1000.0f);

        auto remaining = game.remainingEffectDuration(instance);
        ASSERT_TRUE(remaining);
        EXPECT_NEAR(*remaining, 45.0f, 0.001f)
            << "expiry is stored in world milliseconds, which are real milliseconds";

        advance(20.0f);
        remaining = game.remainingEffectDuration(instance);
        ASSERT_TRUE(remaining);
        EXPECT_NEAR(*remaining, 25.0f, 0.05f);
    }
}

TEST_P(WorldTimeFixture, composes_the_canonical_clock_from_the_retail_pause_pair) {
    auto ifo = Gff::Builder().type(0xffffffff)
        .field(Gff::Field::newDword("Mod_PauseDay", 3))
        .field(Gff::Field::newDword("Mod_PauseTime", 1234u))
        .field(Gff::Field::newDword("Mod_MinPerHour", 2))
        .build();

    ASSERT_NO_THROW(TestGameModule::prepareWorldTimeFromIfo(game, *ifo));
    EXPECT_EQ(game.minutesPerHour(), 2);
    EXPECT_EQ(game.worldTimeMilliseconds(),
              3ull * (2u * 60u * 1000u * 24u) + 1234ull);
    // And the derived view reproduces exactly what was saved.
    EXPECT_EQ(game.worldTimeDay(), 3u);
    EXPECT_EQ(game.worldTimeOfDay(), 1234u);
}

TEST_P(WorldTimeFixture, out_of_range_saved_time_of_day_carries_into_the_calendar) {
    // Saves written before the day length became Mod_MinPerHour-derived hold a
    // time of day on the old fixed 24-hour scale. Carry it, as
    // CWorldTimer::GetWorldTime does, rather than rejecting the save.
    constexpr uint32_t kLegacyTimeOfDay = 24u * 60u * 60u * 1000u - 1u;
    auto ifo = Gff::Builder().type(0xffffffff)
        .field(Gff::Field::newDword("Mod_PauseDay", 3))
        .field(Gff::Field::newDword("Mod_PauseTime", kLegacyTimeOfDay))
        .field(Gff::Field::newDword("Mod_MinPerHour", 2))
        .build();

    ASSERT_NO_THROW(TestGameModule::prepareWorldTimeFromIfo(game, *ifo));
    EXPECT_EQ(game.minutesPerHour(), 2);
    // Nothing is discarded: the oversized time simply lands further along.
    EXPECT_EQ(game.worldTimeMilliseconds(),
              3ull * (2u * 60u * 1000u * 24u) + kLegacyTimeOfDay);
    // The derived view is normalized, so anything written from here on is too.
    EXPECT_LT(game.worldTimeOfDay(), game.millisecondsPerWorldDay());
    EXPECT_GT(game.worldTimeDay(), 3u);
}

TEST_P(WorldTimeFixture, calendar_pair_round_trips_across_a_day_boundary) {
    // One second before the end of a day, then two seconds of simulation.
    TestGameModule::setSnapshotWorldTime(
        game, 4, 2u * 60u * 1000u * 24u - 1000u, 2);
    advance(2.0f);
    ASSERT_EQ(game.worldTimeDay(), 5u);
    ASSERT_EQ(game.worldTimeOfDay(), 1000u);

    const uint64_t before = game.worldTimeMilliseconds();
    auto ifo = Gff::Builder().type(0xffffffff)
        .field(Gff::Field::newDword("Mod_PauseDay", game.worldTimeDay()))
        .field(Gff::Field::newDword("Mod_PauseTime", game.worldTimeOfDay()))
        .field(Gff::Field::newDword("Mod_MinPerHour", game.minutesPerHour()))
        .build();

    ASSERT_NO_THROW(TestGameModule::prepareWorldTimeFromIfo(game, *ifo));
    EXPECT_EQ(game.worldTimeMilliseconds(), before)
        << "splitting on save and composing on load must be lossless";
}

TEST_P(WorldTimeFixture, reads_legacy_reone_clock_only_when_retail_fields_are_absent) {
    auto legacy = Gff::Builder().type(0xffffffff)
        .field(Gff::Field::newDword("Mod_CalendarDay", 3))
        .field(Gff::Field::newDword("Mod_TimeOfDay", 1234u))
        .field(Gff::Field::newDword("Mod_MinPerHour", 2))
        .build();
    ASSERT_NO_THROW(TestGameModule::prepareWorldTimeFromIfo(game, *legacy));
    EXPECT_EQ(game.worldTimeMilliseconds(),
              3ull * (2u * 60u * 1000u * 24u) + 1234ull);

    auto retail = Gff::Builder().type(0xffffffff)
        .field(Gff::Field::newDword("Mod_PauseDay", 4))
        .field(Gff::Field::newDword("Mod_PauseTime", 5678u))
        .field(Gff::Field::newDword("Mod_CalendarDay", 99))
        .field(Gff::Field::newDword("Mod_TimeOfDay", 99u))
        .field(Gff::Field::newDword("Mod_MinPerHour", 2))
        .build();
    ASSERT_NO_THROW(TestGameModule::prepareWorldTimeFromIfo(game, *retail));
    EXPECT_EQ(game.worldTimeMilliseconds(),
              4ull * (2u * 60u * 1000u * 24u) + 5678ull);
}

TEST_P(WorldTimeFixture, template_autosave_uses_pause_time_from_autosave_params) {
    auto installedIfo = Gff::Builder().type(0xffffffff)
                            .field(Gff::Field::newDword(
                                "Mod_MinPerHour", 2))
                            .build();
    TestGameModule::restoreAutosaveWorldTime(
        game, *installedIfo, 7, 4321);
    EXPECT_EQ(game.minutesPerHour(), 2);
    EXPECT_EQ(game.worldTimeMilliseconds(),
              7ull * (2u * 60u * 1000u * 24u) + 4321ull);
}

INSTANTIATE_TEST_SUITE_P(
    BothGames,
    WorldTimeFixture,
    ::testing::Values(GameID::KotOR, GameID::TSL),
    [](const ::testing::TestParamInfo<GameID> &info) {
        return info.param == GameID::TSL ? "TSL" : "KotOR";
    });
