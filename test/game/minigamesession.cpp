/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

// The minigame simulation, driven through a scripted host that stands in for
// the module scripts.

#include <gtest/gtest.h>

#include <functional>
#include <map>

#include "reone/game/minigame.h"

using namespace reone;
using namespace reone::game;

namespace {

constexpr float kTick = 1.0f / 30.0f;

class ScriptedHost : public IMinigameHost {
public:
    using Script = std::function<void(uint32_t caller)>;

    std::map<std::string, Script> scripts;
    std::vector<std::pair<std::string, uint32_t>> runs;
    std::vector<std::string> animations;
    std::vector<std::string> died;

    void runScript(const std::string &resRef, uint32_t callerId) override {
        runs.emplace_back(resRef, callerId);
        auto it = scripts.find(resRef);
        if (it != scripts.end()) {
            it->second(callerId);
        }
    }
    void playMinigameAnimation(const MinigameActor &actor, const std::string &name, bool, bool, bool) override {
        animations.push_back(name);
    }
    void onMinigameActorDied(const MinigameActor &actor) override {
        died.push_back(actor.name);
    }

    size_t count(const std::string &resRef) const {
        size_t n = 0;
        for (const auto &run : runs) {
            if (run.first == resRef) {
                ++n;
            }
        }
        return n;
    }
};

MinigameFollowerScriptSpec slots(std::initializer_list<std::pair<MinigameSlot, std::string>> entries) {
    MinigameFollowerScriptSpec result;
    for (const auto &[slot, resRef] : entries) {
        switch (slot) {
        case MinigameSlot::OnCreate:
            result.onCreate = resRef;
            break;
        case MinigameSlot::OnHitFollower:
            result.onHitFollower = resRef;
            break;
        case MinigameSlot::OnHeartbeat:
            result.onHeartbeat = resRef;
            break;
        case MinigameSlot::OnDamage:
            result.onDamage = resRef;
            break;
        case MinigameSlot::OnDeath:
            result.onDeath = resRef;
            break;
        case MinigameSlot::OnFire:
            result.onFire = resRef;
            break;
        case MinigameSlot::OnHitObstacle:
            result.onHitObstacle = resRef;
            break;
        case MinigameSlot::OnAccelerate:
            result.onAccelerate = resRef;
            break;
        case MinigameSlot::OnBrake:
            result.onBrake = resRef;
            break;
        default:
            ADD_FAILURE() << "slot not used by these tests: " << minigameSlotName(slot);
            break;
        }
    }
    return result;
}

// A swoop course shaped like Taris: the hook runs 4980 units along +Y in 48 s
// from a track placed at (95, 0, 0) with the hook resting at (5, 0, 2).
MinigameTrack tarisTrack() {
    graphics::KeyframeTrack<glm::vec3> positions;
    positions.add(0.0f, glm::vec3(0.0f));
    positions.add(48.0f, glm::vec3(0.0f, 4980.0f, 0.0f));
    return MinigameTrack(glm::translate(glm::vec3(95.0f, 0.0f, 0.0f)),
                         glm::vec3(5.0f, 0.0f, 2.0f),
                         glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                         std::move(positions),
                         {},
                         48.0f);
}

// A track that turns left (counter-clockwise seen from above) on a circle of
// radius 200 at 0.25 radians per second, for 20 s. The hook's X axis points
// out of the circle, its Y axis along the track and its Z axis up.
MinigameTrack leftCurveTrack() {
    graphics::KeyframeTrack<glm::vec3> positions;
    graphics::KeyframeTrack<glm::quat> orientations;
    constexpr float kRadius = 200.0f;
    constexpr float kRate = 0.25f;
    for (int i = 0; i <= 200; ++i) {
        float time = 0.1f * static_cast<float>(i);
        float angle = kRate * time;
        positions.add(time, glm::vec3(kRadius * glm::cos(angle), kRadius * glm::sin(angle), 0.0f));
        orientations.add(time, glm::angleAxis(angle, glm::vec3(0.0f, 0.0f, 1.0f)));
    }
    return MinigameTrack(glm::mat4(1.0f),
                         glm::vec3(0.0f),
                         glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                         std::move(positions),
                         std::move(orientations),
                         20.0f);
}

MinigameSpec swoopSpec(size_t pads) {
    MinigameSpec spec;
    spec.type = MinigameType::SwoopRace;
    spec.movementPerSec = 100.0f;
    spec.lateralAccel = 300.0f;
    spec.useInertia = true;
    spec.player.models = {{"v_superbike", true}};
    spec.player.hitPoints = 999;
    spec.player.maxHitPoints = 999;
    spec.player.sphereRadius = 3.0f;
    spec.player.invincePeriod = 0.3f;
    spec.player.accelSecs = 25.0f;
    spec.player.numLoops = 1;
    spec.player.scripts = slots({{MinigameSlot::OnCreate, "oncreate"},
                                 {MinigameSlot::OnHeartbeat, "heartbeat"},
                                 {MinigameSlot::OnFire, "onfire"},
                                 {MinigameSlot::OnHitFollower, "accelpad"},
                                 {MinigameSlot::OnHitObstacle, "obstacle"}});
    MinigameGunBankSpec bank;
    bank.bullet.rateOfFire = 0.01f;
    bank.bullet.lifespan = 0.01f;
    bank.bullet.targetType = 3;
    spec.player.gunBanks.push_back(bank);
    for (size_t i = 0; i < pads; ++i) {
        MinigameEnemySpec pad;
        pad.models = {{"mgf_accelpad01", true}};
        pad.hitPoints = 1;
        pad.maxHitPoints = 1;
        pad.sphereRadius = 3.0f;
        pad.numLoops = -1;
        pad.trigger = true;
        spec.enemies.push_back(pad);
    }
    return spec;
}

// The K1 swoop scripts, reduced to what reaches the simulation (from
// tar_m03mg oncreate / heartbeat / onfire / accelpad / obstacle).
struct SwoopScripts {
    MinigameSession *session {nullptr};
    int gear {-6};
    float clock {0.0f}; // the game clock the heartbeat reads
    float countdownStart {0.0f};
    float raceStart {0.0f};
    float raceTime {-1.0f};
    int boosts {0};

