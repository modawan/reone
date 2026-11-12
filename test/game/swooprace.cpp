/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <gtest/gtest.h>

#include <gmock/gmock.h>

#include "../fixtures/engine.h"

#include "reone/game/game.h"
#include "reone/game/minigame.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/graphics/animation.h"
#include "reone/graphics/model.h"
#include "reone/graphics/modelnode.h"
#include "reone/graphics/types.h"

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

// The hook's rest pose in the world.
const glm::vec3 kRestStart(hookWorld(glm::vec3(0.0f)));

class NullHost : public IMinigameHost {
public:
    void runScript(const std::string &resRef, uint32_t callerId) override {}
};

// The bike rides the player's track at speed 100, the authored rate.
struct SwoopRaceFixture : testing::Test {
    NullHost host;
    MinigameSession session {host};

    void startOnTrack(int numLoops) {
        auto track = buildMinigameTrack(*makeTrackModel(), kPlacement);
        ASSERT_TRUE(track.has_value());
        session.load(makeSwoopSpec(numLoops), std::move(*track), {}, {});
        session.setPlayerSpeed(100.0f);
        session.setPlayerMaxSpeed(100.0f);
        session.start();
    }

    const MinigameActor &bike() const { return *session.player(); }

    // Whether a track with no loops left has played to its end.
    bool trackEnded() const { return bike().trackTime >= bike().track.length(); }
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

    expectNear(hookWorld(glm::vec3(0.0f, -10.0f, 0.0f)), bike().position);
    expectNear(yawForward(0.0f), forwardOf(bike().hook));
}

TEST_F(SwoopRaceFixture, the_bike_rides_the_track_at_speed_over_one_hundred) {
    startOnTrack(0);

    session.update(3.0f, MinigameInput());
    EXPECT_NEAR(session.playerSpeed(), 100.0f, kEpsilon);
    EXPECT_NEAR(bike().trackTime, 3.0f, kEpsilon);
    expectNear(hookWorld(glm::vec3(25.0f, 90.0f, 0.0f)), bike().position);
    expectNear(yawForward(45.0f), forwardOf(bike().hook));

    session.update(0.5f, MinigameInput());
    EXPECT_NEAR(bike().trackTime, 3.5f, kEpsilon);
    expectNear(hookWorld(glm::vec3(37.5f, 90.0f, 0.0f)), bike().position);
    EXPECT_FALSE(trackEnded());
}

TEST_F(SwoopRaceFixture, the_bike_stops_at_the_end_of_a_track_without_loops) {
    startOnTrack(0);

    session.update(3.0f, MinigameInput());
    session.update(1.5f, MinigameInput());

    EXPECT_TRUE(trackEnded());
    EXPECT_FLOAT_EQ(bike().trackTime, kTrackLength);
    expectNear(hookWorld(glm::vec3(50.0f, 90.0f, 0.0f)), bike().position);

    session.update(5.0f, MinigameInput());
    EXPECT_FLOAT_EQ(bike().trackTime, kTrackLength);
    expectNear(hookWorld(glm::vec3(50.0f, 90.0f, 0.0f)), bike().position);
}

TEST_F(SwoopRaceFixture, a_track_with_negative_loops_wraps_forever) {
    startOnTrack(-1);

    session.update(3.0f, MinigameInput());
    session.update(2.0f, MinigameInput());

    EXPECT_FALSE(trackEnded());
    EXPECT_NEAR(bike().trackTime, 1.0f, kEpsilon);
    expectNear(hookWorld(glm::vec3(0.0f, 40.0f, 0.0f)), bike().position);
}

TEST_F(SwoopRaceFixture, a_track_with_two_loops_plays_twice_then_stops) {
    startOnTrack(2);

    session.update(3.0f, MinigameInput());
    session.update(2.0f, MinigameInput()); // into the second play
    EXPECT_FALSE(trackEnded());
    EXPECT_NEAR(bike().trackTime, 1.0f, kEpsilon);

    session.update(3.5f, MinigameInput()); // past the end of the second play
    EXPECT_TRUE(trackEnded());
    EXPECT_FLOAT_EQ(bike().trackTime, kTrackLength);
}

TEST_F(SwoopRaceFixture, the_bike_strafes_along_the_hook_x_axis) {
    startOnTrack(0);
    session.setTunnelPos(glm::vec3(50.0f, 0.0f, 0.0f));
    session.setTunnelNeg(glm::vec3(-50.0f, 0.0f, 0.0f));
    session.update(3.0f, MinigameInput());
    session.update(1.5f, MinigameInput()); // at the end, the hook turned 90 degrees

    MinigameInput right;
    right.steer = 1.0f;
    session.update(0.1f, right);

    ASSERT_GT(session.offset().x, 0.0f);
    // The hook's X axis now points along world +Y.
    expectNear(translationOf(bike().hook) + glm::vec3(0.0f, session.offset().x, 0.0f), bike().position);
}

TEST_F(SwoopRaceFixture, without_an_animated_track_the_bike_holds_the_hook_at_rest) {
    auto track = buildMinigameTrack(*makeTrackModel(false), kPlacement);
    ASSERT_TRUE(track.has_value());
    session.load(makeSwoopSpec(0), std::move(*track), {}, {});
    session.setPlayerSpeed(100.0f);
    session.setPlayerMaxSpeed(100.0f);

    session.update(3.0f, MinigameInput());

    expectNear(kRestStart, bike().position);
}

namespace {

// startswooprace is a developer command.
struct SwoopConsoleFixture : testing::Test {
    void SetUp() override {
        engine.options().game.developer = true;
        ON_CALL(engine.sceneModule().graphs(), get(testing::_)).WillByDefault(testing::ReturnRef(sceneGraph));
        game = std::make_unique<Game>(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
        game->initLocalServices();
        TestGameModule::initConsole(*game);
    }

    void TearDown() override {
        game.reset();
        engine.options().game.developer = developerWasEnabled;
    }

    TestEngine &engine {testEngine()};
    bool developerWasEnabled {engine.options().game.developer};
    testing::NiceMock<scene::MockSceneGraph> sceneGraph;
    StubConsole console;
    std::unique_ptr<Game> game;
};

} // namespace

TEST_F(SwoopConsoleFixture, startswooprace_needs_a_module_with_a_swoop_race) {
    ASSERT_TRUE(console.hasCommand("startswooprace"));

    console.execute("startswooprace");
    ASSERT_FALSE(console.lines.empty());
    EXPECT_EQ(console.lines.back(), "swoop: no module loaded");

    TestGameModule::configureModuleSnapshot(*game, game->newArea(), game->newCreature(), "end_m01aa", "end_m01aa");
    console.execute("startswooprace");
    EXPECT_EQ(console.lines.back(), "swoop: current area has no swoop minigame");
    EXPECT_EQ(game->minigameSession(), nullptr);
}
