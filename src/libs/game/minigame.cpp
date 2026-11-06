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

#include "reone/graphics/animation.h"
#include "reone/graphics/model.h"
#include "reone/graphics/modelnode.h"
#include "reone/graphics/types.h"
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

// KeyframeTrack::valueAtTime extrapolates past the last key, so clamp to it.
template <class Value>
bool valueAtTrackTime(const graphics::KeyframeTrack<Value> &track, float time, Value &value) {
    const auto &keys = track.keyframes();
    if (keys.empty()) {
        return false;
    }
    return track.valueAtTime(std::min(time, keys.back().time), value);
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

template <class TScripts>
MinigameFollowerScriptSpec parseFollowerScripts(const TScripts &scripts) {
    MinigameFollowerScriptSpec spec;
    spec.onCreate = scripts.OnCreate;
    spec.onHitBullet = scripts.OnHitBullet;
    spec.onHitFollower = scripts.OnHitFollower;
    spec.onAnimEvent = scripts.OnAnimEvent;
    spec.onHeartbeat = scripts.OnHeartbeat;
    spec.onDamage = scripts.OnDamage;
    spec.onDeath = scripts.OnDeath;
    spec.onFire = scripts.OnFire;
    spec.onHitObstacle = scripts.OnHitObstacle;
    spec.onTrackLoop = scripts.OnTrackLoop;
    spec.onAccelerate = scripts.OnAccelerate;
    spec.onBrake = scripts.OnBrake;
    spec.onHitWorld = scripts.OnHitWorld;
    return spec;
}

} // namespace

float minigameAccelerationPerSecond(float minimumSpeed, float maximumSpeed, float accelSecs) {
    if (accelSecs == 0.0f) {
        return maximumSpeed - minimumSpeed;
    }
    if (accelSecs < 0.0f) {
        return 1.0f;
    }
    return (maximumSpeed - minimumSpeed) / accelSecs;
}

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
    spec.player.accelPerSecond = minigameAccelerationPerSecond(src.Minimum_Speed, src.Maximum_Speed, src.Accel_Secs);
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
    spec.player.scripts = parseFollowerScripts(src.Scripts);
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
        enemy.scripts = parseFollowerScripts(e.Scripts);
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
        obs.scripts.onCreate = o.Scripts.OnCreate;
        obs.scripts.onHitBullet = o.Scripts.OnHitBullet;
        obs.scripts.onHitFollower = o.Scripts.OnHitFollower;
        obs.scripts.onAnimEvent = o.Scripts.OnAnimEvent;
        obs.scripts.onHeartbeat = o.Scripts.OnHeartbeat;
        spec.obstacles.push_back(std::move(obs));
    }

    return spec;
}

MinigameTrack::MinigameTrack(glm::mat4 base,
                             glm::vec3 restPosition,
                             glm::quat restOrientation,
                             graphics::KeyframeTrack<glm::vec3> positions,
                             graphics::KeyframeTrack<glm::quat> orientations,
                             float length) :
    _base(base),
    _restPosition(restPosition),
    _restOrientation(restOrientation),
    _positions(std::move(positions)),
    _orientations(std::move(orientations)),
    _length(length) {
    _positions.update();
    _orientations.update();
}

glm::mat4 MinigameTrack::hookAt(float time) const {
    glm::vec3 offset(0.0f);
    valueAtTrackTime(_positions, time, offset);
    glm::quat orientation(_restOrientation);
    valueAtTrackTime(_orientations, time, orientation);
    return _base * glm::translate(_restPosition + offset) * glm::mat4_cast(orientation);
}

std::optional<MinigameTrack> buildMinigameTrack(const graphics::Model &model, const glm::vec3 &placement) {
    auto hook = model.getNodeByNameRecursive("modelhook");
    if (!hook) {
        return std::nullopt;
    }
    glm::mat4 base(glm::translate(placement));
    if (hook->parent()) {
        base *= hook->parent()->absoluteTransform();
    }
    graphics::KeyframeTrack<glm::vec3> positions;
    graphics::KeyframeTrack<glm::quat> orientations;
    float length = 0.0f;
    if (auto anim = model.getAnimation("track")) {
        if (auto animHook = anim->getNodeByName("modelhook")) {
            length = anim->length();
            auto &vectors = animHook->vectorTracks();
            if (auto it = vectors.find(graphics::ControllerTypes::position); it != vectors.end()) {
                positions = it->second;
            }
            auto &quaternions = animHook->quaternionTracks();
            if (auto it = quaternions.find(graphics::ControllerTypes::orientation); it != quaternions.end()) {
                orientations = it->second;
            }
        }
    }
    return MinigameTrack(base, hook->restPosition(), hook->restOrientation(),
                         std::move(positions), std::move(orientations), length);
}

} // namespace game

} // namespace reone
