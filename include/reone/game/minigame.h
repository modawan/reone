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

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "reone/graphics/keyframetrack.h"

namespace reone {

namespace graphics {

class Model;

}

namespace resource {

namespace generated {

struct ARE;

}

} // namespace resource

namespace game {

enum class MinigameType {
    None = 0,
    SwoopRace = 1,
    Turret = 2,
    Unknown
};

inline MinigameType minigameTypeFromUint(uint32_t value) {
    switch (value) {
    case 0:
        return MinigameType::None;
    case 1:
        return MinigameType::SwoopRace;
    case 2:
        return MinigameType::Turret;
    default:
        return MinigameType::Unknown;
    }
}

inline const char *minigameTypeName(MinigameType t) {
    switch (t) {
    case MinigameType::None:
        return "none";
    case MinigameType::SwoopRace:
        return "swooprace";
    case MinigameType::Turret:
        return "turret";
    default:
        return "unknown";
    }
}

struct MinigameModelSpec {
    std::string resRef;

    // Authored "RotatingModel": models flagged here follow the player/enemy
    // rotation; the rest stay fixed in the track frame. On the K1 turret the
    // gun and HUD models rotate while the Ebon Hawk hull does not.
    bool rotating {false};
};

struct MinigameBulletSpec {
    std::string modelResRef;
    std::string collisionSound;
    uint32_t damage {0};
    float lifespan {0.0f};    // seconds before the bullet is culled
    float rateOfFire {0.0f};  // seconds between shots from the owning bank
    float speed {0.0f};       // world units per second
    uint32_t targetType {0};  // 1 = player, 2 = enemies
};

struct MinigameGunBankSpec {
    uint32_t bankId {0};
    std::string gunModelResRef;
    std::string fireSound;
    MinigameBulletSpec bullet;

    // Enemy-only aiming parameters; zero on player banks.
    float horizSpread {0.0f};
    float vertSpread {0.0f};
    float inaccuracy {0.0f};
    float sensingRadius {0.0f};
};

struct MinigameSoundSpec {
    std::string death;
    std::string engine;
};

/**
 * Script slots of a player or enemy (a "follower"). Each holds a script
 * resref; empty means the slot is unused. OnAccelerate and OnBrake are the
 * accelerate and brake (jump) keys of the K2 swoop races.
 */
struct MinigameFollowerScriptSpec {
    std::string onCreate;
    std::string onHitBullet;
    std::string onHitFollower;
    std::string onAnimEvent;
    std::string onHeartbeat;
    std::string onDamage;
    std::string onDeath;
    std::string onFire;
    std::string onHitObstacle;
    std::string onTrackLoop;
    std::string onAccelerate;
    std::string onBrake;
    std::string onHitWorld;
};

// Script slots of an obstacle: a subset of the follower slots.
struct MinigameObstacleScriptSpec {
    std::string onCreate;
    std::string onHitBullet;
    std::string onHitFollower;
    std::string onAnimEvent;
    std::string onHeartbeat;
};

/**
 * Script slots of a minigame object, in the order of the follower script
 * struct. Obstacles only use the first five.
 */
enum class MinigameSlot {
    OnCreate = 0,
    OnHitBullet,
    OnHitFollower,
    OnAnimEvent,
    OnHeartbeat,
    OnDamage,
    OnDeath,
    OnFire,
    OnHitObstacle,
    OnTrackLoop,
    OnAccelerate,
    OnBrake,
    OnHitWorld,
    Count
};

constexpr size_t kMinigameSlotCount = static_cast<size_t>(MinigameSlot::Count);

/** One script resref per slot, indexed by MinigameSlot; empty for an unused slot. */
using MinigameScripts = std::array<std::string, kMinigameSlotCount>;

const char *minigameSlotName(MinigameSlot slot);

MinigameScripts minigameScripts(const MinigameFollowerScriptSpec &spec);
MinigameScripts minigameScripts(const MinigameObstacleScriptSpec &spec);

/**
 * Player acceleration in speed units per second, from the authored speed
 * range and Accel_Secs (the seconds to go from minimum to maximum speed).
 * Accel_Secs 0 gives the whole range in one second; a negative value gives 1.
 */
float minigameAccelerationPerSecond(float minimumSpeed, float maximumSpeed, float accelSecs);

struct MinigamePlayerSpec {
    std::string cameraResRef;
    std::string trackResRef;
    std::vector<MinigameModelSpec> models;
    std::vector<MinigameGunBankSpec> gunBanks;
    float minimumSpeed {0.0f};
    float maximumSpeed {0.0f};
    float accelSecs {0.0f};

