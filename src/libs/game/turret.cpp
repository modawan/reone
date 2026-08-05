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

#include "reone/game/turret.h"

#include <array>
#include <stack>

#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/camera/firstperson.h"
#include "reone/graphics/animation.h"
#include "reone/graphics/types.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/layouts.h"
#include "reone/resource/provider/models.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/camera.h"
#include "reone/scene/node/model.h"
#include "reone/system/logutil.h"

using namespace reone::audio;
using namespace reone::graphics;
using namespace reone::scene;

namespace reone {

namespace game {

namespace {

// Odyssey models look down +Y.
const glm::vec3 kModelForward(0.0f, 1.0f, 0.0f);

// reone's existing keyboard rotation rates (ThirdPersonCamera), used here as the
// calibration point for a full-turn axis. MiniGame.LateralAccel is deliberately
// not used: the swoop race authors 300 and steers laterally with it, the turret
// authors 1200 and never translates at all, and KotOR.js ignores it for the
// turret too, so it has no confirmed turret aim meaning.
constexpr float kTurnRateMin = 1.0f;      // radians per second
constexpr float kTurnRateMax = 2.5f;      // radians per second
constexpr float kTurnAcceleration = 1.0f; // radians per second squared

// Mouse aim sensitivity, matching FirstPersonCamera's kMouseMultiplier so the
// turret feels like the rest of the engine.
const float kMouseAimRadiansPerPixel = glm::pi<float>() / 4000.0f;

// Engine default field of view, restored to the reused first-person camera on
// exit (mirrors Area's default camera FOV).
constexpr float kDefaultCameraFovDegrees = 75.0f;

// Distance an enemy shoots from when its gun bank declares no sensing radius.
constexpr float kFallbackSensingRadius = 200.0f;

// Names of the hook nodes vanilla uses to assemble a minigame actor.
const std::string kModelHookName("modelhook");
const std::string kCameraHookName("camerahook");
const std::string kBulletHookName("bullethook0");

std::string gunBankHookName(uint32_t bankId) {
    return str(boost::format("gunbank%u") % bankId);
}

glm::vec3 transformOrigin(const glm::mat4 &transform) {
    return glm::vec3(transform[3]);
}

glm::vec3 transformForward(const glm::mat4 &transform) {
    glm::vec3 forward(glm::mat3(transform) * kModelForward);
    float length = glm::length(forward);
    return length > 0.0f ? forward / length : kModelForward;
}

// Rotation of a world transform, with any scale divided out.
glm::quat transformOrientation(const glm::mat4 &transform) {
    glm::mat3 basis(transform);
    for (int i = 0; i < 3; ++i) {
        float length = glm::length(basis[i]);
        basis[i] = length > 0.0f ? basis[i] / length : glm::vec3(0.0f);
    }
    return glm::normalize(glm::quat_cast(basis));
}

// Minigame actors idle on a looping "ready" animation (the fighters bank and
// the turret lights cycle). Names differ between actor and gun models.
void playIdleAnimation(ModelSceneNode &node) {
    static const std::array<const char *, 2> kIdleNames {"ready_01", "ready"};
    for (const auto *name : kIdleNames) {
        if (node.model().getAnimation(name)) {
            node.playAnimation(name, nullptr,
                               AnimationProperties::fromFlags(AnimationFlags::loop));
            return;
        }
    }
}

/**
 * Silence a projectile's emitters for ordinary flight.
 *
 * EmitterSceneNode latches its birth rate from the model at time zero and, in
 * Fountain mode, then emits for as long as the node lives - no animation is
 * involved. The shipped bolt models carry the explosion presentation as
 * emitters (mgb_ebonleft has plume, rim and inner nodes textured
 * LMG_explplume01 / LMG_explrim01), so left alone every bolt trails an
 * explosion for its whole flight and sustained fire fills the cockpit. Vanilla
 * only shows those on the authored explosion event, which reone reaches through
 * ModelSceneNode::signalEvent("detonate"), so free-running emission on a bolt in
 * flight is never wanted. Mesh geometry - the visible bolt itself - is
 * untouched.
 */
void silenceEmitters(SceneNode &node) {
    for (auto &child : node.children()) {
        if (child->type() == SceneNodeType::Emitter) {
            child->setEnabled(false);
            continue;
        }
        silenceEmitters(*child);
    }
}

float normalizeAngle(float radians) {
    while (radians > glm::pi<float>()) {
        radians -= glm::two_pi<float>();
    }
    while (radians < -glm::pi<float>()) {
        radians += glm::two_pi<float>();
    }
    return radians;
}

} // namespace

void TurretAim::configure(const MinigamePlayerSpec &player) {
    // The .are stores signed degree limits per axis, so the usable range is
    // [Neg, Pos]; take the min/max in case an area authors them the other way
    // round. An axis flagged in TunnelInfinite is unbounded and wraps instead.
    _pitchBounded = player.tunnelInfinite.x == 0.0f;
    _yawBounded = player.tunnelInfinite.z == 0.0f;
    _minPitch = glm::radians(glm::min(player.tunnelXNeg, player.tunnelXPos));
    _maxPitch = glm::radians(glm::max(player.tunnelXNeg, player.tunnelXPos));
    _minYaw = glm::radians(glm::min(player.tunnelZNeg, player.tunnelZPos));
    _maxYaw = glm::radians(glm::max(player.tunnelZNeg, player.tunnelZPos));

    // Start_Offset is the authored starting aim, on the same axes and in the
    // same units as the Tunnel limits: X is pitch, Z is yaw. K1 authors it only
    // on the turret area - all three swoop areas leave Start_Offset, Tunnel and
    // Mouse zeroed - and m12ab's Start_Offset_X of 7 degrees sits inside its
    // authored 2..45 degree pitch band. It is an angle, not a translation.
    _startPitch = glm::radians(player.startOffset.x);
    _startYaw = glm::radians(player.startOffset.z);
    if (_pitchBounded) {
        _startPitch = glm::clamp(_startPitch, _minPitch, _maxPitch);
    } else {
        _startPitch = normalizeAngle(_startPitch);
    }
    if (_yawBounded) {
        _startYaw = glm::clamp(_startYaw, _minYaw, _maxYaw);
    } else {
        _startYaw = normalizeAngle(_startYaw);
    }

    reset();
}

void TurretAim::reset() {
    _pitch = _startPitch;
    _yaw = _startYaw;
}

float TurretAim::pitchTravel() const {
    return _pitchBounded ? glm::max(0.0f, _maxPitch - _minPitch) : glm::two_pi<float>();
}

float TurretAim::yawTravel() const {
    return _yawBounded ? glm::max(0.0f, _maxYaw - _minYaw) : glm::two_pi<float>();
}

void TurretAimRate::configure(float travelRadians) {
    // Scale reone's full-turn rates by how much travel this axis actually has,
    // so a narrow axis is proportionally gentler and every axis crosses its
    // authored range in about the same time.
    float scale = travelRadians > 0.0f
                      ? glm::min(travelRadians / glm::two_pi<float>(), 1.0f)
                      : 1.0f;
    _minRate = kTurnRateMin * scale;
    _maxRate = kTurnRateMax * scale;
    _acceleration = kTurnAcceleration * scale;
    reset();
}

void TurretAimRate::setDirection(int direction) {
    int clamped = direction > 0 ? 1 : (direction < 0 ? -1 : 0);
    if (clamped == _direction) {
        return;
    }
    _direction = clamped;
    _held = 0.0f;
}

void TurretAimRate::reset() {
    _direction = 0;
    _held = 0.0f;
}

float TurretAimRate::rateAt(float held) const {
    return glm::min(_maxRate, _minRate + _acceleration * glm::max(0.0f, held));
}

float TurretAimRate::rate() const {
    return _direction != 0 ? rateAt(_held) : 0.0f;
}

float TurretAimRate::angleOver(float from, float to) const {
    // Integrate the ramp exactly rather than stepping it, so the angle covered
    // over an interval is the same however many frames it is split into.
    if (to <= from) {
        return 0.0f;
    }
    float capAt = _acceleration > 0.0f ? (_maxRate - _minRate) / _acceleration
                                       : 0.0f;
    float rampFrom = glm::min(from, capAt);
    float rampTo = glm::min(to, capAt);
    float angle = 0.0f;
    if (rampTo > rampFrom) {
        angle += _minRate * (rampTo - rampFrom) +
                 0.5f * _acceleration * (rampTo * rampTo - rampFrom * rampFrom);
    }
    float flatFrom = glm::max(from, capAt);
    float flatTo = glm::max(to, capAt);
    if (flatTo > flatFrom) {
        angle += _maxRate * (flatTo - flatFrom);
    }
    return angle;
}

float TurretAimRate::advance(float dt) {
    if (_direction == 0 || dt <= 0.0f) {
        return 0.0f;
    }
    float from = _held;
    _held += dt;
    return static_cast<float>(_direction) * angleOver(from, _held);
}

void TurretAim::addPitch(float radians) {
    _pitch += radians;
    if (_pitchBounded) {
        _pitch = glm::clamp(_pitch, _minPitch, _maxPitch);
    } else {
        _pitch = normalizeAngle(_pitch);
    }
}

void TurretAim::addYaw(float radians) {
    _yaw += radians;
    if (_yawBounded) {
        _yaw = glm::clamp(_yaw, _minYaw, _maxYaw);
    } else {
        _yaw = normalizeAngle(_yaw);
    }
}

glm::quat TurretAim::orientation() const {
    return glm::angleAxis(_yaw, glm::vec3(0.0f, 0.0f, 1.0f)) *
           glm::angleAxis(_pitch, glm::vec3(1.0f, 0.0f, 0.0f));
}

glm::vec3 TurretAim::forward() const {
    return orientation() * kModelForward;
}

void TurretGunTimer::update(float dt) {
    if (_cooldown > 0.0f) {
        _cooldown = glm::max(0.0f, _cooldown - dt);
    }
}

bool TurretGunTimer::tryFire() {
    if (_cooldown > 0.0f) {
        return false;
    }
    _cooldown = _rateOfFire;
    return true;
}

bool TurretBullet::advance(float dt) {
    // A bolt is created and integrated within the same tick, so its first step
    // is spent standing at the muzzle: that is the only frame in which it is
    // drawn where the authored gun bank actually points.
    if (atMuzzle) {
        atMuzzle = false;
        return true;
    }
    life += dt;
    if (life >= lifespan) {
        return false;
    }
    position += direction * speed * dt;
    return true;
}

const std::string kTurretAlarmTag("Alarm01");

bool turretIsDestroyed(int hitPoints) {
    // The shipped script's destruction branch: below the gauge floor the hawk is
    // gone. It does not wait for hit points to reach zero, so the authored
    // survivable band on a 3000 point hawk is 3000 down to 2000.
    return hitPoints < kTurretGaugeFloor;
}

int turretHealthState(int hitPoints) {
    // The shipped script only evaluates its formula on the healthy branch; below
    // the floor it takes the destruction branch and plays Health00 outright. Do
    // the same rather than extrapolating the formula, whose truncating division
    // would otherwise still report a live state just under the floor.
    if (hitPoints < kTurretGaugeFloor) {
        return 0;
    }
    // Shipped arithmetic: ((hp - 2000) * 12) / 1000 + 1, integer division.
    long long scaled = (static_cast<long long>(hitPoints) - kTurretGaugeFloor) *
                       (kTurretHealthStateCount - 1);
    int state = static_cast<int>(scaled / kTurretGaugeSpan) + 1;
    // An undamaged hawk evaluates to 13; the script never sees it because
    // OnDamage runs after damage, but clamp so full health reads as full.
    return glm::clamp(state, 1, kTurretHealthStateCount - 1);
}

std::string turretHealthAnimation(int state) {
    int clamped = glm::clamp(state, 0, kTurretHealthStateCount - 1);
    // The script builds the name as "Health0" + n below ten, "Health" + n above.
    return str(boost::format("health%02d") % clamped);
}

bool turretAlarmStartsAtState(int state) {
    return state == kTurretAlarmState;
}

std::string turretContactAnimation(size_t enemyIndex) {
    if (enemyIndex >= kTurretContactCount) {
        return {};
    }
    // Enemy 0 rides track m12ab_mgt02, so the contact channels start at 02.
    return str(boost::format("sithloop%02u") % (enemyIndex + 2));
}

std::string turretContactDeathAnimation(size_t enemyIndex) {
    auto live = turretContactAnimation(enemyIndex);
    return live.empty() ? live : live + "d";
}

bool turretDeathEffectComplete(float elapsed, float duration) {
    if (duration <= 0.0f) {
        return true;
    }
    return elapsed >= duration;
}

float turretMuzzleClearance(float modelMinForward) {
    return glm::max(0.0f, -modelMinForward);
}

void turretDisableReferenceAttachments(ModelSceneNode &model) {
    std::stack<std::reference_wrapper<const graphics::ModelNode>> pending;
    if (auto root = model.model().rootNode()) {
        pending.push(*root);
    }
    while (!pending.empty()) {
        const auto &node = pending.top().get();
        pending.pop();
        if (node.isReference()) {
            if (auto attachment = model.getAttachment(node.name())) {
                attachment->setEnabled(false);
            }
        }
        for (const auto &child : node.children()) {
            pending.push(*child);
        }
    }
}

int turretHeadingState(float yawRadians) {
    float degrees = glm::degrees(yawRadians);
    // Round to the nearest authored whole degree, then wrap into [0, 360).
    int heading = static_cast<int>(glm::round(degrees));
    heading %= 360;
    if (heading < 0) {
        heading += 360;
    }
    return heading;
}

std::string turretHeadingAnimation(int heading) {
    int wrapped = heading % 360;
    if (wrapped < 0) {
        wrapped += 360;
    }
    return str(boost::format("hudrot_%03d") % wrapped);
}

TurretRequestError validateTurretRequest(const std::string &target,
                                         const std::string &originModule,
                                         bool moduleKnown,
                                         bool alreadyActive) {
    if (alreadyActive) {
        return TurretRequestError::AlreadyActive;
    }
    if (target.empty()) {
        return TurretRequestError::MissingModule;
    }
    if (originModule.empty()) {
        return TurretRequestError::NoOrigin;
    }
    if (!moduleKnown) {
        return TurretRequestError::UnknownModule;
    }
    if (boost::iequals(target, originModule)) {
        return TurretRequestError::SameModule;
    }
    return TurretRequestError::None;
}

const char *turretRequestErrorMessage(TurretRequestError error) {
    switch (error) {
    case TurretRequestError::MissingModule:
        return "no module given";
    case TurretRequestError::UnknownModule:
        return "unknown module";
    case TurretRequestError::SameModule:
        return "already in that module";
    case TurretRequestError::NoOrigin:
        return "no origin module loaded";
    case TurretRequestError::AlreadyActive:
        return "a turret session is already active";
    default:
        return "";
    }
}

TurretRequestResolution resolveTurretRequest(bool pendingForModule,
                                             bool hasMinigame,
                                             MinigameType type) {
    if (!pendingForModule) {
        return TurretRequestResolution::NotPending;
    }
    if (!hasMinigame) {
        return TurretRequestResolution::AbortNoMinigame;
    }
    if (type != MinigameType::Turret) {
        return TurretRequestResolution::AbortWrongType;
    }
    return TurretRequestResolution::Start;
}

const char *turretRequestResolutionMessage(TurretRequestResolution resolution) {
    switch (resolution) {
    case TurretRequestResolution::AbortNoMinigame:
        return "module has no minigame";
    case TurretRequestResolution::AbortWrongType:
        return "module is not a turret minigame";
    default:
        return "";
    }
}

std::string turretReturnModule(const std::string &turretModule,
                               const std::string &originModule) {
    // Vanilla turret exit, confirmed from local assets: the M12ab enemy death
    // scripts (k_pebo_sthdeath2..7) and the module heartbeat (k_pebo_mgheart)
    // both end the sequence with StartNewModule("ebo_m12aa"). Other modules are
    // not wired, so they fall back to wherever the session started.
    if (boost::iequals(turretModule, "m12ab")) {
        return "ebo_m12aa";
    }
    return originModule;
}

glm::vec3 turretFireDirection(const glm::quat &shooterOrientation) {
    glm::vec3 direction(shooterOrientation * kModelForward);
    float length = glm::length(direction);
    return length > 0.0f ? direction / length : kModelForward;
}

glm::mat4 turretBulletTransform(const TurretBullet &bullet) {
    glm::mat4 transform(1.0f);
    transform *= glm::translate(bullet.position);
    transform *= glm::mat4_cast(bullet.orientation);
    return transform;
}

bool sphereContainsPoint(const glm::vec3 &center, float radius, const glm::vec3 &point) {
    if (radius <= 0.0f) {
        return false;
    }
    return glm::distance2(center, point) <= radius * radius;
}

bool rayIntersectsSphere(const glm::vec3 &origin,
                         const glm::vec3 &direction,
                         const glm::vec3 &center,
                         float radius,
                         float maxDistance) {
    if (radius <= 0.0f || maxDistance <= 0.0f) {
        return false;
    }
    glm::vec3 toCenter(center - origin);
    float alongRay = glm::dot(toCenter, direction);
    float perpSq = glm::dot(toCenter, toCenter) - alongRay * alongRay;
    float radiusSq = radius * radius;
    if (perpSq > radiusSq) {
        return false;
    }
    float halfChord = glm::sqrt(radiusSq - perpSq);
    float exit = alongRay + halfChord;
    if (exit < 0.0f) {
        return false;
    }
    float entry = alongRay - halfChord;
    return glm::max(entry, 0.0f) <= maxDistance;
}

std::shared_ptr<ModelSceneNode> Turret::loadModel(const std::string &resRef) {
    if (resRef.empty()) {
        return nullptr;
    }
    auto model = _services.resource.models.get(resRef);
    if (!model) {
        return nullptr;
    }
    auto &sceneGraph = _services.scene.graphs.get(kSceneMain);
    auto node = sceneGraph.newModel(*model, ModelUsage::Placeable);
    node->setDrawDistance(_game.options().graphics.drawDistance);
    playIdleAnimation(*node);
    return node;
}

std::shared_ptr<ModelSceneNode> Turret::loadTrack(const std::string &resRef,
                                                  const glm::vec3 &position) {
    auto node = loadModel(resRef);
    if (!node) {
        return nullptr;
    }
    node->setLocalTransform(glm::translate(position));
    // Tracks are authored as a single looping animation that walks the
    // "modelhook" node along the rail; everything riding the track is parented
    // to that hook.
    auto animations = node->model().getAnimationNames();
    if (!animations.empty()) {
        node->playAnimation(animations.front(), nullptr,
                            AnimationProperties::fromFlags(AnimationFlags::loop));
    }
    // The scene graph decides culling per model root and prunes the whole
    // subtree behind a culled one. A track root sits at its authored origin
    // while the rail animation carries the hook - and everything riding it,
    // including the camera - far away, so distance and frustum culling of the
    // root would blink the actors in and out. This is the case the scene node
    // culling flag exists for.
    node->setCullingEnabled(false);
    node->setDrawDistance(std::numeric_limits<float>::max());
    _services.scene.graphs.get(kSceneMain).addRoot(node);
    return node;
}

void Turret::loadGunBanks(const std::vector<MinigameGunBankSpec> &specs,
                          ModelSceneNode &mount,
                          std::vector<GunBank> &out) {
    for (const auto &spec : specs) {
        GunBank bank;
        bank.spec = &spec;
        bank.timer = TurretGunTimer(spec.bullet.rateOfFire);
        bank.modelNode = loadModel(spec.gunModelResRef);
        if (bank.modelNode) {
            mount.attach(gunBankHookName(spec.bankId), *bank.modelNode);
        }
        out.push_back(std::move(bank));
    }
}

bool Turret::loadPlayer(const MinigameSpec &spec) {
    for (const auto &model : spec.player.models) {
        auto node = loadModel(model.resRef);
        if (!node) {
            debug("turret: player model '" + model.resRef + "' missing");
            continue;
        }
        if (model.rotating) {
            if (!_turretRoot) {
                _turretRoot = std::move(node);
            } else {
                _turretRoot->addChild(*node);
                _turretChildNodes.push_back(std::move(node));
            }
        } else {
            if (!_bodyRoot) {
                _bodyRoot = std::move(node);
            } else {
                _bodyRoot->addChild(*node);
                _bodyChildNodes.push_back(std::move(node));
            }
        }
    }
    if (!_turretRoot) {
        return false;
    }

    // Both groups ride the player track's hook so the hull and the gun stay
    // together; only the rotating group takes the aim rotation on top.
    if (_playerTrackNode) {
        _playerTrackNode->attach(kModelHookName, *_turretRoot);
        if (_bodyRoot) {
            _playerTrackNode->attach(kModelHookName, *_bodyRoot);
        }
    } else {
        // No usable track: the actors become roots of their own. They are still
        // driven by transform rather than by the camera's own frame, so keep
        // them out of the culling pass for the same reason as the tracks.
        auto &sceneGraph = _services.scene.graphs.get(kSceneMain);
        _turretRoot->setCullingEnabled(false);
        sceneGraph.addRoot(_turretRoot);
        if (_bodyRoot) {
            _bodyRoot->setCullingEnabled(false);
            sceneGraph.addRoot(_bodyRoot);
        }
    }

    loadGunBanks(spec.player.gunBanks, *_turretRoot, _gunBanks);
    return true;
}

void Turret::loadEnemies(const MinigameSpec &spec) {
    auto layout = _services.resource.layouts.get(_areaName);
    for (const auto &enemySpec : spec.enemies) {
        Enemy enemy;
        enemy.spec = &enemySpec;
        enemy.hitPoints = static_cast<int>(enemySpec.hitPoints);

        glm::vec3 trackPos(0.0f);
        if (layout) {
            if (auto placement = layout->findTrackByName(enemySpec.trackResRef)) {
                trackPos = placement->get().position;
            }
        }
        enemy.trackNode = loadTrack(enemySpec.trackResRef, trackPos);

        for (const auto &model : enemySpec.models) {
            auto node = loadModel(model.resRef);
            if (!node) {
                continue;
            }
            if (!enemy.modelNode) {
                enemy.modelNode = std::move(node);
            } else {
                enemy.modelNode->addChild(*node);
                enemy.extraNodes.push_back(std::move(node));
            }
        }
        if (!enemy.modelNode) {
            debug("turret: enemy on track '" + enemySpec.trackResRef + "' has no model");
            continue;
        }
        if (enemy.trackNode) {
            enemy.trackNode->attach(kModelHookName, *enemy.modelNode);
        } else {
            enemy.modelNode->setCullingEnabled(false);
            _services.scene.graphs.get(kSceneMain).addRoot(enemy.modelNode);
        }
        for (const auto &bankSpec : enemySpec.gunBanks) {
            GunBank bank;
            bank.spec = &bankSpec;
            bank.timer = TurretGunTimer(bankSpec.bullet.rateOfFire);
            bank.modelNode = loadModel(bankSpec.gunModelResRef);
            if (bank.modelNode) {
                enemy.modelNode->attach(gunBankHookName(bankSpec.bankId), *bank.modelNode);
            }
            enemy.gunBanks.push_back(std::move(bank));
        }
        _enemies.push_back(std::move(enemy));
    }
}

bool Turret::start(const MinigameSpec &spec, FirstPersonCamera *camera, const std::string &areaName) {
    if (_active) {
        return false;
    }
    _spec = spec;
    _areaName = areaName;
    _camera = camera;
    _outcome = Outcome::InProgress;
    _elapsed = 0.0f;
    _firing = false;

    _trackResRef = _spec.player.trackResRef;
    _playerSphereRadius = _spec.player.sphereRadius;
    _maxHitPoints = static_cast<int>(_spec.player.maxHitPoints > 0
                                         ? _spec.player.maxHitPoints
                                         : _spec.player.hitPoints);
    _hitPoints = static_cast<int>(_spec.player.hitPoints);
    // Replaying a session starts from the authored aim with no carried-over
    // input velocity.
    _aim.configure(_spec.player);
    _pitchRate.configure(_aim.pitchTravel());
    _yawRate.configure(_aim.yawTravel());

    glm::vec3 trackPos(0.0f);
    _anchorSource = "origin";
    if (auto layout = _services.resource.layouts.get(_areaName)) {
        if (auto placement = layout->findTrackByName(_trackResRef)) {
            trackPos = placement->get().position;
            _anchorSource = "lyt-track";
        }
    }
    _playerTrackNode = loadTrack(_trackResRef, trackPos);
    if (_playerTrackNode) {
        if (_anchorSource == "origin") {
            _anchorSource = "track-model";
        }
    } else {
        _anchorSource = "no-track";
    }

    if (!loadPlayer(_spec)) {
        debug("turret: no visible player models loaded");
        detachAll();
        _camera = nullptr;
        return false;
    }
    loadEnemies(_spec);

    // The camera mount model is not added to the scene: only its static
    // "camerahook" transform is needed, and vanilla hides the mount anyway.
    _cameraHookLocal = glm::mat4(1.0f);
    _haveCameraHook = false;
    if (!_spec.player.cameraResRef.empty()) {
        if (auto cameraModel = _services.resource.models.get(_spec.player.cameraResRef)) {
            if (auto hook = cameraModel->getNodeByNameRecursive(kCameraHookName)) {
                _cameraHookLocal = hook->absoluteTransform();
                _haveCameraHook = true;
            }
        }
    }

    _active = true;
    suppressCanopyGlass();
    bindHudPanes();
    resetHud();
    setCameraFieldOfView(_spec.cameraViewAngle > 0.0f ? _spec.cameraViewAngle
                                                      : kDefaultCameraFovDegrees);
    updateAnchor();
    updatePlayerTransforms();
    updateCamera();
    return true;
}

void Turret::stop() {
    if (!_active) {
        return;
    }
    // Restore the reused camera's projection before releasing it.
    if (_camera) {
        _camera->cameraSceneNode()->setPerspectiveProjection(
            glm::radians(kDefaultCameraFovDegrees),
            cameraAspect(),
            kDefaultClipPlaneNear,
            kDefaultClipPlaneFar);
    }
    clearHud();
    _healthHud.reset();
    _radarHud.reset();
    _contactAlive.clear();
    _healthState = -1;
    _headingState = -1;
    _active = false;
    _camera = nullptr;
    _pitchRate.reset();
    _yawRate.reset();
    _firing = false;
    detachAll();
}

namespace {

// Detach whatever was attached to a hook node of \p model. ModelSceneNode
// attachments are parented to a nested node, so clearing the model's own
// children is not enough.
void clearHook(const std::shared_ptr<ModelSceneNode> &model, const std::string &hookName) {
    if (!model) {
        return;
    }
    if (auto hook = model->getNodeByName(hookName)) {
        hook->removeAllChildren();
    }
}

} // namespace

void Turret::releaseEnemyNodes(Enemy &enemy) {
    // Everything a fighter owns hangs off its model root: the authored gun bank
    // models on their hooks, and - built by the model loader itself - the
    // reference models the fighter names (mgf_sithfighter attaches an fx_ref
    // engine flare at each of its OmenRef nodes), plus its lights and emitters.
    // Dropping the root and its rail therefore retires the whole fighter,
    // without this code having to know which effects it authored.
    auto &sceneGraph = _services.scene.graphs.get(kSceneMain);
    for (auto &bank : enemy.gunBanks) {
        if (bank.spec) {
            clearHook(enemy.modelNode, gunBankHookName(bank.spec->bankId));
        }
    }
    enemy.gunBanks.clear();
    clearHook(enemy.trackNode, kModelHookName);
    if (enemy.modelNode) {
        enemy.modelNode->removeAllChildren();
        sceneGraph.removeRoot(*enemy.modelNode);
    }
    if (enemy.trackNode) {
        sceneGraph.removeRoot(*enemy.trackNode);
    }
    enemy.extraNodes.clear();
    enemy.modelNode.reset();
    enemy.trackNode.reset();
    enemy.nodesReleased = true;
}

void Turret::detachAll() {
    auto &sceneGraph = _services.scene.graphs.get(kSceneMain);

    for (auto &bullet : _bullets) {
        releaseBulletNode(bullet);
    }
    _bullets.clear();
    for (auto &pooled : _bulletNodePool) {
        pooled.second.clear();
    }
    _bulletNodePool.clear();

    for (auto &enemy : _enemies) {
        releaseEnemyNodes(enemy);
    }
    _enemies.clear();

    for (auto &bank : _gunBanks) {
        if (bank.spec) {
            clearHook(_turretRoot, gunBankHookName(bank.spec->bankId));
        }
    }
    _gunBanks.clear();

    clearHook(_playerTrackNode, kModelHookName);
    if (_turretRoot) {
        _turretRoot->removeAllChildren();
        sceneGraph.removeRoot(*_turretRoot);
    }
    if (_bodyRoot) {
        _bodyRoot->removeAllChildren();
        sceneGraph.removeRoot(*_bodyRoot);
    }
    if (_playerTrackNode) {
        sceneGraph.removeRoot(*_playerTrackNode);
    }
    _turretChildNodes.clear();
    _bodyChildNodes.clear();
    _turretRoot.reset();
    _bodyRoot.reset();
    _playerTrackNode.reset();
}

size_t Turret::contactsLive() const {
    return static_cast<size_t>(std::count(_contactAlive.begin(), _contactAlive.end(), true));
}

size_t Turret::radarChannelCount() const {
    return _radarHud ? _radarHud->animationChannelCount() : 0;
}

size_t Turret::enemiesAlive() const {
    size_t count = 0;
    for (const auto &enemy : _enemies) {
        if (enemy.alive) {
            ++count;
        }
    }
    return count;
}

void Turret::update(float dt) {
    if (!_active || dt <= 0.0f) {
        return;
    }
    _elapsed += dt;

    _aim.addPitch(_pitchRate.advance(dt));
    _aim.addYaw(_yawRate.advance(dt));

    updateAnchor();
    updatePlayerTransforms();
    updateCamera();

    for (auto &bank : _gunBanks) {
        bank.timer.update(dt);
    }
    if (_firing) {
        fire();
    }

    updateEnemies(dt);
    updateBullets(dt);
    updateHeadingHud();

    if (_outcome == Outcome::InProgress) {
        if (turretIsDestroyed(_hitPoints)) {
            _outcome = Outcome::Lost;
        } else if (!_enemies.empty() && enemiesAlive() == 0) {
            _outcome = Outcome::Won;
        }
    }
}

void Turret::updateAnchor() {
    if (_playerTrackNode) {
        if (auto hook = _playerTrackNode->getNodeByName(kModelHookName)) {
            _anchorTransform = hook->absoluteTransform();
        } else {
            _anchorTransform = _playerTrackNode->absoluteTransform();
        }
    } else {
        _anchorTransform = glm::mat4(1.0f);
    }
    _anchorPosition = transformOrigin(_anchorTransform);
}

void Turret::updatePlayerTransforms() {
    // The roots are parented to the track hook, so their local transform is the
    // actor's placement within the hook frame: both groups sit on the hook and
    // only the rotating one takes the aim rotation.
    //
    // The authored Start_Offset is deliberately not applied. Its turret meaning
    // is unconfirmed (KotOR.js ignores it too), and m12ab's Start_Offset_X of 7
    // slides the gun sideways out of its mount, leaving the Ebon Hawk hull
    // clipping through the turret frame.
    if (_bodyRoot) {
        _bodyRoot->setLocalTransform(glm::mat4(1.0f));
    }
    if (_turretRoot) {
        _turretRoot->setLocalTransform(glm::mat4_cast(_aim.orientation()));
    }
}

void Turret::updateCamera() {
    if (!_camera || !_turretRoot) {
        return;
    }
    glm::mat4 eye = _turretRoot->absoluteTransform();
    if (_haveCameraHook) {
        eye *= _cameraHookLocal;
    }
    _camera->cameraSceneNode()->setLocalTransform(eye);
}

void Turret::setCameraFieldOfView(float fovDegrees) {
    if (!_camera) {
        return;
    }
    float nearClip = _spec.nearClip > 0.0f ? _spec.nearClip : kDefaultClipPlaneNear;
    float farClip = _spec.farClip > 0.0f ? _spec.farClip : kDefaultClipPlaneFar;
    _camera->cameraSceneNode()->setPerspectiveProjection(
        glm::radians(fovDegrees),
        cameraAspect(),
        nearClip,
        farClip);
}

float Turret::cameraAspect() const {
    const auto &graphicsOpts = _game.options().graphics;
    if (graphicsOpts.height <= 0) {
        return 1.0f;
    }
    return graphicsOpts.width / static_cast<float>(graphicsOpts.height);
}

void Turret::fire() {
    if (!_turretRoot) {
        return;
    }
    bool fired = false;
    for (auto &bank : _gunBanks) {
        if (!bank.spec || !bank.timer.tryFire()) {
            continue;
        }
        glm::mat4 muzzle = _turretRoot->absoluteTransform();
        if (bank.modelNode) {
            if (auto hook = bank.modelNode->getNodeByName(kBulletHookName)) {
                muzzle = hook->absoluteTransform();
            } else {
                muzzle = bank.modelNode->absoluteTransform();
            }
            bank.modelNode->playAnimation("fire");
        }
        // The muzzle decides only where the bolt appears; its travel and its
        // orientation both come from the turret itself, so every bank fires
        // symmetrically and the aim adds no extra rotation per bank.
        spawnBullet(*bank.spec,
                    transformOrigin(muzzle),
                    transformOrientation(_turretRoot->absoluteTransform()),
                    /*fromPlayer=*/true);
        if (!bank.spec->fireSound.empty()) {
            playSound(bank.spec->fireSound);
        }
        fired = true;
    }
    if (fired && _turretRoot) {
        _turretRoot->playAnimation("fire");
    }
}

void Turret::spawnBullet(const MinigameGunBankSpec &bank,
                         const glm::vec3 &origin,
                         const glm::quat &orientation,
                         bool fromPlayer) {
    Bullet bullet;
    bullet.sim.position = origin;
    bullet.sim.orientation = orientation;
    bullet.sim.direction = turretFireDirection(orientation);
    bullet.sim.speed = bank.bullet.speed;
    bullet.sim.lifespan = bank.bullet.lifespan;
    bullet.sim.damage = bank.bullet.damage;
    bullet.sim.fromPlayer = fromPlayer;
    bullet.modelResRef = bank.bullet.modelResRef;
    bullet.collisionSound = bank.bullet.collisionSound;

    bullet.modelNode = acquireBulletNode(bullet.modelResRef);
    if (bullet.modelNode) {
        // The bolt is drawn from its own origin, which sits partway along the
        // shot rather than at its tail. Start it far enough ahead that the part
        // reaching backwards clears the gun instead of being drawn inside it.
        bullet.sim.position += bullet.sim.direction *
                               turretMuzzleClearance(bullet.modelNode->model().aabb().min().y);
        bullet.modelNode->setLocalTransform(turretBulletTransform(bullet.sim));
        _services.scene.graphs.get(kSceneMain).addRoot(bullet.modelNode);
    }
    _bullets.push_back(std::move(bullet));
}

std::shared_ptr<ModelSceneNode> Turret::acquireBulletNode(const std::string &resRef) {
    auto pooled = _bulletNodePool.find(resRef);
    if (pooled != _bulletNodePool.end() && !pooled->second.empty()) {
        auto node = std::move(pooled->second.back());
        pooled->second.pop_back();
        return node;
    }
    auto node = loadModel(resRef);
    if (node) {
        silenceEmitters(*node);
    }
    return node;
}

void Turret::releaseBulletNode(Bullet &bullet) {
    if (!bullet.modelNode) {
        return;
    }
    _services.scene.graphs.get(kSceneMain).removeRoot(*bullet.modelNode);
    _bulletNodePool[bullet.modelResRef].push_back(std::move(bullet.modelNode));
}

void Turret::updateEnemies(float dt) {
    for (auto &enemy : _enemies) {
        if (!enemy.modelNode) {
            continue;
        }
        const glm::mat4 &transform = enemy.modelNode->absoluteTransform();
        enemy.position = transformOrigin(transform);
        enemy.orientation = transformOrientation(transform);
        enemy.forward = turretFireDirection(enemy.orientation);

        if (!enemy.alive) {
            if (!enemy.deathHandled) {
                enemy.deathHandled = true;
                // The authored death animation decides how long the wreck stays.
                if (auto dying = enemy.modelNode->model().getAnimation("die")) {
                    enemy.deathDuration = dying->length();
                }
                // The engine glow is not part of the death presentation: it
                // burns steadily for as long as its node lives, so a wreck that
                // kept it would trail a lit flare through the whole animation.
                turretDisableReferenceAttachments(*enemy.modelNode);
                enemy.modelNode->playAnimation("die");
                if (enemy.spec && !enemy.spec->sounds.death.empty()) {
                    playSound(enemy.spec->sounds.death);
                }
            } else if (!enemy.nodesReleased) {
                // "die" only fades the hull mesh; the fx_ref flares, lights and
                // emitters the fighter owns are untouched by it and would keep
                // riding the rail forever. Retire the whole fighter once the
                // authored effect has played out.
                enemy.deathElapsed += dt;
                if (turretDeathEffectComplete(enemy.deathElapsed, enemy.deathDuration)) {
                    releaseEnemyNodes(enemy);
                }
            }
            continue;
        }

        for (auto &bank : enemy.gunBanks) {
            bank.timer.update(dt);
            if (!bank.spec || !bank.timer.ready()) {
                continue;
            }
            float sensing = bank.spec->sensingRadius > 0.0f
                                ? bank.spec->sensingRadius
                                : kFallbackSensingRadius;
            if (!rayIntersectsSphere(enemy.position, enemy.forward,
                                     _anchorPosition, _playerSphereRadius, sensing)) {
                continue;
            }
            if (!bank.timer.tryFire()) {
                continue;
            }
            glm::vec3 muzzle = enemy.position;
            if (bank.modelNode) {
                if (auto hook = bank.modelNode->getNodeByName(kBulletHookName)) {
                    muzzle = transformOrigin(hook->absoluteTransform());
                }
            }
            spawnBullet(*bank.spec, muzzle, enemy.orientation, /*fromPlayer=*/false);
            if (!bank.spec->fireSound.empty()) {
                playSound(bank.spec->fireSound);
            }
        }
    }
}

void Turret::updateBullets(float dt) {
    for (auto it = _bullets.begin(); it != _bullets.end();) {
        auto &bullet = *it;
        if (!bullet.sim.advance(dt)) {
            releaseBulletNode(bullet);
            it = _bullets.erase(it);
            continue;
        }
        if (bullet.modelNode) {
            bullet.modelNode->setLocalTransform(turretBulletTransform(bullet.sim));
        }

        bool hit = false;
        if (bullet.sim.fromPlayer) {
            for (auto &enemy : _enemies) {
                if (!enemy.alive || !enemy.spec) {
                    continue;
                }
                if (!sphereContainsPoint(enemy.position, enemy.spec->sphereRadius, bullet.sim.position)) {
                    continue;
                }
                damageEnemy(enemy, bullet.sim.damage);
                hit = true;
                break;
            }
        } else if (sphereContainsPoint(_anchorPosition, _playerSphereRadius, bullet.sim.position)) {
            damagePlayer(bullet.sim.damage);
            hit = true;
        }

        if (hit) {
            if (!bullet.collisionSound.empty()) {
                playSound(bullet.collisionSound);
            }
            releaseBulletNode(bullet);
            it = _bullets.erase(it);
            continue;
        }
        ++it;
    }
}

void Turret::damagePlayer(uint32_t amount) {
    if (turretIsDestroyed(_hitPoints)) {
        return; // already destroyed: the destruction branch runs once
    }
    _hitPoints = glm::max(0, _hitPoints - static_cast<int>(amount));
    // Vanilla drives the gauge from the player's OnDamage script, i.e. once per
    // hit rather than once per frame. That script plays no cockpit "damage"
    // animation and touches no LED node, so neither is triggered here.
    updateHealthHud();
}

void Turret::damageEnemy(Enemy &enemy, uint32_t amount) {
    if (!enemy.alive) {
        return;
    }
    enemy.hitPoints -= static_cast<int>(amount);
    if (enemy.hitPoints <= 0) {
        enemy.hitPoints = 0;
        enemy.alive = false;
        killContactHud(static_cast<size_t>(&enemy - _enemies.data()));
    } else if (enemy.modelNode) {
        enemy.modelNode->playAnimation("damage");
    }
}

void Turret::suppressCanopyGlass() {
    if (!_turretRoot) {
        return;
    }
    // Hide the canopy glass until additive environment-mapped model materials
    // can be rendered without compositing across the cockpit. Both shells are
    // authored inward-facing and enclose the camera, so at full strength they
    // lay the scratch texture over the whole field of view - twice. The nodes
    // stay in the loaded model and the shipped resource is untouched; only this
    // session's cockpit instance stops drawing them.
    static const std::array<const char *, 2> kGlassNodes {"sphere04", "sphere05"};
    for (const auto *name : kGlassNodes) {
        auto node = _turretRoot->getNodeByName(name);
        if (!node) {
            debug(str(boost::format("turret: canopy glass node '%s' not present") % name));
            continue;
        }
        node->setEnabled(false);
    }
}

void Turret::bindHudPanes() {
    // The HUD panes are ordinary entries in the player's rotating model set;
    // find them by resref so the gauge and the radar can be driven separately.
    auto findPane = [this](const std::string &resRef) -> std::shared_ptr<ModelSceneNode> {
        if (_turretRoot && boost::iequals(_turretRoot->model().name(), resRef)) {
            return _turretRoot;
        }
        for (const auto &node : _turretChildNodes) {
            if (boost::iequals(node->model().name(), resRef)) {
                return node;
            }
        }
        return nullptr;
    };
    _healthHud = findPane("mgf_hud02");
    _radarHud = findPane("mgf_hud01");
}

void Turret::resetHud() {
    // A replayed session must not inherit the previous run's gauge, heading,
    // contacts or alarm.
    clearHud();
    // No damage state is selected yet. The shipped script only ever plays a
    // Health animation from its damage path, so a pristine session shows the
    // model's authored rest pose - forcing Health12 up front would put the
    // gauge into its healthiest *damaged* state, and on the lower states would
    // switch on damage lighting that should not exist before the first hit.
    _healthState = -1;
    _headingState = -1;
    _contactAlive.assign(_enemies.size(), true);
    startContactHud();
    updateHeadingHud();
}

void Turret::updateHealthHud() {
    int state = turretHealthState(_hitPoints);
    if (state == _healthState) {
        // Nothing crossed a band boundary, so leave the running clip alone -
        // the low states are animated and must not be restarted every frame.
        return;
    }

    // The shipped script starts the alarm on the frame the state becomes 3, and
    // never restarts or clears it except in the destruction branch.
    if (turretAlarmStartsAtState(state)) {
        setAlarmActive(true);
    }

    if (_healthHud) {
        if (_healthState >= 0) {
            _healthHud->removeAnimation(turretHealthAnimation(_healthState));
        }
        _healthHud->playAnimation(turretHealthAnimation(state), nullptr,
                                  AnimationProperties::fromFlags(AnimationFlags::loopOverlay));
    }
    _healthState = state;
}

void Turret::updateHeadingHud() {
    int heading = turretHeadingState(_aim.yaw());
    if (heading == _headingState) {
        return;
    }
    if (_radarHud) {
        if (_headingState >= 0) {
            _radarHud->removeAnimation(turretHeadingAnimation(_headingState));
        }
        // Heading poses are zero length; overlay keeps them from disturbing the
        // contact loops, which animate entirely different nodes.
        _radarHud->playAnimation(turretHeadingAnimation(heading), nullptr,
                                 AnimationProperties::fromFlags(AnimationFlags::loopOverlay));
    }
    _headingState = heading;
}

void Turret::startContactHud() {
    if (!_radarHud) {
        return;
    }
    for (size_t i = 0; i < _enemies.size() && i < kTurretContactCount; ++i) {
        // Clear any death pose left by a previous session before re-arming.
        _radarHud->removeAnimation(turretContactDeathAnimation(i));
        _radarHud->playAnimation(turretContactAnimation(i), nullptr,
                                 AnimationProperties::fromFlags(AnimationFlags::loopOverlay));
    }
}

void Turret::killContactHud(size_t enemyIndex) {
    if (enemyIndex >= _contactAlive.size() || !_contactAlive[enemyIndex]) {
        return; // already dead: repeated notification is harmless
    }
    _contactAlive[enemyIndex] = false;
    if (!_radarHud || enemyIndex >= kTurretContactCount) {
        return;
    }
    // Swap the live loop for the authored removal pose, leaving the other five
    // contacts and the heading untouched.
    _radarHud->removeAnimation(turretContactAnimation(enemyIndex));
    _radarHud->playAnimation(turretContactDeathAnimation(enemyIndex), nullptr,
                             AnimationProperties::fromFlags(AnimationFlags::loopOverlay));
}

void Turret::clearHud() {
    setAlarmActive(false);
    if (_radarHud) {
        for (size_t i = 0; i < kTurretContactCount; ++i) {
            _radarHud->removeAnimation(turretContactAnimation(i));
            _radarHud->removeAnimation(turretContactDeathAnimation(i));
        }
        if (_headingState >= 0) {
            _radarHud->removeAnimation(turretHeadingAnimation(_headingState));
        }
    }
    if (_healthHud && _healthState >= 0) {
        _healthHud->removeAnimation(turretHealthAnimation(_healthState));
    }
}

void Turret::setAlarmActive(bool active) {
    if (active == _alarmActive) {
        return; // never restart a looping sound that is already running
    }
    _alarmActive = active;
    // Drive the authored module sound object rather than an ad hoc source.
    auto module = _game.module();
    auto area = module ? module->area() : nullptr;
    if (!area) {
        return;
    }
    auto object = area->getObjectByTag(kTurretAlarmTag);
    if (!object || object->type() != ObjectType::Sound) {
        return;
    }
    std::static_pointer_cast<Sound>(object)->setActive(active);
}

void Turret::playSound(const std::string &resRef) {
    auto clip = _services.resource.audioClips.get(resRef);
    if (!clip) {
        return;
    }
    _services.audio.mixer.play(std::move(clip), AudioType::Sound);
}

bool Turret::handle(const input::Event &event) {
    switch (event.type) {
    case input::EventType::KeyDown:
        return handleKeyDown(event.key);
    case input::EventType::KeyUp:
        return handleKeyUp(event.key);
    case input::EventType::MouseMotion:
        return handleMouseMotion(event.motion);
    case input::EventType::MouseButtonDown:
    case input::EventType::MouseButtonUp:
        return handleMouseButton(event.button);
    default:
        return false;
    }
}

bool Turret::handleKeyDown(const input::KeyEvent &event) {
    switch (event.code) {
    case input::KeyCode::Up:
    case input::KeyCode::W:
        _pitchRate.setDirection(1);
        return true;
    case input::KeyCode::Down:
    case input::KeyCode::S:
        _pitchRate.setDirection(-1);
        return true;
    case input::KeyCode::Left:
    case input::KeyCode::A:
        _yawRate.setDirection(1);
        return true;
    case input::KeyCode::Right:
    case input::KeyCode::D:
        _yawRate.setDirection(-1);
        return true;
    case input::KeyCode::Space:
        _firing = true;
        return true;
    case input::KeyCode::Escape:
        _game.exitTurret();
        return true;
    default:
        return false;
    }
}

bool Turret::handleKeyUp(const input::KeyEvent &event) {
    switch (event.code) {
    case input::KeyCode::Up:
    case input::KeyCode::W:
        if (_pitchRate.direction() == 1) {
            _pitchRate.setDirection(0);
        }
        return true;
    case input::KeyCode::Down:
    case input::KeyCode::S:
        if (_pitchRate.direction() == -1) {
            _pitchRate.setDirection(0);
        }
        return true;
    case input::KeyCode::Left:
    case input::KeyCode::A:
        if (_yawRate.direction() == 1) {
            _yawRate.setDirection(0);
        }
        return true;
    case input::KeyCode::Right:
    case input::KeyCode::D:
        if (_yawRate.direction() == -1) {
            _yawRate.setDirection(0);
        }
        return true;
    case input::KeyCode::Space:
        _firing = false;
        return true;
    default:
        return false;
    }
}

bool Turret::handleMouseMotion(const input::MouseMotionEvent &event) {
    if (!_active) {
        return false;
    }
    float yaw = -event.xrel * kMouseAimRadiansPerPixel;
    float pitch = -event.yrel * kMouseAimRadiansPerPixel;
    if (_spec.mouse.flipAxisX) {
        yaw = -yaw;
    }
    if (_spec.mouse.flipAxisY) {
        pitch = -pitch;
    }
    _aim.addYaw(yaw);
    _aim.addPitch(pitch);
    return true;
}

bool Turret::handleMouseButton(const input::MouseButtonEvent &event) {
    if (event.button != input::MouseButton::Left) {
        return false;
    }
    _firing = event.pressed;
    return true;
}

} // namespace game

} // namespace reone
