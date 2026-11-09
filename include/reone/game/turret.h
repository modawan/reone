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
#include <limits>
#include <map>
#include <memory>
#include <optional>

#include "reone/input/event.h"
#include "reone/scene/animeventlistener.h"
#include "reone/script/types.h"

#include "minigame.h"

namespace reone {

namespace audio {

class AudioClip;
class AudioSource;

}

namespace scene {

class ModelSceneNode;

}

namespace game {

class Game;
class FirstPersonCamera;
class MinigameObject;
struct ServicesView;

/**
 * The player turret's aim: an offset in degrees, x the pitch, y the roll and z
 * the yaw. Each axis keeps within its origin plus the tunnel's negative and
 * positive deltas, unless the axis is infinite, when it wraps into [0, 360)
 * instead.
 */
class TurretAim {
public:
    /** Takes the authored origin (Start_Offset) and tunnel, and aims at the origin. */
    void configure(const MinigamePlayerSpec &player);

    glm::vec3 offset() const { return _offset; }
    /** Aims at \p degrees, held inside the tunnel. */
    void setOffset(const glm::vec3 &degrees);
    /** Turns the aim by \p degrees, held inside the tunnel. */
    void turn(const glm::vec3 &degrees);

    const glm::vec3 &origin() const { return _origin; }
    /**
     * Moves the origin; while the turret's track has not started, the aim
     * moves to it as well.
     */
    void setOrigin(const glm::vec3 &degrees, bool trackPlaying);
    const glm::vec3 &tunnelPositive() const { return _tunnelPositive; }
    const glm::vec3 &tunnelNegative() const { return _tunnelNegative; }
    const glm::vec3 &tunnelInfinite() const { return _tunnelInfinite; }
    void setTunnelPositive(const glm::vec3 &degrees) { _tunnelPositive = degrees; }
    void setTunnelNegative(const glm::vec3 &degrees) { _tunnelNegative = degrees; }
    void setTunnelInfinite(const glm::vec3 &axes) { _tunnelInfinite = axes; }

    // Pitch and yaw in radians.
    float pitch() const { return glm::radians(_offset.x); }
    float yaw() const { return glm::radians(_offset.z); }
    void addPitch(float radians) { turn(glm::vec3(glm::degrees(radians), 0.0f, 0.0f)); }
    void addYaw(float radians) { turn(glm::vec3(0.0f, 0.0f, glm::degrees(radians))); }

    /**
     * Unit vector the turret is pointing along, in the track frame. Models
     * look down +Y.
     */
    glm::vec3 forward() const;

    /** Yaw about Z, then pitch about X, then roll about Y. */
    glm::quat orientation() const;

private:
    glm::vec3 _offset {0.0f};
    glm::vec3 _origin {0.0f};
    glm::vec3 _tunnelPositive {0.0f};
    glm::vec3 _tunnelNegative {0.0f};
    glm::vec3 _tunnelInfinite {0.0f};

    void keepInTunnel();
};

/**
 * Turn rate of one aim axis while a key is held, in degrees per second. A held
 * key accelerates the axis towards the top rate, MovementPerSec, at the
 * lateral acceleration, LateralAccel; let go, it slows the same way. The rate
 * is integrated exactly, so the angle covered over an interval does not depend
 * on how many frames it is split into.
 */
class TurretAimRate {
public:
    void configure(float lateralAcceleration, float movementPerSecond);
    void setLateralAcceleration(float lateralAcceleration) { _acceleration = lateralAcceleration; }

    /** -1, 0 or +1. */
    void setDirection(int direction);

    /** Degrees to turn this step. */
    float advance(float dt);

    /** Drop any held direction and the rate it built up. */
    void reset();

    int direction() const { return _direction; }
    float rate() const { return _rate; }

private:
    float _acceleration {0.0f};
    float _topRate {0.0f};
    int _direction {0};
    float _rate {0.0f};
};

/**
 * Rate-of-fire gate for a single gun bank: the bank's time between shots.
 */
class TurretGunTimer {
public:
    explicit TurretGunTimer(float rateOfFire = 0.0f) :
        _rateOfFire(rateOfFire) {
    }

    void update(float dt);

    bool ready() const { return _cooldown <= 0.0f; }

    /**
     * Consume the gate: returns false (and changes nothing) when still cooling
     * down, otherwise arms the cooldown and returns true.
     */
    bool tryFire();