    void install(ScriptedHost &host) {
        host.scripts["oncreate"] = [this](uint32_t) {
            gear = -6;
            session->setPlayerSpeed(0.0f);
        };
        host.scripts["heartbeat"] = [this](uint32_t) {
            if (gear == -6) {
                countdownStart = clock;
                gear = -5;
            }
            float elapsed = clock - countdownStart;
            if (gear < 0 && elapsed > 5.0f) {
                raceStart = clock;
                gear = 0;
            }
            auto player = session->player();
            if (gear > -1 && gear < 6 && player->position.y > 3800.0f) {
                raceTime = clock - raceStart;
                session->setMinSpeed(0.0f);
                session->setPlayerMaxSpeed(0.0f);
                gear = 6;
            }
            float speed = session->playerSpeed();
            session->setLateralAccel(speed < 300.0f ? speed : 300.0f);
        };
        host.scripts["onfire"] = [this](uint32_t) {
            static const float kMin[] = {35.0f, 60.0f, 100.0f, 150.0f, 210.0f};
            static const float kMax[] = {70.0f, 120.0f, 190.0f, 225.0f, 420.0f};
            static const float kAccel[] = {20.0f, 8.0f, 6.0f, 3.0f, 2.0f};
            float speed = session->playerSpeed();
            bool shift = gear == 0 || (gear >= 1 && gear <= 4 && speed > kMin[gear - 1]);
            if (!shift) {
                return;
            }
            session->setMinSpeed(kMin[gear]);
            session->setPlayerMaxSpeed(kMax[gear]);
            session->setAccelPerSecond(kAccel[gear]);
            if (gear == 0) {
                session->setTunnelPos(glm::vec3(20.0f, 0.0f, 0.0f));
                session->setTunnelNeg(glm::vec3(-20.0f, 0.0f, 0.0f));
            }
            ++gear;
        };
        host.scripts["accelpad"] = [this](uint32_t caller) {
            auto player = session->player();
            if (!player->invulnerable() && gear != 5) {
                session->setAccelPerSecond(session->accelPerSecond() * 1.1f);
                session->setPlayerSpeed(session->playerSpeed() * 1.05f);
                ++boosts;
            }
            player->invulnerableTime = player->invincePeriod;
            if (auto pad = session->actorByObjectId(player->lastFollowerHit)) {
                session->changeHitPoints(*pad, -100);
            }
        };
    }
};

std::unique_ptr<MinigameSession> makeSwoop(ScriptedHost &host, size_t pads, const std::vector<glm::vec3> &padPositions) {
    auto session = std::make_unique<MinigameSession>(host);
    std::vector<MinigameTrack> padTracks;
    for (const auto &position : padPositions) {
        padTracks.push_back(MinigameTrack::fixed(position));
    }
    session->load(swoopSpec(pads), tarisTrack(), std::move(padTracks), {});
    for (size_t i = 0; i < session->actors().size(); ++i) {
        session->setObjectId(i, static_cast<uint32_t>(100 + i));
    }
    return session;
}

// One full race in 1/30 s steps.
struct RaceResult {
    float raceTime {-1.0f};
    uint64_t ticks {0};
    int boosts {0};
    glm::vec3 finish {0.0f};
};

RaceResult runRace() {
    std::vector<glm::vec3> pads {{111.573f, 190.006f, 0.0f}, {81.483f, 338.016f, 0.0f}, {113.217f, 528.379f, 0.0f},
                                 {102.557f, 582.581f, 0.0f}, {90.152f, 724.358f, 0.0f}, {107.983f, 852.101f, 0.0f}};
    ScriptedHost host;
    auto session = makeSwoop(host, pads.size(), pads);
    SwoopScripts scripts;
    scripts.session = session.get();
    scripts.install(host);
    session->start();

    MinigameInput input;
    input.fire = true;
    RaceResult result;
    for (int step = 0; step < 200000 && scripts.raceTime < 0.0f; ++step) {
        // Steer for the next pad ahead.
        auto player = session->player();
        float targetX = player->position.x;
        float bestY = 1e9f;
        for (int i = 0; i < session->enemyCount(); ++i) {
            auto pad = session->enemy(i);
            if (pad->position.y > player->position.y + 2.0f && pad->position.y < bestY) {
                bestY = pad->position.y;
                targetX = pad->position.x;
            }
        }
        float error = targetX - player->position.x;
        input.steer = glm::clamp(error / 4.0f - session->lateralVelocity().x / 100.0f, -1.0f, 1.0f);
        scripts.clock += kTick;
        session->update(kTick, input);
    }
    result.raceTime = scripts.raceTime;
    result.ticks = session->tickCount();
    result.boosts = scripts.boosts;
    result.finish = session->player()->position;
    return result;
}

} // namespace

