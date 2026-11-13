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

#include <algorithm>
#include <cmath>

#include <boost/algorithm/string.hpp>

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

const char *minigameSlotName(MinigameSlot slot) {
    switch (slot) {
    case MinigameSlot::OnCreate:
        return "OnCreate";
    case MinigameSlot::OnHitBullet:
        return "OnHitBullet";
    case MinigameSlot::OnHitFollower:
        return "OnHitFollower";
    case MinigameSlot::OnAnimEvent:
        return "OnAnimEvent";
    case MinigameSlot::OnHeartbeat:
        return "OnHeartbeat";
    case MinigameSlot::OnDamage:
        return "OnDamage";
    case MinigameSlot::OnDeath:
        return "OnDeath";
    case MinigameSlot::OnFire:
        return "OnFire";
    case MinigameSlot::OnHitObstacle:
        return "OnHitObstacle";
    case MinigameSlot::OnTrackLoop:
        return "OnTrackLoop";
    case MinigameSlot::OnAccelerate:
        return "OnAccelerate";
    case MinigameSlot::OnBrake:
        return "OnBrake";
    case MinigameSlot::OnHitWorld:
        return "OnHitWorld";
    default:
        return "?";
    }
}

MinigameScripts minigameScripts(const MinigameFollowerScriptSpec &spec) {
    MinigameScripts scripts;
    scripts[static_cast<size_t>(MinigameSlot::OnCreate)] = spec.onCreate;
    scripts[static_cast<size_t>(MinigameSlot::OnHitBullet)] = spec.onHitBullet;
    scripts[static_cast<size_t>(MinigameSlot::OnHitFollower)] = spec.onHitFollower;
    scripts[static_cast<size_t>(MinigameSlot::OnAnimEvent)] = spec.onAnimEvent;
    scripts[static_cast<size_t>(MinigameSlot::OnHeartbeat)] = spec.onHeartbeat;
    scripts[static_cast<size_t>(MinigameSlot::OnDamage)] = spec.onDamage;
    scripts[static_cast<size_t>(MinigameSlot::OnDeath)] = spec.onDeath;
    scripts[static_cast<size_t>(MinigameSlot::OnFire)] = spec.onFire;
    scripts[static_cast<size_t>(MinigameSlot::OnHitObstacle)] = spec.onHitObstacle;
    scripts[static_cast<size_t>(MinigameSlot::OnTrackLoop)] = spec.onTrackLoop;
    scripts[static_cast<size_t>(MinigameSlot::OnAccelerate)] = spec.onAccelerate;
    scripts[static_cast<size_t>(MinigameSlot::OnBrake)] = spec.onBrake;
    scripts[static_cast<size_t>(MinigameSlot::OnHitWorld)] = spec.onHitWorld;
    return scripts;
}

MinigameScripts minigameScripts(const MinigameObstacleScriptSpec &spec) {
    MinigameScripts scripts;
    scripts[static_cast<size_t>(MinigameSlot::OnCreate)] = spec.onCreate;
    scripts[static_cast<size_t>(MinigameSlot::OnHitBullet)] = spec.onHitBullet;
    scripts[static_cast<size_t>(MinigameSlot::OnHitFollower)] = spec.onHitFollower;
    scripts[static_cast<size_t>(MinigameSlot::OnAnimEvent)] = spec.onAnimEvent;
    scripts[static_cast<size_t>(MinigameSlot::OnHeartbeat)] = spec.onHeartbeat;
    return scripts;
}

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
    // A binding beyond the three axes binds none.
    spec.mouse.axisX = are.MiniGame.Mouse.AxisX <= 3 ? are.MiniGame.Mouse.AxisX : 0;
    spec.mouse.axisY = are.MiniGame.Mouse.AxisY <= 3 ? are.MiniGame.Mouse.AxisY : 0;
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

MinigameTrack MinigameTrack::fixed(const glm::vec3 &position) {
    return MinigameTrack(glm::translate(position), glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), {}, {}, 0.0f);
}