    /**
     * A new time between shots. A shot counting down within the old time is
     * brought forward, or put back, by the difference.
     */
    void setRateOfFire(float rateOfFire);

    float cooldown() const { return _cooldown; }
    float rateOfFire() const { return _rateOfFire; }

private:
    float _rateOfFire {0.0f};
    float _cooldown {0.0f};
};

/**
 * Scene-free state of a bullet in flight.
 */
struct TurretBullet {
    glm::vec3 position {0.0f};
    /** Where the bullet was before its last step: the hit tests sweep from here to position. */
    glm::vec3 previousPosition {0.0f};
    glm::vec3 direction {0.0f, 1.0f, 0.0f};

    // World orientation of the firing actor at the moment of the shot. Authored
    // bolt models are modelled along +Y, so this is what makes the bolt's long
    // axis lie along its travel instead of along world Y.
    glm::quat orientation {1.0f, 0.0f, 0.0f, 0.0f};

    float speed {0.0f};
    float life {0.0f};
    float lifespan {0.0f};
    uint32_t damage {0};
    bool fromPlayer {false};

    // Set on creation, cleared by the first advance(). Firing and bullet
    // integration run in the same tick, so without this a bolt is carried
    // speed*dt down range before it is ever drawn and never appears at the
    // barrel it came from. Holding the first step presents the authored muzzle
    // transform once; flight then proceeds normally.
    bool atMuzzle {true};

    /**
     * Advance the bullet by one step. Returns false once it has outlived its
     * lifespan and should be culled.
     *
     * The first call after spawning consumes the muzzle frame and does not
     * integrate; every later call moves the bullet.
     */
    bool advance(float dt);