TEST(MinigameSession, speed_moves_toward_the_maximum_at_the_acceleration) {
    ScriptedHost host;
    auto session = makeSwoop(host, 0, {});
    session->setPlayerSpeed(0.0f);
    session->setPlayerMaxSpeed(70.0f);
    session->setAccelPerSecond(20.0f);
    MinigameInput input;
    for (int i = 0; i < 30; ++i) {
        session->update(kTick, input);
    }
    EXPECT_NEAR(session->playerSpeed(), 20.0f, 1e-3f);
    for (int i = 0; i < 300; ++i) {
        session->update(kTick, input);
    }
    EXPECT_FLOAT_EQ(session->playerSpeed(), 70.0f);

    // From above it comes down to the maximum, and stops there.
    session->setPlayerMaxSpeed(50.0f);
    for (int i = 0; i < 300; ++i) {
        session->update(kTick, input);
    }
    EXPECT_FLOAT_EQ(session->playerSpeed(), 50.0f);
}

TEST(MinigameSession, the_minimum_speed_is_only_stored) {
    ScriptedHost host;
    auto session = makeSwoop(host, 0, {});
    session->setPlayerSpeed(10.0f);
    session->setPlayerMaxSpeed(10.0f);
    session->setMinSpeed(60.0f);
    session->update(kTick, MinigameInput());
    EXPECT_FLOAT_EQ(session->playerSpeed(), 10.0f);
    EXPECT_FLOAT_EQ(session->minSpeed(), 60.0f);
}

TEST(MinigameSession, the_player_starts_with_the_area_speeds_and_acceleration) {
    ScriptedHost host;
    auto spec = swoopSpec(0);
    spec.player.minimumSpeed = 10.0f;
    spec.player.maximumSpeed = 60.0f;
    spec.player.accelPerSecond = minigameAccelerationPerSecond(10.0f, 60.0f, 25.0f);
    MinigameSession session(host);
    session.load(spec, tarisTrack(), {}, {});
    EXPECT_FLOAT_EQ(session.minSpeed(), 10.0f);
    EXPECT_FLOAT_EQ(session.maxSpeed(), 60.0f);
    EXPECT_FLOAT_EQ(session.accelPerSecond(), 2.0f);
}

TEST(MinigameSession, the_track_plays_at_speed_over_one_hundred) {
    ScriptedHost host;
    auto session = makeSwoop(host, 0, {});
    session->setPlayerSpeed(100.0f);
    session->setPlayerMaxSpeed(100.0f);
    for (int i = 0; i < 30; ++i) {
        session->update(kTick, MinigameInput());
    }
    // One second at speed 100 is one second of the 48 s, 4980 unit track.
    EXPECT_NEAR(session->player()->position.y, 4980.0f / 48.0f, 0.05f);
    EXPECT_NEAR(session->player()->position.x, 100.0f, 1e-3f);
    EXPECT_NEAR(session->player()->position.z, 2.0f, 1e-3f);
}

TEST(MinigameSession, steering_is_held_inside_the_signed_tunnel) {
    ScriptedHost host;
    auto session = makeSwoop(host, 0, {});
    session->setTunnelPos(glm::vec3(20.0f, 0.0f, 0.0f));
    session->setTunnelNeg(glm::vec3(-20.0f, 0.0f, 0.0f));
    MinigameInput input;
    input.steer = 1.0f;
    for (int i = 0; i < 90; ++i) {
        session->update(kTick, input);
    }
    EXPECT_FLOAT_EQ(session->offset().x, 20.0f);
    // Hitting the wall stops the lateral drift.
    EXPECT_FLOAT_EQ(session->lateralVelocity().x, 0.0f);
    input.steer = -1.0f;
    for (int i = 0; i < 90; ++i) {
        session->update(kTick, input);
    }
    EXPECT_FLOAT_EQ(session->offset().x, -20.0f);
}

TEST(MinigameSession, with_inertia_the_lateral_speed_settles_at_movement_per_second) {
    ScriptedHost host;
    auto session = makeSwoop(host, 0, {});
    session->setTunnelPos(glm::vec3(1e6f, 0.0f, 0.0f));
    session->setTunnelNeg(glm::vec3(-1e6f, 0.0f, 0.0f));
    MinigameInput input;
    input.steer = 0.5f;
    session->update(kTick, input);
    EXPECT_LT(session->lateralVelocity().x, 50.0f); // it has to build up
    for (int i = 0; i < 300; ++i) {
        session->update(kTick, input);
    }
    EXPECT_NEAR(session->lateralVelocity().x, 50.0f, 0.01f);
}

// The steering lag does not depend on UseInertia.
TEST(MinigameSession, without_inertia_the_steering_is_still_smoothed) {
    ScriptedHost host;
    auto spec = swoopSpec(0);
    spec.useInertia = false;
    MinigameSession session(host);
    session.load(spec, tarisTrack(), {}, {});
    session.setTunnelPos(glm::vec3(1e6f, 0.0f, 0.0f));
    session.setTunnelNeg(glm::vec3(-1e6f, 0.0f, 0.0f));
    MinigameInput input;
    input.steer = 0.5f;
    session.update(kTick, input);
    EXPECT_GT(session.lateralVelocity().x, 0.0f);
    EXPECT_LT(session.lateralVelocity().x, 50.0f);
    for (int i = 0; i < 300; ++i) {
        session.update(kTick, input);
    }
    EXPECT_NEAR(session.lateralVelocity().x, 50.0f, 0.01f);
}