glm::mat4 MinigameTrack::hookAt(float time) const {
    glm::vec3 offset(0.0f);
    _positions.valueAtTime(time, offset);
    glm::quat orientation(_restOrientation);
    _orientations.valueAtTime(time, orientation);
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

namespace {

constexpr float kDefaultSwoopMovementPerSec = 6.0f;
constexpr float kTunnelWallTolerance = 0.01f;
constexpr float kBumpClearance = 0.1f;

} // namespace

// Geometry

float minigameSegmentDistance(const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec3 &q0, const glm::vec3 &q1) {
    glm::vec3 d1 = p1 - p0;
    glm::vec3 d2 = q1 - q0;
    glm::vec3 r = p0 - q0;
    float a = glm::dot(d1, d1);
    float e = glm::dot(d2, d2);
    float f = glm::dot(d2, r);
    float s = 0.0f;
    float t = 0.0f;
    constexpr float eps = 1e-8f;
    if (a <= eps && e <= eps) {
        return glm::length(r);
    }
    if (a <= eps) {
        t = glm::clamp(f / e, 0.0f, 1.0f);
    } else {
        float c = glm::dot(d1, r);
        if (e <= eps) {
            s = glm::clamp(-c / a, 0.0f, 1.0f);
        } else {
            float b = glm::dot(d1, d2);
            float denom = a * e - b * b;
            s = denom > eps ? glm::clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0.0f) {
                t = 0.0f;
                s = glm::clamp(-c / a, 0.0f, 1.0f);
            } else if (t > 1.0f) {
                t = 1.0f;
                s = glm::clamp((b - c) / a, 0.0f, 1.0f);
            }
        }
    }
    return glm::length((p0 + d1 * s) - (q0 + d2 * t));
}

bool minigameSegmentHitsBox2D(const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec3 &boxMin, const glm::vec3 &boxMax) {
    float tmin = 0.0f;
    float tmax = 1.0f;
    for (int axis = 0; axis < 2; ++axis) {
        float origin = p0[axis];
        float dir = p1[axis] - p0[axis];
        if (glm::abs(dir) < 1e-8f) {
            if (origin < boxMin[axis] || origin > boxMax[axis]) {
                return false;
            }
            continue;
        }
        float t1 = (boxMin[axis] - origin) / dir;
        float t2 = (boxMax[axis] - origin) / dir;
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        tmin = glm::max(tmin, t1);
        tmax = glm::min(tmax, t2);
        if (tmin > tmax) {
            return false;
        }
    }
    return true;
}

float minigameCurveDrift(const glm::mat4 &previousHook, const glm::mat4 &hook, float dt) {
    if (dt <= 0.0f) {
        return 0.0f;
    }
    // The hook's X axis is the steering axis and its Z axis is up: the turn
    // is the angle the X axis swept about Z. A left turn is positive, and
    // its outside is +X.
    glm::vec3 x0 = glm::vec3(previousHook[0]);
    glm::vec3 x1 = glm::vec3(hook[0]);
    glm::vec3 up = glm::vec3(hook[2]);
    if (glm::length(x0) < 1e-6f || glm::length(x1) < 1e-6f || glm::length(up) < 1e-6f) {
        return 0.0f;
    }
    x0 = glm::normalize(x0);
    x1 = glm::normalize(x1);
    up = glm::normalize(up);
    float turn = std::atan2(glm::dot(glm::cross(x0, x1), up), glm::dot(x0, x1));
    float forward = glm::length(glm::vec3(hook[3]) - glm::vec3(previousHook[3])) / dt;
    return kMinigameCurveDrift * forward * turn / dt;
}

void setMinigameShiftKey(MinigameInput &input, bool held, bool tsl) {
    input.fire = held && !tsl;
    input.accelerate = held && tsl;
}

// Session