    /**
     * Mark the bullet spent so the next cull pass removes it.
     */
    void expire() { life = std::numeric_limits<float>::max(); }
};

/**
 * Whether a destroyed fighter has finished presenting its death effect.
 *
 * The shipped fighter carries its destruction as a non-looping "die" animation
 * (mgf_sithfighter authors 2.3s), so the animation's own length is how long the
 * wreck is meant to stay on screen. A fighter with no authored death animation
 * has nothing to wait for and is released at once.
 */
bool turretDeathEffectComplete(float elapsed, float duration);

/**
 * Switch off the always-on effect models a minigame actor carries.
 *
 * The model loader resolves every authored reference node into a separate
 * model and attaches it there; on mgf_sithfighter those are the fx_ref engine
 * flares hanging at its OmenRef nodes, which burn for as long as the node
 * lives because nothing animates them. A fighter that has just been destroyed
 * should stop showing engine glow at once, while its hull death animation and
 * the emitters that animation detonates play out.
 *
 * Only loader-built reference attachments are touched, so the actor's meshes,
 * lights, emitters and any gun bank models hooked on by the minigame are left
 * running. Identifying them by the authored reference flag keeps this free of
 * per-model node names.
 */
void turretDisableReferenceAttachments(scene::ModelSceneNode &model);

/**
 * How far along its own forward axis a bolt must start so that none of its
 * visible geometry lies behind the muzzle it was fired from.
 *
 * A bolt model's origin is not its tail. The shipped Ebon Hawk bolt
 * (mgb_ebonleft) draws from -1.953 to +8.620 along model +Y, so pinning the
 * origin to the authored bullethook0 buries nearly two units of bolt inside the
 * gun. Starting the bolt ahead by however far it reaches backwards puts its
 * tail on the muzzle plane instead.
 *
 * \param modelMinForward the model-space minimum of the bolt's visible bounds
 *                        along the model forward axis (+Y)
 * \return a non-negative displacement; zero for a bolt drawn wholly ahead of
 *         its origin
 */
float turretMuzzleClearance(float modelMinForward);

/**
 * Why a requested turret session cannot be scheduled.
 */
enum class TurretRequestError {
    None,
    MissingModule,  // no module argument
    UnknownModule,  // not a module of this installation
    SameModule,     // already in the requested module
    NoOrigin,       // nothing loaded to return to
    AlreadyActive   // a session is already scheduled or running
};

/**
 * Validate a console-requested turret session before it is scheduled. The
 * caller supplies what it knows synchronously; whether the target actually
 * declares a turret minigame is only knowable once the module has loaded (see
 * resolveTurretRequest).
 */
TurretRequestError validateTurretRequest(const std::string &target,
                                         const std::string &originModule,
                                         bool moduleKnown,
                                         bool alreadyActive);

const char *turretRequestErrorMessage(TurretRequestError error);

/**
 * What to do with a scheduled turret session once its module has loaded.
 */
enum class TurretRequestResolution {
    NotPending,      // no session was scheduled for the loaded module
    Start,           // the loaded area is a turret minigame
    AbortNoMinigame, // the loaded area declares no minigame at all
    AbortWrongType   // the loaded area declares a different minigame
};

TurretRequestResolution resolveTurretRequest(bool pendingForModule,
                                             bool hasMinigame,
                                             MinigameType type);

const char *turretRequestResolutionMessage(TurretRequestResolution resolution);

/**
 * Direction an actor with this world orientation fires along. Models look
 * down +Y, so both the bolt's travel and its visual long axis come from the
 * same orientation - which is also why every gun bank of one actor fires
 * symmetrically, differing only in muzzle position.
 */
glm::vec3 turretFireDirection(const glm::quat &shooterOrientation);

/**
 * World transform of a bullet in flight: its position, oriented by the actor
 * that fired it. Kept separate from the scene node so it can be checked without
 * a graphics context.
 */
glm::mat4 turretBulletTransform(const TurretBullet &bullet);

bool sphereContainsPoint(const glm::vec3 &center, float radius, const glm::vec3 &point);

/**
 * True when the ray from \p origin along \p direction (assumed normalized)
 * enters the sphere ahead of the origin, within \p maxDistance.
 */
bool rayIntersectsSphere(const glm::vec3 &origin,
                         const glm::vec3 &direction,
                         const glm::vec3 &center,
                         float radius,
                         float maxDistance);

/**
 * The turret minigame (.are MiniGame Type 2).
 *
 * The player is a gun turret riding an authored rail: the player track model
 * animates a "modelhook" node, and everything is parented to it, so the whole
 * scene flies past while the player only aims and shoots. Enemies ride their own
 * tracks the same way. The resource relationships and the aim/fire/bullet model
 * follow the public GPLv3 KotOR.js implementation (commit
 * 9703b1c827b75312811a0fb95322d98902057599; see ModuleMGPlayer, ModuleMGGunBank
 * and ModuleMGGunBullet).
 *
 * Player models are split by the .are "RotatingModel" flag: flagged models (the
 * gun, the HUD panes) follow the aim, the rest (the Ebon Hawk hull) stay fixed
 * in the track frame. Gun banks mount on the "gunbankN" hooks of whichever of
 * the actor's models carries them, and spawn bullets from the gun model's
 * "bullethook0".
 *
 * The module's scripts drive it. The player, each enemy and each obstacle of
 * the area layout is an object its scripts run as and the SWMG routines take.
 * A hook with a script replaces the built-in handling, which the script can
 * still ask for. Nothing here decides a win or a loss: the scripts end the
 * fight.
 */
class Turret {
public:
    /** The fields of a bullet a gun bank fires that a hit reports. */
    struct BulletInfo {
        int damage {0};
        uint32_t targetType {0};
        std::string collisionSound;
    };

    struct GunBank {
        /** The bank as authored, changed by scripts. */
        MinigameGunBankSpec spec;
        std::shared_ptr<scene::ModelSceneNode> modelNode;
        TurretGunTimer timer;
    };

    struct Actor;

    /** Passes one model's animation events to the turret, which runs them in its next update. */
    class ModelEvents : public scene::IAnimationEventListener {
    public:
        ModelEvents(Turret &turret, Actor &actor, std::string modelName) :
            _turret(turret),
            _actor(actor),
            _modelName(std::move(modelName)) {
        }

        void onEventSignalled(const std::string &name) override;

    private:
        Turret &_turret;
        Actor &_actor;
        std::string _modelName;
    };

    /** What the player, an enemy and an obstacle have alike. */
    struct Actor {
        virtual ~Actor() = default;

        std::shared_ptr<MinigameObject> object;
        /** A follower's track model name, an obstacle's model name. */
        std::string name;
        std::vector<std::shared_ptr<scene::ModelSceneNode>> models;
        std::vector<std::unique_ptr<ModelEvents>> modelEvents;

        // The last bullet to hit it.
        BulletInfo lastBulletHit;
        uint32_t lastBulletShooter {script::kObjectInvalid};
        std::string lastBulletHitPart;
        glm::vec3 lastBulletHitPosition {0.0f};
        uint32_t lastFollowerHit {script::kObjectInvalid};

        // The last custom animation event: its tag and the model that raised it.
        std::string lastEvent;
        std::string lastEventModel;