TEST(MinigameSession, the_curve_drift_is_the_centripetal_acceleration_toward_the_outside) {
    // 10 units forward while turning 0.01 radians left, in 0.1 s: 100 units
    // per second at 0.1 radians per second.
    glm::mat4 before(1.0f);
    glm::mat4 after = glm::translate(glm::vec3(0.0f, 10.0f, 0.0f)) *
                      glm::mat4_cast(glm::angleAxis(0.01f, glm::vec3(0.0f, 0.0f, 1.0f)));
    EXPECT_NEAR(minigameCurveDrift(before, after, 0.1f), kMinigameCurveDrift * 100.0f * 0.1f, 1e-3f);
    // A right turn pushes the other way, and a straight track not at all.
    glm::mat4 right = glm::translate(glm::vec3(0.0f, 10.0f, 0.0f)) *
                      glm::mat4_cast(glm::angleAxis(-0.01f, glm::vec3(0.0f, 0.0f, 1.0f)));
    EXPECT_NEAR(minigameCurveDrift(before, right, 0.1f), -kMinigameCurveDrift * 100.0f * 0.1f, 1e-3f);
    EXPECT_FLOAT_EQ(minigameCurveDrift(before, glm::translate(glm::vec3(0.0f, 10.0f, 0.0f)), 0.1f), 0.0f);
}

// UseInertia: on a curve the bike drifts to the outside unless it is steered
// against it.
TEST(MinigameSession, with_inertia_a_curve_pushes_the_bike_outward) {
    auto drift = [](bool inertia) {
        ScriptedHost host;
        auto spec = swoopSpec(0);
        spec.useInertia = inertia;
        MinigameSession session(host);
        session.load(spec, leftCurveTrack(), {}, {});
        session.setTunnelPos(glm::vec3(20.0f, 0.0f, 0.0f));
        session.setTunnelNeg(glm::vec3(-20.0f, 0.0f, 0.0f));
        session.setPlayerSpeed(100.0f);
        session.setPlayerMaxSpeed(100.0f);
        for (int i = 0; i < 60; ++i) {
            session.update(kTick, MinigameInput());
        }
        return session.offset().x;
    };
    // 200 * 0.25 = 50 units per second at 0.25 radians per second: 12.5 units
    // per second squared outward, against a damping of 300 / 100 per second.
    EXPECT_GT(drift(true), 1.0f);
    EXPECT_FLOAT_EQ(drift(false), 0.0f);
}

// TSL: a bike that is not steered comes back to the centre of the tunnel.
// KotOR leaves it where it is.
TEST(MinigameSession, on_tsl_the_bike_is_pulled_back_to_centre_without_steering) {
    auto offsetAfterRelease = [](bool tsl) {
        ScriptedHost host;
        auto session = makeSwoop(host, 0, {});
        session->setTSL(tsl);
        session->setTunnelPos(glm::vec3(20.0f, 0.0f, 0.0f));
        session->setTunnelNeg(glm::vec3(-20.0f, 0.0f, 0.0f));
        session->setPlayerOffset(glm::vec3(10.0f, 0.0f, 0.0f));
        for (int i = 0; i < 150; ++i) {
            session->update(kTick, MinigameInput());
        }
        return session->offset().x;
    };
    EXPECT_NEAR(offsetAfterRelease(true), 0.0f, 0.5f);
    EXPECT_FLOAT_EQ(offsetAfterRelease(false), 10.0f);
}

// TSL: steering against a fast slide brakes it harder than the steering alone.
TEST(MinigameSession, on_tsl_steering_against_a_fast_slide_brakes_it) {
    auto speedAfterCounterSteer = [](bool tsl) {
        ScriptedHost host;
        auto session = makeSwoop(host, 0, {});
        session->setTSL(tsl);
        session->setTunnelPos(glm::vec3(1e6f, 0.0f, 0.0f));
        session->setTunnelNeg(glm::vec3(-1e6f, 0.0f, 0.0f));
        MinigameInput input;
        input.steer = 1.0f;
        for (int i = 0; i < 60; ++i) {
            session->update(kTick, input);
        }
        EXPECT_GT(session->lateralVelocity().x, 90.0f);
        input.steer = -1.0f;
        for (int i = 0; i < 3; ++i) {
            session->update(kTick, input);
        }
        return session->lateralVelocity().x;
    };
    float tsl = speedAfterCounterSteer(true);
    float kotor = speedAfterCounterSteer(false);
    EXPECT_LT(tsl, kotor - 10.0f);

    // A slow slide is not braked: below the threshold both games agree.
    auto slow = [](bool tsl) {
        ScriptedHost host;
        auto session = makeSwoop(host, 0, {});
        session->setTSL(tsl);
        session->setTunnelPos(glm::vec3(1e6f, 0.0f, 0.0f));
        session->setTunnelNeg(glm::vec3(-1e6f, 0.0f, 0.0f));
        MinigameInput input;
        input.steer = 0.3f;
        for (int i = 0; i < 90; ++i) {
            session->update(kTick, input);
        }
        input.steer = -1.0f;
        session->update(kTick, input);
        return session->lateralVelocity().x;
    };
    EXPECT_FLOAT_EQ(slow(true), slow(false));
}

