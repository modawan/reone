/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <gtest/gtest.h>

#include "reone/game/turret.h"
#include "reone/graphics/animation.h"
#include "reone/graphics/model.h"
#include "reone/graphics/options.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/model.h"
#include "reone/scene/node/modelnode.h"

#include "../fixtures/audio.h"
#include "../fixtures/graphics.h"
#include "../fixtures/resource.h"
#include "../fixtures/scene.h"

using namespace reone::audio;
using namespace reone::game;
using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;

namespace {

// The K1 Ebon Hawk turret aim limits: pitch clamped to [2, 45] degrees, yaw
// flagged infinite on the Z axis.
MinigamePlayerSpec makeEbonHawkPlayerSpec() {
    MinigamePlayerSpec player;
    player.startOffset = glm::vec3(7.0f, 0.0f, 0.0f);
    player.tunnelXNeg = 2.0f;
    player.tunnelXPos = 45.0f;
    player.tunnelZNeg = -9999.0f;
    player.tunnelZPos = 9999.0f;
    player.tunnelInfinite = glm::vec3(0.0f, 0.0f, 1.0f);
    return player;
}

} // namespace

TEST(TurretAim, a_session_starts_at_the_authored_pitch) {
    TurretAim aim;
    aim.configure(makeEbonHawkPlayerSpec());

    EXPECT_TRUE(aim.pitchBounded());
    EXPECT_FALSE(aim.yawBounded());
    // Start_Offset_X, not the lower tunnel bound.
    EXPECT_NEAR(glm::degrees(aim.pitch()), 7.0f, 1e-3f);
    EXPECT_NEAR(glm::degrees(aim.startPitch()), 7.0f, 1e-3f);
    EXPECT_NEAR(glm::degrees(aim.yaw()), 0.0f, 1e-6f);
}

TEST(TurretAim, an_authored_start_outside_the_bounds_is_clamped) {
    auto player = makeEbonHawkPlayerSpec();
    player.startOffset = glm::vec3(90.0f, 0.0f, 0.0f);

    TurretAim aim;
    aim.configure(player);
    EXPECT_NEAR(glm::degrees(aim.pitch()), 45.0f, 1e-3f);

    player.startOffset = glm::vec3(-30.0f, 0.0f, 0.0f);
    aim.configure(player);
    EXPECT_NEAR(glm::degrees(aim.pitch()), 2.0f, 1e-3f);
}

TEST(TurretAim, an_authored_start_of_zero_still_clamps_to_the_lower_bound) {
    auto player = makeEbonHawkPlayerSpec();
    player.startOffset = glm::vec3(0.0f);

    TurretAim aim;
    aim.configure(player);

    EXPECT_NEAR(glm::degrees(aim.pitch()), 2.0f, 1e-3f);
}

TEST(TurretAim, the_authored_start_applies_to_yaw_as_well) {
    MinigamePlayerSpec player;
    player.startOffset = glm::vec3(0.0f, 0.0f, 30.0f);
    player.tunnelZNeg = -90.0f;
    player.tunnelZPos = 90.0f;

    TurretAim aim;
    aim.configure(player);

    EXPECT_NEAR(glm::degrees(aim.yaw()), 30.0f, 1e-3f);
}

TEST(TurretAim, reset_restores_the_authored_start_after_aiming_away) {
    TurretAim aim;
    aim.configure(makeEbonHawkPlayerSpec());
    aim.addPitch(glm::radians(20.0f));
    aim.addYaw(glm::radians(120.0f));
    ASSERT_GT(glm::degrees(aim.pitch()), 20.0f);

    aim.reset();

    EXPECT_NEAR(glm::degrees(aim.pitch()), 7.0f, 1e-3f);
    EXPECT_NEAR(glm::degrees(aim.yaw()), 0.0f, 1e-6f);
}

TEST(TurretAim, reconfiguring_a_session_restores_the_authored_start) {
    auto player = makeEbonHawkPlayerSpec();
    TurretAim aim;
    aim.configure(player);
    aim.addPitch(glm::radians(30.0f));

    aim.configure(player);

    EXPECT_NEAR(glm::degrees(aim.pitch()), 7.0f, 1e-3f);
}

TEST(TurretAim, axis_travel_reports_the_authored_range) {
    TurretAim aim;
    aim.configure(makeEbonHawkPlayerSpec());

    EXPECT_NEAR(glm::degrees(aim.pitchTravel()), 43.0f, 1e-3f);
    // An infinite axis travels a full turn.
    EXPECT_NEAR(aim.yawTravel(), glm::two_pi<float>(), 1e-5f);
}

TEST(TurretAim, pitch_clamps_at_both_tunnel_limits) {
    TurretAim aim;
    aim.configure(makeEbonHawkPlayerSpec());

    aim.addPitch(glm::radians(90.0f));
    EXPECT_NEAR(glm::degrees(aim.pitch()), 45.0f, 1e-3f);

    aim.addPitch(glm::radians(-90.0f));
    EXPECT_NEAR(glm::degrees(aim.pitch()), 2.0f, 1e-3f);
}

TEST(TurretAim, infinite_axis_wraps_instead_of_clamping) {
    TurretAim aim;
    aim.configure(makeEbonHawkPlayerSpec());

    aim.addYaw(glm::radians(190.0f));

    EXPECT_NEAR(glm::degrees(aim.yaw()), -170.0f, 1e-3f);
}

TEST(TurretAim, bounded_axis_uses_signed_limits_from_the_are) {
    MinigamePlayerSpec player;
    player.tunnelZNeg = -30.0f;
    player.tunnelZPos = 30.0f;

    TurretAim aim;
    aim.configure(player);

    EXPECT_TRUE(aim.yawBounded());
    aim.addYaw(glm::radians(45.0f));
    EXPECT_NEAR(glm::degrees(aim.yaw()), 30.0f, 1e-3f);
    aim.addYaw(glm::radians(-90.0f));
    EXPECT_NEAR(glm::degrees(aim.yaw()), -30.0f, 1e-3f);
}

TEST(TurretAim, forward_points_down_model_y_when_level) {
    MinigamePlayerSpec player;
    player.tunnelInfinite = glm::vec3(1.0f, 0.0f, 1.0f);

    TurretAim aim;
    aim.configure(player);
    glm::vec3 forward = aim.forward();

    EXPECT_NEAR(forward.x, 0.0f, 1e-5f);
    EXPECT_NEAR(forward.y, 1.0f, 1e-5f);
    EXPECT_NEAR(forward.z, 0.0f, 1e-5f);
}

TEST(TurretAim, yaw_rotates_forward_about_z_and_pitch_lifts_it) {
    MinigamePlayerSpec player;
    player.tunnelInfinite = glm::vec3(1.0f, 0.0f, 1.0f);

    TurretAim aim;
    aim.configure(player);
    aim.addYaw(glm::radians(90.0f));
    aim.addPitch(glm::radians(30.0f));

    glm::vec3 forward = aim.forward();
    // Rz(yaw) * Rx(pitch) applied to +Y.
    EXPECT_NEAR(forward.x, -glm::cos(glm::radians(30.0f)), 1e-5f);
    EXPECT_NEAR(forward.y, 0.0f, 1e-5f);
    EXPECT_NEAR(forward.z, glm::sin(glm::radians(30.0f)), 1e-5f);
    EXPECT_NEAR(glm::length(forward), 1.0f, 1e-5f);
}

// Aim rate: reone's accelerating keyboard turn model, scaled by axis travel.

