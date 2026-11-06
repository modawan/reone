/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <algorithm>
#include <cctype>

#include <gtest/gtest.h>

#include "reone/game/minigame.h"
#include "reone/resource/gff.h"
#include "reone/resource/parser/gff/are.h"

using namespace reone::game;
using namespace reone::resource;
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

// A Scripts struct whose every listed slot names a distinct script: the
// prefix plus the slot name without "On", lowercased, e.g. "pl_hitworld".
std::shared_ptr<Gff> makeScriptsGff(const std::string &prefix, const std::vector<std::string> &slots) {
    Gff::Builder builder;
    for (const auto &slot : slots) {
        std::string name = slot.substr(2);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        builder.field(Gff::Field::newResRef(slot, prefix + name));
    }
    return builder.build();
}

// The 13 follower script slots and the 5 obstacle script slots, as named in
// the .are MiniGame struct.
const std::vector<std::string> kFollowerSlots {
    "OnCreate", "OnHitBullet", "OnHitFollower", "OnAnimEvent", "OnHeartbeat", "OnDamage", "OnDeath",
    "OnFire", "OnHitObstacle", "OnTrackLoop", "OnAccelerate", "OnBrake", "OnHitWorld"};
const std::vector<std::string> kObstacleSlots {
    "OnCreate", "OnHitBullet", "OnHitFollower", "OnAnimEvent", "OnHeartbeat"};

std::shared_ptr<Gff> makeSoundsGff(const std::string &death, const std::string &engine) {
    return Gff::Builder()
        .field(Gff::Field::newResRef("Death", death))
        .field(Gff::Field::newResRef("Engine", engine))
        .build();
}

std::shared_ptr<Gff> makeModelGff(const std::string &model, uint32_t rotating) {
    return Gff::Builder()
        .field(Gff::Field::newResRef("Model", model))
        .field(Gff::Field::newByte("RotatingModel", rotating))
        .build();
}

std::shared_ptr<Gff> makeBulletGff(const std::string &model, uint32_t damage) {
    return Gff::Builder()
        .field(Gff::Field::newResRef("Bullet_Model", model))
        .field(Gff::Field::newResRef("Collision_Sound", model + "_hit"))
        .field(Gff::Field::newDword("Damage", damage))
        .field(Gff::Field::newFloat("Lifespan", 2.5f))
        .field(Gff::Field::newFloat("Rate_Of_Fire", 0.25f))
        .field(Gff::Field::newFloat("Speed", 400.0f))
        .field(Gff::Field::newDword("Target_Type", 2))
        .build();
}