TEST(MinigameSession, creation_scripts_run_player_first_then_enemies) {
    ScriptedHost host;
    auto spec = swoopSpec(2);
    spec.enemies[0].scripts = slots({{MinigameSlot::OnCreate, "padcreate"}});
    spec.enemies[1].scripts = slots({{MinigameSlot::OnCreate, "padcreate"}});
    MinigameSession session(host);
    session.load(spec, tarisTrack(), {MinigameTrack::fixed(glm::vec3(0.0f)), MinigameTrack::fixed(glm::vec3(0.0f))}, {});
    for (size_t i = 0; i < session.actors().size(); ++i) {
        session.setObjectId(i, static_cast<uint32_t>(10 + i));
    }
    session.start();
    ASSERT_EQ(host.runs.size(), 3u);
    EXPECT_EQ(host.runs[0], std::make_pair(std::string("oncreate"), 10u));
    EXPECT_EQ(host.runs[1], std::make_pair(std::string("padcreate"), 11u));
    EXPECT_EQ(host.runs[2], std::make_pair(std::string("padcreate"), 12u));
    session.start(); // once only
    EXPECT_EQ(host.runs.size(), 3u);
}

TEST(MinigameSession, the_heartbeat_runs_every_update) {
    ScriptedHost host;
    auto session = makeSwoop(host, 0, {});
    for (int i = 0; i < 7; ++i) {
        session->update(kTick, MinigameInput());
    }
    EXPECT_EQ(host.count("heartbeat"), 7u);
}

TEST(MinigameSession, fire_runs_the_owners_on_fire_while_the_gun_is_ready) {
    ScriptedHost host;
    auto session = makeSwoop(host, 0, {});
    MinigameInput input;
    input.fire = true;
    for (int i = 0; i < 5; ++i) {
        session->update(kTick, input);
    }
    // The 0.01 s swoop gun is ready again on every 1/30 s update.
    EXPECT_EQ(host.count("onfire"), 5u);
    input.fire = false;
    session->update(kTick, input);
    EXPECT_EQ(host.count("onfire"), 5u);
}

// The TSL races shift gear on accelerate and jump on brake: each press runs
// the player's slot once, and holding the key does not run it again.
TEST(MinigameSession, accelerate_and_brake_run_the_players_slots_once_per_press) {
    ScriptedHost host;
    auto session = std::make_unique<MinigameSession>(host);
    auto spec = swoopSpec(0);
    spec.player.scripts = slots({{MinigameSlot::OnAccelerate, "onaccelerate"},
                                 {MinigameSlot::OnBrake, "onjump"}});
    session->load(spec, tarisTrack(), {}, {});
    session->setObjectId(0, 100);

    MinigameInput input;
    input.accelerate = true;
    for (int i = 0; i < 3; ++i) {
        session->update(kTick, input);
    }
    EXPECT_EQ(host.count("onaccelerate"), 1u);
    input.accelerate = false;
    session->update(kTick, input);
    input.accelerate = true;
    session->update(kTick, input);
    EXPECT_EQ(host.count("onaccelerate"), 2u);

    input.brake = true;
    session->update(kTick, input);
    session->update(kTick, input);
    EXPECT_EQ(host.count("onjump"), 1u);
    EXPECT_EQ(host.count("onaccelerate"), 2u);
}

TEST(MinigameSession, the_shift_key_fires_on_kotor_and_accelerates_on_tsl) {
    MinigameInput input;
    setMinigameShiftKey(input, true, false);
    EXPECT_TRUE(input.fire);
    EXPECT_FALSE(input.accelerate);
    setMinigameShiftKey(input, true, true);
    EXPECT_FALSE(input.fire);
    EXPECT_TRUE(input.accelerate);
    setMinigameShiftKey(input, false, true);
    EXPECT_FALSE(input.fire);
    EXPECT_FALSE(input.accelerate);
}

TEST(MinigameSession, a_trigger_pad_raises_on_hit_follower_on_both_and_dies_to_the_script) {
    ScriptedHost host;
    std::vector<glm::vec3> pads {{100.0f, 30.0f, 2.0f}};
    auto session = makeSwoop(host, 1, pads);
    SwoopScripts scripts;
    scripts.session = session.get();
    scripts.gear = 1;
    scripts.install(host);
    session->setPlayerSpeed(100.0f);
    session->setPlayerMaxSpeed(100.0f);
    session->setAccelPerSecond(10.0f);
    int before = session->enemyCount();
    for (int i = 0; i < 30; ++i) {
        session->update(kTick, MinigameInput());
    }
    EXPECT_EQ(scripts.boosts, 1);
    EXPECT_NEAR(session->accelPerSecond(), 11.0f, 1e-4f);
    EXPECT_EQ(session->player()->lastFollowerHit, 101u);
    EXPECT_EQ(session->actors()[1].lastFollowerHit, 100u);
    // -100 HP on a 1 HP pad kills it and takes it out of the enemy list.
    EXPECT_TRUE(session->actors()[1].dead);
    EXPECT_EQ(session->enemyCount(), before - 1);
    ASSERT_EQ(host.died.size(), 1u);
}