void MinigameSession::load(const MinigameSpec &spec,
                           MinigameTrack playerTrack,
                           std::vector<MinigameTrack> enemyTracks,
                           std::vector<ObstaclePlacement> obstacles) {
    _actors.clear();
    _enemyList.clear();
    _obstacleList.clear();
    _ticks = 0;
    _elapsed = 0.0f;
    _scriptsRun = 0;
    _started = false;
    _speedHeld = false;
    _accelerateHeld = false;
    _brakeHeld = false;
    _lateralVelocity = glm::vec3(0.0f);
    _previousInput = glm::vec3(0.0f);
    _jumpSpeed = 0.0f;

    _type = spec.type;
    _lateralAccel = spec.lateralAccel;
    _movementPerSec = spec.movementPerSec;
    if (_movementPerSec <= 0.0f && _type == MinigameType::SwoopRace) {
        _movementPerSec = kDefaultSwoopMovementPerSec;
    }
    _useInertia = spec.useInertia;
    _doBumping = spec.doBumping;
    _nearClip = spec.nearClip;
    _farClip = spec.farClip;
    _speedBlur = false;
    _speedBlurRatio = 0.0f;

    auto applyFollower = [](MinigameActor &actor,
                            uint32_t hitPoints,
                            uint32_t maxHitPoints,
                            float sphereRadius,
                            float invincePeriod,
                            int bumpDamage,
                            int numLoops) {
        // The follower setters keep their defaults for out-of-range values.
        if (static_cast<int>(hitPoints) > 0) {
            actor.hitPoints = static_cast<int>(hitPoints);
        }
        if (static_cast<int>(maxHitPoints) > 0) {
            actor.maxHitPoints = static_cast<int>(maxHitPoints);
        }
        if (sphereRadius >= 0.0f) {
            actor.sphereRadius = sphereRadius;
        }
        if (invincePeriod >= 0.0f) {
            actor.invincePeriod = invincePeriod;
        }
        actor.bumpDamage = bumpDamage;
        actor.numLoops = numLoops;
    };

    // Player
    MinigameActor player;
    player.kind = MinigameActorKind::Player;
    player.models = spec.player.models;
    player.name = spec.player.models.empty() ? std::string() : spec.player.models.front().resRef;
    player.scripts = minigameScripts(spec.player.scripts);
    player.sounds = spec.player.sounds;
    applyFollower(player, spec.player.hitPoints, spec.player.maxHitPoints, spec.player.sphereRadius,
                  spec.player.invincePeriod, spec.player.bumpDamage, spec.player.numLoops);
    for (const auto &bank : spec.player.gunBanks) {
        player.gunBanks.push_back({bank, 0.0f});
    }
    player.track = std::move(playerTrack);
    _actors.push_back(std::move(player));

    _minSpeed = spec.player.minimumSpeed;
    _maxSpeed = 0.0f;
    if (spec.player.maximumSpeed >= 0.0f) {
        _maxSpeed = spec.player.maximumSpeed;
    }
    _accelPerSecond = spec.player.accelPerSecond;
    _tunnelPos = glm::vec3(spec.player.tunnelXPos, spec.player.tunnelYPos, spec.player.tunnelZPos);
    _tunnelNeg = glm::vec3(spec.player.tunnelXNeg, spec.player.tunnelYNeg, spec.player.tunnelZNeg);
    _tunnelInfinite = spec.player.tunnelInfinite;
    _startOffset = spec.player.startOffset;
    _offset = _startOffset;
    _targetOffset = spec.player.targetOffset;

    // Enemies
    for (size_t i = 0; i < spec.enemies.size(); ++i) {
        const auto &enemySpec = spec.enemies[i];
        MinigameActor enemy;
        enemy.kind = MinigameActorKind::Enemy;
        enemy.specIndex = i;
        enemy.models = enemySpec.models;
        enemy.name = enemySpec.models.empty() ? enemySpec.trackResRef : enemySpec.models.front().resRef;
        enemy.scripts = minigameScripts(enemySpec.scripts);
        enemy.sounds = enemySpec.sounds;
        enemy.trigger = enemySpec.trigger;
        applyFollower(enemy, enemySpec.hitPoints, enemySpec.maxHitPoints, enemySpec.sphereRadius,
                      enemySpec.invincePeriod, enemySpec.bumpDamage, enemySpec.numLoops);
        for (const auto &bank : enemySpec.gunBanks) {
            enemy.gunBanks.push_back({bank, 0.0f});
        }
        if (i < enemyTracks.size()) {
            enemy.track = std::move(enemyTracks[i]);
        }
        _enemyList.push_back(_actors.size());
        _actors.push_back(std::move(enemy));
    }

    // Obstacles
    for (size_t i = 0; i < obstacles.size(); ++i) {
        auto &placement = obstacles[i];
        MinigameActor obstacle;
        obstacle.kind = MinigameActorKind::Obstacle;
        obstacle.specIndex = i;
        obstacle.name = placement.name;
        if (i < spec.obstacles.size()) {
            obstacle.scripts = minigameScripts(spec.obstacles[i].scripts);
        }
        obstacle.track = MinigameTrack::fixed(placement.position);
        obstacle.hasHitbump = placement.hasHitbump;
        obstacle.hitbumpMin = placement.hitbumpMin;
        obstacle.hitbumpMax = placement.hitbumpMax;
        _obstacleList.push_back(_actors.size());
        _actors.push_back(std::move(obstacle));
    }

    for (auto &actor : _actors) {
        placeActor(actor);
        actor.previousPosition = actor.position;
    }
}