    // Derived from the speed range and accelSecs at parse time, see
    // minigameAccelerationPerSecond.
    float accelPerSecond {0.0f};

    float sphereRadius {0.0f};
    uint32_t hitPoints {0};
    uint32_t maxHitPoints {0};
    float invincePeriod {0.0f};
    int bumpDamage {0};
    int numLoops {0};
    bool cameraRotate {false};
    MinigameSoundSpec sounds;
    MinigameFollowerScriptSpec scripts;

    // Placement of the player actor relative to its track hook, and of the
    // aim/target point relative to the actor.
    glm::vec3 startOffset {0.0f};
    glm::vec3 targetOffset {0.0f};

    /**
     * Tunnel bounds are angular limits in degrees. Swoop races use them for
     * lean and rotation; turrets use X for pitch and Z for yaw.
     */
    float tunnelXPos {0.0f};
    float tunnelXNeg {0.0f};
    float tunnelYPos {0.0f};
    float tunnelYNeg {0.0f};
    float tunnelZPos {0.0f};
    float tunnelZNeg {0.0f};

    // Per-axis "unbounded" flags. A non-zero component means the matching
    // tunnel limits do not apply (the K1 turret sets Z, giving free 360 yaw).
    glm::vec3 tunnelInfinite {0.0f};
};

struct MinigameEnemySpec {
    std::string trackResRef;
    std::vector<MinigameModelSpec> models;
    std::vector<MinigameGunBankSpec> gunBanks;
    uint32_t hitPoints {0};
    uint32_t maxHitPoints {0};
    float sphereRadius {0.0f};
    float invincePeriod {0.0f};
    int bumpDamage {0};
    int numLoops {0};
    bool trigger {false};
    MinigameSoundSpec sounds;
    MinigameFollowerScriptSpec scripts;

    // Kept for call sites that only need the creation hook.
    std::string onCreate;
};

struct MinigameObstacleSpec {
    std::string name;
    MinigameObstacleScriptSpec scripts;
};

/**
 * Minigame mouse-axis bindings: the axis of the aim each mouse axis turns, 1
 * the pitch, 2 the roll and 3 the yaw, or 0 for none, and whether it turns it
 * the other way.
 */
struct MinigameMouseSpec {
    uint32_t axisX {0};
    uint32_t axisY {0};
    bool flipAxisX {false};
    bool flipAxisY {false};
};

// Passive data parsed from the .are MiniGame struct. Stored on Area.
struct MinigameSpec {
    MinigameType type {MinigameType::None};
    float cameraViewAngle {0.0f};
    float nearClip {0.0f};
    float farClip {0.0f};
    float lateralAccel {0.0f};
    float movementPerSec {0.0f};
    bool useInertia {false};
    uint32_t bumpPlane {0};
    uint32_t depthOfField {0};
    bool doBumping {false};
    std::string music; // resref of the music played during the minigame
    MinigameMouseSpec mouse;
    MinigamePlayerSpec player;
    std::vector<MinigameEnemySpec> enemies;
    std::vector<MinigameObstacleSpec> obstacles;

    // Unique track resrefs referenced by player and enemies.
    // Derived at parse time; not stored separately in the .are format.
    std::vector<std::string> trackResRefs;
};

/**
 * Convert the MiniGame struct of a parsed .are into engine-side metadata.
 *
 * Returns nullopt when the area declares no minigame (Type 0).
 */
std::optional<MinigameSpec> parseMinigameSpec(const resource::generated::ARE &are);

/**
 * The path of a minigame track: where the track model's "modelhook" node is
 * at each time of the model's "track" animation. Actors ride the hook.
 *
 * Position keys are offsets from the hook's rest position and orientation
 * keys replace its rest orientation, as when the animation plays on the
 * model. Times before the first key give the first key, and times past the
 * last key give the last key.
 */
class MinigameTrack {
public:
    MinigameTrack() = default;

    /**
     * @param base world transform of the hook's parent: the track placement
     *             times the rest pose of the hook's ancestors
     * @param restPosition rest position of the hook in its parent
     * @param restOrientation rest orientation of the hook in its parent
     * @param positions position keys of the hook in the "track" animation
     * @param orientations orientation keys of the hook in the "track" animation
     * @param length length of the "track" animation in seconds
     */
    MinigameTrack(glm::mat4 base,
                  glm::vec3 restPosition,
                  glm::quat restOrientation,
                  graphics::KeyframeTrack<glm::vec3> positions,
                  graphics::KeyframeTrack<glm::quat> orientations,
                  float length);

    /** A track of length 0 whose hook stays at a world position. */
    static MinigameTrack fixed(const glm::vec3 &position);

