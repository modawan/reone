/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <gtest/gtest.h>

#include "reone/game/minigame.h"
#include "reone/resource/parser/gff/are.h"

using namespace reone::game;
using namespace reone::resource::generated;

namespace {

// The .are MiniGame struct of K1's m12ab (the Ebon Hawk turret), reduced to the
// fields the runtime consumes.
ARE makeEbonHawkTurretARE() {
    ARE are;
    are.MiniGame.Type = 2;
    are.MiniGame.CameraViewAngle = 65.0f;
    are.MiniGame.Near_Clip = 0.1f;
    are.MiniGame.Far_Clip = 2000.0f;
    are.MiniGame.Bump_Plane = 3;
    are.MiniGame.DOF = 5;
    are.MiniGame.MovementPerSec = 100.0f;
    are.MiniGame.LateralAccel = 1200.0f;
    are.MiniGame.Music = "mus_bat_sithbs";
    are.MiniGame.Mouse.AxisX = 3;
    are.MiniGame.Mouse.AxisY = 1;

    auto &player = are.MiniGame.Player;
    player.Track = "m12ab_mgt01";
    player.Camera = "m12ab_camera";
    player.CameraRotate = 1;
    player.Sphere_Radius = 40.0f;
    player.Hit_Points = 3000;
    player.Max_HPs = 3000;
    player.Start_Offset_X = 7.0f;
    player.Target_Offset_Z = -5.0f;
    player.TunnelInfinite = glm::vec3(0.0f, 0.0f, 1.0f);
    player.TunnelXPos = 45.0f;
    player.TunnelXNeg = 2.0f;
    player.TunnelZPos = 9999.0f;
    player.TunnelZNeg = -9999.0f;
    player.Scripts.OnHeartbeat = "k_heartbeat";
    player.Scripts.OnDamage = "k_pebo_hawkhit";

    ARE_MiniGame_Player_Models gun;
    gun.Model = "mgf_turret";
    gun.RotatingModel = 1;
    ARE_MiniGame_Player_Models hull;
    hull.Model = "mgf_ebonhawk";
    hull.RotatingModel = 0;
    player.Models = {gun, hull};

    ARE_MiniGame_Player_Gun_Banks bank;
    bank.BankID = 0;
    bank.Gun_Model = "mgg_turret";
    bank.Fire_Sound = "mgs_ebon_fire";
    bank.Bullet.Bullet_Model = "mgb_ebonleft";
    bank.Bullet.Collision_Sound = "mgs_sith_hit";
    bank.Bullet.Damage = 30;
    bank.Bullet.Lifespan = 3.0f;
    bank.Bullet.Rate_Of_Fire = 0.3f;
    bank.Bullet.Speed = 300.0f;
    bank.Bullet.Target_Type = 2;
    player.Gun_Banks = {bank};

    ARE_MiniGame_Enemies enemy;
    enemy.Track = "m12ab_mgt02";
    enemy.Hit_Points = 100;
    enemy.Max_HPs = 100;
    enemy.Sphere_Radius = 20.0f;
    enemy.Sounds.Death = "mgs_sith_expl";
    enemy.Scripts.OnCreate = "k_pebo_sthcreate";
    enemy.Scripts.OnDeath = "k_pebo_sthdeath2";
    ARE_MiniGame_Enemies_Models fighter;
    fighter.Model = "mgf_sithfighter";
    fighter.RotatingModel = 1;
    enemy.Models = {fighter};
    ARE_MiniGame_Enemies_Gun_Banks enemyBank;
    enemyBank.BankID = 0;
    enemyBank.Gun_Model = "mgg_null";
    enemyBank.Fire_Sound = "mgs_sith_fire";
    enemyBank.Inaccuracy = 0.01f;
    enemyBank.Sensing_Radius = 200.0f;
    enemyBank.Horiz_Spread = 70.0f;
    enemyBank.Vert_Spread = 70.0f;
    enemyBank.Bullet.Bullet_Model = "mgb_sithfighter";
    enemyBank.Bullet.Collision_Sound = "mgs_ebon_hit";
    enemyBank.Bullet.Damage = 10;
    enemyBank.Bullet.Lifespan = 2.0f;
    enemyBank.Bullet.Rate_Of_Fire = 0.4f;
    enemyBank.Bullet.Speed = 200.0f;
    enemyBank.Bullet.Target_Type = 1;
    enemy.Gun_Banks = {enemyBank};
    are.MiniGame.Enemies = {enemy};

    ARE_MiniGame_Obstacles obstacle;
    obstacle.Name = "m12ab_mgo01";
    are.MiniGame.Obstacles = {obstacle};

    return are;
}

} // namespace

TEST(MinigameSpec, area_without_minigame_yields_no_spec) {
    ARE are;

    EXPECT_FALSE(parseMinigameSpec(are).has_value());
}

TEST(MinigameSpec, turret_area_is_recognized) {
    auto spec = parseMinigameSpec(makeEbonHawkTurretARE());

    ASSERT_TRUE(spec.has_value());
    EXPECT_EQ(spec->type, MinigameType::Turret);
    EXPECT_STREQ(minigameTypeName(spec->type), "turret");
    EXPECT_FLOAT_EQ(spec->cameraViewAngle, 65.0f);
    EXPECT_FLOAT_EQ(spec->nearClip, 0.1f);
    EXPECT_FLOAT_EQ(spec->farClip, 2000.0f);
    EXPECT_EQ(spec->music, "mus_bat_sithbs");
    EXPECT_EQ(spec->mouse.axisX, 3u);
    EXPECT_FALSE(spec->mouse.flipAxisX);
}

