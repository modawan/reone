/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <gtest/gtest.h>

#include "../fixtures/engine.h"

#include "reone/game/game.h"
#include "reone/game/minigame.h"
#include "reone/game/swooprace.h"
#include "reone/graphics/animation.h"
#include "reone/graphics/model.h"
#include "reone/graphics/modelnode.h"
#include "reone/graphics/types.h"
#include "reone/input/event.h"

using namespace reone;
using namespace reone::game;
using namespace reone::graphics;

namespace {

constexpr float kEpsilon = 1e-3f;

// Where the track model is placed in the area (its LYT position).
const glm::vec3 kPlacement(1000.0f, 2000.0f, 10.0f);

// The hook's parent sits 1 unit up in the model; the hook rests 5 units along
// X from it, turned 30 degrees.
const glm::vec3 kHookParentRest(0.0f, 0.0f, 1.0f);
const glm::vec3 kHookRest(5.0f, 0.0f, 0.0f);
const glm::quat kHookRestOrientation(glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 0.0f, 1.0f)));

// The "track" animation: 4 seconds, the hook moving from 10 units behind its
// rest position to 90 ahead, then 50 to the side while turning 90 degrees.
constexpr float kTrackLength = 4.0f;

glm::quat yaw(float degrees) {
    return glm::angleAxis(glm::radians(degrees), glm::vec3(0.0f, 0.0f, 1.0f));
}