    /** World transform of the hook at a time of the track animation. */
    glm::mat4 hookAt(float time) const;

    float length() const { return _length; }

private:
    glm::mat4 _base {1.0f};
    glm::vec3 _restPosition {0.0f};
    glm::quat _restOrientation {1.0f, 0.0f, 0.0f, 0.0f};
    graphics::KeyframeTrack<glm::vec3> _positions;
    graphics::KeyframeTrack<glm::quat> _orientations;
    float _length {0.0f};
};

/**
 * Build the path of the "modelhook" node of a track model placed at
 * \p placement (the LYT track position). A model without a "track" animation
 * gives a track of length 0, whose hook stays at its rest pose.
 *
 * Returns nullopt when the model has no "modelhook" node.
 */
std::optional<MinigameTrack> buildMinigameTrack(const graphics::Model &model, const glm::vec3 &placement);

/**
 * A minigame track plays at speed / 100: at speed 100 an actor rides the
 * hook at the rate the animation was authored.
 */
constexpr float kMinigameTrackRateDivisor = 100.0f;

// The pull on a jumping swoop bike, units per second squared. Only the TSL
// races jump. A jump at the speed the TSL jump script gives (30) is about
// 1.3 s in the air.
constexpr float kMinigameGravity = 45.8f;

/**
 * The share of a curve's centripetal acceleration (forward speed times turn
 * rate) that pushes a swoop bike to the outside of the curve when the area
 * sets UseInertia: 1 is all of it, as if the bike kept its momentum.
 */
constexpr float kMinigameCurveDrift = 1.0f;

/**
 * The outward acceleration on a swoop bike, along the hook's X axis, as its
 * track hook moves from \p previousHook to \p hook in \p dt seconds (see
 * kMinigameCurveDrift). The hook's X axis is the steering axis and Z is up;
 * a left turn pushes toward +X.
 */
float minigameCurveDrift(const glm::mat4 &previousHook, const glm::mat4 &hook, float dt);

/**
 * Player intent for a minigame. The keyboard and mouse handlers of the view
 * write it; the session reads it once per update.
 */
struct MinigameInput {
    // Lateral steering (swoop), -1 = full left .. +1 = full right.
    float steer {0.0f};

    // Fire held. On the K1 swoop this is the gear-shift key.
    bool fire {false};

    // Accelerate and brake held. The K2 swoop races shift gear on accelerate
    // and jump on brake; each press runs the player's slot once.
    bool accelerate {false};
    bool brake {false};
};

enum class MinigameActorKind {
    Player,
    Enemy,
    Obstacle
};

struct MinigameGunBank {
    MinigameGunBankSpec spec;
    float cooldown {0.0f};
};

/**
 * One minigame object: the player, an enemy (follower) or an obstacle. The
 * state the scripts read and write lives here; objectId is the registered
 * game object the scripts see as OBJECT_SELF.
 */
struct MinigameActor {
    MinigameActorKind kind {MinigameActorKind::Enemy};
    size_t specIndex {0};
    uint32_t objectId {0};
    std::string name;
    std::vector<MinigameModelSpec> models;
    MinigameScripts scripts;
    MinigameSoundSpec sounds;

    int hitPoints {100};
    int maxHitPoints {100};
    float sphereRadius {0.0f};
    float invincePeriod {0.0f};
    float invulnerableTime {0.0f};
    int bumpDamage {0};
    int numLoops {1};
    bool trigger {false};
    float speed {100.0f};

    bool dead {false};
    bool listed {true}; // still in the enemy / obstacle list

    MinigameTrack track;
    float trackTime {0.0f};
    bool trackRunning {true};
    glm::mat4 hook {1.0f};
    glm::vec3 position {0.0f};
    glm::vec3 previousPosition {0.0f};

    uint32_t lastFollowerHit {0};
    uint32_t lastObstacleHit {0};
    int lastHPChange {0};
    int lastBulletHitDamage {0};
    int lastBulletHitTarget {0};
    uint32_t lastBulletHitShooter {0};
    std::string lastBulletHitPart;
    int lastBulletFiredDamage {0};
    int lastBulletFiredTarget {0};
    std::string lastEvent;
    std::string lastEventModelName;

    std::vector<MinigameGunBank> gunBanks;

    // Per-sound playback settings (0 = death, 1 = engine).
    std::array<int, 2> soundFrequency {0, 0};
    std::array<int, 2> soundFrequencyRandom {0, 0};
    std::array<int, 2> soundVolume {127, 127};