void MinigameSession::start() {
    if (_started || _actors.empty()) {
        return;
    }
    _started = true;
    // Creation order: the player, then the enemies in file order, then the
    // obstacles. The lists are copied: a creation script may change them.
    std::vector<size_t> order;
    order.push_back(0);
    order.insert(order.end(), _enemyList.begin(), _enemyList.end());
    order.insert(order.end(), _obstacleList.begin(), _obstacleList.end());
    for (size_t index : order) {
        runScriptSlot(_actors[index], MinigameSlot::OnCreate);
    }
}

bool MinigameSession::runScriptSlot(MinigameActor &actor, MinigameSlot slot) {
    // Every slot runs from here: OnCreate OnHitBullet OnHitFollower
    // OnAnimEvent OnHeartbeat OnDamage OnDeath OnFire OnHitObstacle
    // OnTrackLoop OnAccelerate OnBrake OnHitWorld.
    const std::string &script = actor.scripts[static_cast<size_t>(slot)];
    if (script.empty()) {
        return false;
    }
    ++_scriptsRun;
    _host.runScript(script, actor.objectId);
    // An authored script replaces the engine default; it can still ask for
    // it through the SWMG_On* routines.
    return true;
}

void MinigameSession::update(float dt, const MinigameInput &input) {
    if (_actors.empty() || dt <= 0.0f) {
        return;
    }
    ++_ticks;
    _elapsed += dt;

    auto &playerActor = _actors.front();
    // A press of accelerate or brake runs the player's slot once; holding the
    // key does not run it again.
    bool accelerate = input.accelerate && !_accelerateHeld;
    bool brake = input.brake && !_brakeHeld;
    _accelerateHeld = input.accelerate;
    _brakeHeld = input.brake;
    if (!playerActor.dead && accelerate) {
        runScriptSlot(playerActor, MinigameSlot::OnAccelerate);
    }
    if (!playerActor.dead && brake) {
        runScriptSlot(playerActor, MinigameSlot::OnBrake);
    }
    if (!playerActor.dead) {
        updatePlayerMotion(dt, input);
    }

    // Object updates: the player and every listed enemy run their heartbeat
    // and count down their invulnerability; enemies move along their tracks.
    std::vector<size_t> order;
    if (!playerActor.dead) {
        order.push_back(0);
    }
    order.insert(order.end(), _enemyList.begin(), _enemyList.end());
    for (size_t index : order) {
        auto &actor = _actors[index];
        if (index != 0) {
            actor.previousPosition = actor.position;
            advanceTrack(actor, dt);
            placeActor(actor);
        }
        updateFollower(actor, dt);
    }
    std::vector<size_t> obstacles(_obstacleList);
    for (size_t index : obstacles) {
        runScriptSlot(_actors[index], MinigameSlot::OnHeartbeat);
    }

    updatePlayerGuns(dt, input.fire);

    if (!_actors.front().dead) {
        collideObstacles();
    }
    if (!_actors.front().dead) {
        collideFollowers();
    }
}

void MinigameSession::updateFollower(MinigameActor &actor, float dt) {
    runScriptSlot(actor, MinigameSlot::OnHeartbeat);
    if (actor.invulnerableTime > 0.0f) {
        actor.invulnerableTime = glm::max(0.0f, actor.invulnerableTime - dt);
    }
}