TEST(MinigameSession, the_invulnerability_window_counts_down_by_frame_time) {
    ScriptedHost host;
    auto session = makeSwoop(host, 0, {});
    auto player = session->player();
    player->invulnerableTime = player->invincePeriod;
    EXPECT_TRUE(player->invulnerable());
    for (int i = 0; i < 8; ++i) {
        session->update(kTick, MinigameInput());
    }
    EXPECT_TRUE(player->invulnerable()); // 0.3 s is 9 updates
    session->update(kTick, MinigameInput());
    session->update(kTick, MinigameInput());
    EXPECT_FALSE(player->invulnerable());
    EXPECT_FLOAT_EQ(player->invulnerableTime, 0.0f);
}

TEST(MinigameSession, damage_defaults_and_scripted_overrides) {
    ScriptedHost host;
    auto spec = swoopSpec(1);
    spec.enemies[0].hitPoints = 100;
    spec.enemies[0].maxHitPoints = 100;
    spec.enemies[0].scripts = slots({{MinigameSlot::OnDeath, "sthdeath"}});
    spec.player.scripts = slots({{MinigameSlot::OnDamage, "hawkhit"}});
    MinigameSession session(host);
    session.load(spec, tarisTrack(), {MinigameTrack::fixed(glm::vec3(0.0f))}, {});
    session.setObjectId(0, 1);
    session.setObjectId(1, 2);
    auto &player = session.actors()[0];
    auto &enemy = session.actors()[1];

    // Without a script the change applies: a loss plays "damage".
    session.changeHitPoints(enemy, -30);
    EXPECT_EQ(enemy.hitPoints, 70);
    EXPECT_EQ(enemy.lastHPChange, -30);
    EXPECT_EQ(host.animations.front(), "damage");
    // A gain is capped at the maximum.
    session.changeHitPoints(enemy, 500);
    EXPECT_EQ(enemy.hitPoints, 100);

    // With an OnDamage script only the script runs; it asks for the default.
    int hawkhits = 0;
    host.scripts["hawkhit"] = [&](uint32_t) {
        ++hawkhits;
        session.defaultDamage(player);
    };
    session.changeHitPoints(player, -10);
    EXPECT_EQ(hawkhits, 1);
    EXPECT_EQ(player.hitPoints, 989);

    // Death runs OnDeath once; the script asks for the default death.
    int deaths = 0;
    host.scripts["sthdeath"] = [&](uint32_t) {
        ++deaths;
        session.defaultDeath(enemy);
    };
    session.changeHitPoints(enemy, -100);
    session.changeHitPoints(enemy, -100);
    EXPECT_EQ(deaths, 1);
    EXPECT_TRUE(enemy.dead);
    EXPECT_EQ(session.enemyCount(), 0);
    EXPECT_EQ(session.enemy(0), nullptr);
}