TEST(TurretAimRate, an_idle_axis_does_not_move) {
    TurretAimRate rate;
    rate.configure(glm::two_pi<float>());

    EXPECT_EQ(rate.direction(), 0);
    EXPECT_FLOAT_EQ(rate.advance(1.0f), 0.0f);
    EXPECT_FLOAT_EQ(rate.rate(), 0.0f);
}

TEST(TurretAimRate, a_full_turn_axis_reproduces_reone_turn_rates) {
    TurretAimRate rate;
    rate.configure(glm::two_pi<float>());

    EXPECT_NEAR(rate.minRate(), 1.0f, 1e-5f);
    EXPECT_NEAR(rate.maxRate(), 2.5f, 1e-5f);
}

TEST(TurretAimRate, a_narrow_axis_moves_proportionally_slower) {
    TurretAimRate rate;
    rate.configure(glm::radians(43.0f));

    // 43 degrees of 360 is about a twelfth of a turn.
    float scale = glm::radians(43.0f) / glm::two_pi<float>();
    EXPECT_NEAR(rate.minRate(), 1.0f * scale, 1e-5f);
    EXPECT_NEAR(rate.maxRate(), 2.5f * scale, 1e-5f);
    // Fine control: a brief tap moves a fraction of a degree.
    rate.setDirection(1);
    EXPECT_LT(glm::degrees(rate.advance(1.0f / 60.0f)), 0.2f);
}

TEST(TurretAimRate, movement_over_an_interval_does_not_depend_on_step_size) {
    auto travelled = [](float step, float total) {
        TurretAimRate rate;
        rate.configure(glm::radians(43.0f));
        rate.setDirection(1);
        float sum = 0.0f;
        for (float t = 0.0f; t < total - 1e-6f; t += step) {
            sum += rate.advance(step);
        }
        return sum;
    };

    float coarse = travelled(1.0f / 15.0f, 2.0f);
    float fine = travelled(1.0f / 240.0f, 2.0f);
    float single = travelled(2.0f, 2.0f);

    EXPECT_NEAR(coarse, fine, 1e-4f);
    EXPECT_NEAR(single, fine, 1e-4f);
}

TEST(TurretAimRate, a_held_axis_accelerates_to_its_cap) {
    TurretAimRate rate;
    rate.configure(glm::two_pi<float>());
    rate.setDirection(1);

    EXPECT_NEAR(rate.rate(), rate.minRate(), 1e-5f);
    rate.advance(10.0f);
    EXPECT_NEAR(rate.rate(), rate.maxRate(), 1e-5f);
}

TEST(TurretAimRate, reversing_restarts_the_acceleration_ramp) {
    TurretAimRate rate;
    rate.configure(glm::two_pi<float>());
    rate.setDirection(1);
    rate.advance(10.0f);
    ASSERT_NEAR(rate.rate(), rate.maxRate(), 1e-5f);

    rate.setDirection(-1);

    EXPECT_EQ(rate.direction(), -1);
    EXPECT_NEAR(rate.rate(), rate.minRate(), 1e-5f);
    EXPECT_LT(rate.advance(0.1f), 0.0f);
}

TEST(TurretAimRate, reset_clears_direction_and_accumulated_acceleration) {
    TurretAimRate rate;
    rate.configure(glm::two_pi<float>());
    rate.setDirection(1);
    rate.advance(10.0f);

    rate.reset();

    EXPECT_EQ(rate.direction(), 0);
    EXPECT_FLOAT_EQ(rate.rate(), 0.0f);
    EXPECT_FLOAT_EQ(rate.advance(1.0f), 0.0f);
}

TEST(TurretAimRate, the_authored_pitch_range_is_traversable_but_not_instant) {
    TurretAim aim;
    aim.configure(makeEbonHawkPlayerSpec());
    TurretAimRate rate;
    rate.configure(aim.pitchTravel());
    rate.setDirection(1);

    // Half a second of hold must not consume the whole band, but a few seconds
    // must reach the top.
    aim.addPitch(rate.advance(0.5f));
    EXPECT_LT(glm::degrees(aim.pitch()), 45.0f);

    for (int i = 0; i < 300; ++i) {
        aim.addPitch(rate.advance(1.0f / 60.0f));
    }
    EXPECT_NEAR(glm::degrees(aim.pitch()), 45.0f, 1e-3f);
}

TEST(TurretAimRate, driving_an_axis_never_escapes_the_authored_bounds) {
    TurretAim aim;
    aim.configure(makeEbonHawkPlayerSpec());
    TurretAimRate rate;
    rate.configure(aim.pitchTravel());

    rate.setDirection(-1);
    for (int i = 0; i < 600; ++i) {
        aim.addPitch(rate.advance(1.0f / 60.0f));
    }
    EXPECT_NEAR(glm::degrees(aim.pitch()), 2.0f, 1e-3f);

    rate.setDirection(1);
    for (int i = 0; i < 600; ++i) {
        aim.addPitch(rate.advance(1.0f / 60.0f));
    }
    EXPECT_NEAR(glm::degrees(aim.pitch()), 45.0f, 1e-3f);
}

TEST(TurretGunTimer, first_shot_is_free_then_the_rate_of_fire_gates) {
    TurretGunTimer timer(0.3f);

    EXPECT_TRUE(timer.ready());
    EXPECT_TRUE(timer.tryFire());
    EXPECT_FALSE(timer.ready());
    EXPECT_FALSE(timer.tryFire());
}

TEST(TurretGunTimer, cooldown_expires_after_the_rate_of_fire_elapses) {
    TurretGunTimer timer(0.3f);
    ASSERT_TRUE(timer.tryFire());

    timer.update(0.2f);
    EXPECT_FALSE(timer.tryFire());

    timer.update(0.15f);
    EXPECT_TRUE(timer.ready());
    EXPECT_TRUE(timer.tryFire());
}

TEST(TurretGunTimer, a_zero_rate_of_fire_never_gates) {
    TurretGunTimer timer(0.0f);

    EXPECT_TRUE(timer.tryFire());
    EXPECT_TRUE(timer.tryFire());
}

TEST(TurretBullet, advances_along_its_direction_at_the_authored_speed) {
    TurretBullet bullet;
    bullet.direction = glm::vec3(0.0f, 1.0f, 0.0f);
    bullet.speed = 300.0f;
    bullet.lifespan = 3.0f;

    ASSERT_TRUE(bullet.advance(0.0f)); // muzzle frame
    ASSERT_TRUE(bullet.advance(0.5f));

    EXPECT_NEAR(bullet.position.y, 150.0f, 1e-3f);
    EXPECT_FLOAT_EQ(bullet.life, 0.5f);
}

TEST(TurretBullet, is_culled_once_it_outlives_its_lifespan) {
    TurretBullet bullet;
    bullet.direction = glm::vec3(0.0f, 1.0f, 0.0f);
    bullet.speed = 300.0f;
    bullet.lifespan = 1.0f;

    ASSERT_TRUE(bullet.advance(0.0f)); // muzzle frame
    ASSERT_TRUE(bullet.advance(0.9f));
    EXPECT_FALSE(bullet.advance(0.2f));
}

TEST(TurretBullet, an_expired_bullet_is_culled_on_the_next_step) {
    TurretBullet bullet;
    bullet.lifespan = 10.0f;

    bullet.expire();

    ASSERT_TRUE(bullet.advance(0.01f)); // the muzzle frame is presented first
    EXPECT_FALSE(bullet.advance(0.01f));
}

// Firing and bullet integration run in the same tick, so without a held first
// step every bolt is carried speed*dt down range before it is ever drawn, and
// never appears at the barrel that fired it.