// A track model built the way the MDL reader builds one: a root, the hook's
// parent and the "modelhook" node, plus a "track" animation with position and
// orientation keys on the hook. withAnimation false leaves the animation out.
std::shared_ptr<Model> makeTrackModel(bool withAnimation = true, bool withHook = true) {
    auto root = std::make_shared<ModelNode>(0, "trk_root", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
    auto base = std::make_shared<ModelNode>(1, "trk_base", kHookParentRest, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, root.get());
    root->addChild(base);
    if (withHook) {
        auto hook = std::make_shared<ModelNode>(2, "modelhook", kHookRest, kHookRestOrientation, true, base.get());
        base->addChild(hook);
    }

    std::vector<std::shared_ptr<Animation>> animations;
    if (withAnimation) {
        auto animRoot = std::make_shared<ModelNode>(0, "trk_root", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
        auto animHook = std::make_shared<ModelNode>(2, "modelhook", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, animRoot.get());
        // Keys added out of order: the track sorts them.
        auto &positions = animHook->vectorTracks()[ControllerTypes::position];
        positions.add(2.0f, glm::vec3(0.0f, 90.0f, 0.0f));
        positions.add(0.0f, glm::vec3(0.0f, -10.0f, 0.0f));
        positions.add(4.0f, glm::vec3(50.0f, 90.0f, 0.0f));
        auto &orientations = animHook->quaternionTracks()[ControllerTypes::orientation];
        orientations.add(0.0f, yaw(0.0f));
        orientations.add(2.0f, yaw(0.0f));
        orientations.add(4.0f, yaw(90.0f));
        animRoot->addChild(animHook);
        animations.push_back(std::make_shared<Animation>("track", kTrackLength, 0.0f, "trk_root", animRoot, std::vector<Animation::Event>()));
    }
    return std::make_shared<Model>("trk_test", 0, root, animations, "", 1.0f);
}

// World position of the hook at rest, with the animation's position offset.
glm::vec3 hookWorld(const glm::vec3 &animOffset) {
    return kPlacement + kHookParentRest + kHookRest + animOffset;
}

glm::vec3 translationOf(const glm::mat4 &m) {
    return glm::vec3(m[3]);
}

glm::vec3 forwardOf(const glm::mat4 &m) {
    return glm::normalize(glm::mat3(m) * glm::vec3(0.0f, 1.0f, 0.0f));
}

void expectNear(const glm::vec3 &expected, const glm::vec3 &actual) {
    EXPECT_NEAR(expected.x, actual.x, kEpsilon);
    EXPECT_NEAR(expected.y, actual.y, kEpsilon);
    EXPECT_NEAR(expected.z, actual.z, kEpsilon);
}

glm::vec3 yawForward(float degrees) {
    return glm::vec3(-glm::sin(glm::radians(degrees)), glm::cos(glm::radians(degrees)), 0.0f);
}

MinigameSpec makeSwoopSpec(int numLoops) {
    MinigameSpec spec;
    spec.type = MinigameType::SwoopRace;
    spec.movementPerSec = 100.0f;
    spec.lateralAccel = 300.0f;
    spec.player.numLoops = numLoops;
    return spec;
}

// The start frame openSwoopRace derives from the hook's rest pose.
const glm::vec3 kRestStart(hookWorld(glm::vec3(0.0f)));
constexpr float kRestFacing = 0.5235988f; // 30 degrees

struct SwoopRaceFixture : testing::Test {
    TestEngine &engine {testEngine()};
    StubConsole console;
    Game game {resource::GameID::KotOR, "", engine.options(), engine.services(), console};
    SwoopRace race {game};

    void startOnTrack(int numLoops) {
        auto track = buildMinigameTrack(*makeTrackModel(), kPlacement);
        ASSERT_TRUE(track.has_value());
        race.start(makeSwoopSpec(numLoops), nullptr, nullptr, {}, kRestStart, kRestFacing, 0.0f, std::move(track));
    }
};

} // namespace

TEST(MinigameTrack, the_hook_follows_the_track_animation_keys) {
    auto track = buildMinigameTrack(*makeTrackModel(), kPlacement);

    ASSERT_TRUE(track.has_value());
    EXPECT_FLOAT_EQ(track->length(), kTrackLength);

    // On a key: the key's offset from the rest position, the key's orientation
    // in place of the rest orientation.
    expectNear(hookWorld(glm::vec3(0.0f, -10.0f, 0.0f)), translationOf(track->hookAt(0.0f)));
    expectNear(yawForward(0.0f), forwardOf(track->hookAt(0.0f)));
    expectNear(hookWorld(glm::vec3(0.0f, 90.0f, 0.0f)), translationOf(track->hookAt(2.0f)));

    // Between keys: positions mix and orientations slerp.
    expectNear(hookWorld(glm::vec3(0.0f, 40.0f, 0.0f)), translationOf(track->hookAt(1.0f)));
    expectNear(hookWorld(glm::vec3(25.0f, 90.0f, 0.0f)), translationOf(track->hookAt(3.0f)));
    expectNear(yawForward(45.0f), forwardOf(track->hookAt(3.0f)));
}

TEST(MinigameTrack, the_hook_holds_the_last_key_at_and_past_the_end) {
    auto track = buildMinigameTrack(*makeTrackModel(), kPlacement);

    ASSERT_TRUE(track.has_value());
    for (float time : {4.0f, 4.5f, 10.0f}) {
        expectNear(hookWorld(glm::vec3(50.0f, 90.0f, 0.0f)), translationOf(track->hookAt(time)));
        expectNear(yawForward(90.0f), forwardOf(track->hookAt(time)));
    }
    // Before the first key: the first key.
    expectNear(hookWorld(glm::vec3(0.0f, -10.0f, 0.0f)), translationOf(track->hookAt(-1.0f)));
}

TEST(MinigameTrack, a_model_without_a_track_animation_gives_a_still_hook_at_rest) {
    auto track = buildMinigameTrack(*makeTrackModel(false), kPlacement);

    ASSERT_TRUE(track.has_value());
    EXPECT_FLOAT_EQ(track->length(), 0.0f);
    expectNear(kRestStart, translationOf(track->hookAt(0.0f)));
    expectNear(kRestStart, translationOf(track->hookAt(3.0f)));
    expectNear(yawForward(30.0f), forwardOf(track->hookAt(3.0f)));
}

TEST(MinigameTrack, a_model_without_a_modelhook_gives_no_track) {
    EXPECT_FALSE(buildMinigameTrack(*makeTrackModel(true, false), kPlacement).has_value());
}

TEST_F(SwoopRaceFixture, the_bike_starts_on_the_first_track_key_not_the_rest_pose) {
    startOnTrack(0);

    EXPECT_TRUE(race.followsTrack());
    expectNear(hookWorld(glm::vec3(0.0f, -10.0f, 0.0f)), race.position());
    expectNear(yawForward(0.0f), forwardOf(race.trackHook()));
}

TEST_F(SwoopRaceFixture, the_bike_rides_the_track_at_speed_over_one_hundred) {
    startOnTrack(0);

    // The first update ramps the speed to MovementPerSec (100) over its 3
    // seconds, so the track plays at the authored rate: 3 seconds in.
    race.update(3.0f);
    EXPECT_NEAR(race.speed(), 100.0f, kEpsilon);
    EXPECT_NEAR(race.trackTime(), 3.0f, kEpsilon);
    expectNear(hookWorld(glm::vec3(25.0f, 90.0f, 0.0f)), race.position());
    expectNear(yawForward(45.0f), forwardOf(race.trackHook()));

    race.update(0.5f);
    EXPECT_NEAR(race.trackTime(), 3.5f, kEpsilon);
    expectNear(hookWorld(glm::vec3(37.5f, 90.0f, 0.0f)), race.position());
    EXPECT_FALSE(race.trackEnded());
}

TEST_F(SwoopRaceFixture, the_bike_stops_at_the_end_of_a_track_without_loops) {
    startOnTrack(0);

    race.update(3.0f);
    race.update(1.5f);

    EXPECT_TRUE(race.trackEnded());
    EXPECT_FLOAT_EQ(race.trackTime(), kTrackLength);
    expectNear(hookWorld(glm::vec3(50.0f, 90.0f, 0.0f)), race.position());

    race.update(5.0f);
    EXPECT_FLOAT_EQ(race.trackTime(), kTrackLength);
    expectNear(hookWorld(glm::vec3(50.0f, 90.0f, 0.0f)), race.position());
}

TEST_F(SwoopRaceFixture, a_track_with_negative_loops_wraps_forever) {
    startOnTrack(-1);

    race.update(3.0f);
    race.update(2.0f);

    EXPECT_FALSE(race.trackEnded());
    EXPECT_NEAR(race.trackTime(), 1.0f, kEpsilon);
    expectNear(hookWorld(glm::vec3(0.0f, 40.0f, 0.0f)), race.position());
}

TEST_F(SwoopRaceFixture, a_track_with_two_loops_plays_twice_then_stops) {
    startOnTrack(2);

    race.update(3.0f);
    race.update(2.0f); // into the second play
    EXPECT_FALSE(race.trackEnded());
    EXPECT_NEAR(race.trackTime(), 1.0f, kEpsilon);

    race.update(3.5f); // past the end of the second play
    EXPECT_TRUE(race.trackEnded());
    EXPECT_FLOAT_EQ(race.trackTime(), kTrackLength);
}

TEST_F(SwoopRaceFixture, the_bike_strafes_along_the_hook_x_axis) {
    startOnTrack(0);
    race.update(3.0f);
    race.update(1.5f); // at the end, the hook turned 90 degrees

    race.handle(input::Event::newKeyDown(input::KeyEvent(true, input::KeyCode::D, 0, false)));
    race.update(0.1f);

    ASSERT_GT(race.lateralOffset(), 0.0f);
    // The hook's X axis now points along world +Y.
    expectNear(translationOf(race.trackHook()) + glm::vec3(0.0f, race.lateralOffset(), 0.0f), race.position());
}

TEST_F(SwoopRaceFixture, a_race_without_an_animated_track_keeps_the_straight_line) {
    auto track = buildMinigameTrack(*makeTrackModel(false), kPlacement);
    race.start(makeSwoopSpec(0), nullptr, nullptr, {}, kRestStart, kRestFacing, 0.0f, std::move(track));

    race.update(3.0f);

    EXPECT_FALSE(race.followsTrack());
    expectNear(kRestStart + yawForward(30.0f) * race.progress(), race.position());
}