// A swoop race MiniGame struct with every field of the .are format set to a
// distinct value, inside an ARE root, as the area loader reads it.
std::shared_ptr<Gff> makeSwoopAreGff() {
    auto playerBank = Gff::Builder()
                          .field(Gff::Field::newDword("BankID", 1))
                          .field(Gff::Field::newStruct("Bullet", makeBulletGff("pl_bullet", 7)))
                          .field(Gff::Field::newResRef("Fire_Sound", "pl_fire"))
                          .field(Gff::Field::newResRef("Gun_Model", "pl_gun"))
                          .build();
    auto player = Gff::Builder()
                      .field(Gff::Field::newFloat("Accel_Secs", 4.0f))
                      .field(Gff::Field::newInt("Bump_Damage", 3))
                      .field(Gff::Field::newResRef("Camera", "pl_camera"))
                      .field(Gff::Field::newByte("CameraRotate", 1))
                      .field(Gff::Field::newList("Gun_Banks", {playerBank}))
                      .field(Gff::Field::newDword("Hit_Points", 90))
                      .field(Gff::Field::newFloat("Invince_Period", 1.5f))
                      .field(Gff::Field::newDword("Max_HPs", 100))
                      .field(Gff::Field::newFloat("Maximum_Speed", 120.0f))
                      .field(Gff::Field::newFloat("Minimum_Speed", 20.0f))
                      .field(Gff::Field::newList("Models", {makeModelGff("pl_bike", 1), makeModelGff("pl_dash", 0)}))
                      .field(Gff::Field::newInt("Num_Loops", 2))
                      .field(Gff::Field::newStruct("Scripts", makeScriptsGff("pl_", kFollowerSlots)))
                      .field(Gff::Field::newStruct("Sounds", makeSoundsGff("pl_death", "pl_engine")))
                      .field(Gff::Field::newFloat("Sphere_Radius", 1.25f))
                      .field(Gff::Field::newFloat("Start_Offset_X", 1.0f))
                      .field(Gff::Field::newFloat("Start_Offset_Y", 2.0f))
                      .field(Gff::Field::newFloat("Start_Offset_Z", 3.0f))
                      .field(Gff::Field::newFloat("Target_Offset_X", 4.0f))
                      .field(Gff::Field::newFloat("Target_Offset_Y", 5.0f))
                      .field(Gff::Field::newFloat("Target_Offset_Z", 6.0f))
                      .field(Gff::Field::newResRef("Track", "pl_track"))
                      .field(Gff::Field::newVector("TunnelInfinite", glm::vec3(0.0f, 1.0f, 0.0f)))
                      .field(Gff::Field::newFloat("TunnelXNeg", -20.0f))
                      .field(Gff::Field::newFloat("TunnelXPos", 21.0f))
                      .field(Gff::Field::newFloat("TunnelYNeg", -22.0f))
                      .field(Gff::Field::newFloat("TunnelYPos", 23.0f))
                      .field(Gff::Field::newFloat("TunnelZNeg", -24.0f))
                      .field(Gff::Field::newFloat("TunnelZPos", 25.0f))
                      .build();
    auto enemyBank = Gff::Builder()
                         .field(Gff::Field::newDword("BankID", 2))
                         .field(Gff::Field::newStruct("Bullet", makeBulletGff("en_bullet", 11)))
                         .field(Gff::Field::newResRef("Fire_Sound", "en_fire"))
                         .field(Gff::Field::newResRef("Gun_Model", "en_gun"))
                         .field(Gff::Field::newFloat("Horiz_Spread", 30.0f))
                         .field(Gff::Field::newFloat("Inaccuracy", 0.5f))
                         .field(Gff::Field::newFloat("Sensing_Radius", 150.0f))
                         .field(Gff::Field::newFloat("Vert_Spread", 40.0f))
                         .build();
    auto enemy = Gff::Builder()
                     .field(Gff::Field::newInt("Bump_Damage", 5))
                     .field(Gff::Field::newList("Gun_Banks", {enemyBank}))
                     .field(Gff::Field::newDword("Hit_Points", 40))
                     .field(Gff::Field::newFloat("Invince_Period", 0.5f))
                     .field(Gff::Field::newDword("Max_HPs", 50))
                     .field(Gff::Field::newList("Models", {makeModelGff("en_pad", 0)}))
                     .field(Gff::Field::newInt("Num_Loops", -1))
                     .field(Gff::Field::newStruct("Scripts", makeScriptsGff("en_", kFollowerSlots)))
                     .field(Gff::Field::newStruct("Sounds", makeSoundsGff("en_death", "en_engine")))
                     .field(Gff::Field::newFloat("Sphere_Radius", 6.0f))
                     .field(Gff::Field::newResRef("Track", "en_track"))
                     .field(Gff::Field::newByte("Trigger", 1))
                     .build();
    auto obstacle = Gff::Builder()
                        .field(Gff::Field::newResRef("Name", "ob_wall"))
                        .field(Gff::Field::newStruct("Scripts", makeScriptsGff("ob_", kObstacleSlots)))
                        .build();
    auto mouse = Gff::Builder()
                     .field(Gff::Field::newDword("AxisX", 2))
                     .field(Gff::Field::newDword("AxisY", 3))
                     .field(Gff::Field::newByte("FlipAxisX", 1))
                     .field(Gff::Field::newByte("FlipAxisY", 0))
                     .build();
    auto miniGame = Gff::Builder()
                        .field(Gff::Field::newDword("Bump_Plane", 4))
                        .field(Gff::Field::newFloat("CameraViewAngle", 70.0f))
                        .field(Gff::Field::newDword("DOF", 6))
                        .field(Gff::Field::newByte("DoBumping", 1))
                        .field(Gff::Field::newList("Enemies", {enemy}))
                        .field(Gff::Field::newFloat("Far_Clip", 5000.0f))
                        .field(Gff::Field::newFloat("LateralAccel", 60.0f))
                        .field(Gff::Field::newStruct("Mouse", mouse))
                        .field(Gff::Field::newFloat("MovementPerSec", 100.0f))
                        .field(Gff::Field::newResRef("Music", "mg_music"))
                        .field(Gff::Field::newFloat("Near_Clip", 0.5f))
                        .field(Gff::Field::newList("Obstacles", {obstacle}))
                        .field(Gff::Field::newStruct("Player", player))
                        .field(Gff::Field::newDword("Type", 1))
                        .field(Gff::Field::newByte("UseInertia", 1))
                        .build();
    return Gff::Builder().field(Gff::Field::newStruct("MiniGame", miniGame)).build();
}