        uint32_t id() const;
    };

    /** The volume, frequency and random frequency of an engine or death sound. */
    struct SoundSettings {
        int volume {0};
        int frequency {0};
        int frequencyIsRandom {0};
    };

    /** The player or an enemy: an actor that rides a track. */
    struct Follower : Actor {
        bool player {false};
        bool trigger {false};
        MinigameFollowerScriptSpec scripts;
        std::shared_ptr<scene::ModelSceneNode> trackNode;
        std::string trackAnimation;
        /** Where the track's animation was at the last update, to tell when it goes round. */
        std::optional<float> lastTrackTime;
        std::vector<GunBank> gunBanks;
        std::string deathSound;
        /** The engine's looping sound, heard where the follower is while its track plays. */
        std::string engineSound;
        std::shared_ptr<audio::AudioSource> engineSource;
        std::array<SoundSettings, 2> sounds;

        int hitPoints {100};
        int maxHitPoints {0};
        float sphereRadius {0.0f};
        int numLoops {1};
        float invincibility {0.0f};
        float invincibilityTimer {0.0f};
        /** The last change made to the hit points, pending while OnDamage runs. */
        int hitPointAdjust {0};
        uint32_t lastObstacleHit {script::kObjectInvalid};
        BulletInfo lastBulletFired;

        bool dead {false};
        /** In the enemy list; the built-in death takes it out. */
        bool listed {true};

        glm::vec3 position {0.0f};
        glm::quat orientation {1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 forward {0.0f, 1.0f, 0.0f};

        // Death effect bookkeeping. The built-in death plays each model's "die"
        // animation for that animation's own length, after which the follower
        // and everything it owns leave the scene.
        bool dying {false};
        float deathElapsed {0.0f};
        float deathDuration {0.0f};
        bool released {false};
        bool detachedFromTrack {false};

        /** The bullet target bit it answers to: 1 the player, 2 an enemy. */
        uint32_t targetType() const { return player ? 1 : 2; }

        /**
         * Scripts number gun banks by bank id. The bank with the id, the last
         * made when several share it, or none.
         */
        GunBank *gunBank(int bankId);
        /** One more than the highest bank id: the ids below it without a bank count too. */
        int gunBankCount() const;
    };

    struct Obstacle : Actor {
        MinigameObstacleScriptSpec scripts;
        bool listed {true};
    };

    /** Where a bullet hit, the surface's normal there and the part it hit. */
    struct BulletHit {
        glm::vec3 position {0.0f};
        glm::vec3 normal {0.0f, 0.0f, 1.0f};
        std::string part;
    };

    struct Bullet {
        TurretBullet sim;
        std::shared_ptr<scene::ModelSceneNode> modelNode;
        std::string modelResRef;
        BulletInfo info;
        uint32_t shooter {script::kObjectInvalid};
    };

    Turret(Game &game, ServicesView &services) :
        _game(game),
        _services(services) {
    }

    /**
     * Load the minigame actors for \p spec, run their creation scripts and
     * activate the turret.
     *
     * @param camera the area first-person camera, reused as the turret camera; may be null
     * @param areaName the area resref, used to look up the LYT track placements
     * @return true when the turret became active
     */
    bool start(const MinigameSpec &spec, FirstPersonCamera *camera, const std::string &areaName);

    /**
     * Deactivate the turret and remove everything it added to the scene.
     */
    void stop();

    void update(float dt);
    bool handle(const input::Event &event);

    bool isActive() const { return _active; }

    // Script objects

    /** The player while it exists, else none. */
    Follower *player() { return _player.get(); }
    /** The follower (player or enemy) an object id names. */
    Follower *follower(uint32_t id);
    Obstacle *obstacle(uint32_t id);
    /** The follower or obstacle an object id names. */
    Actor *actor(uint32_t id);
    /** The enemies still in the enemy list, in authored order. */
    std::vector<Follower *> listedEnemies();
    /** The obstacles still in the obstacle list, in layout order. */
    std::vector<Obstacle *> listedObstacles();
    /** The first object with the name, in the order the objects were made. */
    Actor *actorByName(const std::string &name);

    // Script hooks with their built-in handling. Without \p apply a script on
    // the hook runs in place of the built-in handling; with it, the built-in
    // handling runs.

