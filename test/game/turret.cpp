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
    std::shared_ptr<Model> model;
    std::shared_ptr<ModelSceneNode> node;

    std::shared_ptr<Model> flareModel;
    std::shared_ptr<ModelSceneNode> flare;
    std::shared_ptr<Model> gunModel;
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

        model = std::make_shared<Model>("fighter", 0, root,
                                        std::vector<std::shared_ptr<Animation>>(), "", 1.0f);
        node = makeModelSceneNode(model);

        flareModel = makeSimpleModel("fx_ref");
        flare = makeModelSceneNode(flareModel);
        node->attach("omenref05", *flare);

        gunModel = makeSimpleModel("mgg_null");
        gun = makeModelSceneNode(gunModel);
        node->attach("gunbank0", *gun);
    }

    std::shared_ptr<Model> makeSimpleModel(const std::string &name) {
        auto modelRoot = std::make_shared<ModelNode>(0, name, glm::vec3(0.0f),
                                                     glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
        modelRoot->setMesh(std::make_shared<ModelNode::TriangleMesh>());
        return std::make_shared<Model>(name, 0, modelRoot,
                                       std::vector<std::shared_ptr<Animation>>(), "", 1.0f);
    }

    std::shared_ptr<ModelSceneNode> makeModelSceneNode(std::shared_ptr<Model> m) {
        auto n = std::make_shared<ModelSceneNode>(m, ModelUsage::Creature, *scene,
                                                  graphicsModule.services(),
                                                  audioModule.services(),
                                                  resourceModule.services());
        n->init();
        return n;
    }
};

} // namespace

// Rail detachment. Emitter particles are children of the emitter that spawned
// them, so a wreck still attached to the looping rail hook carries its whole
// explosion along with it. These cover the transform bookkeeping the detach
// depends on; the wiring itself is exercised in the engine.

namespace {

// A rail carrying a fighter on its "modelhook", the way loadEnemies builds it.
struct RailFixture : FighterFixture {
    std::shared_ptr<Model> railModel;
    std::shared_ptr<ModelSceneNode> rail;

    RailFixture() {
        auto railRoot = std::make_shared<ModelNode>(0, "m12ab_mgt02", glm::vec3(0.0f),
                                                    glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
        auto hook = std::make_shared<ModelNode>(1, "modelhook", glm::vec3(0.0f),
                                                glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, railRoot.get());
        railRoot->addChild(hook);
        railModel = std::make_shared<Model>("m12ab_mgt02", 0, railRoot,
                                            std::vector<std::shared_ptr<Animation>>(), "", 1.0f);
        rail = makeModelSceneNode(railModel);
        rail->attach("modelhook", *node);
    }

    // Where the rail's hook has carried the fighter to.
    glm::vec3 fighterWorldPos() const {
        return glm::vec3(node->absoluteTransform()[3]);
    }

    void moveRail(const glm::vec3 &to) {
        rail->getNodeByName("modelhook")->setLocalTransform(glm::translate(to));
    }
};

} // namespace

TEST(TurretDestruction, a_wreck_keeps_the_pose_it_died_in_when_it_leaves_the_rail) {
    RailFixture fixture;
    fixture.moveRail(glm::vec3(40.0f, 12.0f, -3.0f));
    glm::mat4 atDeath = fixture.node->absoluteTransform();

    // What detachEnemyFromTrack does: capture the world pose, drop the hook
    // attachment, then re-root at that pose.
    fixture.rail->getNodeByName("modelhook")->removeAllChildren();
    fixture.node->setLocalTransform(atDeath);

    EXPECT_NEAR(fixture.fighterWorldPos().x, 40.0f, 1e-4f);
    EXPECT_NEAR(fixture.fighterWorldPos().y, 12.0f, 1e-4f);
    EXPECT_NEAR(fixture.fighterWorldPos().z, -3.0f, 1e-4f);
}

TEST(TurretDestruction, the_rail_no_longer_carries_a_detached_wreck) {
    RailFixture fixture;
    fixture.moveRail(glm::vec3(40.0f, 12.0f, -3.0f));
    glm::mat4 atDeath = fixture.node->absoluteTransform();
    fixture.rail->getNodeByName("modelhook")->removeAllChildren();
    fixture.node->setLocalTransform(atDeath);

    // The rail keeps looping; the wreck must not follow it any more.
    fixture.moveRail(glm::vec3(500.0f, -250.0f, 90.0f));

    EXPECT_NEAR(fixture.fighterWorldPos().x, 40.0f, 1e-4f);
    EXPECT_NEAR(fixture.fighterWorldPos().y, 12.0f, 1e-4f);
    EXPECT_NEAR(fixture.fighterWorldPos().z, -3.0f, 1e-4f);
}

TEST(TurretDestruction, a_live_fighter_still_rides_its_rail) {
    RailFixture fixture;

    fixture.moveRail(glm::vec3(7.0f, 8.0f, 9.0f));

    EXPECT_NEAR(fixture.fighterWorldPos().x, 7.0f, 1e-4f);
    EXPECT_NEAR(fixture.fighterWorldPos().y, 8.0f, 1e-4f);
    EXPECT_NEAR(fixture.fighterWorldPos().z, 9.0f, 1e-4f);
}

TEST(TurretDestruction, detaching_leaves_the_death_effects_running) {
    RailFixture fixture;
    glm::mat4 atDeath = fixture.node->absoluteTransform();
    fixture.rail->getNodeByName("modelhook")->removeAllChildren();
    fixture.node->setLocalTransform(atDeath);
    turretDisableReferenceAttachments(*fixture.node);

    // Only the engine flare goes out. The explosion emitter, the hull and the
    // wreck itself must all survive the detach so the blast can play.
    EXPECT_TRUE(fixture.node->isEnabled());
    EXPECT_TRUE(fixture.node->getNodeByName("explode")->isEnabled());
    EXPECT_TRUE(fixture.node->getNodeByName("sith_wing")->isEnabled());
    EXPECT_FALSE(fixture.flare->isEnabled());
}

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