TEST(TurretBullet, the_first_step_presents_the_muzzle_without_moving) {
    TurretBullet bullet;
    bullet.position = glm::vec3(1.285f, 2.067f, 0.736f); // authored bullethook0
    bullet.direction = glm::vec3(0.0f, 1.0f, 0.0f);
    bullet.speed = 300.0f;
    bullet.lifespan = 3.0f;

    ASSERT_TRUE(bullet.advance(1.0f / 60.0f));

    EXPECT_NEAR(bullet.position.x, 1.285f, 1e-4f);
    EXPECT_NEAR(bullet.position.y, 2.067f, 1e-4f);
    EXPECT_NEAR(bullet.position.z, 0.736f, 1e-4f);
    EXPECT_FLOAT_EQ(bullet.life, 0.0f);
    EXPECT_FALSE(bullet.atMuzzle);
}

TEST(TurretBullet, flight_resumes_normally_after_the_muzzle_frame) {
    TurretBullet bullet;
    bullet.direction = glm::vec3(0.0f, 1.0f, 0.0f);
    bullet.speed = 300.0f;
    bullet.lifespan = 3.0f;

    bullet.advance(1.0f / 60.0f);
    bullet.advance(1.0f / 60.0f);
    bullet.advance(1.0f / 60.0f);

    // Two integrating steps, not three: the muzzle frame is held.
    EXPECT_NEAR(bullet.position.y, 2.0f * 300.0f / 60.0f, 1e-3f);
}

// A bolt model's origin is not its tail. mgb_ebonleft draws from -1.953 to
// +8.620 along model +Y, so pinning the origin to bullethook0 buries nearly two
// units of bolt inside the gun.

TEST(TurretBullet, a_bolt_starts_clear_of_the_muzzle_by_its_own_reach_behind) {
    EXPECT_NEAR(turretMuzzleClearance(-1.953f), 1.953f, 1e-4f); // mgb_ebonleft
    EXPECT_NEAR(turretMuzzleClearance(-1.520f), 1.520f, 1e-4f); // mgb_sithfighter
}

TEST(TurretBullet, a_bolt_drawn_wholly_ahead_of_its_origin_is_not_displaced) {
    EXPECT_FLOAT_EQ(turretMuzzleClearance(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(turretMuzzleClearance(2.5f), 0.0f);
}

TEST(TurretBullet, no_visible_geometry_is_left_behind_the_muzzle_plane) {
    const float modelMin = -1.953f; // mgb_ebonleft rear reach
    const float muzzleY = 2.067f;   // authored bullethook0

    float spawnY = muzzleY + turretMuzzleClearance(modelMin);
    float tailY = spawnY + modelMin;

    EXPECT_GE(tailY, muzzleY - 1e-4f);
    EXPECT_NEAR(tailY, muzzleY, 1e-4f); // sits exactly on the plane, no further
}

TEST(TurretBullet, the_clearance_follows_the_bolt_heading_not_a_world_axis) {
    // Yawed 90 degrees, model +Y maps onto -X, so the displacement must too.
    glm::quat yawed = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));

    TurretBullet bullet;
    bullet.position = glm::vec3(1.285f, 2.067f, 0.736f);
    bullet.orientation = yawed;
    bullet.direction = turretFireDirection(yawed);
    bullet.speed = 300.0f;
    bullet.lifespan = 3.0f;

    float clearance = turretMuzzleClearance(-1.953f);
    bullet.position += bullet.direction * clearance;

    EXPECT_NEAR(bullet.position.x, 1.285f - 1.953f, 1e-3f);
    EXPECT_NEAR(bullet.position.y, 2.067f, 1e-3f);
}

TEST(TurretBullet, clearance_does_not_consume_the_muzzle_frame_or_alter_speed) {
    TurretBullet bullet;
    bullet.position = glm::vec3(0.0f, 2.067f, 0.0f);
    bullet.direction = glm::vec3(0.0f, 1.0f, 0.0f);
    bullet.speed = 300.0f;
    bullet.lifespan = 3.0f;
    bullet.position += bullet.direction * turretMuzzleClearance(-1.953f);
    float spawnY = bullet.position.y;

    // Still a held first frame: placement is not a movement step.
    ASSERT_TRUE(bullet.advance(1.0f / 60.0f));
    EXPECT_NEAR(bullet.position.y, spawnY, 1e-4f);
    EXPECT_FLOAT_EQ(bullet.life, 0.0f);

    // Flight then runs at the authored speed, unchanged by the displacement.
    ASSERT_TRUE(bullet.advance(1.0f / 60.0f));
    EXPECT_NEAR(bullet.position.y, spawnY + 300.0f / 60.0f, 1e-3f);
    EXPECT_NEAR(bullet.life, 1.0f / 60.0f, 1e-5f);
}

TEST(TurretBullet, clearance_keeps_the_banks_distinct_and_symmetric) {
    float clearance = turretMuzzleClearance(-1.953f);
    glm::vec3 forward(0.0f, 1.0f, 0.0f);

    glm::vec3 left = glm::vec3(-1.285f, 2.067f, 0.736f) + forward * clearance;
    glm::vec3 right = glm::vec3(1.285f, 2.067f, 0.736f) + forward * clearance;

    // The displacement is along the shared heading, so it cannot pull the two
    // banks together.
    EXPECT_NEAR(right.x - left.x, 2.570f, 1e-3f);
    EXPECT_NEAR(left.x, -right.x, 1e-4f);
    EXPECT_NEAR(left.y, right.y, 1e-4f);
}

TEST(TurretBullet, both_banks_hold_their_own_muzzle_on_the_first_frame) {
    // The authored gun bank hooks of mgf_turret, carrying mgg_turret's
    // bullethook0: distinct, symmetric, 2.57 units apart.
    TurretBullet left;
    left.position = glm::vec3(-1.285f, 2.067f, 0.736f);
    left.direction = glm::vec3(0.0f, 1.0f, 0.0f);
    left.speed = 300.0f;
    left.lifespan = 3.0f;

    TurretBullet right;
    right.position = glm::vec3(1.285f, 2.067f, 0.736f);
    right.direction = glm::vec3(0.0f, 1.0f, 0.0f);
    right.speed = 300.0f;
    right.lifespan = 3.0f;

    left.advance(1.0f / 60.0f);
    right.advance(1.0f / 60.0f);

    EXPECT_NEAR(right.position.x - left.position.x, 2.570f, 1e-3f);
    EXPECT_NEAR(left.position.x, -right.position.x, 1e-4f);
    EXPECT_GT(glm::abs(right.position.x), 1.0f); // neither collapses to centre

    // Separation survives flight: the banks fire on parallel headings.
    left.advance(0.5f);
    right.advance(0.5f);
    EXPECT_NEAR(right.position.x - left.position.x, 2.570f, 1e-3f);
    EXPECT_NEAR(left.position.y, right.position.y, 1e-4f);
}

// Projectile presentation. The bolt models are authored along +Y, so a bullet
// carrying no orientation renders across world Y regardless of where it is
// travelling - which is what made a single bolt impossible to pick out.

TEST(TurretProjectile, a_bolt_fired_level_travels_and_points_down_model_y) {
    glm::quat level(1.0f, 0.0f, 0.0f, 0.0f);

    glm::vec3 direction = turretFireDirection(level);

    EXPECT_NEAR(direction.x, 0.0f, 1e-5f);
    EXPECT_NEAR(direction.y, 1.0f, 1e-5f);
    EXPECT_NEAR(direction.z, 0.0f, 1e-5f);
}