TEST(MinigameSpec, player_models_keep_their_rotating_flag) {
    auto spec = parseMinigameSpec(makeEbonHawkTurretARE());

    ASSERT_TRUE(spec.has_value());
    ASSERT_EQ(spec->player.models.size(), 2u);
    EXPECT_EQ(spec->player.models[0].resRef, "mgf_turret");
    EXPECT_TRUE(spec->player.models[0].rotating);
    EXPECT_EQ(spec->player.models[1].resRef, "mgf_ebonhawk");
    EXPECT_FALSE(spec->player.models[1].rotating);
}

TEST(MinigameSpec, player_gun_bank_and_bullet_are_parsed) {
    auto spec = parseMinigameSpec(makeEbonHawkTurretARE());

    ASSERT_TRUE(spec.has_value());
    ASSERT_EQ(spec->player.gunBanks.size(), 1u);
    const auto &bank = spec->player.gunBanks.front();
    EXPECT_EQ(bank.bankId, 0u);
    EXPECT_EQ(bank.gunModelResRef, "mgg_turret");
    EXPECT_EQ(bank.fireSound, "mgs_ebon_fire");
    EXPECT_EQ(bank.bullet.modelResRef, "mgb_ebonleft");
    EXPECT_EQ(bank.bullet.collisionSound, "mgs_sith_hit");
    EXPECT_EQ(bank.bullet.damage, 30u);
    EXPECT_FLOAT_EQ(bank.bullet.lifespan, 3.0f);
    EXPECT_FLOAT_EQ(bank.bullet.rateOfFire, 0.3f);
    EXPECT_FLOAT_EQ(bank.bullet.speed, 300.0f);
    EXPECT_EQ(bank.bullet.targetType, 2u);
}

TEST(MinigameSpec, player_placement_and_tunnel_limits_are_parsed) {
    auto spec = parseMinigameSpec(makeEbonHawkTurretARE());

    ASSERT_TRUE(spec.has_value());
    EXPECT_EQ(spec->player.startOffset, glm::vec3(7.0f, 0.0f, 0.0f));
    EXPECT_EQ(spec->player.targetOffset, glm::vec3(0.0f, 0.0f, -5.0f));
    EXPECT_EQ(spec->player.tunnelInfinite, glm::vec3(0.0f, 0.0f, 1.0f));
    EXPECT_FLOAT_EQ(spec->player.tunnelXPos, 45.0f);
    EXPECT_FLOAT_EQ(spec->player.tunnelXNeg, 2.0f);
    EXPECT_TRUE(spec->player.cameraRotate);
    EXPECT_EQ(spec->player.cameraResRef, "m12ab_camera");
    EXPECT_EQ(spec->player.hitPoints, 3000u);
    EXPECT_EQ(spec->player.maxHitPoints, 3000u);
    EXPECT_FLOAT_EQ(spec->player.sphereRadius, 40.0f);
}

TEST(MinigameSpec, enemy_gun_banks_carry_aiming_parameters) {
    auto spec = parseMinigameSpec(makeEbonHawkTurretARE());

    ASSERT_TRUE(spec.has_value());
    ASSERT_EQ(spec->enemies.size(), 1u);
    const auto &enemy = spec->enemies.front();
    EXPECT_EQ(enemy.trackResRef, "m12ab_mgt02");
    EXPECT_EQ(enemy.hitPoints, 100u);
    EXPECT_FLOAT_EQ(enemy.sphereRadius, 20.0f);
    EXPECT_EQ(enemy.sounds.death, "mgs_sith_expl");
    EXPECT_EQ(enemy.scripts.onDeath, "k_pebo_sthdeath2");
    ASSERT_EQ(enemy.models.size(), 1u);
    EXPECT_EQ(enemy.models.front().resRef, "mgf_sithfighter");
    ASSERT_EQ(enemy.gunBanks.size(), 1u);
    const auto &bank = enemy.gunBanks.front();
    EXPECT_FLOAT_EQ(bank.sensingRadius, 200.0f);
    EXPECT_FLOAT_EQ(bank.inaccuracy, 0.01f);
    EXPECT_FLOAT_EQ(bank.horizSpread, 70.0f);
    EXPECT_FLOAT_EQ(bank.vertSpread, 70.0f);
    EXPECT_EQ(bank.bullet.damage, 10u);
}

TEST(MinigameSpec, track_resrefs_are_deduplicated_across_player_and_enemies) {
    auto are = makeEbonHawkTurretARE();
    // A second enemy sharing the first enemy's rail must not add a duplicate.
    auto sharedRail = are.MiniGame.Enemies.front();
    are.MiniGame.Enemies.push_back(sharedRail);

    auto spec = parseMinigameSpec(are);

    ASSERT_TRUE(spec.has_value());
    EXPECT_EQ(spec->enemies.size(), 2u);
    EXPECT_EQ(spec->trackResRefs, (std::vector<std::string> {"m12ab_mgt01", "m12ab_mgt02"}));
}

TEST(MinigameSpec, obstacles_are_parsed) {
    auto spec = parseMinigameSpec(makeEbonHawkTurretARE());

    ASSERT_TRUE(spec.has_value());
    ASSERT_EQ(spec->obstacles.size(), 1u);
    EXPECT_EQ(spec->obstacles.front().name, "m12ab_mgo01");
}