std::vector<std::string> followerScripts(const MinigameFollowerScriptSpec &s) {
    return {s.onCreate, s.onHitBullet, s.onHitFollower, s.onAnimEvent, s.onHeartbeat, s.onDamage, s.onDeath,
            s.onFire, s.onHitObstacle, s.onTrackLoop, s.onAccelerate, s.onBrake, s.onHitWorld};
}

std::vector<std::string> obstacleScripts(const MinigameObstacleScriptSpec &s) {
    return {s.onCreate, s.onHitBullet, s.onHitFollower, s.onAnimEvent, s.onHeartbeat};
}

// The names makeScriptsGff gives the first count follower slots.
std::vector<std::string> expectedScripts(const std::string &prefix, size_t count) {
    std::vector<std::string> names {"create", "hitbullet", "hitfollower", "animevent", "heartbeat", "damage", "death",
                                    "fire", "hitobstacle", "trackloop", "accelerate", "brake", "hitworld"};
    names.resize(count);
    for (auto &name : names) {
        name = prefix + name;
    }
    return names;
}

std::optional<MinigameSpec> parseGff(const Gff &gff) {
    return parseMinigameSpec(parseARE(gff));
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

TEST(MinigameSpec, gff_area_level_fields_and_music_are_parsed) {
    auto spec = parseGff(*makeSwoopAreGff());

    ASSERT_TRUE(spec.has_value());
    EXPECT_EQ(spec->type, MinigameType::SwoopRace);
    EXPECT_EQ(spec->bumpPlane, 4u);
    EXPECT_FLOAT_EQ(spec->cameraViewAngle, 70.0f);
    EXPECT_EQ(spec->depthOfField, 6u);
    EXPECT_TRUE(spec->doBumping);
    EXPECT_FLOAT_EQ(spec->farClip, 5000.0f);
    EXPECT_FLOAT_EQ(spec->lateralAccel, 60.0f);
    EXPECT_FLOAT_EQ(spec->movementPerSec, 100.0f);
    EXPECT_EQ(spec->music, "mg_music");
    EXPECT_FLOAT_EQ(spec->nearClip, 0.5f);
    EXPECT_TRUE(spec->useInertia);
    EXPECT_EQ(spec->mouse.axisX, 2u);
    EXPECT_EQ(spec->mouse.axisY, 3u);
    EXPECT_TRUE(spec->mouse.flipAxisX);
    EXPECT_FALSE(spec->mouse.flipAxisY);
    EXPECT_EQ(spec->trackResRefs, (std::vector<std::string> {"pl_track", "en_track"}));
}

TEST(MinigameSpec, gff_player_fields_are_parsed) {
    auto spec = parseGff(*makeSwoopAreGff());

    ASSERT_TRUE(spec.has_value());
    const auto &player = spec->player;
    EXPECT_FLOAT_EQ(player.accelSecs, 4.0f);
    EXPECT_EQ(player.bumpDamage, 3);
    EXPECT_EQ(player.cameraResRef, "pl_camera");
    EXPECT_TRUE(player.cameraRotate);
    EXPECT_EQ(player.hitPoints, 90u);
    EXPECT_FLOAT_EQ(player.invincePeriod, 1.5f);
    EXPECT_EQ(player.maxHitPoints, 100u);
    EXPECT_FLOAT_EQ(player.maximumSpeed, 120.0f);
    EXPECT_FLOAT_EQ(player.minimumSpeed, 20.0f);
    EXPECT_EQ(player.numLoops, 2);
    EXPECT_EQ(player.sounds.death, "pl_death");
    EXPECT_EQ(player.sounds.engine, "pl_engine");
    EXPECT_FLOAT_EQ(player.sphereRadius, 1.25f);
    EXPECT_EQ(player.startOffset, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(player.targetOffset, glm::vec3(4.0f, 5.0f, 6.0f));
    EXPECT_EQ(player.trackResRef, "pl_track");
    EXPECT_EQ(player.tunnelInfinite, glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_FLOAT_EQ(player.tunnelXNeg, -20.0f);
    EXPECT_FLOAT_EQ(player.tunnelXPos, 21.0f);
    EXPECT_FLOAT_EQ(player.tunnelYNeg, -22.0f);
    EXPECT_FLOAT_EQ(player.tunnelYPos, 23.0f);
    EXPECT_FLOAT_EQ(player.tunnelZNeg, -24.0f);
    EXPECT_FLOAT_EQ(player.tunnelZPos, 25.0f);
    ASSERT_EQ(player.models.size(), 2u);
    EXPECT_EQ(player.models[0].resRef, "pl_bike");
    EXPECT_TRUE(player.models[0].rotating);
    EXPECT_EQ(player.models[1].resRef, "pl_dash");
    EXPECT_FALSE(player.models[1].rotating);
    ASSERT_EQ(player.gunBanks.size(), 1u);
    const auto &bank = player.gunBanks.front();
    EXPECT_EQ(bank.bankId, 1u);
    EXPECT_EQ(bank.gunModelResRef, "pl_gun");
    EXPECT_EQ(bank.fireSound, "pl_fire");
    EXPECT_EQ(bank.bullet.modelResRef, "pl_bullet");
    EXPECT_EQ(bank.bullet.collisionSound, "pl_bullet_hit");
    EXPECT_EQ(bank.bullet.damage, 7u);
    EXPECT_FLOAT_EQ(bank.bullet.lifespan, 2.5f);
    EXPECT_FLOAT_EQ(bank.bullet.rateOfFire, 0.25f);
    EXPECT_FLOAT_EQ(bank.bullet.speed, 400.0f);
    EXPECT_EQ(bank.bullet.targetType, 2u);
}

TEST(MinigameSpec, gff_player_has_all_thirteen_script_slots) {
    auto spec = parseGff(*makeSwoopAreGff());

    ASSERT_TRUE(spec.has_value());
    EXPECT_EQ(followerScripts(spec->player.scripts), expectedScripts("pl_", 13));
}

TEST(MinigameSpec, gff_enemy_fields_and_all_thirteen_script_slots_are_parsed) {
    auto spec = parseGff(*makeSwoopAreGff());

    ASSERT_TRUE(spec.has_value());
    ASSERT_EQ(spec->enemies.size(), 1u);
    const auto &enemy = spec->enemies.front();
    EXPECT_EQ(enemy.bumpDamage, 5);
    EXPECT_EQ(enemy.hitPoints, 40u);
    EXPECT_FLOAT_EQ(enemy.invincePeriod, 0.5f);
    EXPECT_EQ(enemy.maxHitPoints, 50u);
    EXPECT_EQ(enemy.numLoops, -1);
    EXPECT_EQ(enemy.sounds.death, "en_death");
    EXPECT_EQ(enemy.sounds.engine, "en_engine");
    EXPECT_FLOAT_EQ(enemy.sphereRadius, 6.0f);
    EXPECT_EQ(enemy.trackResRef, "en_track");
    EXPECT_TRUE(enemy.trigger);
    ASSERT_EQ(enemy.models.size(), 1u);
    EXPECT_EQ(enemy.models.front().resRef, "en_pad");
    EXPECT_FALSE(enemy.models.front().rotating);
    ASSERT_EQ(enemy.gunBanks.size(), 1u);
    const auto &bank = enemy.gunBanks.front();
    EXPECT_EQ(bank.bankId, 2u);
    EXPECT_EQ(bank.gunModelResRef, "en_gun");
    EXPECT_EQ(bank.fireSound, "en_fire");
    EXPECT_FLOAT_EQ(bank.horizSpread, 30.0f);
    EXPECT_FLOAT_EQ(bank.inaccuracy, 0.5f);
    EXPECT_FLOAT_EQ(bank.sensingRadius, 150.0f);
    EXPECT_FLOAT_EQ(bank.vertSpread, 40.0f);
    EXPECT_EQ(bank.bullet.modelResRef, "en_bullet");
    EXPECT_EQ(bank.bullet.damage, 11u);
    EXPECT_EQ(followerScripts(enemy.scripts), expectedScripts("en_", 13));
    EXPECT_EQ(enemy.onCreate, "en_create");
}

TEST(MinigameSpec, gff_obstacle_has_all_five_script_slots) {
    auto spec = parseGff(*makeSwoopAreGff());

    ASSERT_TRUE(spec.has_value());
    ASSERT_EQ(spec->obstacles.size(), 1u);
    const auto &obstacle = spec->obstacles.front();
    EXPECT_EQ(obstacle.name, "ob_wall");
    EXPECT_EQ(obstacleScripts(obstacle.scripts), expectedScripts("ob_", 5));
}

TEST(MinigameSpec, gff_missing_fields_take_defaults) {
    // Only the type is set. The enemy and the obstacle are empty structs, and
    // the Player, Mouse, Scripts and Sounds structs are absent.
    auto miniGame = Gff::Builder()
                        .field(Gff::Field::newDword("Type", 1))
                        .field(Gff::Field::newList("Enemies", {Gff::Builder().build()}))
                        .field(Gff::Field::newList("Obstacles", {Gff::Builder().build()}))
                        .build();
    auto root = Gff::Builder().field(Gff::Field::newStruct("MiniGame", miniGame)).build();

    auto spec = parseGff(*root);

    ASSERT_TRUE(spec.has_value());
    EXPECT_EQ(spec->type, MinigameType::SwoopRace);
    EXPECT_TRUE(spec->music.empty());
    EXPECT_FLOAT_EQ(spec->movementPerSec, 0.0f);
    EXPECT_FALSE(spec->useInertia);
    EXPECT_FALSE(spec->mouse.flipAxisX);
    EXPECT_TRUE(spec->trackResRefs.empty());

    const auto &player = spec->player;
    EXPECT_TRUE(player.trackResRef.empty());
    EXPECT_TRUE(player.models.empty());
    EXPECT_TRUE(player.gunBanks.empty());
    EXPECT_TRUE(player.sounds.engine.empty());
    EXPECT_FLOAT_EQ(player.minimumSpeed, 0.0f);
    EXPECT_FLOAT_EQ(player.maximumSpeed, 0.0f);
    EXPECT_FLOAT_EQ(player.accelSecs, 0.0f);
    EXPECT_FLOAT_EQ(player.accelPerSecond, 0.0f);
    EXPECT_EQ(player.hitPoints, 0u);
    EXPECT_EQ(player.numLoops, 0);
    EXPECT_EQ(player.startOffset, glm::vec3(0.0f));
    EXPECT_EQ(player.tunnelInfinite, glm::vec3(0.0f));
    EXPECT_EQ(followerScripts(player.scripts), std::vector<std::string>(13));

    ASSERT_EQ(spec->enemies.size(), 1u);
    const auto &enemy = spec->enemies.front();
    EXPECT_TRUE(enemy.trackResRef.empty());
    EXPECT_FALSE(enemy.trigger);
    EXPECT_EQ(enemy.numLoops, 0);
    EXPECT_EQ(followerScripts(enemy.scripts), std::vector<std::string>(13));
    ASSERT_EQ(spec->obstacles.size(), 1u);
    EXPECT_TRUE(spec->obstacles.front().name.empty());
    EXPECT_EQ(obstacleScripts(spec->obstacles.front().scripts), std::vector<std::string>(5));
}

TEST(MinigameSpec, gff_accel_secs_sets_the_acceleration_per_second) {
    // makeSwoopAreGff has minimum speed 20 and maximum 120: a range of 100.
    auto parseWithAccelSecs = [](float accelSecs) {
        auto gff = makeSwoopAreGff();
        auto player = gff->findStruct("MiniGame")->findStruct("Player");
        for (auto &field : player->fields()) {
            if (field.label == "Accel_Secs") {
                field.floatValue = accelSecs;
            }
        }
        return parseGff(*gff)->player.accelPerSecond;
    };

    EXPECT_FLOAT_EQ(parseWithAccelSecs(4.0f), 25.0f); // the range over the seconds
    EXPECT_FLOAT_EQ(parseWithAccelSecs(0.5f), 200.0f);
    EXPECT_FLOAT_EQ(parseWithAccelSecs(0.0f), 100.0f); // the whole range in one second
    EXPECT_FLOAT_EQ(parseWithAccelSecs(-2.0f), 1.0f); // negative leaves it at 1
}