TEST(TurretProjectile, the_visual_long_axis_follows_the_direction_of_travel) {
    TurretAim aim;
    MinigamePlayerSpec player;
    player.tunnelInfinite = glm::vec3(1.0f, 0.0f, 1.0f);
    aim.configure(player);
    aim.addYaw(glm::radians(70.0f));
    aim.addPitch(glm::radians(25.0f));

    TurretBullet bullet;
    bullet.orientation = aim.orientation();
    bullet.direction = turretFireDirection(bullet.orientation);

    // The model's +Y axis, once transformed, must coincide with travel.
    glm::mat4 transform = turretBulletTransform(bullet);
    glm::vec3 modelAxis = glm::normalize(glm::vec3(glm::mat3(transform) * glm::vec3(0.0f, 1.0f, 0.0f)));

    EXPECT_NEAR(modelAxis.x, bullet.direction.x, 1e-5f);
    EXPECT_NEAR(modelAxis.y, bullet.direction.y, 1e-5f);
    EXPECT_NEAR(modelAxis.z, bullet.direction.z, 1e-5f);
}

TEST(TurretProjectile, the_transform_places_the_bolt_at_its_muzzle_position) {
    TurretBullet bullet;
    bullet.position = glm::vec3(12.0f, -3.0f, 4.5f);
    bullet.orientation = glm::angleAxis(glm::radians(40.0f), glm::vec3(0.0f, 0.0f, 1.0f));

    glm::mat4 transform = turretBulletTransform(bullet);

    EXPECT_NEAR(transform[3].x, 12.0f, 1e-5f);
    EXPECT_NEAR(transform[3].y, -3.0f, 1e-5f);
    EXPECT_NEAR(transform[3].z, 4.5f, 1e-5f);
}

TEST(TurretProjectile, both_gun_banks_of_one_actor_fire_in_the_same_direction) {
    // The muzzles differ, the orientation does not, so banks stay symmetrical.
    glm::quat shooter = glm::angleAxis(glm::radians(33.0f), glm::vec3(0.0f, 0.0f, 1.0f)) *
                        glm::angleAxis(glm::radians(12.0f), glm::vec3(1.0f, 0.0f, 0.0f));

    TurretBullet left;
    left.position = glm::vec3(-2.0f, 0.0f, 0.0f);
    left.orientation = shooter;
    left.direction = turretFireDirection(shooter);

    TurretBullet right;
    right.position = glm::vec3(2.0f, 0.0f, 0.0f);
    right.orientation = shooter;
    right.direction = turretFireDirection(shooter);

    EXPECT_NEAR(left.direction.x, right.direction.x, 1e-6f);
    EXPECT_NEAR(left.direction.y, right.direction.y, 1e-6f);
    EXPECT_NEAR(left.direction.z, right.direction.z, 1e-6f);
    EXPECT_NE(turretBulletTransform(left)[3].x, turretBulletTransform(right)[3].x);
}

TEST(TurretProjectile, aiming_adds_no_rotation_beyond_the_aim_itself) {
    // A bolt fired at neutral aim must not be rotated at all.
    MinigamePlayerSpec player;
    player.tunnelInfinite = glm::vec3(1.0f, 0.0f, 1.0f);
    TurretAim aim;
    aim.configure(player);

    TurretBullet bullet;
    bullet.orientation = aim.orientation();

    glm::mat4 transform = turretBulletTransform(bullet);
    glm::mat3 basis(transform);

    EXPECT_NEAR(basis[0].x, 1.0f, 1e-5f);
    EXPECT_NEAR(basis[1].y, 1.0f, 1e-5f);
    EXPECT_NEAR(basis[2].z, 1.0f, 1e-5f);
}

TEST(TurretProjectile, a_bolt_advances_along_its_oriented_direction) {
    TurretBullet bullet;
    bullet.orientation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    bullet.direction = turretFireDirection(bullet.orientation);
    bullet.speed = 300.0f;
    bullet.lifespan = 3.0f;

    ASSERT_TRUE(bullet.advance(0.0f)); // muzzle frame
    ASSERT_TRUE(bullet.advance(0.1f));

    // Yawed 90 degrees, +Y maps onto -X.
    EXPECT_NEAR(bullet.position.x, -30.0f, 1e-3f);
    EXPECT_NEAR(bullet.position.y, 0.0f, 1e-3f);
}

// Muzzle heading. The gun banks are not aligned with the hull they hang on: on
// the K1 turret each bullethook0 sits about five degrees below the turret body
// and half a degree inboard, so the pair converges down range. Firing on the
// turret root's heading throws both corrections away.

namespace {

// The authored hook orientations, as measured from the shipped models: pitch up
// 2 degrees, toed inboard 0.5 degrees, mirrored per bank. The turret body they
// hang on is pitched 7 degrees.
glm::quat makeMuzzleOrientation(float pitchDeg, float toeDeg) {
    return glm::angleAxis(glm::radians(toeDeg), glm::vec3(0.0f, 0.0f, 1.0f)) *
           glm::angleAxis(glm::radians(pitchDeg), glm::vec3(1.0f, 0.0f, 0.0f));
}

} // namespace

TEST(TurretProjectile, each_bank_fires_on_its_own_muzzle_heading) {
    glm::vec3 left = turretFireDirection(makeMuzzleOrientation(2.0f, 0.5f));
    glm::vec3 right = turretFireDirection(makeMuzzleOrientation(2.0f, -0.5f));

    // Distinct headings, mirrored about the centre plane.
    EXPECT_NEAR(left.x, -right.x, 1e-5f);
    EXPECT_GT(glm::abs(left.x), 1e-4f);
    EXPECT_NEAR(left.y, right.y, 1e-5f);
    EXPECT_NEAR(left.z, right.z, 1e-5f);
}

TEST(TurretProjectile, the_authored_toe_in_makes_the_banks_converge) {
    glm::vec3 leftOrigin(-1.2848f, 2.0665f, 0.7359f);
    glm::vec3 rightOrigin(1.2848f, 2.0665f, 0.7359f);
    glm::vec3 left = turretFireDirection(makeMuzzleOrientation(2.0f, -0.5f));
    glm::vec3 right = turretFireDirection(makeMuzzleOrientation(2.0f, 0.5f));

    float startGap = rightOrigin.x - leftOrigin.x;
    glm::vec3 leftFar = leftOrigin + left * 100.0f;
    glm::vec3 rightFar = rightOrigin + right * 100.0f;

    // Toed inboard: the gap closes with distance instead of staying constant.
    EXPECT_LT(rightFar.x - leftFar.x, startGap);
}

TEST(TurretProjectile, the_authored_muzzle_pitch_is_not_replaced_by_the_hull) {
    glm::vec3 fromMuzzle = turretFireDirection(makeMuzzleOrientation(2.0f, 0.0f));
    glm::vec3 fromRoot = turretFireDirection(makeMuzzleOrientation(7.0f, 0.0f));

    EXPECT_NEAR(glm::degrees(glm::asin(fromMuzzle.z)), 2.0f, 1e-3f);
    EXPECT_NEAR(glm::degrees(glm::asin(fromRoot.z)), 7.0f, 1e-3f);
    // Substituting the hull heading would raise the shot five degrees.
    EXPECT_GT(fromRoot.z, fromMuzzle.z);
}

