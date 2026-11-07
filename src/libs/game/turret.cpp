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

#include <algorithm>
#include <array>
#include <stack>

#include "glm/gtx/intersect.hpp"

#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/camera/firstperson.h"
#include "reone/game/event.h"
#include "reone/game/object/area.h"
#include "reone/game/object/minigameobject.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/game/savedruntime.h"
#include "reone/game/script/runner.h"
#include "reone/graphics/animation.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/modelnode.h"
#include "reone/graphics/types.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/layouts.h"
#include "reone/resource/provider/models.h"
#include "reone/scene/di/services.h"
#include "reone/scene/collision.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/camera.h"
#include "reone/scene/node/model.h"
#include "reone/scene/node/modelnode.h"
#include "reone/system/logutil.h"
#include "reone/system/randomutil.h"

using namespace reone::audio;
using namespace reone::graphics;
using namespace reone::scene;

namespace reone {

namespace game {

namespace {

// Models look down +Y.
const glm::vec3 kModelForward(0.0f, 1.0f, 0.0f);

// The aim's top turn rate and its acceleration, in degrees per second, where
// the area gives none.
constexpr float kDefaultMovementPerSecond = 90.0f;
constexpr float kDefaultLateralAcceleration = 60.0f;

// The player's default speed; its track plays at a hundredth of it.
constexpr float kDefaultSpeed = 100.0f;

// Mouse aim sensitivity, matching FirstPersonCamera's kMouseMultiplier so the
// turret aims like the first-person camera.
const float kMouseAimRadiansPerPixel = glm::pi<float>() / 4000.0f;

// Default field of view, restored to the reused first-person camera on
// exit (mirrors Area's default camera FOV).
constexpr float kDefaultCameraFovDegrees = 75.0f;

// Hook names used to assemble a minigame actor.
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

// The yaw of a direction, in degrees, positive toward -X from +Y, and its
// pitch, positive upward; and the orientation that turns +Y to them.
float turretYawDegrees(const glm::vec3 &direction) {
    if (direction.x == 0.0f && direction.y == 0.0f) {
        return 0.0f;
    }
    return glm::degrees(glm::atan(-direction.x, direction.y));
}

float turretPitchDegrees(const glm::vec3 &direction) {
    return glm::degrees(glm::atan(direction.z, glm::sqrt(direction.x * direction.x + direction.y * direction.y)));
}

glm::quat turretAimOrientation(float yawDegrees, float pitchDegrees) {
    return glm::angleAxis(glm::radians(yawDegrees), glm::vec3(0.0f, 0.0f, 1.0f)) *
           glm::angleAxis(glm::radians(pitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
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
 * Suppress free-running projectile emitters during flight.
 *
 * The bolt model's explosion emitters activate on the detonate event. Leaving their birth
 * rate active throughout flight would create a continuous explosion trail. Keep the visible
 * bolt mesh unchanged.
 */
void silenceEmitters(SceneNode &node, bool silent = true) {
    for (auto &child : node.children()) {
        if (child->type() == SceneNodeType::Emitter) {
            child->setEnabled(!silent);
            continue;
        }
        silenceEmitters(*child, silent);
    }
}

// The nearest point where a segment crosses a mesh of the model at or under
// \p node, with the face's normal and the mesh's name. Attached models are
// models of their own and are not searched.
void intersectMeshes(const SceneNode &node,
                     const glm::vec3 &from,
                     const glm::vec3 &to,
                     std::optional<std::pair<float, Turret::BulletHit>> &nearest) {
    if (node.type() == SceneNodeType::Mesh) {
        const auto &meshNode = static_cast<const ModelNodeSceneNode &>(node);
        auto mesh = meshNode.modelNode().mesh();
        if (mesh && mesh->mesh && !mesh->skin && !mesh->saber) {
            const glm::vec3 localFrom {meshNode.absoluteTransformInverse() * glm::vec4(from, 1.0f)};
            const glm::vec3 localTo {meshNode.absoluteTransformInverse() * glm::vec4(to, 1.0f)};
            const float length = glm::length(localTo - localFrom);
            float boxDistance = 0.0f;
            if (length > 0.0f) {
                const glm::vec3 dir {(localTo - localFrom) / length};
                if (mesh->mesh->aabb().raycast(localFrom, 1.0f / dir, length, boxDistance)) {
                    const auto &vertices = mesh->mesh->vertices();
                    for (const auto &face : mesh->mesh->faces()) {
                        const auto &a = vertices[face.vertices[0]].position;
                        const auto &b = vertices[face.vertices[1]].position;
                        const auto &c = vertices[face.vertices[2]].position;
                        glm::vec2 barycentric {0.0f};
                        float distance = 0.0f;
                        if (!glm::intersectRayTriangle(localFrom, dir, a, b, c, barycentric, distance) ||
                            distance < 0.0f || distance > length) {
                            continue;
                        }
                        const float fraction = distance / length;
                        if (nearest && nearest->first <= fraction) {
                            continue;
                        }
                        Turret::BulletHit hit;
                        hit.position = from + (to - from) * fraction;
                        glm::vec3 normal = glm::mat3(meshNode.absoluteTransform()) * glm::cross(b - a, c - a);
                        hit.normal = glm::length2(normal) > 0.0f ? glm::normalize(normal) : glm::vec3(0.0f, 0.0f, 1.0f);
                        hit.part = meshNode.modelNode().name();
                        nearest = std::make_pair(fraction, std::move(hit));
                    }
                }
            }
        }
    }
    for (auto &child : node.children()) {
        if (child->type() == SceneNodeType::Mesh || child->type() == SceneNodeType::Dummy) {
            intersectMeshes(*child, from, to, nearest);
        }
    }
}


// An infinite axis wraps into [0, 360); any other keeps between its origin
// plus the negative and positive tunnel deltas.
float keepAxisInTunnel(float offset, float origin, float positive, float negative, bool infinite) {
    if (infinite) {
        if (offset < 0.0f) {
            return offset + 360.0f;
        }
        if (offset > 359.0f) {
            return offset - 360.0f;
        }
        return offset;
    }
    if (offset > origin + positive) {
        return origin + positive;
    }
    if (offset < origin + negative) {
        return origin + negative;
    }
    return offset;
}

} // namespace

void TurretAim::configure(const MinigamePlayerSpec &player) {
    _origin = player.startOffset;
    _offset = _origin;
    _tunnelPositive = glm::vec3(player.tunnelXPos, player.tunnelYPos, player.tunnelZPos);
    _tunnelNegative = glm::vec3(player.tunnelXNeg, player.tunnelYNeg, player.tunnelZNeg);
    _tunnelInfinite = player.tunnelInfinite;
}

void TurretAim::setOffset(const glm::vec3 &degrees) {
    _offset = degrees;
    keepInTunnel();
}

void TurretAim::turn(const glm::vec3 &degrees) {
    setOffset(_offset + degrees);
}

void TurretAim::setOrigin(const glm::vec3 &degrees, bool trackPlaying) {
    _origin = degrees;
    if (!trackPlaying) {
        _offset = degrees;
    }
}

void TurretAim::keepInTunnel() {
    for (int axis = 0; axis < 3; ++axis) {
        _offset[axis] = keepAxisInTunnel(_offset[axis],
                                         _origin[axis],
                                         _tunnelPositive[axis],
                                         _tunnelNegative[axis],
                                         _tunnelInfinite[axis] != 0.0f);
    }
}

glm::quat TurretAim::orientation() const {
    return glm::angleAxis(glm::radians(_offset.z), glm::vec3(0.0f, 0.0f, 1.0f)) *
           glm::angleAxis(glm::radians(_offset.x), glm::vec3(1.0f, 0.0f, 0.0f)) *
           glm::angleAxis(glm::radians(_offset.y), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::vec3 TurretAim::forward() const {
    return orientation() * kModelForward;
}

void TurretAimRate::configure(float lateralAcceleration, float movementPerSecond) {
    _acceleration = lateralAcceleration;
    _topRate = movementPerSecond;
    reset();
}

void TurretAimRate::setDirection(int direction) {
    _direction = direction > 0 ? 1 : (direction < 0 ? -1 : 0);
}

void TurretAimRate::reset() {
    _direction = 0;
    _rate = 0.0f;
}

// The rate follows dv/dt = a * (target - v) / top, the target being the held
// direction times the top rate, which settles on the target with the time
// constant top / a. Integrated in closed form over the step.
float TurretAimRate::advance(float dt) {
    if (dt <= 0.0f || _topRate <= 0.0f || _acceleration <= 0.0f) {
        return 0.0f;
    }
    const float target = static_cast<float>(_direction) * _topRate;
    const float k = _acceleration / _topRate;
    const float decay = glm::exp(-k * dt);
    const float angle = target * dt + (_rate - target) * (1.0f - decay) / k;
    _rate = target + (_rate - target) * decay;
    return angle;
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

void TurretGunTimer::setRateOfFire(float rateOfFire) {
    if (_cooldown > 0.0f && _cooldown < _rateOfFire) {
        _cooldown -= _rateOfFire - rateOfFire;
    }
    _rateOfFire = rateOfFire;
}

bool TurretBullet::advance(float dt) {
    // A bolt is created and integrated within the same tick, so its first step
    // is spent standing at the muzzle: that is the only frame in which it is
    // drawn where the authored gun bank actually points.
    previousPosition = position;
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


uint32_t Turret::Actor::id() const {
    return object ? object->id() : script::kObjectInvalid;
}

void Turret::ModelEvents::onEventSignalled(const std::string &name) {
    _turret.queueAnimationEvent(_actor, _modelName, name);
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
                                                  const glm::vec3 &position,
                                                  std::string &animation) {
    auto node = loadModel(resRef);
    if (!node) {
        return nullptr;
    }
    node->setLocalTransform(glm::translate(position));
    // A track's "track" animation walks the "modelhook" node along the rail;
    // everything riding the track is parented to that hook.
    if (node->model().getAnimation("track")) {
        animation = "track";
    } else if (auto animations = node->model().getAnimationNames(); !animations.empty()) {
        animation = animations.front();
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
        bank.spec = spec;
        // A value the routines that set it would refuse is not taken.
        auto &bullet = bank.spec.bullet;
        bullet.speed = glm::max(0.0f, bullet.speed);
        bullet.rateOfFire = glm::max(0.0f, bullet.rateOfFire);
        bullet.lifespan = glm::max(0.0f, bullet.lifespan);
        if ((bullet.targetType & ~3u) != 0) {
            bullet.targetType = 0;
        }
        for (float *spread : {&bank.spec.horizSpread, &bank.spec.vertSpread}) {
            if (*spread < 0.0f && *spread != -1.0f) {
                *spread = 0.0f;
            }
        }
        bank.spec.sensingRadius = glm::max(0.0f, bank.spec.sensingRadius);
        bank.spec.inaccuracy = glm::max(0.0f, bank.spec.inaccuracy);
        bank.timer = TurretGunTimer(bullet.rateOfFire);
        bank.modelNode = loadModel(spec.gunModelResRef);
        if (bank.modelNode) {
            mount.attach(gunBankHookName(spec.bankId), *bank.modelNode);
        }
        out.push_back(std::move(bank));
    }
}

void Turret::registerActor(Actor &actor) {
    actor.object = _game.newObject<MinigameObject>(kSceneMain, _game, _services);
}

void Turret::listenTo(Actor &actor, ModelSceneNode &model) {
    actor.modelEvents.push_back(std::make_unique<ModelEvents>(*this, actor, model.model().name()));
    model.setAnimationEventListener(*actor.modelEvents.back());
}

void Turret::loadFollower(Follower &follower,
                          uint32_t hitPoints,
                          uint32_t maxHitPoints,
                          float sphereRadius,
                          int numLoops,
                          float invincibility,
                          const std::string &deathSound) {
    if (hitPoints > 0) {
        follower.hitPoints = static_cast<int>(hitPoints);
    }
    follower.maxHitPoints = maxHitPoints > 0 ? static_cast<int>(maxHitPoints) : follower.hitPoints;
    follower.sphereRadius = glm::max(0.0f, sphereRadius);
    setNumLoops(follower, numLoops);
    follower.invincibility = glm::max(0.0f, invincibility);
    follower.deathSound = deathSound;
    for (auto &model : follower.models) {
        listenTo(follower, *model);
    }
    if (follower.trackNode) {
        follower.name = follower.trackNode->model().name();
        listenTo(follower, *follower.trackNode);
    }
}

// The player loops a number of times or, at -1, for ever; an enemy loops once
// or for ever. Any other count is ignored.
void Turret::setNumLoops(Follower &follower, int loops) {
    const bool valid = follower.player ? (loops == -1 || loops > 0) : (loops == 1 || loops == -1);
    if (valid) {
        follower.numLoops = loops;
    }
}

// A follower sets off along its track at its speed: once through when it is to
// loop once, else round and round.
void Turret::go(Follower &follower) {
    if (!follower.trackNode || follower.trackAnimation.empty()) {
        return;
    }
    const int flags = follower.numLoops == 1 ? 0 : AnimationFlags::loop;
    follower.trackNode->playAnimation(follower.trackAnimation, nullptr, AnimationProperties::fromFlags(flags));
    if (follower.player) {
        follower.trackNode->setAnimationSpeed(follower.trackAnimation, _speed / kDefaultSpeed);
    }
}

bool Turret::loadPlayer() {
    const auto &spec = _spec.player;
    auto player = std::make_unique<Follower>();
    player->player = true;

    glm::vec3 trackPos(0.0f);
    _anchorSource = "origin";
    if (auto layout = _services.resource.layouts.get(_areaName)) {
        if (auto placement = layout->findTrackByName(_trackResRef)) {
            trackPos = placement->get().position;
            _anchorSource = "lyt-track";
        }
    }
    player->trackNode = loadTrack(_trackResRef, trackPos, player->trackAnimation);
    if (player->trackNode) {
        if (_anchorSource == "origin") {
            _anchorSource = "track-model";
        }
    } else {
        _anchorSource = "no-track";
    }

    std::vector<std::shared_ptr<ModelSceneNode>> bodyModels;
    for (const auto &model : spec.models) {
        auto node = loadModel(model.resRef);
        if (!node) {
            debug("turret: player model '" + model.resRef + "' missing");
            continue;
        }
        if (model.rotating) {
            if (!_turretRoot) {
                _turretRoot = node;
            } else {
                _turretRoot->addChild(*node);
            }
            player->models.push_back(std::move(node));
        } else {
            if (!_bodyRoot) {
                _bodyRoot = node;
            } else {
                _bodyRoot->addChild(*node);
            }
            bodyModels.push_back(std::move(node));
        }
    }
    if (!_turretRoot) {
        if (player->trackNode) {
            _services.scene.graphs.get(kSceneMain).removeRoot(*player->trackNode);
        }
        return false;
    }
    player->models.insert(player->models.end(), bodyModels.begin(), bodyModels.end());

    // Both groups ride the player track's hook so the hull and the gun stay
    // together; only the rotating group takes the aim rotation on top.
    if (player->trackNode) {
        player->trackNode->attach(kModelHookName, *_turretRoot);
        if (_bodyRoot) {
            player->trackNode->attach(kModelHookName, *_bodyRoot);
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

    loadGunBanks(spec.gunBanks, *_turretRoot, player->gunBanks);
    player->scripts = {spec.scripts.onCreate,
                       spec.scripts.onHeartbeat,
                       spec.scripts.onDamage,
                       spec.scripts.onDeath,
                       spec.scripts.onFire,
                       spec.scripts.onHitBullet,
                       spec.scripts.onHitFollower,
                       spec.scripts.onHitObstacle,
                       spec.scripts.onTrackLoop,
                       spec.scripts.onAnimEvent};
    registerActor(*player);
    loadFollower(*player, spec.hitPoints, spec.maxHitPoints, spec.sphereRadius,
                 spec.numLoops, spec.invincePeriod, spec.sounds.death);
    _player = std::move(player);
    return true;
}

void Turret::loadEnemies() {
    auto layout = _services.resource.layouts.get(_areaName);
    for (const auto &spec : _spec.enemies) {
        auto enemy = std::make_unique<Follower>();

        glm::vec3 trackPos(0.0f);
        if (layout) {
            if (auto placement = layout->findTrackByName(spec.trackResRef)) {
                trackPos = placement->get().position;
            }
        }
        enemy->trackNode = loadTrack(spec.trackResRef, trackPos, enemy->trackAnimation);

        std::shared_ptr<ModelSceneNode> root;
        for (const auto &model : spec.models) {
            auto node = loadModel(model.resRef);
            if (!node) {
                continue;
            }
            if (!root) {
                root = node;
            } else {
                root->addChild(*node);
            }
            enemy->models.push_back(std::move(node));
        }
        // An enemy with no model or no track is not made.
        if (!root || !enemy->trackNode) {
            debug("turret: enemy on track '" + spec.trackResRef + "' has no model or no track");
            if (enemy->trackNode) {
                _services.scene.graphs.get(kSceneMain).removeRoot(*enemy->trackNode);
            }
            continue;
        }
        enemy->trackNode->attach(kModelHookName, *root);
        loadGunBanks(spec.gunBanks, *root, enemy->gunBanks);
        enemy->scripts = {spec.scripts.onCreate,
                          spec.scripts.onHeartbeat,
                          spec.scripts.onDamage,
                          spec.scripts.onDeath,
                          spec.scripts.onFire,
                          spec.scripts.onHitBullet,
                          spec.scripts.onHitFollower,
                          spec.scripts.onHitObstacle,
                          spec.scripts.onTrackLoop,
                          spec.scripts.onAnimEvent};
        registerActor(*enemy);
        loadFollower(*enemy, spec.hitPoints, spec.maxHitPoints, spec.sphereRadius,
                     spec.numLoops, spec.invincePeriod, spec.sounds.death);
        _enemies.push_back(std::move(enemy));
        // An enemy's creation script runs before it is known to be a trigger.
        auto &created = *_enemies.back();
        runScript(created.scripts.onCreate, created);
        created.trigger = spec.trigger;
    }
}

// Each obstacle of the area layout is an object.
void Turret::loadObstacles() {
    auto layout = _services.resource.layouts.get(_areaName);
    if (!layout) {
        return;
    }
    for (const auto &placement : layout->obstacles) {
        auto obstacle = std::make_unique<Obstacle>();
        auto node = loadModel(placement.name);
        if (node) {
            node->setLocalTransform(glm::translate(placement.position));
            _services.scene.graphs.get(kSceneMain).addRoot(node);
            obstacle->name = node->model().name();
            obstacle->models.push_back(std::move(node));
        } else {
            obstacle->name = placement.name;
        }
        registerActor(*obstacle);
        _obstacles.push_back(std::move(obstacle));
    }
}

void Turret::loadObstacleScripts() {
    // The area names the scripts of the first obstacle with each name.
    for (const auto &spec : _spec.obstacles) {
        auto named = actorByName(spec.name);
        auto obstacle = named ? this->obstacle(named->id()) : nullptr;
        if (obstacle) {
            obstacle->onHeartbeat = spec.onHeartbeat;
            obstacle->onHitBullet = spec.onHitBullet;
            obstacle->onHitFollower = spec.onHitFollower;
        }
    }
}

bool Turret::start(const MinigameSpec &spec, FirstPersonCamera *camera, const std::string &areaName) {
    if (_active) {
        return false;
    }
    _spec = spec;
    _areaName = areaName;
    _camera = camera;
    _elapsed = 0.0f;
    _firing = false;
    _trackPlaying = false;
    _speed = kDefaultSpeed;
    _trackResRef = _spec.player.trackResRef;
    _lateralAcceleration = _spec.lateralAccel > 0.0f ? _spec.lateralAccel : kDefaultLateralAcceleration;
    _nearClip = _spec.nearClip > 0.0f ? _spec.nearClip : kDefaultClipPlaneNear;
    _farClip = _spec.farClip > 0.0f ? _spec.farClip : kDefaultClipPlaneFar;
    _aim = TurretAim();

    // The layout's obstacles are made first, then the player and the enemies.
    // The player's creation script runs before its aim is read from the area.
    loadObstacles();
    if (!loadPlayer()) {
        debug("turret: no visible player models loaded");
        detachAll();
        _camera = nullptr;
        return false;
    }
    _active = true;
    runScript(_player->scripts.onCreate, *_player);
    _aim.configure(_spec.player);
    const float movementPerSecond = _spec.movementPerSec > 0.0f ? _spec.movementPerSec : kDefaultMovementPerSecond;
    _pitchRate.configure(_lateralAcceleration, movementPerSecond);
    _yawRate.configure(_lateralAcceleration, movementPerSecond);
    loadEnemies();
    loadObstacleScripts();

    // The camera mount supplies only its static camerahook transform
    // and is not added to the scene.
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

    suppressCanopyGlass();
    applyCameraProjection();
    go(*_player);
    for (auto it = _enemies.rbegin(); it != _enemies.rend(); ++it) {
        go(**it);
    }
    _trackPlaying = true;
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
    _active = false;
    _trackPlaying = false;
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

void Turret::detachFromTrack(Follower &follower) {
    if (follower.models.empty() || follower.detachedFromTrack) {
        return;
    }
    follower.detachedFromTrack = true;
    if (!follower.trackNode) {
        return; // already a scene root: nothing is translating it
    }
    // Preserve the pose the fighter died in, then move it from the rail's hook
    // to the scene root the tracks themselves live under. The hook keeps
    // looping, but the wreck no longer inherits it; anything the "die"
    // animation does to the model still applies on top of this transform.
    auto &root = follower.models.front();
    glm::mat4 world = root->absoluteTransform();
    clearHook(follower.trackNode, kModelHookName);
    root->setLocalTransform(world);
    // A wreck sits where it was killed while the rail carries on, so the
    // culling that assumes actors stay near their track root no longer holds.
    root->setCullingEnabled(false);
    _services.scene.graphs.get(kSceneMain).addRoot(root);
}

void Turret::releaseFollowerNodes(Follower &follower) {
    // Everything a follower owns hangs off its model roots: the authored gun
    // bank models on their hooks, and - built by the model loader itself - the
    // reference models it names (mgf_sithfighter attaches an fx_ref engine
    // flare at each of its OmenRef nodes), plus its lights and emitters.
    auto &sceneGraph = _services.scene.graphs.get(kSceneMain);
    for (auto &model : follower.models) {
        model->clearAnimationEventListener();
    }
    if (follower.trackNode) {
        follower.trackNode->clearAnimationEventListener();
    }
    for (auto &bank : follower.gunBanks) {
        for (auto &model : follower.models) {
            clearHook(model, gunBankHookName(bank.spec.bankId));
        }
    }
    follower.gunBanks.clear();
    clearHook(follower.trackNode, kModelHookName);
    for (auto &model : follower.models) {
        model->removeAllChildren();
        sceneGraph.removeRoot(*model);
    }
    if (follower.trackNode) {
        sceneGraph.removeRoot(*follower.trackNode);
    }
    follower.models.clear();
    follower.trackNode.reset();
    follower.released = true;
}

void Turret::detachAll() {
    auto &sceneGraph = _services.scene.graphs.get(kSceneMain);

    _pendingAnimationEvents.clear();
    _readyReturns.clear();
    for (auto &explosion : _explosions) {
        sceneGraph.removeRoot(*explosion.modelNode);
    }
    _explosions.clear();

    for (auto &bullet : _bullets) {
        releaseBulletNode(bullet);
    }
    _bullets.clear();
    for (auto &pooled : _bulletNodePool) {
        pooled.second.clear();
    }
    _bulletNodePool.clear();

    for (auto &enemy : _enemies) {
        releaseFollowerNodes(*enemy);
        _game.destroyRuntimeObjectGraph(enemy->object);
    }
    _enemies.clear();

    if (_player) {
        releaseFollowerNodes(*_player);
        _game.destroyRuntimeObjectGraph(_player->object);
        _player.reset();
    }
    _turretRoot.reset();
    _bodyRoot.reset();

    for (auto &obstacle : _obstacles) {
        for (auto &model : obstacle->models) {
            model->clearAnimationEventListener();
            sceneGraph.removeRoot(*model);
        }
        _game.destroyRuntimeObjectGraph(obstacle->object);
    }
    _obstacles.clear();
}

Turret::Follower *Turret::follower(uint32_t id) {
    if (_player && _player->id() == id) {
        return _player.get();
    }
    for (auto &enemy : _enemies) {
        if (enemy->id() == id) {
            return enemy.get();
        }
    }
    return nullptr;
}

Turret::Obstacle *Turret::obstacle(uint32_t id) {
    for (auto &obstacle : _obstacles) {
        if (obstacle->id() == id) {
            return obstacle.get();
        }
    }
    return nullptr;
}

Turret::Actor *Turret::actor(uint32_t id) {
    if (auto found = follower(id)) {
        return found;
    }
    return obstacle(id);
}

std::vector<Turret::Follower *> Turret::listedEnemies() {
    std::vector<Follower *> result;
    for (auto &enemy : _enemies) {
        if (enemy->listed) {
            result.push_back(enemy.get());
        }
    }
    return result;
}

std::vector<Turret::Obstacle *> Turret::listedObstacles() {
    std::vector<Obstacle *> result;
    for (auto &obstacle : _obstacles) {
        if (obstacle->listed) {
            result.push_back(obstacle.get());
        }
    }
    return result;
}

// Model names are compared without case: model resrefs are read in lower case.
Turret::Actor *Turret::actorByName(const std::string &name) {
    for (auto &obstacle : _obstacles) {
        if (boost::iequals(obstacle->name, name)) {
            return obstacle.get();
        }
    }
    if (_player && boost::iequals(_player->name, name)) {
        return _player.get();
    }
    for (auto &enemy : _enemies) {
        if (boost::iequals(enemy->name, name)) {
            return enemy.get();
        }
    }
    return nullptr;
}

void Turret::runScript(const std::string &resRef, const Actor &actor) {
    if (resRef.empty() || !actor.object) {
        return;
    }
    _game.scriptRunner().run(resRef, actor.id());
}

// Each frame the objects run their heartbeats and count down their
// invulnerability, the player first, then the enemies and the obstacles, each
// last to first. Bullets are then checked against them while the player's track
// plays.
void Turret::update(float dt) {
    if (!_active || dt <= 0.0f) {
        return;
    }
    _elapsed += dt;

    runAnimationEvents();
    updateReadyReturns();
    updateExplosions();
    if (_player) {
        checkTrackLoop(*_player);
    }
    for (size_t i = _enemies.size(); i-- > 0;) {
        if (i < _enemies.size()) {
            checkTrackLoop(*_enemies[i]);
        }
    }

    _aim.turn(glm::vec3(_pitchRate.advance(dt), 0.0f, _yawRate.advance(dt)));

    updateAnchor();
    updatePlayerTransforms();
    updateCamera();

    if (_player) {
        updateFollower(*_player, dt);
    }
    for (size_t i = _enemies.size(); i-- > 0;) {
        if (i < _enemies.size() && _enemies[i]->listed) {
            updateFollower(*_enemies[i], dt);
        }
    }
    for (size_t i = _obstacles.size(); i-- > 0;) {
        if (i < _obstacles.size() && _obstacles[i]->listed) {
            runScript(_obstacles[i]->onHeartbeat, *_obstacles[i]);
        }
    }
    if (!_active) {
        return;
    }

    if (_player) {
        for (auto &bank : _player->gunBanks) {
            bank.timer.update(dt);
        }
        if (_firing && !_player->dying) {
            fire();
        }
    }
    for (auto &enemy : _enemies) {
        if (!enemy->dying) {
            updateEnemyFire(*enemy, dt);
        }
    }
    updateBullets(dt);
    if (_player && _trackPlaying) {
        checkBulletHits();
    }

    if (_player) {
        updateDeath(*_player, dt);
    }
    for (auto &enemy : _enemies) {
        updateDeath(*enemy, dt);
    }
    // A follower whose death has played out is gone.
    if (_player && _player->released) {
        _game.destroyRuntimeObjectGraph(_player->object);
        _player.reset();
        _turretRoot.reset();
        _bodyRoot.reset();
    }
    for (auto it = _enemies.begin(); it != _enemies.end();) {
        if ((*it)->released) {
            _game.destroyRuntimeObjectGraph((*it)->object);
            it = _enemies.erase(it);
        } else {
            ++it;
        }
    }
}

void Turret::updateFollower(Follower &follower, float dt) {
    if (!follower.models.empty()) {
        const glm::mat4 &transform = follower.models.front()->absoluteTransform();
        follower.position = transformOrigin(transform);
        follower.orientation = transformOrientation(transform);
        follower.forward = turretFireDirection(follower.orientation);
    }
    runScript(follower.scripts.onHeartbeat, follower);
    follower.invincibilityTimer = glm::max(0.0f, follower.invincibilityTimer - dt);
}

// A track that loops raises a loop event each time it goes round. The scene
// raises none, so a track whose animation is further back than at the last
// update has gone round. A dying follower's track no longer counts.
void Turret::checkTrackLoop(Follower &follower) {
    auto channel = follower.trackNode && !follower.dying
                       ? follower.trackNode->baseAnimationChannel()
                       : std::nullopt;
    if (!channel || !boost::iequals(channel->get().anim->name(), follower.trackAnimation)) {
        follower.lastTrackTime.reset();
        return;
    }
    const auto &track = channel->get();
    const bool looping = (track.properties.flags & AnimationFlags::loop) != 0;
    const float speed = track.properties.speed;
    const bool wentRound = looping && follower.lastTrackTime &&
                           ((speed > 0.0f && track.time < *follower.lastTrackTime) ||
                            (speed < 0.0f && track.time > *follower.lastTrackTime));
    follower.lastTrackTime = track.time;
    if (wentRound) {
        onAnimationEvent(follower, follower.trackNode->model().name(), "loop" + follower.trackAnimation);
    }
}

void Turret::updateAnchor() {
    if (_player && _player->trackNode) {
        if (auto hook = _player->trackNode->getNodeByName(kModelHookName)) {
            _anchorTransform = hook->absoluteTransform();
        } else {
            _anchorTransform = _player->trackNode->absoluteTransform();
        }
    }
    _anchorPosition = transformOrigin(_anchorTransform);
}

void Turret::updatePlayerTransforms() {
    // The roots are parented to the track hook, so their local transform is the
    // actor's placement within the hook frame: both groups sit on the hook and
    // only the rotating one takes the aim rotation.
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

void Turret::applyCameraProjection() {
    if (!_camera) {
        return;
    }
    const float fov = _spec.cameraViewAngle > 0.0f ? _spec.cameraViewAngle : kDefaultCameraFovDegrees;
    _camera->cameraSceneNode()->setPerspectiveProjection(
        glm::radians(fov),
        cameraAspect(),
        _nearClip,
        _farClip);
}

void Turret::setCameraClip(float nearClip, float farClip) {
    _nearClip = glm::min(nearClip, farClip);
    _farClip = glm::max(nearClip, farClip);
    applyCameraProjection();
}

void Turret::setSpeed(float speed) {
    _speed = speed;
    if (_trackPlaying && _player && _player->trackNode && !_player->trackAnimation.empty()) {
        _player->trackNode->setAnimationSpeed(_player->trackAnimation, speed / kDefaultSpeed);
    }
}

void Turret::setLateralAcceleration(float acceleration) {
    _lateralAcceleration = acceleration;
    _pitchRate.setLateralAcceleration(acceleration);
    _yawRate.setLateralAcceleration(acceleration);
}

float Turret::cameraAspect() const {
    const auto &graphicsOpts = _game.options().graphics;
    if (graphicsOpts.height <= 0) {
        return 1.0f;
    }
    return graphicsOpts.width / static_cast<float>(graphicsOpts.height);
}

void Turret::fire() {
    if (!_turretRoot || !_player) {
        return;
    }
    bool fired = false;
    for (auto &bank : _player->gunBanks) {
        if (!bank.timer.tryFire()) {
            continue;
        }
        glm::mat4 muzzle = _turretRoot->absoluteTransform();
        if (bank.modelNode) {
            if (auto hook = bank.modelNode->getNodeByName(kBulletHookName)) {
                muzzle = hook->absoluteTransform();
            } else {
                muzzle = bank.modelNode->absoluteTransform();
            }
            playThenReady(bank.modelNode, "fire", "ready");
        }
        // The muzzle decides where the bolt appears and, in KotOR, where it
        // goes. The gun banks are not aligned with the hull they are mounted
        // on: on the K1 turret each bullethook0 sits five degrees below the
        // turret body and toed half a degree inboard, so the pair converges
        // down range. TSL fires along the player's first model instead.
        const glm::mat4 &heading = _game.isTSL() ? _turretRoot->absoluteTransform() : muzzle;
        spawnBullet(bank, transformOrigin(muzzle), transformOrientation(heading), *_player);
        fired = true;
    }
    if (fired && _turretRoot) {
        _turretRoot->playAnimation("fire");
    }
}

// An enemy's gun turns toward the player's aim point, as far as the bank's
// horizontal and vertical spreads allow either way, while the point lies
// within the bank's sensing radius of the gun's mount. A gun that reaches the
// point fires once its time between shots has passed, at the point scattered
// sideways by the bank's inaccuracy, as a share of the player's sphere. A
// negative spread leaves that axis free.
void Turret::updateEnemyFire(Follower &enemy, float dt) {
    if (enemy.models.empty()) {
        return;
    }
    auto &root = *enemy.models.front();
    const bool targetable = _player && !_player->models.empty();
    glm::vec3 aimPoint(0.0f);
    if (targetable) {
        const glm::mat4 &target = _player->models.front()->absoluteTransform();
        aimPoint = transformOrigin(target) + transformOrientation(target) * _spec.player.targetOffset;
    }
    for (auto &bank : enemy.gunBanks) {
        bank.timer.update(dt);
        glm::mat4 mount = root.absoluteTransform();
        if (auto hook = root.getNodeByName(gunBankHookName(bank.spec.bankId))) {
            mount = hook->absoluteTransform();
        }
        if (!targetable) {
            continue;
        }
        const glm::vec3 mountOrigin = transformOrigin(mount);
        const glm::quat mountOrientation = transformOrientation(mount);
        const glm::vec3 local = glm::inverse(mountOrientation) * (aimPoint - mountOrigin);
        if (glm::length2(local) > bank.spec.sensingRadius * bank.spec.sensingRadius) {
            continue;
        }
        float yaw = turretYawDegrees(local);
        float pitch = turretPitchDegrees(local);
        bool inCone = true;
        if (bank.spec.horizSpread >= 0.0f && glm::abs(yaw) > bank.spec.horizSpread) {
            yaw = glm::sign(yaw) * bank.spec.horizSpread;
            inCone = false;
        }
        if (bank.spec.vertSpread >= 0.0f && glm::abs(pitch) > bank.spec.vertSpread) {
            pitch = glm::sign(pitch) * bank.spec.vertSpread;
            inCone = false;
        }
        if (bank.modelNode) {
            bank.modelNode->setLocalTransform(glm::mat4_cast(turretAimOrientation(yaw, pitch)));
        }
        if (!inCone || !bank.timer.tryFire()) {
            continue;
        }
        glm::vec3 muzzle = mountOrigin;
        if (bank.modelNode) {
            if (auto hook = bank.modelNode->getNodeByName(kBulletHookName)) {
                muzzle = transformOrigin(hook->absoluteTransform());
            }
            playThenReady(bank.modelNode, "fire", "ready");
        }
        glm::vec3 shotPoint = aimPoint;
        const float scatter = bank.spec.inaccuracy * _player->sphereRadius;
        if (scatter > 0.0f) {
            glm::vec3 spread(randomFloat(-1.0f, 1.0f), randomFloat(-1.0f, 1.0f), randomFloat(-1.0f, 1.0f));
            if (glm::length2(spread) > 0.0f) {
                spread = glm::normalize(spread) * scatter;
                const glm::vec3 line = aimPoint - muzzle;
                if (glm::length2(line) > 0.0f) {
                    const glm::vec3 direction = glm::normalize(line);
                    spread -= direction * glm::dot(direction, spread);
                }
                shotPoint += spread;
            }
        }
        const glm::vec3 toShot = shotPoint - muzzle;
        const glm::quat heading = glm::length2(toShot) > 0.0f
                                      ? turretAimOrientation(turretYawDegrees(toShot), turretPitchDegrees(toShot))
                                      : mountOrientation;
        spawnBullet(bank, muzzle, heading, enemy);
    }
}

// A shot plays the bank's fire sound and runs the shooter's fire script, which
// finds the shot as the last bullet fired. In TSL the player's bullets also
// carry the player's speed.
void Turret::spawnBullet(const GunBank &bank,
                         const glm::vec3 &origin,
                         const glm::quat &orientation,
                         Follower &shooter) {
    const auto &spec = bank.spec;
    Bullet bullet;
    bullet.sim.position = origin;
    bullet.sim.orientation = orientation;
    bullet.sim.direction = turretFireDirection(orientation);
    bullet.sim.speed = spec.bullet.speed;
    if (shooter.player && _game.isTSL()) {
        bullet.sim.speed += _speed;
    }
    bullet.sim.lifespan = spec.bullet.lifespan;
    bullet.sim.damage = spec.bullet.damage;
    bullet.sim.fromPlayer = shooter.player;
    bullet.modelResRef = spec.bullet.modelResRef;
    bullet.info = {static_cast<int>(spec.bullet.damage), spec.bullet.targetType, spec.bullet.collisionSound};
    bullet.shooter = shooter.id();

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
    shooter.lastBulletFired = bullet.info;
    _bullets.push_back(std::move(bullet));
    if (!spec.fireSound.empty()) {
        playSound(spec.fireSound, shooter.player ? std::nullopt : std::optional<glm::vec3>(shooter.position));
    }
    runScript(shooter.scripts.onFire, shooter);
}

std::shared_ptr<ModelSceneNode> Turret::acquireBulletNode(const std::string &resRef) {
    auto pooled = _bulletNodePool.find(resRef);
    if (pooled != _bulletNodePool.end() && !pooled->second.empty()) {
        auto node = std::move(pooled->second.back());
        pooled->second.pop_back();
        playIdleAnimation(*node);
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
        ++it;
    }
}

// A bullet hits what it crossed on its last step: in KotOR a model's
// "hitbullet" part, which a model without one does not have, and in TSL the
// whole model. A follower is hit on any of its models, the nearest hit taken.
// No follower is hit by its own bullets.
std::optional<Turret::BulletHit> Turret::bulletHits(const Bullet &bullet, const Actor &target) const {
    if (target.id() == bullet.shooter) {
        return std::nullopt;
    }
    std::optional<std::pair<float, BulletHit>> nearest;
    for (const auto &model : target.models) {
        if (_game.isTSL()) {
            intersectMeshes(*model, bullet.sim.previousPosition, bullet.sim.position, nearest);
        } else if (auto part = model->getNodeByName("hitbullet")) {
            intersectMeshes(*part, bullet.sim.previousPosition, bullet.sim.position, nearest);
        }
    }
    if (!nearest) {
        return std::nullopt;
    }
    return nearest->second;
}

// Each bullet, last to first, is checked against the player, then the listed
// enemies and obstacles, each last to first; the first it hits takes it. The
// target keeps the hit, then its bullet hit hook runs, and the bullet explodes
// where it hit. A bullet that hits an obstacle in TSL takes the obstacle away.
void Turret::checkBulletHits() {
    for (size_t i = _bullets.size(); i-- > 0;) {
        if (i >= _bullets.size()) {
            continue;
        }
        const Bullet bullet = _bullets[i];
        Actor *target = nullptr;
        Obstacle *hitObstacle = nullptr;
        std::optional<BulletHit> hit;
        if (_player) {
            hit = bulletHits(bullet, *_player);
            if (hit) {
                target = _player.get();
            }
        }
        if (!target) {
            auto enemies = listedEnemies();
            for (auto it = enemies.rbegin(); it != enemies.rend(); ++it) {
                if ((hit = bulletHits(bullet, **it))) {
                    target = *it;
                    break;
                }
            }
        }
        if (!target) {
            auto obstacles = listedObstacles();
            for (auto it = obstacles.rbegin(); it != obstacles.rend(); ++it) {
                if ((hit = bulletHits(bullet, **it))) {
                    target = hitObstacle = *it;
                    break;
                }
            }
        }
        if (!target) {
            if (hitAreaObjects(bullet)) {
                releaseBulletNode(_bullets[i]);
                _bullets.erase(_bullets.begin() + static_cast<std::ptrdiff_t>(i));
            }
            continue;
        }
        Bullet spent = std::move(_bullets[i]);
        _bullets.erase(_bullets.begin() + static_cast<std::ptrdiff_t>(i));

        target->lastBulletHit = bullet.info;
        target->lastBulletShooter = bullet.shooter;
        target->lastBulletHitPart = hit->part;
        target->lastBulletHitPosition = hit->position;
        onBulletHit(*target, false);
        explodeBullet(spent, *hit);
        if (hitObstacle && _game.isTSL()) {
            destroyObstacle(*hitObstacle);
        }
        if (!_active) {
            return;
        }
    }
}

// In TSL's Peragus turret, a bullet that hits nothing of the minigame stops
// where it crosses a wall of the area, and hits the first of the area's
// creatures outside the party and its placeables whose bounds it crosses,
// which is signalled a user-defined event numbered by the bullet's damage.
bool Turret::hitAreaObjects(const Bullet &bullet) {
    if (!_game.isTSL() || !boost::iequals(_areaName, "107per")) {
        return false;
    }
    const glm::vec3 from = bullet.sim.previousPosition;
    const glm::vec3 to = bullet.sim.position;
    const float length = glm::length(to - from);
    if (length <= 0.0f) {
        return false;
    }
    Collision wall;
    if (_services.scene.graphs.get(kSceneMain).testLineOfSight(from, to, wall)) {
        return true;
    }
    auto module = _game.module();
    auto area = module ? module->area() : nullptr;
    if (!area) {
        return false;
    }
    const glm::vec3 direction = (to - from) / length;
    for (const auto &object : area->objects()) {
        if (!object) {
            continue;
        }
        const bool creature = object->type() == ObjectType::Creature;
        if (!creature && object->type() != ObjectType::Placeable) {
            continue;
        }
        if (creature && _game.party().isMember(*object)) {
            continue;
        }
        auto model = std::dynamic_pointer_cast<ModelSceneNode>(object->sceneNode());
        if (!model) {
            continue;
        }
        const auto bounds = model->model().aabb() * model->absoluteTransform();
        float distance = 0.0f;
        if (!bounds.raycast(from, 1.0f / direction, length, distance)) {
            continue;
        }
        auto event = _game.newEvent(static_cast<int>(SavedSignalEventType::UserDefined),
                                    std::vector<int32_t> {bullet.info.damage}, std::vector<float> {},
                                    std::vector<std::string> {}, std::vector<std::shared_ptr<Object>> {});
        _game.queueScriptEvent(*object, *object, *event);
        return true;
    }
    return false;
}

// The bolt plays its explosion where it hit, turned to the surface, and goes
// once the explosion has played. TSL's Onderon turret and its test module
// only take the bolt away.
void Turret::explodeBullet(Bullet &bullet, const BulletHit &hit) {
    const bool quiet = _game.isTSL() && (boost::iequals(_areaName, "505ond") || boost::iequals(_areaName, "module000"));
    if (!bullet.modelNode || quiet || !bullet.modelNode->model().getAnimation("explode")) {
        releaseBulletNode(bullet);
        return;
    }
    auto node = std::move(bullet.modelNode);
    const glm::quat facing = turretAimOrientation(turretYawDegrees(hit.normal), turretPitchDegrees(hit.normal));
    node->setLocalTransform(glm::translate(hit.position) * glm::mat4_cast(facing));
    silenceEmitters(*node, false);
    node->playAnimation("explode", nullptr, AnimationProperties::fromFlags(0));
    _explosions.push_back({std::move(node), bullet.modelResRef});
}

void Turret::updateExplosions() {
    for (auto it = _explosions.begin(); it != _explosions.end();) {
        auto base = it->modelNode->baseAnimationChannel();
        if (base && boost::iequals(base->get().anim->name(), "explode") && !base->get().finished) {
            ++it;
            continue;
        }
        silenceEmitters(*it->modelNode);
        _services.scene.graphs.get(kSceneMain).removeRoot(*it->modelNode);
        _bulletNodePool[it->modelResRef].push_back(std::move(it->modelNode));
        it = _explosions.erase(it);
    }
}

void Turret::onBulletHit(Actor &actor, bool apply) {
    auto follower = dynamic_cast<Follower *>(&actor);
    const auto &script = follower ? follower->scripts.onHitBullet : static_cast<Obstacle &>(actor).onHitBullet;
    if (!apply && !script.empty()) {
        runScript(script, actor);
        return;
    }
    // The built-in handling damages a follower the bullet was aimed at, then
    // sounds the bullet's collision where it hit.
    const auto hit = actor.lastBulletHit;
    const auto position = actor.lastBulletHitPosition;
    if (follower && (follower->targetType() & hit.targetType) != 0) {
        adjustHitPoints(*follower, -hit.damage);
    }
    if (!hit.collisionSound.empty()) {
        playSound(hit.collisionSound, position);
    }
}

void Turret::adjustHitPoints(Follower &follower, int amount) {
    follower.hitPointAdjust = amount;
    onDamage(follower, false);
}

// The built-in handling applies the last change. A heal stops at the maximum;
// damage that leaves none kills, and any other plays the damage animation.
void Turret::onDamage(Follower &follower, bool apply) {
    if (!apply && !follower.scripts.onDamage.empty()) {
        runScript(follower.scripts.onDamage, follower);
        return;
    }
    follower.hitPoints += follower.hitPointAdjust;
    if (follower.hitPointAdjust >= 0) {
        follower.hitPoints = glm::min(follower.hitPoints, follower.maxHitPoints);
        return;
    }
    if (follower.hitPoints <= 0) {
        die(follower);
        return;
    }
    for (auto &model : follower.models) {
        playThenReady(model, "damage", "ready_01");
    }
}

// A follower dies once.
void Turret::die(Follower &follower) {
    if (follower.dead) {
        return;
    }
    follower.dead = true;
    onDeath(follower, false);
}

// The built-in death stops the follower and its guns, sounds its death, plays
// each model's "die" animation and takes an enemy out of the enemy list. The
// follower goes once the animation has played.
void Turret::onDeath(Follower &follower, bool apply) {
    if (!apply && !follower.scripts.onDeath.empty()) {
        runScript(follower.scripts.onDeath, follower);
        return;
    }
    if (follower.dying || follower.models.empty()) {
        return;
    }
    follower.dying = true;
    if (follower.player) {
        _trackPlaying = false;
        _firing = false;
        if (follower.trackNode && !follower.trackAnimation.empty()) {
            follower.trackNode->setAnimationSpeed(follower.trackAnimation, 0.0f);
        }
    } else {
        // Particles are children of the emitter that made them, so a wreck
        // still riding its rail carries its whole explosion along with it. Take
        // the fighter off the rail first, keeping it where it died, and the
        // blast stays at the kill instead of sailing across the sky.
        detachFromTrack(follower);
        follower.listed = false;
    }
    if (!follower.deathSound.empty()) {
        playSound(follower.deathSound);
    }
    auto &root = *follower.models.front();
    if (auto dying = root.model().getAnimation("die")) {
        follower.deathDuration = dying->length();
    }
    // The engine glow is not part of the death presentation: it burns steadily
    // for as long as its node lives, so a wreck that kept it would trail a lit
    // flare through the whole animation.
    turretDisableReferenceAttachments(root);
    for (auto &model : follower.models) {
        model->playAnimation("die");
    }
    for (auto &bank : follower.gunBanks) {
        if (bank.modelNode) {
            bank.modelNode->playAnimation("die");
        }
    }
}

void Turret::updateDeath(Follower &follower, float dt) {
    if (!follower.dying || follower.released) {
        return;
    }
    follower.deathElapsed += dt;
    if (turretDeathEffectComplete(follower.deathElapsed, follower.deathDuration)) {
        releaseFollowerNodes(follower);
    }
}

// Without bumping or obstacle contact here, nothing raises this hook; a script
// asking for the built-in handling wrecks the follower.
void Turret::onObstacleHit(Follower &follower, bool apply) {
    if (!apply && !follower.scripts.onHitObstacle.empty()) {
        runScript(follower.scripts.onHitObstacle, follower);
        return;
    }
    if (_game.isTSL() && _spec.type == MinigameType::SwoopRace) {
        return;
    }
    die(follower);
}

void Turret::removeObstacle(Obstacle &obstacle) {
    obstacle.listed = false;
    for (auto &model : obstacle.models) {
        _services.scene.graphs.get(kSceneMain).removeRoot(*model);
    }
}

void Turret::destroyObstacle(Obstacle &obstacle) {
    removeObstacle(obstacle);
    _game.destroyRuntimeObjectGraph(obstacle.object);
    auto it = std::find_if(_obstacles.begin(), _obstacles.end(), [&obstacle](const auto &other) {
        return other.get() == &obstacle;
    });
    if (it != _obstacles.end()) {
        _obstacles.erase(it);
    }
}

// A follower plays an animation on each of its models, an obstacle on its
// model. No name would stop every animation, which is not supported. An
// overlay goes over whatever plays and stays over the animations played after
// it, as the radar's lost contacts stay over the health gauge.
void Turret::playAnimation(Actor &actor, const std::string &name, bool looping, bool overlay) {
    if (name.empty()) {
        return;
    }
    int flags = 0;
    if (looping) {
        flags |= AnimationFlags::loop;
    }
    if (overlay) {
        flags |= AnimationFlags::layer;
    }
    for (auto &model : actor.models) {
        model->playAnimation(name, nullptr, AnimationProperties::fromFlags(flags));
    }
}

void Turret::removeAnimation(Actor &actor, const std::string &name) {
    for (auto &model : actor.models) {
        model->removeAnimation(name);
    }
}

Turret::GunBank *Turret::Follower::gunBank(int bankId) {
    for (auto it = gunBanks.rbegin(); it != gunBanks.rend(); ++it) {
        if (static_cast<int>(it->spec.bankId) == bankId) {
            return &*it;
        }
    }
    return nullptr;
}

int Turret::Follower::gunBankCount() const {
    int count = 0;
    for (const auto &bank : gunBanks) {
        count = glm::max(count, static_cast<int>(bank.spec.bankId) + 1);
    }
    return count;
}

void Turret::setGunModel(Follower &follower, GunBank &gunBank, const std::string &resRef) {
    gunBank.spec.gunModelResRef = resRef;
    if (follower.models.empty()) {
        return;
    }
    auto model = loadModel(resRef);
    if (!model) {
        return;
    }
    auto &mount = *follower.models.front();
    const auto hook = gunBankHookName(gunBank.spec.bankId);
    clearHook(follower.models.front(), hook);
    mount.attach(hook, *model);
    gunBank.modelNode = std::move(model);
}

// The scene raises animation events while it steps the animations, where a
// script playing an animation on the same model would change the animations
// being stepped. They wait for the turret's next update and run in the order
// they came, each on the actor it came from if that is still there.
void Turret::queueAnimationEvent(const Actor &actor, const std::string &modelName, const std::string &event) {
    _pendingAnimationEvents.push_back({actor.id(), modelName, event});
}

void Turret::runAnimationEvents() {
    auto events = std::move(_pendingAnimationEvents);
    _pendingAnimationEvents.clear();
    for (const auto &pending : events) {
        if (auto target = actor(pending.actorId)) {
            onAnimationEvent(*target, pending.modelName, pending.event);
        }
    }
}

// A custom event keeps its tag and the model that raised it, then runs the
// animation event script; a loop event on a track runs the track loop script.
void Turret::onAnimationEvent(Actor &actor, const std::string &modelName, const std::string &event) {
    static const std::string kCustomPrefix("custom_");
    if (boost::istarts_with(event, kCustomPrefix)) {
        actor.lastEvent = event.substr(kCustomPrefix.size());
        actor.lastEventModel = modelName;
        if (auto follower = dynamic_cast<Follower *>(&actor)) {
            runScript(follower->scripts.onAnimEvent, actor);
        }
        return;
    }
    if (boost::istarts_with(event, "loop")) {
        if (auto follower = dynamic_cast<Follower *>(&actor)) {
            // A counted track goes round one time fewer, and on its last time
            // round plays once and stops.
            if (follower->numLoops != -1) {
                setNumLoops(*follower, follower->numLoops - 1);
                if (follower->numLoops == 1 && !follower->dying) {
                    go(*follower);
                    follower->lastTrackTime.reset();
                }
            }
            runScript(follower->scripts.onTrackLoop, actor);
        }
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

// A hit and a shot play once with the model's ready loop queued behind them:
// once the animation has played, the model goes back to that loop. Played
// again before then, it starts over. Another animation played over it in the
// meantime drops the return.
void Turret::playThenReady(const std::shared_ptr<ModelSceneNode> &model,
                           const std::string &animation,
                           const std::string &ready) {
    if (!model || !model->model().getAnimation(animation)) {
        return;
    }
    auto base = model->baseAnimationChannel();
    if (base && boost::iequals(base->get().anim->name(), animation)) {
        model->restartAnimation(animation);
    } else {
        model->playAnimation(animation);
    }
    _readyReturns.erase(std::remove_if(_readyReturns.begin(), _readyReturns.end(), [&model](const auto &pending) {
                            return pending.model.lock() == model;
                        }),
                        _readyReturns.end());
    _readyReturns.push_back({model, animation, ready});
}

void Turret::updateReadyReturns() {
    for (auto it = _readyReturns.begin(); it != _readyReturns.end();) {
        auto model = it->model.lock();
        auto base = model ? model->baseAnimationChannel() : std::nullopt;
        if (!base || !boost::iequals(base->get().anim->name(), it->animation)) {
            it = _readyReturns.erase(it);
            continue;
        }
        if (!base->get().finished) {
            ++it;
            continue;
        }
        model->playAnimation(it->ready, nullptr, AnimationProperties::fromFlags(AnimationFlags::loop));
        it = _readyReturns.erase(it);
    }
}

void Turret::playSound(const std::string &resRef, std::optional<glm::vec3> position) {
    auto clip = _services.resource.audioClips.get(resRef);
    if (!clip) {
        return;
    }
    _services.audio.mixer.play(std::move(clip), AudioType::Sound, 1.0f, false, position);
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
        // Fire input is dropped while paused.
        if (!_game.isPaused()) {
            _firing = true;
        }
        return true;
    case input::KeyCode::Escape:
    case input::KeyCode::Pause:
        // Escape pauses the minigame; it does not leave it.
        if (!event.repeat) {
            _game.pressPauseKey();
        }
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
    // The mouse does not aim while paused.
    if (_game.isPaused()) {
        return true;
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
    // Fire input is dropped while paused; a release still stops firing.
    if (event.pressed && _game.isPaused()) {
        return true;
    }
    _firing = event.pressed;
    return true;
}

} // namespace game

} // namespace reone