void MinigameSession::updatePlayerMotion(float dt, const MinigameInput &input) {
    auto &p = _actors.front();
    p.previousPosition = p.position;

    // Speed moves toward the maximum at the acceleration per second, unless a
    // bump already set it this update.
    if (!_speedHeld && p.speed != _maxSpeed) {
        float step = _accelPerSecond * dt;
        if (_maxSpeed <= p.speed) {
            p.speed = (p.speed - step <= _maxSpeed) ? _maxSpeed : p.speed - step;
        } else {
            p.speed = (_maxSpeed <= p.speed + step) ? _maxSpeed : p.speed + step;
        }
    }
    _speedHeld = false;

    // The track moves first: the steering reads how it turned this update.
    glm::mat4 previousHook = p.hook;
    float previousTrackTime = p.trackTime;
    advanceTrack(p, dt);
    if (_type == MinigameType::SwoopRace) {
        float drift = 0.0f;
        // A track that looped this update jumped back to its start: no curve.
        if (_useInertia && p.trackTime >= previousTrackTime) {
            drift = minigameCurveDrift(previousHook, p.track.hookAt(p.trackTime), dt);
        }
        steerPlayer(dt, input, drift);
        updateJump(dt);
    }
    placeActor(p);
}

void MinigameSession::steerPlayer(float dt, const MinigameInput &input, float drift) {
    // The lateral speed follows the steering through a lag:
    //   dv/dt = L * u - (L / M) * v + drift - pull
    // with L the lateral acceleration, M the top lateral speed (MovementPerSec)
    // and u the steering, blended from the previous update's to this one's.
    // At full steering the speed settles at M, in a time of about M / L. The
    // drift is the outward push of a curve (UseInertia). On TSL, the pull
    // brings a bike that is not steered back to the centre of the tunnel, and
    // steering against a fast slide brakes it. 4th-order Runge-Kutta.
    glm::vec3 in(glm::clamp(input.steer, -1.0f, 1.0f), 0.0f, 0.0f);
    float top = _movementPerSec;
    glm::vec3 delta(0.0f);
    if (top > 0.0f) {
        float L = _lateralAccel;
        float damping = L / top;
        glm::vec3 u0 = _previousInput;
        glm::vec3 um = (_previousInput + in) * 0.5f;
        glm::vec3 u1 = in;
        glm::vec3 v = _lateralVelocity;
        if (_tsl && in.x * v.x < 0.0f && glm::abs(v.x) > kMinigameSlideBrakeShare * top) {
            v.x *= glm::exp(-kMinigameSlideBrakeRate * dt);
        }
        bool pull = _tsl && in.x == 0.0f;
        float centre = _startOffset.x;
        float x0 = _offset.x;
        auto accel = [&](const glm::vec3 &vel, const glm::vec3 &u, float x) {
            glm::vec3 a = L * u - damping * vel;
            a.x += drift;
            if (pull) {
                a.x -= kMinigameCentrePull * (x - centre);
            }
            return a;
        };
        glm::vec3 k1v = accel(v, u0, x0);
        glm::vec3 k1x = v;
        glm::vec3 k2v = accel(v + 0.5f * dt * k1v, um, x0 + 0.5f * dt * k1x.x);
        glm::vec3 k2x = v + 0.5f * dt * k1v;
        glm::vec3 k3v = accel(v + 0.5f * dt * k2v, um, x0 + 0.5f * dt * k2x.x);
        glm::vec3 k3x = v + 0.5f * dt * k2v;
        glm::vec3 k4v = accel(v + dt * k3v, u1, x0 + dt * k3x.x);
        glm::vec3 k4x = v + dt * k3v;
        _lateralVelocity = v + (dt / 6.0f) * (k1v + 2.0f * k2v + 2.0f * k3v + k4v);
        delta = (dt / 6.0f) * (k1x + 2.0f * k2x + 2.0f * k3x + k4x);
    } else {
        _lateralVelocity = glm::vec3(0.0f);
    }
    _previousInput = in;

    glm::vec3 unclamped = _offset + delta;
    glm::vec3 clamped = unclamped;
    clampOffset(clamped);
    if (glm::abs(unclamped.x - clamped.x) > kTunnelWallTolerance) {
        _lateralVelocity = glm::vec3(0.0f);
    }
    _offset = clamped;
}