TEST(MinigameSession, the_enemy_list_shifts_when_an_enemy_dies) {
    ScriptedHost host;
    std::vector<glm::vec3> pads {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    auto session = makeSwoop(host, 3, pads);
    ASSERT_EQ(session->enemyCount(), 3);
    session->kill(*session->enemy(0));
    ASSERT_EQ(session->enemyCount(), 2);
    EXPECT_EQ(session->enemy(0)->objectId, 102u);
    EXPECT_EQ(session->enemy(1)->objectId, 103u);
}

TEST(MinigameSession, bullets_hit_through_on_hit_bullet_or_the_default) {
    ScriptedHost host;
    std::vector<glm::vec3> pads {{0.0f, 0.0f, 0.0f}};
    auto session = makeSwoop(host, 1, pads);
    auto &enemy = session->actors()[1];
    enemy.hitPoints = 100;
    session->bulletHit(enemy, 30, 2, 100, "hull");
    EXPECT_EQ(enemy.hitPoints, 70);
    EXPECT_EQ(enemy.lastBulletHitDamage, 30);
    EXPECT_EQ(enemy.lastBulletHitShooter, 100u);
    EXPECT_EQ(enemy.lastBulletHitPart, "hull");
}

TEST(MinigameSession, a_swoop_obstacle_runs_on_hit_obstacle_once_per_crossing) {
    ScriptedHost host;
    auto session = std::make_unique<MinigameSession>(host);
    MinigameSession::ObstaclePlacement buoy;
    buoy.name = "m03mg_mgo22";
    buoy.position = glm::vec3(100.0f, 60.0f, 0.0f);
    buoy.hasHitbump = true;
    buoy.hitbumpMin = glm::vec3(97.0f, 58.0f, -1.0f);
    buoy.hitbumpMax = glm::vec3(103.0f, 62.0f, 1.0f);
    session->load(swoopSpec(0), tarisTrack(), {}, {buoy});
    session->setObjectId(0, 1);
    session->setObjectId(1, 2);
    session->setPlayerSpeed(100.0f);
    session->setPlayerMaxSpeed(100.0f);
    for (int i = 0; i < 60; ++i) {
        session->update(kTick, MinigameInput());
    }
    EXPECT_EQ(host.count("obstacle"), 1u);
    EXPECT_EQ(session->player()->lastObstacleHit, 2u);
}

namespace {

// A swoop session with one obstacle on the line of the Taris-shaped track,
// 60 units ahead; the bike rides at speed 100.
std::unique_ptr<MinigameSession> swoopWithObstacle(ScriptedHost &host,
                                                   bool tsl,
                                                   bool bikeScript,
                                                   const std::string &obstacleScript) {
    auto spec = swoopSpec(0);
    if (!bikeScript) {
        spec.player.scripts.onHitObstacle.clear();
    }
    MinigameObstacleSpec obstacleSpec;
    obstacleSpec.name = "mgo01";
    obstacleSpec.scripts.onHitFollower = obstacleScript;
    spec.obstacles.push_back(obstacleSpec);
    MinigameSession::ObstaclePlacement placement;
    placement.name = "mgo01";
    placement.position = glm::vec3(100.0f, 60.0f, 0.0f);
    placement.hasHitbump = true;
    placement.hitbumpMin = glm::vec3(97.0f, 58.0f, -1.0f);
    placement.hitbumpMax = glm::vec3(103.0f, 62.0f, 1.0f);
    auto session = std::make_unique<MinigameSession>(host);
    session->setTSL(tsl);
    session->load(spec, tarisTrack(), {}, {placement});
    session->setObjectId(0, 1);
    session->setObjectId(1, 2);
    session->setPlayerSpeed(100.0f);
    session->setPlayerMaxSpeed(100.0f);
    return session;
}

} // namespace

// KotOR: a hit runs the bike's OnHitObstacle and the obstacle's OnHitFollower.
TEST(MinigameSession, a_kotor_obstacle_hit_runs_the_bikes_and_the_obstacles_scripts) {
    ScriptedHost host;
    auto session = swoopWithObstacle(host, false, true, "mine");
    for (int i = 0; i < 60; ++i) {
        session->update(kTick, MinigameInput());
    }
    EXPECT_EQ(host.count("obstacle"), 1u);
    EXPECT_EQ(host.count("mine"), 1u);
    EXPECT_FALSE(session->player()->dead);
}

// TSL: the obstacle's script, and the bike's only when the obstacle has none.
TEST(MinigameSession, a_tsl_obstacle_hit_runs_the_obstacles_script_first) {
    {
        ScriptedHost host;
        auto session = swoopWithObstacle(host, true, true, "mine");
        for (int i = 0; i < 60; ++i) {
            session->update(kTick, MinigameInput());
        }
        EXPECT_EQ(host.count("mine"), 1u);
        EXPECT_EQ(host.count("obstacle"), 0u);
    }
    {
        ScriptedHost host;
        auto session = swoopWithObstacle(host, true, true, "");
        for (int i = 0; i < 60; ++i) {
            session->update(kTick, MinigameInput());
        }
        EXPECT_EQ(host.count("obstacle"), 1u);
    }
}

// With no OnHitObstacle on the bike, the default wrecks it.
TEST(MinigameSession, the_default_obstacle_hit_wrecks_the_bike) {
    for (bool tsl : {false, true}) {
        ScriptedHost host;
        auto session = swoopWithObstacle(host, tsl, false, "");
        for (int i = 0; i < 60; ++i) {
            session->update(kTick, MinigameInput());
        }
        EXPECT_TRUE(session->player()->dead) << (tsl ? "TSL" : "KotOR");
        ASSERT_EQ(host.died.size(), 1u);
        EXPECT_EQ(host.died.front(), "v_superbike");
    }
}

// The TSL courses put arches over the track. Their hitbump starts above the
// bike, which passes under it.
TEST(MinigameSession, a_hitbump_above_the_bike_is_passed_under) {
    ScriptedHost host;
    auto session = std::make_unique<MinigameSession>(host);
    MinigameSession::ObstaclePlacement arch;
    arch.name = "510ond_mgc12";
    arch.position = glm::vec3(100.0f, 60.0f, 0.0f);
    arch.hasHitbump = true;
    arch.hitbumpMin = glm::vec3(90.0f, 58.0f, 7.5f);
    arch.hitbumpMax = glm::vec3(110.0f, 92.0f, 10.0f);
    session->load(swoopSpec(0), tarisTrack(), {}, {arch});
    session->setObjectId(0, 1);
    session->setObjectId(1, 2);
    session->setPlayerSpeed(100.0f);
    session->setPlayerMaxSpeed(100.0f);
    for (int i = 0; i < 60; ++i) {
        session->update(kTick, MinigameInput());
    }
    EXPECT_EQ(host.count("obstacle"), 0u);
    EXPECT_EQ(session->player()->lastObstacleHit, 0u);
}

// The TSL jump: SWMG_SetJumpSpeed lifts the bike, the tunnel's Z limit is
// the ceiling, and gravity brings it back to its starting height.
TEST(MinigameSession, a_jump_rises_to_the_tunnel_ceiling_and_lands) {
    ScriptedHost host;
    auto session = makeSwoop(host, 0, {});
    session->setTunnelPos(glm::vec3(13.0f, 3.0f, 6.0f));
    session->setTunnelNeg(glm::vec3(-13.0f, -3.0f, 0.0f));
    float ground = session->offset().z;

    session->setJumpSpeed(30.0f);
    session->update(kTick, MinigameInput());
    EXPECT_NEAR(session->offset().z, ground + 1.0f, 1e-4f);
    for (int i = 0; i < 9; ++i) {
        session->update(kTick, MinigameInput());
    }
    EXPECT_GT(session->offset().z, ground + 5.0f);
    EXPECT_LE(session->offset().z, ground + 6.0f);
    EXPECT_LE(session->jumpSpeed(), 0.0f);

    for (int i = 0; i < 60; ++i) {
        session->update(kTick, MinigameInput());
    }
    EXPECT_FLOAT_EQ(session->offset().z, ground);
    EXPECT_FLOAT_EQ(session->jumpSpeed(), 0.0f);
}

// Without a ceiling, a jump at 30 units per second (the speed the TSL jump
// script gives) stays in the air 2 * 30 / 45.8 = 1.31 s.
TEST(MinigameSession, a_jump_stays_in_the_air_for_the_time_gravity_gives) {
    ScriptedHost host;
    auto session = makeSwoop(host, 0, {});
    session->setTunnelPos(glm::vec3(13.0f, 3.0f, 100.0f));
    session->setTunnelNeg(glm::vec3(-13.0f, -3.0f, 0.0f));
    float ground = session->offset().z;

    session->setJumpSpeed(30.0f);
    int airborne = 0;
    float peak = ground;
    for (int i = 0; i < 300; ++i) {
        session->update(kTick, MinigameInput());
        if (session->offset().z <= ground) {
            break;
        }
        peak = glm::max(peak, session->offset().z);
        ++airborne;
    }
    float expected = 2.0f * 30.0f / kMinigameGravity;
    EXPECT_NEAR(airborne * kTick, expected, 2.0f * kTick);
    // Stepped at 1/30 s, the peak is within one update's rise of v^2 / 2g.
    EXPECT_NEAR(peak - ground, 30.0f * 30.0f / (2.0f * kMinigameGravity), 30.0f * kTick);
}

// A barrier across the track (the TSL courses have them) stops a bike on the
// ground. A bike that jumps before it passes over.
TEST(MinigameSession, a_jumping_bike_passes_over_a_hitbump) {
    auto run = [](bool jump) {
        ScriptedHost host;
        auto session = std::make_unique<MinigameSession>(host);
        MinigameSession::ObstaclePlacement barrier;
        barrier.name = "510ond_mgc13";
        barrier.position = glm::vec3(100.0f, 60.0f, 0.0f);
        barrier.hasHitbump = true;
        barrier.hitbumpMin = glm::vec3(80.0f, 58.0f, -1.0f);
        barrier.hitbumpMax = glm::vec3(120.0f, 62.0f, 4.0f);
        session->load(swoopSpec(0), tarisTrack(), {}, {barrier});
        session->setObjectId(0, 1);
        session->setObjectId(1, 2);
        session->setTunnelPos(glm::vec3(13.0f, 3.0f, 6.0f));
        session->setTunnelNeg(glm::vec3(-13.0f, -3.0f, 0.0f));
        session->setPlayerSpeed(100.0f);
        session->setPlayerMaxSpeed(100.0f);
        if (jump) {
            session->setJumpSpeed(30.0f);
        }
        for (int i = 0; i < 60; ++i) {
            session->update(kTick, MinigameInput());
        }
        return host.count("obstacle");
    };
    EXPECT_EQ(run(false), 1u);
    EXPECT_EQ(run(true), 0u);
}

TEST(MinigameSession, a_segment_that_passes_a_follower_still_touches_it) {
    // At speed the bike covers more than a pad's diameter in one update; the
    // test is between movement segments, not end points.
    EXPECT_NEAR(minigameSegmentDistance(glm::vec3(0.0f, -10.0f, 0.0f), glm::vec3(0.0f, 10.0f, 0.0f),
                                        glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
                1.0f, 1e-5f);
    EXPECT_TRUE(minigameSegmentHitsBox2D(glm::vec3(0.0f, -10.0f, 5.0f), glm::vec3(0.0f, 10.0f, 5.0f),
                                         glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f)));
    EXPECT_FALSE(minigameSegmentHitsBox2D(glm::vec3(3.0f, -10.0f, 0.0f), glm::vec3(3.0f, 10.0f, 0.0f),
                                          glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f)));
}