    // Obstacle collision geometry (the model's "hitbump"), world space.
    bool hasHitbump {false};
    glm::vec3 hitbumpMin {0.0f};
    glm::vec3 hitbumpMax {0.0f};

    bool follower() const { return kind != MinigameActorKind::Obstacle; }
    bool invulnerable() const { return invulnerableTime > 0.0f; }
};

/**
 * Presentation of a minigame. The session calls it for the animations and
 * sounds the scripts ask for, the default death and gun fire; none of it
 * feeds back into the simulation.
 */
class IMinigameView {
public:
    virtual ~IMinigameView() = default;

    virtual void playMinigameAnimation(const MinigameActor &actor,
                                       const std::string &name,
                                       bool looping,
                                       bool queue,
                                       bool overlay) {}
    virtual void removeMinigameAnimation(const MinigameActor &actor, const std::string &name) {}
    virtual void playMinigameSound(const std::string &resRef, const glm::vec3 &position) {}
    virtual void onMinigameActorDied(const MinigameActor &actor) {}
    virtual void onMinigameGunFired(const MinigameActor &actor, size_t bankIndex) {}
};

/**
 * What the session needs from the engine: script execution, plus the view.
 * The simulation itself has no engine or scene dependency.
 */
class IMinigameHost : public IMinigameView {
public:
    virtual void runScript(const std::string &resRef, uint32_t callerId) = 0;
};

/**
 * The minigame simulation: actors, player speed and steering, tracks,
 * collisions, hit points and the script slots. update() is one step.
 */
class MinigameSession {
public:
    struct ObstaclePlacement {
        std::string name;
        glm::vec3 position {0.0f};
        bool hasHitbump {false};
        glm::vec3 hitbumpMin {0.0f}; // world space
        glm::vec3 hitbumpMax {0.0f};
    };

    explicit MinigameSession(IMinigameHost &host) :
        _host(host) {
    }

    /**
     * Build the actors from the area data. The enemy tracks follow the order
     * of spec.enemies. Object ids are assigned afterwards with setObjectId,
     * then start() runs the creation scripts.
     */
    void load(const MinigameSpec &spec,
              MinigameTrack playerTrack,
              std::vector<MinigameTrack> enemyTracks,
              std::vector<ObstaclePlacement> obstacles);

    void setObjectId(size_t actorIndex, uint32_t id) { _actors[actorIndex].objectId = id; }

    /** Run every OnCreate: the player first, then the enemies, then the obstacles. */
    void start();

    /** One simulation step. */
    void update(float dt, const MinigameInput &input);

    // Events (also used by the SWMG_ routines)

    /** A hit point change: stored as the last change, then OnDamage or the default. */
    void changeHitPoints(MinigameActor &actor, int delta);

    /** SWMG_OnDamage: apply the stored change. */
    void defaultDamage(MinigameActor &actor);

    /** Kill once: OnDeath or the default. */
    void kill(MinigameActor &actor);

    /** SWMG_OnDeath. */
    void defaultDeath(MinigameActor &actor);

    /** A bullet reached \p target: OnHitBullet or the default. */
    void bulletHit(MinigameActor &target, int damage, int targetType, uint32_t shooter, const std::string &part);

    /** SWMG_OnBulletHit. */
    void defaultBulletHit(MinigameActor &target);

    /** SWMG_OnObstacleHit: the default for the follower that hit an obstacle. */
    void defaultObstacleHit(MinigameActor &follower);

    /** SWMG_DestroyMiniGameObject: out of play and out of the lists, without OnDeath. */
    void destroy(MinigameActor &actor);

    /** A gun bank of \p actor fired. */
    void gunFired(MinigameActor &actor, size_t bankIndex);

    void playAnimation(MinigameActor &actor, const std::string &name, bool looping, bool queue, bool overlay);
    void removeAnimation(MinigameActor &actor, const std::string &name);

    // Player motion

    void setPlayerSpeed(float speed);
    void setPlayerMaxSpeed(float speed);
    void setPlayerOffset(const glm::vec3 &offset);
    void setPlayerOrigin(const glm::vec3 &origin);

    // Lookups

    MinigameActor *actorByObjectId(uint32_t id);
    MinigameActor *player();
    const MinigameActor *player() const;
    MinigameActor *enemy(int index);
    MinigameActor *obstacle(int index);
    MinigameActor *findByName(const std::string &name);
    int enemyCount() const { return static_cast<int>(_enemyList.size()); }
    int obstacleCount() const { return static_cast<int>(_obstacleList.size()); }
    std::vector<MinigameActor> &actors() { return _actors; }
    const std::vector<MinigameActor> &actors() const { return _actors; }