void MinigameSession::updateJump(float dt) {
    // A jump (TSL): the vertical speed lifts the bike off the track and
    // gravity brings it back. The tunnel's Z limit is the ceiling: the bike
    // stops rising there. It lands at its starting height.
    if (_jumpSpeed == 0.0f && _offset.z <= _startOffset.z) {
        return;
    }
    glm::vec3 lifted = _offset;
    lifted.z += _jumpSpeed * dt;
    _jumpSpeed -= kMinigameGravity * dt;
    glm::vec3 held = lifted;
    clampOffset(held);
    if (held.z < lifted.z - kTunnelWallTolerance) {
        _jumpSpeed = glm::min(_jumpSpeed, 0.0f);
    }
    if (held.z <= _startOffset.z) {
        held.z = _startOffset.z;
        _jumpSpeed = 0.0f;
    }
    _offset.z = held.z;
}

void MinigameSession::clampOffset(glm::vec3 &offset) const {
    for (int axis = 0; axis < 3; ++axis) {
        if (_tunnelInfinite[axis] == 0.0f) {
            float hi = _startOffset[axis] + _tunnelPos[axis];
            float lo = _startOffset[axis] + _tunnelNeg[axis];
            if (offset[axis] > hi) {
                offset[axis] = hi;
            } else if (offset[axis] < lo) {
                offset[axis] = lo;
            }
        }
    }
}

void MinigameSession::advanceTrack(MinigameActor &actor, float dt) {
    if (!actor.trackRunning) {
        return;
    }
    float length = actor.track.length();
    if (length <= 0.0f) {
        return;
    }
    actor.trackTime += dt * actor.speed / kMinigameTrackRateDivisor;
    while (actor.trackTime >= length) {
        if (actor.numLoops < 0) {
            actor.trackTime -= length;
        } else if (actor.numLoops > 1) {
            --actor.numLoops;
            actor.trackTime -= length;
        } else {
            actor.trackTime = length;
            break;
        }
        runScriptSlot(actor, MinigameSlot::OnTrackLoop);
    }
    while (actor.trackTime < 0.0f) {
        actor.trackTime += length;
    }
}

void MinigameSession::placeActor(MinigameActor &actor) {
    actor.hook = actor.track.hookAt(actor.trackTime);
    if (actor.kind == MinigameActorKind::Player && _type == MinigameType::SwoopRace) {
        actor.position = glm::vec3(actor.hook * glm::vec4(_offset, 1.0f));
    } else {
        actor.position = glm::vec3(actor.hook[3]);
    }
}

void MinigameSession::updatePlayerGuns(float dt, bool fire) {
    auto &p = _actors.front();
    for (size_t i = 0; i < p.gunBanks.size(); ++i) {
        auto &bank = p.gunBanks[i];
        if (bank.cooldown > 0.0f) {
            bank.cooldown = glm::max(0.0f, bank.cooldown - dt);
        }
        if (!fire || p.dead || bank.cooldown > 0.0f) {
            continue;
        }
        bank.cooldown = bank.spec.bullet.rateOfFire;
        _host.onMinigameGunFired(p, i);
        gunFired(p, i);
    }
}

void MinigameSession::gunFired(MinigameActor &actor, size_t bankIndex) {
    if (bankIndex < actor.gunBanks.size()) {
        const auto &bullet = actor.gunBanks[bankIndex].spec.bullet;
        actor.lastBulletFiredDamage = static_cast<int>(bullet.damage);
        actor.lastBulletFiredTarget = static_cast<int>(bullet.targetType);
    }
    runScriptSlot(actor, MinigameSlot::OnFire);
}