    /** Records \p amount as the last change to the hit points, then handles the damage. */
    void adjustHitPoints(Follower &follower, int amount);
    void onDamage(Follower &follower, bool apply);
    void onDeath(Follower &follower, bool apply);
    void onBulletHit(Actor &actor, bool apply);
    void onObstacleHit(Follower &follower, bool apply);

    /** Takes an obstacle out of the obstacle list and the scene; its object stays. */
    void removeObstacle(Obstacle &obstacle);
    /** Takes an obstacle away, its object too. */
    void destroyObstacle(Obstacle &obstacle);

    void playAnimation(Actor &actor, const std::string &name, bool looping, bool overlay);
    void removeAnimation(Actor &actor, const std::string &name);

    void setNumLoops(Follower &follower, int loops);

    /** Sets the engine (0) or death (1) sound's volume, out of 127; the engine's takes effect at once. */
    void setSoundVolume(Follower &follower, int sound, int volume);

    /** Puts a new gun model on every gun of the bank. */
    void setGunModel(Follower &follower, GunBank &bank, const std::string &resRef);

    // Player state for scripts

    TurretAim &aim() { return _aim; }
    const TurretAim &aim() const { return _aim; }
    float speed() const { return _speed; }
    /** The player's speed; the track plays at a hundredth of it. */
    void setSpeed(float speed);
    float lateralAcceleration() const { return _lateralAcceleration; }
    void setLateralAcceleration(float acceleration);
    float nearClip() const { return _nearClip; }
    float farClip() const { return _farClip; }
    void setCameraClip(float nearClip, float farClip);
    bool trackPlaying() const { return _trackPlaying; }
    /** Presses the player's trigger once, as the fire button does. */
    void pullTrigger();

    // Diagnostics

    glm::vec3 position() const { return _anchorPosition; }
    float elapsed() const { return _elapsed; }
    size_t bulletCount() const { return _bullets.size(); }
    const std::string &trackResRef() const { return _trackResRef; }
    const std::string &anchorSource() const { return _anchorSource; }

    // Eye offset the camera mount contributes, in the rotating group's frame.
    glm::vec3 cameraHookOffset() const { return glm::vec3(_cameraHookLocal[3]); }
    bool haveCameraHook() const { return _haveCameraHook; }

    // END Diagnostics

private:
    Game &_game;
    ServicesView &_services;

    bool _active {false};

    // Owned copy of the area metadata: the runtime holds pointers into its
    // enemy and gun bank lists, which must outlive the area on a module change.
    MinigameSpec _spec;

    std::string _areaName;
    std::string _trackResRef;
    std::string _anchorSource;

    FirstPersonCamera *_camera {nullptr};

    // The minigame's music, looping while it runs.
    std::shared_ptr<audio::AudioSource> _music;

    std::unique_ptr<Follower> _player;
    std::vector<std::unique_ptr<Follower>> _enemies;
    std::vector<std::unique_ptr<Obstacle>> _obstacles;

    // Root of the player models that stay fixed in the track frame (the hull).
    std::shared_ptr<scene::ModelSceneNode> _bodyRoot;
    // Root of the player models that follow the aim (the gun, the HUD).
    std::shared_ptr<scene::ModelSceneNode> _turretRoot;

    // Static model-space transform of the camera mount's "camerahook" node. The
    // mount model itself is not added to the scene.
    glm::mat4 _cameraHookLocal {1.0f};
    bool _haveCameraHook {false};

    std::vector<Bullet> _bullets;

    // Bullets playing their explosion where they hit.
    struct Explosion {
        std::shared_ptr<scene::ModelSceneNode> modelNode;
        std::string modelResRef;
    };
    std::vector<Explosion> _explosions;

    // An animation event, waiting for the turret's update.
    struct PendingAnimationEvent {
        uint32_t actorId {script::kObjectInvalid};
        std::string modelName;
        std::string event;
    };
    std::vector<PendingAnimationEvent> _pendingAnimationEvents;

    // A model playing an animation once, with its ready loop to go back to.
    struct ReadyReturn {
        std::weak_ptr<scene::ModelSceneNode> model;
        std::string animation;
        std::string ready;
    };
    std::vector<ReadyReturn> _readyReturns;

    // Culled bullet models, kept for reuse: the scene graph owns every node it
    // creates until the graph is cleared, so a node per shot would accumulate
    // for the whole minigame.
    std::map<std::string, std::vector<std::shared_ptr<scene::ModelSceneNode>>> _bulletNodePool;

    // Runtime state