    MinigameType type() const { return _type; }
    uint64_t tickCount() const { return _ticks; }
    float elapsed() const { return _elapsed; }

    float minSpeed() const { return _minSpeed; }
    float maxSpeed() const { return _maxSpeed; }
    float accelPerSecond() const { return _accelPerSecond; }
    float playerSpeed() const;
    const glm::vec3 &offset() const { return _offset; }
    const glm::vec3 &origin() const { return _startOffset; }
    const glm::vec3 &targetOffset() const { return _targetOffset; }
    const glm::vec3 &tunnelPos() const { return _tunnelPos; }
    const glm::vec3 &tunnelNeg() const { return _tunnelNeg; }
    const glm::vec3 &tunnelInfinite() const { return _tunnelInfinite; }
    float lateralAccel() const { return _lateralAccel; }
    float movementPerSec() const { return _movementPerSec; }
    const glm::vec3 &lateralVelocity() const { return _lateralVelocity; }
    bool speedBlurEnabled() const { return _speedBlur; }
    float speedBlurRatio() const { return _speedBlurRatio; }
    float nearClip() const { return _nearClip; }
    float farClip() const { return _farClip; }

    void setMinSpeed(float speed) { _minSpeed = speed; }
    void setAccelPerSecond(float accel) { _accelPerSecond = accel; }
    void setTunnelPos(const glm::vec3 &v) { _tunnelPos = v; }
    void setTunnelNeg(const glm::vec3 &v) { _tunnelNeg = v; }
    void setTunnelInfinite(const glm::vec3 &v) { _tunnelInfinite = v; }
    void setLateralAccel(float accel) { _lateralAccel = accel; }

    /** The bike's vertical speed: a jump (TSL), brought back by gravity. */
    void setJumpSpeed(float speed) { _jumpSpeed = speed; }
    float jumpSpeed() const { return _jumpSpeed; }
    void setSpeedBlur(bool enabled, float ratio) {
        _speedBlur = enabled;
        _speedBlurRatio = ratio;
    }
    void setCameraClip(float nearClip, float farClip) {
        _nearClip = nearClip;
        _farClip = farClip;
    }

    /** Slot scripts run so far (diagnostics and tests). */
    size_t scriptsRun() const { return _scriptsRun; }

private:
    IMinigameHost &_host;

    MinigameType _type {MinigameType::None};
    std::vector<MinigameActor> _actors;
    std::vector<size_t> _enemyList;
    std::vector<size_t> _obstacleList;

    float _lateralAccel {0.0f};
    float _movementPerSec {0.0f};
    bool _useInertia {false};
    bool _doBumping {false};
    bool _speedBlur {false};
    float _speedBlurRatio {0.0f};
    float _nearClip {0.0f};
    float _farClip {0.0f};

    float _minSpeed {0.0f};
    float _maxSpeed {0.0f};
    float _accelPerSecond {0.0f};
    glm::vec3 _offset {0.0f};
    glm::vec3 _startOffset {0.0f};
    glm::vec3 _targetOffset {0.0f};
    glm::vec3 _tunnelPos {0.0f};
    glm::vec3 _tunnelNeg {0.0f};
    glm::vec3 _tunnelInfinite {0.0f};
    glm::vec3 _lateralVelocity {0.0f};
    glm::vec3 _previousInput {0.0f};
    float _jumpSpeed {0.0f};
    bool _speedHeld {false};
    bool _accelerateHeld {false};
    bool _brakeHeld {false};

    uint64_t _ticks {0};
    float _elapsed {0.0f};
    size_t _scriptsRun {0};
    bool _started {false};

    bool runScriptSlot(MinigameActor &actor, MinigameSlot slot);
    void clampOffset(glm::vec3 &offset) const;
    void updatePlayerMotion(float dt, const MinigameInput &input);
    void steerPlayer(float dt, const MinigameInput &input, float drift);
    void updateJump(float dt);
    void advanceTrack(MinigameActor &actor, float dt);
    void placeActor(MinigameActor &actor);
    void updateFollower(MinigameActor &actor, float dt);
    void updatePlayerGuns(float dt, bool fire);
    void collideObstacles();
    void collideFollowers();
    void unlist(MinigameActor &actor);
};

/** Distance between two line segments. */
float minigameSegmentDistance(const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec3 &q0, const glm::vec3 &q1);

/**
 * Whether a segment crosses a box, tested in the ground plane (the swoop
 * obstacle geometry is authored flat, level with the course).
 */
bool minigameSegmentHitsBox2D(const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec3 &boxMin, const glm::vec3 &boxMax);

} // namespace game

} // namespace reone