TEST(TurretProjectile, the_muzzle_heading_survives_turret_pitch_and_yaw) {
    // A hook keeps its authored offset from the body under any aim, because it
    // is composed with the turret transform rather than replacing it.
    glm::quat body = glm::angleAxis(glm::radians(40.0f), glm::vec3(0.0f, 0.0f, 1.0f)) *
                     glm::angleAxis(glm::radians(25.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    glm::quat hookLocal = makeMuzzleOrientation(-5.0f, 0.5f);

    glm::vec3 bodyDir = turretFireDirection(body);
    glm::vec3 muzzleDir = turretFireDirection(body * hookLocal);

    EXPECT_NEAR(glm::length(muzzleDir), 1.0f, 1e-5f);
    // Still offset from the hull heading after the aim is applied.
    float cosAngle = glm::clamp(glm::dot(bodyDir, muzzleDir), -1.0f, 1.0f);
    EXPECT_NEAR(glm::degrees(glm::acos(cosAngle)), 5.025f, 0.05f);
}

TEST(TurretProjectile, clearance_is_applied_along_the_muzzle_heading) {
    glm::quat muzzle = makeMuzzleOrientation(2.0f, 0.5f);

    TurretBullet bullet;
    bullet.position = glm::vec3(-1.2848f, 2.0665f, 0.7359f);
    bullet.orientation = muzzle;
    bullet.direction = turretFireDirection(muzzle);
    bullet.speed = 300.0f;
    bullet.lifespan = 3.0f;

    glm::vec3 origin = bullet.position;
    float clearance = turretMuzzleClearance(-1.953f);
    bullet.position += bullet.direction * clearance;

    // Displaced exactly along the heading it will fly, not along world Y.
    glm::vec3 delta = bullet.position - origin;
    EXPECT_NEAR(glm::length(delta), clearance, 1e-4f);
    EXPECT_NEAR(glm::dot(glm::normalize(delta), bullet.direction), 1.0f, 1e-5f);
}

TEST(TurretProjectile, the_drawn_bolt_and_its_collision_path_share_one_heading) {
    glm::quat muzzle = makeMuzzleOrientation(2.0f, -0.5f);

    TurretBullet bullet;
    bullet.orientation = muzzle;
    bullet.direction = turretFireDirection(muzzle);
    bullet.speed = 300.0f;
    bullet.lifespan = 3.0f;

    // The model is drawn along its own +Y, transformed by the same orientation
    // the simulation integrates along.
    glm::mat4 transform = turretBulletTransform(bullet);
    glm::vec3 drawnAxis = glm::normalize(glm::vec3(glm::mat3(transform) * glm::vec3(0.0f, 1.0f, 0.0f)));

    EXPECT_NEAR(drawnAxis.x, bullet.direction.x, 1e-5f);
    EXPECT_NEAR(drawnAxis.y, bullet.direction.y, 1e-5f);
    EXPECT_NEAR(drawnAxis.z, bullet.direction.z, 1e-5f);

    bullet.advance(1.0f / 60.0f); // muzzle frame
    glm::vec3 before = bullet.position;
    bullet.advance(1.0f / 60.0f);
    glm::vec3 travelled = glm::normalize(bullet.position - before);
    EXPECT_NEAR(glm::dot(travelled, drawnAxis), 1.0f, 1e-5f);
}

TEST(TurretProjectile, a_bolt_on_its_muzzle_heading_reaches_a_target_on_that_axis) {
    glm::vec3 origin(1.2848f, 2.0665f, 0.7359f);
    glm::quat muzzle = makeMuzzleOrientation(2.0f, 0.5f);
    glm::vec3 direction = turretFireDirection(muzzle);

    // A fighter sitting on the authored firing axis is hit. Fired on the hull
    // heading instead, the same shot is five degrees high and clears the
    // authored 20 unit sphere well inside the bolt's 900 unit range.
    const float range = 300.0f;
    glm::vec3 target = origin + direction * range;
    glm::vec3 hullDirection = turretFireDirection(makeMuzzleOrientation(7.0f, 0.0f));

    EXPECT_TRUE(sphereContainsPoint(target, 20.0f, origin + direction * range));
    EXPECT_FALSE(sphereContainsPoint(target, 20.0f, origin + hullDirection * range));
}

TEST(TurretCollision, sphere_contains_points_within_its_radius) {
    glm::vec3 center(10.0f, 20.0f, 30.0f);

    EXPECT_TRUE(sphereContainsPoint(center, 20.0f, glm::vec3(10.0f, 35.0f, 30.0f)));
    EXPECT_FALSE(sphereContainsPoint(center, 20.0f, glm::vec3(10.0f, 45.0f, 30.0f)));
}

TEST(TurretCollision, a_zero_radius_sphere_never_contains_anything) {
    EXPECT_FALSE(sphereContainsPoint(glm::vec3(0.0f), 0.0f, glm::vec3(0.0f)));
}

TEST(TurretCollision, ray_hits_a_sphere_ahead_of_it_within_range) {
    glm::vec3 origin(0.0f);
    glm::vec3 direction(0.0f, 1.0f, 0.0f);

    EXPECT_TRUE(rayIntersectsSphere(origin, direction, glm::vec3(0.0f, 100.0f, 0.0f), 40.0f, 200.0f));
}

TEST(TurretCollision, ray_misses_a_sphere_off_to_the_side) {
    glm::vec3 origin(0.0f);
    glm::vec3 direction(0.0f, 1.0f, 0.0f);

    EXPECT_FALSE(rayIntersectsSphere(origin, direction, glm::vec3(80.0f, 100.0f, 0.0f), 40.0f, 200.0f));
}

TEST(TurretCollision, ray_misses_a_sphere_behind_it) {
    glm::vec3 origin(0.0f);
    glm::vec3 direction(0.0f, 1.0f, 0.0f);

    EXPECT_FALSE(rayIntersectsSphere(origin, direction, glm::vec3(0.0f, -100.0f, 0.0f), 40.0f, 200.0f));
}

TEST(TurretCollision, ray_misses_a_sphere_beyond_the_sensing_radius) {
    glm::vec3 origin(0.0f);
    glm::vec3 direction(0.0f, 1.0f, 0.0f);

    EXPECT_FALSE(rayIntersectsSphere(origin, direction, glm::vec3(0.0f, 500.0f, 0.0f), 40.0f, 200.0f));
    EXPECT_TRUE(rayIntersectsSphere(origin, direction, glm::vec3(0.0f, 500.0f, 0.0f), 40.0f, 600.0f));
}

// Cockpit HUD state, reproducing k_pebo_hawkhit's shipped arithmetic:
//   state = ((hp - 2000) * 12) / 1000 + 1
// so each state covers a band of hit points and the gauge steps down cleanly.

TEST(TurretDestruction, the_hawk_is_lost_below_the_gauge_floor_not_at_zero) {
    EXPECT_FALSE(turretIsDestroyed(3000));
    EXPECT_FALSE(turretIsDestroyed(2001));
    EXPECT_FALSE(turretIsDestroyed(2000));
    EXPECT_TRUE(turretIsDestroyed(1999));
    EXPECT_TRUE(turretIsDestroyed(0));
    EXPECT_TRUE(turretIsDestroyed(-50));
}

TEST(TurretDestruction, the_authored_survivable_band_is_one_thousand_points) {
    // A 3000 point hawk is destroyed after 1001 points of damage, not 3000.
    EXPECT_FALSE(turretIsDestroyed(3000 - 1000));
    EXPECT_TRUE(turretIsDestroyed(3000 - 1001));
}

TEST(TurretDestruction, the_destruction_band_selects_the_empty_gauge) {
    EXPECT_EQ(turretHealthState(1999), 0);
    EXPECT_EQ(turretHealthAnimation(turretHealthState(1999)), "health00");
}

// A destroyed fighter keeps its authored "die" animation on screen for that
// animation's own length, then everything it owns is retired. Without the
// retirement the fighter's fx_ref engine flares, lights and emitters survive
// the hull fade and keep riding the rail.

TEST(TurretDestruction, the_death_effect_lasts_the_authored_animation_length) {
    // mgf_sithfighter authors a 2.3s non-looping "die".
    EXPECT_FALSE(turretDeathEffectComplete(0.0f, 2.3f));
    EXPECT_FALSE(turretDeathEffectComplete(1.0f, 2.3f));
    EXPECT_FALSE(turretDeathEffectComplete(2.29f, 2.3f));
    EXPECT_TRUE(turretDeathEffectComplete(2.3f, 2.3f));
    EXPECT_TRUE(turretDeathEffectComplete(5.0f, 2.3f));
}

TEST(TurretDestruction, a_fighter_with_no_death_animation_is_retired_at_once) {
    EXPECT_TRUE(turretDeathEffectComplete(0.0f, 0.0f));
    EXPECT_TRUE(turretDeathEffectComplete(0.0f, -1.0f));
}

namespace {

// A fighter shaped like mgf_sithfighter: a hull mesh, an explosion emitter, a
// reference node the loader hangs an engine flare on, and a plain hook the
// minigame attaches a gun bank model to.
struct FighterFixture {
    MockRenderPipelineFactory pipelineFactory;
    GraphicsOptions graphicsOpt;
    TestGraphicsModule graphicsModule;
    TestAudioModule audioModule;
    TestResourceModule resourceModule;
    std::unique_ptr<SceneGraph> scene;

    std::shared_ptr<ModelNode> root;
    std::unique_ptr<Model> model;
    std::shared_ptr<ModelSceneNode> node;

    std::unique_ptr<Model> flareModel;
    std::shared_ptr<ModelSceneNode> flare;
    std::unique_ptr<Model> gunModel;
    std::shared_ptr<ModelSceneNode> gun;

    FighterFixture() {
        graphicsModule.init();
        audioModule.init();
        resourceModule.init();
        scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt,
                                             graphicsModule.services(),
                                             audioModule.services(),
                                             resourceModule.services());

        root = std::make_shared<ModelNode>(0, "fighter", glm::vec3(0.0f),
                                           glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);

        auto hull = std::make_shared<ModelNode>(1, "sith_wing", glm::vec3(0.0f),
                                                glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, root.get());
        hull->setMesh(std::make_shared<ModelNode::TriangleMesh>());
        root->addChild(hull);

        auto explode = std::make_shared<ModelNode>(2, "explode", glm::vec3(0.0f),
                                                   glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, root.get());
        explode->setEmitter(std::make_shared<ModelNode::Emitter>());
        root->addChild(explode);

        // An authored reference node. The referenced model name is left empty so
        // the loader does not try to resolve it; the flare is attached below the
        // same way the loader would.
        auto omenRef = std::make_shared<ModelNode>(3, "omenref05", glm::vec3(0.0f),
                                                   glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, root.get());
        omenRef->setReference(std::make_shared<ModelNode::Reference>());
        root->addChild(omenRef);

        auto gunHook = std::make_shared<ModelNode>(4, "gunbank0", glm::vec3(0.0f),
                                                   glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, root.get());
        root->addChild(gunHook);

        model = std::make_unique<Model>("fighter", 0, root,
                                        std::vector<std::shared_ptr<Animation>>(), "", 1.0f);
        node = makeModelSceneNode(*model);

        flareModel = makeSimpleModel("fx_ref");
        flare = makeModelSceneNode(*flareModel);
        node->attach("omenref05", *flare);

        gunModel = makeSimpleModel("mgg_null");
        gun = makeModelSceneNode(*gunModel);
        node->attach("gunbank0", *gun);
    }

    std::unique_ptr<Model> makeSimpleModel(const std::string &name) {
        auto modelRoot = std::make_shared<ModelNode>(0, name, glm::vec3(0.0f),
                                                     glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
        modelRoot->setMesh(std::make_shared<ModelNode::TriangleMesh>());
        return std::make_unique<Model>(name, 0, modelRoot,
                                       std::vector<std::shared_ptr<Animation>>(), "", 1.0f);
    }

    std::shared_ptr<ModelSceneNode> makeModelSceneNode(Model &m) {
        auto n = std::make_shared<ModelSceneNode>(m, ModelUsage::Creature, *scene,
                                                  graphicsModule.services(),
                                                  audioModule.services(),
                                                  resourceModule.services());
        n->init();
        return n;
    }
};

} // namespace

TEST(TurretDestruction, the_engine_flare_goes_out_the_moment_a_fighter_dies) {
    FighterFixture fighter;
    ASSERT_TRUE(fighter.flare->isEnabled());

    turretDisableReferenceAttachments(*fighter.node);

    EXPECT_FALSE(fighter.flare->isEnabled());
}

TEST(TurretDestruction, the_hull_survives_for_its_death_animation) {
    FighterFixture fighter;

    turretDisableReferenceAttachments(*fighter.node);

    // The wreck itself, its explosion emitter and the gun bank model hooked on
    // by the minigame all keep running: only loader-built reference
    // attachments are switched off.
    EXPECT_TRUE(fighter.node->isEnabled());
    EXPECT_TRUE(fighter.gun->isEnabled());
    EXPECT_TRUE(fighter.node->getNodeByName("sith_wing")->isEnabled());
    EXPECT_TRUE(fighter.node->getNodeByName("explode")->isEnabled());
}

TEST(TurretDestruction, disabling_the_flare_twice_is_harmless) {
    FighterFixture fighter;

    turretDisableReferenceAttachments(*fighter.node);
    turretDisableReferenceAttachments(*fighter.node);

    EXPECT_FALSE(fighter.flare->isEnabled());
    EXPECT_TRUE(fighter.gun->isEnabled());
}

TEST(TurretDestruction, a_replayed_fighter_starts_with_its_flare_lit) {
    // Sessions build their fighters fresh, so a kill in one run cannot leave a
    // later run's flare switched off.
    FighterFixture first;
    turretDisableReferenceAttachments(*first.node);
    ASSERT_FALSE(first.flare->isEnabled());

    FighterFixture replay;
    EXPECT_TRUE(replay.flare->isEnabled());
}

TEST(TurretDestruction, death_effects_of_separate_fighters_are_independent) {
    // Each fighter accumulates its own elapsed time, so one dying later does
    // not retire early on another's clock.
    const float duration = 2.3f;
    float first = 0.0f;
    float second = 0.0f;

    for (int i = 0; i < 100; ++i) { // ~1.67s
        first += 1.0f / 60.0f;
    }
    EXPECT_FALSE(turretDeathEffectComplete(first, duration));

    for (int i = 0; i < 40; ++i) {
        first += 1.0f / 60.0f;
        second += 1.0f / 60.0f;
    }
    EXPECT_TRUE(turretDeathEffectComplete(first, duration));
    EXPECT_FALSE(turretDeathEffectComplete(second, duration));
}

TEST(TurretHealthGauge, an_undamaged_hawk_reads_full) {
    // 3000 is the authored Hit_Points/Max_HPs; the first damage lands below it.
    EXPECT_EQ(turretHealthState(2999), 12);
    EXPECT_EQ(turretHealthAnimation(12), "health12");
}

TEST(TurretHealthGauge, the_destruction_floor_reads_the_lowest_live_state) {
    EXPECT_EQ(turretHealthState(2000), 1);
    EXPECT_EQ(turretHealthAnimation(1), "health01");
}

TEST(TurretHealthGauge, below_the_floor_reads_empty) {
    EXPECT_EQ(turretHealthState(1999), 0);
    EXPECT_EQ(turretHealthState(0), 0);
    EXPECT_EQ(turretHealthAnimation(0), "health00");
}

TEST(TurretHealthGauge, every_authored_state_band_is_reachable) {
    // Each state n covers hp in [2000 + ceil((n-1)*1000/12), ...]; walking the
    // whole range must visit all thirteen states.
    std::set<int> seen;
    for (int hp = 0; hp <= 3000; ++hp) {
        seen.insert(turretHealthState(hp));
    }
    for (int state = 0; state < kTurretHealthStateCount; ++state) {
        EXPECT_TRUE(seen.count(state) == 1) << "state " << state << " unreachable";
    }
}

TEST(TurretHealthGauge, state_boundaries_match_the_shipped_arithmetic) {
    // Spot-check the exact truncating-division boundaries.
    EXPECT_EQ(turretHealthState(2083), 1);
    EXPECT_EQ(turretHealthState(2084), 2);
    EXPECT_EQ(turretHealthState(2166), 2);
    EXPECT_EQ(turretHealthState(2167), 3);
    EXPECT_EQ(turretHealthState(2249), 3);
    EXPECT_EQ(turretHealthState(2250), 4);
    EXPECT_EQ(turretHealthState(2916), 11);
    EXPECT_EQ(turretHealthState(2917), 12);
}

TEST(TurretHealthGauge, the_gauge_never_rises_as_hit_points_fall) {
    int previous = turretHealthState(3000);
    for (int hp = 3000; hp >= 0; --hp) {
        int state = turretHealthState(hp);
        EXPECT_LE(state, previous) << "gauge rose at hp " << hp;
        previous = state;
    }
}

TEST(TurretHealthGauge, values_beyond_the_authored_range_clamp) {
    EXPECT_EQ(turretHealthState(999999), kTurretHealthStateCount - 1);
    EXPECT_EQ(turretHealthState(-999999), 0);
    EXPECT_EQ(turretHealthAnimation(99), "health12");
    EXPECT_EQ(turretHealthAnimation(-5), "health00");
}

TEST(TurretHealthGauge, unchanged_hit_points_select_the_same_state) {
    EXPECT_EQ(turretHealthState(2500), turretHealthState(2500));
    // A hit too small to cross a band boundary must not change the state, so
    // the caller can skip restarting the animation. 2500 and 2502 both sit in
    // the band that truncates to 6, i.e. state 7.
    EXPECT_EQ(turretHealthState(2500), 7);
    EXPECT_EQ(turretHealthState(2502), 7);
}

// Alarm01: the shipped script plays the authored looping sound object when the
// computed state is exactly 3, and stops it only in the destruction branch.

TEST(TurretAlarm, it_triggers_only_on_the_authored_state) {
    EXPECT_TRUE(turretAlarmStartsAtState(3));
    EXPECT_FALSE(turretAlarmStartsAtState(2));
    EXPECT_FALSE(turretAlarmStartsAtState(4));
    EXPECT_FALSE(turretAlarmStartsAtState(0));
    EXPECT_FALSE(turretAlarmStartsAtState(12));
}

TEST(TurretAlarm, the_trigger_band_matches_the_shipped_hit_points) {
    EXPECT_FALSE(turretAlarmStartsAtState(turretHealthState(2250)));
    EXPECT_TRUE(turretAlarmStartsAtState(turretHealthState(2249)));
    EXPECT_TRUE(turretAlarmStartsAtState(turretHealthState(2167)));
    EXPECT_FALSE(turretAlarmStartsAtState(turretHealthState(2166)));
}

TEST(TurretAlarm, it_uses_the_authored_module_sound_object_tag) {
    EXPECT_EQ(kTurretAlarmTag, "Alarm01");
}

// Radar contacts: one authored loop per enemy track, plus a removal pose.

TEST(TurretRadar, every_enemy_maps_to_its_authored_contact_loop) {
    EXPECT_EQ(turretContactAnimation(0), "sithloop02");
    EXPECT_EQ(turretContactAnimation(1), "sithloop03");
    EXPECT_EQ(turretContactAnimation(2), "sithloop04");
    EXPECT_EQ(turretContactAnimation(3), "sithloop05");
    EXPECT_EQ(turretContactAnimation(4), "sithloop06");
    EXPECT_EQ(turretContactAnimation(5), "sithloop07");
}

TEST(TurretRadar, each_contact_has_a_matching_removal_pose) {
    for (size_t i = 0; i < kTurretContactCount; ++i) {
        EXPECT_EQ(turretContactDeathAnimation(i), turretContactAnimation(i) + "d");
    }
    EXPECT_EQ(turretContactDeathAnimation(0), "sithloop02d");
    EXPECT_EQ(turretContactDeathAnimation(5), "sithloop07d");
}

TEST(TurretRadar, an_index_beyond_the_authored_contacts_has_no_channel) {
    EXPECT_TRUE(turretContactAnimation(kTurretContactCount).empty());
    EXPECT_TRUE(turretContactDeathAnimation(99).empty());
}

// Radar heading: one authored pose per whole degree.

TEST(TurretHeading, zero_yaw_selects_the_authored_forward_pose) {
    EXPECT_EQ(turretHeadingState(0.0f), 0);
    EXPECT_EQ(turretHeadingAnimation(0), "hudrot_000");
}

TEST(TurretHeading, positive_yaw_counts_up_in_whole_degrees) {
    EXPECT_EQ(turretHeadingState(glm::radians(1.0f)), 1);
    EXPECT_EQ(turretHeadingState(glm::radians(90.0f)), 90);
    EXPECT_EQ(turretHeadingAnimation(90), "hudrot_090");
    EXPECT_EQ(turretHeadingAnimation(359), "hudrot_359");
}

TEST(TurretHeading, negative_yaw_wraps_into_the_authored_range) {
    EXPECT_EQ(turretHeadingState(glm::radians(-1.0f)), 359);
    EXPECT_EQ(turretHeadingState(glm::radians(-90.0f)), 270);
    EXPECT_EQ(turretHeadingAnimation(turretHeadingState(glm::radians(-90.0f))), "hudrot_270");
}

TEST(TurretHeading, it_wraps_cleanly_across_the_seam) {
    EXPECT_EQ(turretHeadingState(glm::radians(359.6f)), 0);
    EXPECT_EQ(turretHeadingState(glm::radians(360.0f)), 0);
    EXPECT_EQ(turretHeadingState(glm::radians(361.0f)), 1);
}

TEST(TurretHeading, several_full_rotations_stay_in_range) {
    for (float turns = -5.0f; turns <= 5.0f; turns += 0.25f) {
        int heading = turretHeadingState(turns * glm::two_pi<float>());
        EXPECT_GE(heading, 0);
        EXPECT_LT(heading, 360);
    }
}

TEST(TurretHeading, a_yaw_within_the_same_degree_does_not_change_state) {
    int before = turretHeadingState(glm::radians(45.1f));
    int after = turretHeadingState(glm::radians(45.2f));
    EXPECT_EQ(before, after);
}

// The startturretgame lifecycle: the command validates what it can up front,
// schedules an ordinary module transition, and the request is resolved against
// whatever module actually loaded.

TEST(TurretRequest, a_valid_request_from_a_gameplay_module_is_accepted) {
    EXPECT_EQ(validateTurretRequest("m12ab", "ebo_m12aa", /*moduleKnown=*/true, /*alreadyActive=*/false),
              TurretRequestError::None);
}

TEST(TurretRequest, the_module_argument_is_required) {
    EXPECT_EQ(validateTurretRequest("", "ebo_m12aa", /*moduleKnown=*/false, /*alreadyActive=*/false),
              TurretRequestError::MissingModule);
}

TEST(TurretRequest, an_unknown_module_is_rejected) {
    EXPECT_EQ(validateTurretRequest("not_a_module", "ebo_m12aa", /*moduleKnown=*/false, /*alreadyActive=*/false),
              TurretRequestError::UnknownModule);
}

TEST(TurretRequest, requesting_the_current_module_is_rejected) {
    EXPECT_EQ(validateTurretRequest("m12ab", "M12ab", /*moduleKnown=*/true, /*alreadyActive=*/false),
              TurretRequestError::SameModule);
}

TEST(TurretRequest, a_request_without_an_origin_module_is_rejected) {
    EXPECT_EQ(validateTurretRequest("m12ab", "", /*moduleKnown=*/true, /*alreadyActive=*/false),
              TurretRequestError::NoOrigin);
}

TEST(TurretRequest, a_second_request_is_rejected_while_one_is_active) {
    EXPECT_EQ(validateTurretRequest("m12ab", "ebo_m12aa", /*moduleKnown=*/true, /*alreadyActive=*/true),
              TurretRequestError::AlreadyActive);
}

TEST(TurretRequest, every_rejection_carries_a_message) {
    for (auto error : {TurretRequestError::MissingModule,
                       TurretRequestError::UnknownModule,
                       TurretRequestError::SameModule,
                       TurretRequestError::NoOrigin,
                       TurretRequestError::AlreadyActive}) {
        EXPECT_STRNE(turretRequestErrorMessage(error), "");
    }
    EXPECT_STREQ(turretRequestErrorMessage(TurretRequestError::None), "");
}

TEST(TurretRequest, a_loaded_turret_module_starts_the_minigame) {
    EXPECT_EQ(resolveTurretRequest(/*pendingForModule=*/true, /*hasMinigame=*/true, MinigameType::Turret),
              TurretRequestResolution::Start);
}

TEST(TurretRequest, a_module_without_a_minigame_aborts_the_session) {
    EXPECT_EQ(resolveTurretRequest(/*pendingForModule=*/true, /*hasMinigame=*/false, MinigameType::None),
              TurretRequestResolution::AbortNoMinigame);
}

TEST(TurretRequest, a_module_with_a_different_minigame_aborts_the_session) {
    EXPECT_EQ(resolveTurretRequest(/*pendingForModule=*/true, /*hasMinigame=*/true, MinigameType::SwoopRace),
              TurretRequestResolution::AbortWrongType);
}

TEST(TurretRequest, an_unrelated_module_load_leaves_the_request_alone) {
    EXPECT_EQ(resolveTurretRequest(/*pendingForModule=*/false, /*hasMinigame=*/true, MinigameType::Turret),
              TurretRequestResolution::NotPending);
    EXPECT_EQ(resolveTurretRequest(/*pendingForModule=*/false, /*hasMinigame=*/false, MinigameType::None),
              TurretRequestResolution::NotPending);
}

TEST(TurretRequest, only_the_aborting_resolutions_carry_a_message) {
    EXPECT_STRNE(turretRequestResolutionMessage(TurretRequestResolution::AbortNoMinigame), "");
    EXPECT_STRNE(turretRequestResolutionMessage(TurretRequestResolution::AbortWrongType), "");
    EXPECT_STREQ(turretRequestResolutionMessage(TurretRequestResolution::Start), "");
    EXPECT_STREQ(turretRequestResolutionMessage(TurretRequestResolution::NotPending), "");
}

// Return-destination precedence. Note that starting m12ab from ebo_m12aa
// cannot distinguish these cases, because the authored return and the captured
// origin are the same module there; each case below uses a distinct origin.

TEST(TurretReturn, an_authored_destination_overrides_the_captured_origin) {
    // K1 m12ab always returns to ebo_m12aa, wherever the session started.
    EXPECT_EQ(turretReturnModule("m12ab", "tar_m02ab"), "ebo_m12aa");
    EXPECT_EQ(turretReturnModule("M12ab", "somewhere_else"), "ebo_m12aa");
}

TEST(TurretReturn, the_captured_origin_is_used_when_no_destination_is_authored) {
    EXPECT_EQ(turretReturnModule("custom_mg", "tar_m02ab"), "tar_m02ab");
}

TEST(TurretReturn, with_neither_destination_no_module_is_named) {
    // An empty result is the caller's signal to stay put rather than schedule a
    // transition to an empty module name.
    EXPECT_EQ(turretReturnModule("custom_mg", ""), "");
    EXPECT_EQ(turretReturnModule("", ""), "");
}

TEST(TurretReturn, an_authored_destination_still_applies_with_no_captured_origin) {
    EXPECT_EQ(turretReturnModule("m12ab", ""), "ebo_m12aa");
}

// Launch parity. A session started in place (startturret) captures no origin;
// one entered through a module transition (startturretgame) captures the module
// it came from. Both are lifecycle sessions, so an outcome resolves the same
// way for either - the entry route may only decide where the player lands.

TEST(TurretReturn, both_launch_routes_resolve_a_win_the_same_way) {
    const auto direct = turretReturnModule("m12ab", "");                 // startturret
    const auto viaTransition = turretReturnModule("m12ab", "ebo_m12aa"); // startturretgame

    EXPECT_EQ(direct, viaTransition);
    EXPECT_EQ(direct, "ebo_m12aa");
    EXPECT_TRUE(turretSessionSucceeded(Turret::Outcome::Won));
}

TEST(TurretReturn, a_direct_session_stays_put_only_when_nothing_is_authored) {
    // No authored destination and no captured origin: the only case in which a
    // resolved session leaves the player where they are.
    EXPECT_EQ(turretReturnModule("custom_mg", ""), "");
    // An authored destination is honoured even without an origin, so a direct
    // session is not stranded in the minigame module.
    EXPECT_FALSE(turretReturnModule("m12ab", "").empty());
}

// Outcome semantics. Only a victory may emit the completion state; a defeat and
// an abandoned session must both be distinguishable from it and from each other.

TEST(TurretOutcome, only_a_victory_is_a_success) {
    EXPECT_TRUE(turretSessionSucceeded(Turret::Outcome::Won));
    EXPECT_FALSE(turretSessionSucceeded(Turret::Outcome::Lost));
    EXPECT_FALSE(turretSessionSucceeded(Turret::Outcome::InProgress));
}

TEST(TurretOutcome, an_abandoned_session_is_neither_a_win_nor_a_loss) {
    EXPECT_TRUE(turretSessionAborted(Turret::Outcome::InProgress));
    EXPECT_FALSE(turretSessionSucceeded(Turret::Outcome::InProgress));
    EXPECT_FALSE(turretSessionAborted(Turret::Outcome::Won));
    EXPECT_FALSE(turretSessionAborted(Turret::Outcome::Lost));
}

TEST(TurretOutcome, a_defeat_does_not_masquerade_as_a_victory) {
    EXPECT_FALSE(turretSessionSucceeded(Turret::Outcome::Lost));
    EXPECT_FALSE(turretSessionAborted(Turret::Outcome::Lost));
    EXPECT_STREQ(turretOutcomeName(Turret::Outcome::Lost), "lost");
}

TEST(TurretOutcome, every_outcome_reports_a_distinct_name) {
    EXPECT_STREQ(turretOutcomeName(Turret::Outcome::Won), "won");
    EXPECT_STREQ(turretOutcomeName(Turret::Outcome::Lost), "lost");
    EXPECT_STREQ(turretOutcomeName(Turret::Outcome::InProgress), "aborted");
}

TEST(TurretOutcome, a_defeat_and_an_abort_still_name_a_return_destination) {
    // Every outcome returns; only the completion state is victory-gated.
    EXPECT_EQ(turretReturnModule("m12ab", "tar_m02ab"), "ebo_m12aa");
    EXPECT_FALSE(turretSessionSucceeded(Turret::Outcome::Lost));
    EXPECT_FALSE(turretSessionSucceeded(Turret::Outcome::InProgress));
}
