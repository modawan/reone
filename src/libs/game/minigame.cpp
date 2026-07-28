/*
 * Copyright (c) 2020-2026 The reone project contributors
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

#include "reone/game/minigame.h"

#include "reone/resource/parser/gff/are.h"

namespace reone {

namespace game {

namespace {

template <class TModels>
std::vector<MinigameModelSpec> parseModels(const TModels &models) {
    std::vector<MinigameModelSpec> result;
    for (const auto &m : models) {
        if (m.Model.empty()) {
            continue;
        }
        MinigameModelSpec spec;
        spec.resRef = m.Model;
        spec.rotating = m.RotatingModel != 0;
        result.push_back(std::move(spec));
    }
    return result;
}

template <class TBullet>
MinigameBulletSpec parseBullet(const TBullet &bullet) {
    MinigameBulletSpec spec;
    spec.modelResRef = bullet.Bullet_Model;
    spec.collisionSound = bullet.Collision_Sound;
    spec.damage = bullet.Damage;
    spec.lifespan = bullet.Lifespan;
    spec.rateOfFire = bullet.Rate_Of_Fire;
    spec.speed = bullet.Speed;
    spec.targetType = bullet.Target_Type;
    return spec;
}

} // namespace

std::optional<MinigameSpec> parseMinigameSpec(const resource::generated::ARE &are) {
    if (are.MiniGame.Type == 0) {
        return std::nullopt;
    }
    MinigameSpec spec;
    spec.type = minigameTypeFromUint(are.MiniGame.Type);
    spec.cameraViewAngle = are.MiniGame.CameraViewAngle;
    spec.nearClip = are.MiniGame.Near_Clip;
    spec.farClip = are.MiniGame.Far_Clip;
    spec.lateralAccel = are.MiniGame.LateralAccel;
    spec.movementPerSec = are.MiniGame.MovementPerSec;
    spec.useInertia = are.MiniGame.UseInertia != 0;
    spec.bumpPlane = are.MiniGame.Bump_Plane;
    spec.depthOfField = are.MiniGame.DOF;
    spec.doBumping = are.MiniGame.DoBumping != 0;
    spec.music = are.MiniGame.Music;
    spec.mouse.axisX = are.MiniGame.Mouse.AxisX;
    spec.mouse.axisY = are.MiniGame.Mouse.AxisY;
    spec.mouse.flipAxisX = are.MiniGame.Mouse.FlipAxisX != 0;
    spec.mouse.flipAxisY = are.MiniGame.Mouse.FlipAxisY != 0;

    const auto &src = are.MiniGame.Player;
    spec.player.cameraResRef = src.Camera;
    spec.player.trackResRef = src.Track;
    spec.player.minimumSpeed = src.Minimum_Speed;
    spec.player.maximumSpeed = src.Maximum_Speed;
    spec.player.accelSecs = src.Accel_Secs;
    spec.player.sphereRadius = src.Sphere_Radius;
    spec.player.hitPoints = src.Hit_Points;
    spec.player.maxHitPoints = src.Max_HPs;
    spec.player.invincePeriod = src.Invince_Period;
    spec.player.bumpDamage = src.Bump_Damage;
    spec.player.numLoops = src.Num_Loops;
    spec.player.cameraRotate = src.CameraRotate != 0;
    spec.player.sounds.death = src.Sounds.Death;
    spec.player.sounds.engine = src.Sounds.Engine;
    spec.player.startOffset = glm::vec3(src.Start_Offset_X, src.Start_Offset_Y, src.Start_Offset_Z);
    spec.player.targetOffset = glm::vec3(src.Target_Offset_X, src.Target_Offset_Y, src.Target_Offset_Z);
    spec.player.tunnelXPos = src.TunnelXPos;
    spec.player.tunnelXNeg = src.TunnelXNeg;
    spec.player.tunnelYPos = src.TunnelYPos;
    spec.player.tunnelYNeg = src.TunnelYNeg;
    spec.player.tunnelZPos = src.TunnelZPos;
    spec.player.tunnelZNeg = src.TunnelZNeg;
    spec.player.tunnelInfinite = src.TunnelInfinite;
    spec.player.scripts.onCreate = src.Scripts.OnCreate;
    spec.player.scripts.onDeath = src.Scripts.OnDeath;
    spec.player.scripts.onTrackLoop = src.Scripts.OnTrackLoop;
    spec.player.scripts.onDamage = src.Scripts.OnDamage;
    spec.player.scripts.onAccelerate = src.Scripts.OnAccelerate;
    spec.player.scripts.onHeartbeat = src.Scripts.OnHeartbeat;
    spec.player.scripts.onFire = src.Scripts.OnFire;
    spec.player.scripts.onHitBullet = src.Scripts.OnHitBullet;
    spec.player.scripts.onHitFollower = src.Scripts.OnHitFollower;
    spec.player.scripts.onHitObstacle = src.Scripts.OnHitObstacle;
    spec.player.models = parseModels(src.Models);
    for (const auto &b : src.Gun_Banks) {
        MinigameGunBankSpec bank;
        bank.bankId = b.BankID;
        bank.gunModelResRef = b.Gun_Model;
        bank.fireSound = b.Fire_Sound;
        bank.bullet = parseBullet(b.Bullet);
        spec.player.gunBanks.push_back(std::move(bank));
    }

    std::set<std::string> seenTracks;
    auto addTrack = [&](const std::string &ref) {
        if (!ref.empty() && seenTracks.insert(ref).second) {
            spec.trackResRefs.push_back(ref);
        }
    };
    addTrack(src.Track);

    for (const auto &e : are.MiniGame.Enemies) {
        MinigameEnemySpec enemy;
        enemy.trackResRef = e.Track;
        enemy.hitPoints = e.Hit_Points;
        enemy.maxHitPoints = e.Max_HPs;
        enemy.sphereRadius = e.Sphere_Radius;
        enemy.invincePeriod = e.Invince_Period;
        enemy.bumpDamage = e.Bump_Damage;
        enemy.numLoops = e.Num_Loops;
        enemy.trigger = e.Trigger != 0;
        enemy.sounds.death = e.Sounds.Death;
        enemy.sounds.engine = e.Sounds.Engine;
        enemy.scripts.onCreate = e.Scripts.OnCreate;
        enemy.scripts.onDeath = e.Scripts.OnDeath;
        enemy.scripts.onDamage = e.Scripts.OnDamage;
        enemy.scripts.onHeartbeat = e.Scripts.OnHeartbeat;
        enemy.scripts.onTrackLoop = e.Scripts.OnTrackLoop;
        enemy.onCreate = e.Scripts.OnCreate;
        enemy.models = parseModels(e.Models);
        for (const auto &b : e.Gun_Banks) {
            MinigameGunBankSpec bank;
            bank.bankId = b.BankID;
            bank.gunModelResRef = b.Gun_Model;
            bank.fireSound = b.Fire_Sound;
            bank.bullet = parseBullet(b.Bullet);
            bank.horizSpread = b.Horiz_Spread;
            bank.vertSpread = b.Vert_Spread;
            bank.inaccuracy = b.Inaccuracy;
            bank.sensingRadius = b.Sensing_Radius;
            enemy.gunBanks.push_back(std::move(bank));
        }
        spec.enemies.push_back(std::move(enemy));
        addTrack(e.Track);
    }

    for (const auto &o : are.MiniGame.Obstacles) {
        MinigameObstacleSpec obs;
        obs.name = o.Name;
        obs.onCreate = o.Scripts.OnCreate;
        spec.obstacles.push_back(std::move(obs));
    }

    return spec;
}

} // namespace game

} // namespace reone