    TurretAim _aim;
    TurretAimRate _pitchRate;
    TurretAimRate _yawRate;
    glm::vec3 _anchorPosition {0.0f};
    glm::mat4 _anchorTransform {1.0f};
    float _speed {100.0f};
    float _lateralAcceleration {0.0f};
    float _nearClip {0.0f};
    float _farClip {0.0f};
    bool _trackPlaying {false};
    float _elapsed {0.0f};

    // Loading

    std::shared_ptr<scene::ModelSceneNode> loadModel(const std::string &resRef);
    std::shared_ptr<scene::ModelSceneNode> loadTrack(const std::string &resRef,
                                                     const glm::vec3 &position,
                                                     std::string &animation);
    bool loadPlayer();
    void loadEnemies();
    void loadObstacles();
    void loadObstacleScripts();
    /** Gives the actor its script object and listens to its models' animation events. */
    void registerActor(Actor &actor);
    void listenTo(Actor &actor, scene::ModelSceneNode &model);
    void loadFollower(Follower &follower,
                      uint32_t hitPoints,
                      uint32_t maxHitPoints,
                      float sphereRadius,
                      int numLoops,
                      float invincibility,
                      const std::string &deathSound,
                      const std::string &engineSound);
    void startEngineSound(Follower &follower);
    void stopEngineSound(Follower &follower);
    void go(Follower &follower);

    /**
     * Retire everything a follower owns: its gun bank models, its models with
     * the reference models, lights and emitters the loader built under them,
     * and its rail. Safe to call twice.
     */
    void releaseFollowerNodes(Follower &follower);

    /**
     * Take a destroyed enemy off its flight rail, keeping the world pose it
     * died in, and re-root it alongside the tracks.
     *
     * Emitter particles are children of the emitter that spawned them, so a
     * wreck still attached to the looping rail hook carries its whole explosion
     * with it. Detaching stops the rail translating the fighter; the authored
     * "die" animation still plays on top. Safe to call twice.
     */
    void detachFromTrack(Follower &follower);
    /** Mounts each bank's gun on the first of \p models that carries the bank's hook. */
    void loadGunBanks(const std::vector<MinigameGunBankSpec> &specs,
                      const std::vector<std::shared_ptr<scene::ModelSceneNode>> &models,
                      std::vector<GunBank> &out);

    // Simulation

    void runScript(const std::string &resRef, const Actor &actor);
    void updateAnchor();
    void updatePlayerTransforms();
    void updateCamera();
    void updateFollower(Follower &follower, float dt);
    void checkTrackLoop(Follower &follower);
    void updateEnemyFire(Follower &enemy, float dt);
    void updateDeath(Follower &follower, float dt);
    void updateBullets(float dt);
    void checkBulletHits();
    std::optional<BulletHit> bulletHits(const Bullet &bullet, const Actor &target) const;
    /** Whether a bullet that hit nothing of the minigame stops on the area's walls or hits an area object. */
    bool hitAreaObjects(const Bullet &bullet);
    /** Plays the bullet's explosion where it hit, or takes it away when it has none. */
    void explodeBullet(Bullet &bullet, const BulletHit &hit);
    void updateExplosions();
    void fire();
    void spawnBullet(const GunBank &bank,
                     const glm::vec3 &origin,
                     const glm::quat &orientation,
                     Follower &shooter);
    void die(Follower &follower);
    void queueAnimationEvent(const Actor &actor, const std::string &modelName, const std::string &event);
    void runAnimationEvents();
    void onAnimationEvent(Actor &actor, const std::string &modelName, const std::string &event);
    void playSound(const std::string &resRef, std::optional<glm::vec3> position = std::nullopt);
    void playThenReady(const std::shared_ptr<scene::ModelSceneNode> &model,
                       const std::string &animation,
                       const std::string &ready);
    void updateReadyReturns();
    void applyCameraProjection();

    // Hide the authored canopy glass shells for this session's cockpit only.
    void suppressCanopyGlass();

    void detachAll();
    std::shared_ptr<scene::ModelSceneNode> acquireBulletNode(const std::string &resRef);
    void releaseBulletNode(Bullet &bullet);

    // Input

    bool handleKeyDown(const input::KeyEvent &event);
    bool handleKeyUp(const input::KeyEvent &event);
    bool handleMouseMotion(const input::MouseMotionEvent &event);
    bool handleMouseButton(const input::MouseButtonEvent &event);

    float cameraAspect() const;
};

} // namespace game

} // namespace reone