TEST(MinigameSession, an_infinite_tunnel_axis_is_not_held) {
    ScriptedHost host;
    auto session = makeSwoop(host, 0, {});
    session->setTunnelPos(glm::vec3(20.0f, 0.0f, 0.0f));
    session->setTunnelNeg(glm::vec3(-20.0f, 0.0f, 0.0f));
    session->setPlayerOffset(glm::vec3(30.0f, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(session->offset().x, 20.0f);
    session->setTunnelInfinite(glm::vec3(1.0f, 0.0f, 0.0f));
    session->setPlayerOffset(glm::vec3(30.0f, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(session->offset().x, 30.0f);
}

TEST(MinigameSession, a_destroyed_object_leaves_its_list_without_on_death) {
    ScriptedHost host;
    std::vector<glm::vec3> pads {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    auto session = makeSwoop(host, 2, pads);
    session->destroy(*session->enemy(0));
    EXPECT_EQ(session->enemyCount(), 1);
    EXPECT_EQ(session->enemy(0)->objectId, 102u);
    EXPECT_TRUE(session->actors()[1].dead);
    EXPECT_TRUE(host.died.empty());
}

TEST(MinigameSession, a_scripted_race_runs_from_the_countdown_to_the_finish) {
    RaceResult race = runRace();
    ASSERT_GT(race.raceTime, 0.0f);
    EXPECT_GT(race.boosts, 0);
    EXPECT_GT(race.finish.y, 3800.0f);
    // Five gears and a few pads: in the range of the Taris beat time (38.43 s).
    EXPECT_GT(race.raceTime, 25.0f);
    EXPECT_LT(race.raceTime, 50.0f);
    // Running it again gives the same race.
    RaceResult again = runRace();
    EXPECT_EQ(race.raceTime, again.raceTime);
    EXPECT_EQ(race.ticks, again.ticks);
    EXPECT_EQ(race.finish, again.finish);
}