void MinigameSession::collideObstacles() {
    auto &p = _actors.front();
    std::vector<size_t> obstacles(_obstacleList);
    for (size_t index : obstacles) {
        auto &o = _actors[index];
        if (!o.hasHitbump) {
            continue;
        }
        // A hitbump that starts above the bike is a roof or an arch over the
        // track (the TSL courses have them): the bike passes under it. A bike
        // in the air above a hitbump passes over it.
        bool airborne = _offset.z > _startOffset.z;
        if (o.hitbumpMin.z > p.position.z || (airborne && o.hitbumpMax.z < p.position.z)) {
            continue;
        }
        // A hit is the bike's movement this update entering the hitbump.
        bool wasInside = p.previousPosition.x >= o.hitbumpMin.x && p.previousPosition.x <= o.hitbumpMax.x &&
                         p.previousPosition.y >= o.hitbumpMin.y && p.previousPosition.y <= o.hitbumpMax.y;
        if (wasInside || !minigameSegmentHitsBox2D(p.previousPosition, p.position, o.hitbumpMin, o.hitbumpMax)) {
            continue;
        }
        p.lastObstacleHit = o.objectId;
        o.lastFollowerHit = p.objectId;
        if (_tsl) {
            // TSL: the obstacle's OnHitFollower, else the bike's OnHitObstacle.
            if (!runScriptSlot(o, MinigameSlot::OnHitFollower) && !runScriptSlot(p, MinigameSlot::OnHitObstacle)) {
                defaultObstacleHit(p);
            }
        } else {
            // KotOR: the bike's OnHitObstacle (or the default) and the
            // obstacle's OnHitFollower both run.
            if (!runScriptSlot(p, MinigameSlot::OnHitObstacle)) {
                defaultObstacleHit(p);
            }
            if (!p.dead) {
                runScriptSlot(o, MinigameSlot::OnHitFollower);
            }
        }
        if (p.dead) {
            return;
        }
    }
}

void MinigameSession::defaultObstacleHit(MinigameActor &follower) {
    // An obstacle wrecks what hits it, a swoop bike included.
    kill(follower);
}

void MinigameSession::collideFollowers() {
    auto &p = _actors.front();
    std::vector<size_t> enemies(_enemyList);
    glm::vec3 push(0.0f);
    int contacts = 0;
    int bump = 0;
    for (auto it = enemies.rbegin(); it != enemies.rend(); ++it) {
        auto &e = _actors[*it];
        if (e.dead || !(e.trigger || _doBumping)) {
            continue;
        }
        float distance = minigameSegmentDistance(p.previousPosition, p.position, e.previousPosition, e.position);
        float reach = p.sphereRadius + e.sphereRadius;
        if (distance >= reach) {
            continue;
        }
        if (!e.trigger) {
            glm::vec3 away = p.position - e.position;
            float len = glm::length(away);
            glm::vec3 dir = len > 1e-6f ? away / len : glm::vec3(1.0f, 0.0f, 0.0f);
            push += (e.position + dir * (reach + kBumpClearance)) - p.position;
            if (!e.invulnerable()) {
                playAnimation(e, "damage", false, false, false);
                playAnimation(e, "Ready_01", true, true, false);
                changeHitPoints(e, -p.bumpDamage);
                e.invulnerableTime = e.invincePeriod;
            }
            bump = glm::max(bump, e.bumpDamage);
        }
        p.lastFollowerHit = e.objectId;
        runScriptSlot(p, MinigameSlot::OnHitFollower);
        e.lastFollowerHit = p.objectId;
        runScriptSlot(e, MinigameSlot::OnHitFollower);
        ++contacts;
        if (p.dead) {
            return;
        }
    }
    if (contacts == 0) {
        return;
    }
    if (_type == MinigameType::SwoopRace) {
        glm::vec3 local = glm::vec3(glm::inverse(p.hook) * glm::vec4(p.position + push / static_cast<float>(contacts), 1.0f));
        glm::vec3 moved = local;
        clampOffset(moved);
        _offset = moved;
        placeActor(p);
    }
    if (!p.invulnerable()) {
        p.invulnerableTime = p.invincePeriod + p.invincePeriod;
        changeHitPoints(p, -bump);
        _speedHeld = true;
    }
}

void MinigameSession::changeHitPoints(MinigameActor &actor, int delta) {
    if (!actor.follower()) {
        return;
    }
    actor.lastHPChange = delta;
    if (!runScriptSlot(actor, MinigameSlot::OnDamage)) {
        defaultDamage(actor);
    }
}

void MinigameSession::defaultDamage(MinigameActor &actor) {
    if (!actor.follower()) {
        return;
    }
    actor.hitPoints += actor.lastHPChange;
    if (actor.lastHPChange < 0) {
        if (actor.hitPoints < 1) {
            kill(actor);
        } else {
            playAnimation(actor, "damage", false, false, false);
            playAnimation(actor, "Ready_01", true, true, false);
        }
    } else {
        actor.hitPoints = glm::min(actor.hitPoints, actor.maxHitPoints);
    }
}

void MinigameSession::kill(MinigameActor &actor) {
    if (actor.dead) {
        return;
    }
    actor.dead = true;
    if (!runScriptSlot(actor, MinigameSlot::OnDeath)) {
        defaultDeath(actor);
    }
}

void MinigameSession::defaultDeath(MinigameActor &actor) {
    actor.dead = true;
    actor.trackRunning = false;
    if (!actor.sounds.death.empty()) {
        _host.playMinigameSound(actor.sounds.death, actor.position);
    }
    _host.onMinigameActorDied(actor);
    actor.gunBanks.clear();
    unlist(actor);
}

void MinigameSession::destroy(MinigameActor &actor) {
    actor.dead = true;
    actor.trackRunning = false;
    unlist(actor);
}

void MinigameSession::unlist(MinigameActor &actor) {
    size_t index = static_cast<size_t>(&actor - _actors.data());
    auto drop = [index](std::vector<size_t> &list) {
        list.erase(std::remove(list.begin(), list.end(), index), list.end());
    };
    drop(_enemyList);
    drop(_obstacleList);
    actor.listed = false;
}

void MinigameSession::bulletHit(MinigameActor &target, int damage, int targetType, uint32_t shooter, const std::string &part) {
    target.lastBulletHitDamage = damage;
    target.lastBulletHitTarget = targetType;
    target.lastBulletHitShooter = shooter;
    target.lastBulletHitPart = part;
    if (!runScriptSlot(target, MinigameSlot::OnHitBullet)) {
        defaultBulletHit(target);
    }
}

void MinigameSession::defaultBulletHit(MinigameActor &target) {
    if (target.follower()) {
        changeHitPoints(target, -target.lastBulletHitDamage);
    }
}

void MinigameSession::playAnimation(MinigameActor &actor, const std::string &name, bool looping, bool queue, bool overlay) {
    _host.playMinigameAnimation(actor, name, looping, queue, overlay);
}

void MinigameSession::removeAnimation(MinigameActor &actor, const std::string &name) {
    _host.removeMinigameAnimation(actor, name);
}

void MinigameSession::setPlayerSpeed(float speed) {
    if (!_actors.empty()) {
        _actors.front().speed = speed;
    }
}

void MinigameSession::setPlayerMaxSpeed(float speed) {
    if (speed >= 0.0f) {
        _maxSpeed = speed;
    }
}

void MinigameSession::setPlayerOffset(const glm::vec3 &offset) {
    _offset = offset;
    clampOffset(_offset);
    if (!_actors.empty()) {
        placeActor(_actors.front());
    }
}

void MinigameSession::setPlayerOrigin(const glm::vec3 &origin) {
    _startOffset = origin;
    if (!_started) {
        _offset = origin;
    }
}

float MinigameSession::playerSpeed() const {
    return _actors.empty() ? 0.0f : _actors.front().speed;
}

MinigameActor *MinigameSession::actorByObjectId(uint32_t id) {
    for (auto &actor : _actors) {
        if (actor.objectId == id) {
            return &actor;
        }
    }
    return nullptr;
}

MinigameActor *MinigameSession::player() {
    return _actors.empty() ? nullptr : &_actors.front();
}

const MinigameActor *MinigameSession::player() const {
    return _actors.empty() ? nullptr : &_actors.front();
}

MinigameActor *MinigameSession::enemy(int index) {
    if (index < 0 || index >= static_cast<int>(_enemyList.size())) {
        return nullptr;
    }
    return &_actors[_enemyList[index]];
}

MinigameActor *MinigameSession::obstacle(int index) {
    if (index < 0 || index >= static_cast<int>(_obstacleList.size())) {
        return nullptr;
    }
    return &_actors[_obstacleList[index]];
}

MinigameActor *MinigameSession::findByName(const std::string &name) {
    for (auto &actor : _actors) {
        if (boost::iequals(actor.name, name)) {
            return &actor;
        }
        for (const auto &model : actor.models) {
            if (boost::iequals(model.resRef, name)) {
                return &actor;
            }
        }
    }
    return nullptr;
}

} // namespace game

} // namespace reone
