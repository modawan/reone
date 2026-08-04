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

#include <limits>

#include "reone/input/event.h"

#include "minigame.h"

namespace reone {

namespace audio {

class AudioClip;

}

namespace scene {

class ModelSceneNode;

}

namespace game {

class Game;
class FirstPersonCamera;
struct ServicesView;

/**
 * Aim state of the player turret, in radians.
 *
 * Vanilla stores the aim limits in the .are MiniGame Player "Tunnel" fields:
 * X is the pitch axis and Z is the yaw axis (Y is unused by the turret). The
 * stored values are signed degree limits, so the usable range of an axis is
 * [Neg, Pos] rather than [-Neg, +Pos]; K1's Ebon Hawk turret pitches between
 * TunnelXNeg = 2 and TunnelXPos = 45 degrees. An axis whose TunnelInfinite
 * component is set is unbounded and wraps instead of clamping, which is how the
 * turret gets free 360 degree yaw.
 */
class TurretAim {
public:
    void configure(const MinigamePlayerSpec &player);

    /**
     * Return to the authored starting aim. Replaying a session must not inherit
     * the previous run's aim.
     */
    void reset();

    void addPitch(float radians);
    void addYaw(float radians);

    float pitch() const { return _pitch; }
    float yaw() const { return _yaw; }

    // Authored starting aim, already clamped into the authored bounds.
    float startPitch() const { return _startPitch; }
    float startYaw() const { return _startYaw; }

    // How far an axis can travel, in radians; a full turn when unbounded.
    float pitchTravel() const;
    float yawTravel() const;

    /**
     * Unit vector the turret is pointing along, in the track frame. Odyssey
     * models look down +Y, so this is Rz(yaw) * Rx(pitch) applied to +Y.
     */
    glm::vec3 forward() const;

    glm::quat orientation() const;

    bool pitchBounded() const { return _pitchBounded; }
    bool yawBounded() const { return _yawBounded; }
    float minPitch() const { return _minPitch; }
    float maxPitch() const { return _maxPitch; }
    float minYaw() const { return _minYaw; }
    float maxYaw() const { return _maxYaw; }

private:
    float _pitch {0.0f};
    float _yaw {0.0f};
    float _startPitch {0.0f};
    float _startYaw {0.0f};

    bool _pitchBounded {true};
    bool _yawBounded {true};
    float _minPitch {0.0f};
    float _maxPitch {0.0f};
    float _minYaw {0.0f};
    float _maxYaw {0.0f};
};

/**
 * Turn rate of one aim axis while a key is held.
 *
 * This is reone's existing keyboard rotation model (ThirdPersonCamera): a held
 * key starts at a minimum rate and accelerates to a cap, so a tap gives fine
 * adjustment while a hold sweeps. The rates are scaled by the axis's authored
 * travel and calibrated so an unbounded axis reproduces ThirdPersonCamera's
 * rates exactly; a narrow axis such as the turret's 43 degree pitch band then
 * moves proportionally slower, and both axes cross their full authored travel
 * in about the same time.
 *
 * The rate is integrated analytically rather than stepped, so the angle covered
 * over a given interval does not depend on how many frames it is split into.
 */
class TurretAimRate {
public:
    void configure(float travelRadians);

    /**
     * -1, 0 or +1. Engaging an axis, or reversing it, restarts the acceleration
     * ramp from the minimum rate.
     */
    void setDirection(int direction);

    /**
     * Angle to apply this step, signed by the held direction; zero when idle.
     */
    float advance(float dt);

    /** Drop any held direction and accumulated acceleration. */
    void reset();

    int direction() const { return _direction; }
    float minRate() const { return _minRate; }
    float maxRate() const { return _maxRate; }
    float rate() const;

private:
    float _minRate {0.0f};
    float _maxRate {0.0f};
    float _acceleration {0.0f};
    int _direction {0};
    float _held {0.0f}; // seconds the current direction has been engaged

    float rateAt(float held) const;
    float angleOver(float from, float to) const;
};

/**
 * Rate-of-fire gate for a single gun bank. Vanilla stores the interval between
 * shots in the bank's Bullet.Rate_Of_Fire.
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
 * Cockpit HUD state, reproducing what K1's module-local OnDamage script
 * (k_pebo_hawkhit) computes. reone substitutes the minigame and cannot run that
 * script - its SWMG_ routines are unimplemented - so the small part of it that
 * drives the HUD is reproduced here from the shipped bytecode:
 *
 *   if (!GetGlobalBoolean("M12AB_END_SYNC")) {
 *     if (SWMG_GetHitPoints(OBJECT_SELF) >= 2000) {
 *       SWMG_OnDamage();
 *       state = ((SWMG_GetHitPoints(OBJECT_SELF) - 2000) * 12) / 1000 + 1;
 *       anim  = (state <= 9) ? "Health0" + IntToString(state)
 *                            : "Health"  + IntToString(state);
 *       SWMG_PlayAnimation(OBJECT_SELF, anim, TRUE, FALSE, FALSE);
 *       if (state == 3) { SoundObjectPlay(GetObjectByTag("Alarm01")); }
 *     } else {
 *       ... SoundObjectStop(GetObjectByTag("Alarm01"));
 *       SWMG_PlayAnimation(OBJECT_SELF, "Health00", TRUE, FALSE, FALSE);
 *       ... loss presentation
 *     }
 *   }
 */

// The hit point floor the shipped script treats as destruction, and the span of
// the authored gauge above it. The Ebon Hawk is authored with 3000 hit points,
// so the gauge covers 2000..3000.
constexpr int kTurretGaugeFloor = 2000;
constexpr int kTurretGaugeSpan = 1000;
constexpr int kTurretHealthStateCount = 13; // Health00..Health12

/**
 * Authored health state for a hit point total, clamped to Health00..Health12.
 *
 * Uses the shipped integer arithmetic verbatim, including its truncating
 * division: a state covers a whole band of hit points, so ordinary damage steps
 * the gauge down one state at a time rather than flickering between two.
 */
/**
 * Whether the hawk has been destroyed. The shipped script takes its destruction
 * branch below the gauge floor rather than at zero hit points.
 */
bool turretIsDestroyed(int hitPoints);

int turretHealthState(int hitPoints);

/** "Health00".."Health12" for a state index. */
std::string turretHealthAnimation(int state);

/**
 * The shipped script starts the alarm when the computed state is exactly 3 -
 * a boundary trigger as the gauge falls through that band, not a level test,
 * and with no authored hysteresis or restart.
 */
constexpr int kTurretAlarmState = 3;

bool turretAlarmStartsAtState(int state);

/** Tag of the authored looping alarm sound object placed in the module. */
extern const std::string kTurretAlarmTag;

/**
 * Radar contact channel for an enemy index (0-based). The shipped HUD authors
 * one contact loop per enemy track: track m12ab_mgt02 is SithLoop02 on node
 * sithhud002, and so on through mgt07 / SithLoop07 / sithhud007.
 */
std::string turretContactAnimation(size_t enemyIndex);

/**
 * The matching contact-removal pose ("SithLoop02d"..), a zero length pose that
 * drops the contact's alpha.
 */
std::string turretContactDeathAnimation(size_t enemyIndex);

/** How many contact channels the shipped HUD authors. */
constexpr size_t kTurretContactCount = 6;

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
 * Radar heading pose index for a turret yaw, as a whole degree in [0, 360).
 */
int turretHeadingState(float yawRadians);

/** "HudRot_000".."HudRot_359" for a heading index. */
std::string turretHeadingAnimation(int heading);

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
 * Where a finished turret session returns to: the vanilla end-script target for
 * the turret module when one is known, otherwise the module it started from.
 */
std::string turretReturnModule(const std::string &turretModule,
                               const std::string &originModule);

/**
 * Direction an actor with this world orientation fires along. Odyssey models
 * look down +Y, so both the bolt's travel and its visual long axis come from the
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
 * The K1 Ebon Hawk turret minigame (.are MiniGame Type 2).
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
 * in the track frame. Gun banks mount on the rotating group's "gunbankN" hooks
 * and spawn bullets from the gun model's "bullethook0".
 *
 * Not implemented in this slice: the vanilla minigame scripts (the SWMG_*
 * routines they need are unimplemented), obstacles, and the player invincibility
 * period. Win/lose is decided engine-side: destroying every enemy wins, running
 * out of hit points loses.
 */
class Turret {
public:
    struct GunBank {
        const MinigameGunBankSpec *spec {nullptr};
        std::shared_ptr<scene::ModelSceneNode> modelNode;
        TurretGunTimer timer;
    };

    struct Enemy {
        const MinigameEnemySpec *spec {nullptr};
        std::shared_ptr<scene::ModelSceneNode> trackNode;
        std::shared_ptr<scene::ModelSceneNode> modelNode;
        std::vector<std::shared_ptr<scene::ModelSceneNode>> extraNodes;
        std::vector<GunBank> gunBanks;
        glm::vec3 position {0.0f};
        glm::quat orientation {1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 forward {0.0f, 1.0f, 0.0f};
        int hitPoints {0};
        bool alive {true};
        bool deathHandled {false};

        // Death effect bookkeeping. A destroyed fighter keeps playing its
        // authored "die" animation for that animation's own length, after which
        // everything it owns leaves the scene.
        float deathElapsed {0.0f};
        float deathDuration {0.0f};
        bool nodesReleased {false};
    };

    struct Bullet {
        TurretBullet sim;
        std::shared_ptr<scene::ModelSceneNode> modelNode;
        std::string modelResRef;
        std::string collisionSound;
    };

    enum class Outcome {
        InProgress,
        Won,
        Lost
    };

    Turret(Game &game, ServicesView &services) :
        _game(game),
        _services(services) {
    }

    /**
     * Load the minigame actors for \p spec and activate the turret.
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

    // Diagnostics

    Outcome outcome() const { return _outcome; }
    bool finished() const { return _outcome != Outcome::InProgress; }

    const TurretAim &aim() const { return _aim; }
    glm::vec3 position() const { return _anchorPosition; }
    int hitPoints() const { return _hitPoints; }
    int maxHitPoints() const { return _maxHitPoints; }
    float elapsed() const { return _elapsed; }
    size_t enemyCount() const { return _enemies.size(); }
    size_t enemiesAlive() const;
    size_t bulletCount() const { return _bullets.size(); }
    size_t gunBankCount() const { return _gunBanks.size(); }
    const std::string &trackResRef() const { return _trackResRef; }
    const std::string &anchorSource() const { return _anchorSource; }

    // HUD diagnostics
    int healthState() const { return _healthState; }
    int headingState() const { return _headingState; }
    bool alarmActive() const { return _alarmActive; }
    bool haveHealthHud() const { return static_cast<bool>(_healthHud); }
    bool haveRadarHud() const { return static_cast<bool>(_radarHud); }
    size_t contactsLive() const;
    size_t radarChannelCount() const;

    // Eye offset the camera mount contributes, in the rotating group's frame.
    glm::vec3 cameraHookOffset() const { return glm::vec3(_cameraHookLocal[3]); }
    bool haveCameraHook() const { return _haveCameraHook; }

    // END Diagnostics

private:
    Game &_game;
    ServicesView &_services;

    bool _active {false};
    Outcome _outcome {Outcome::InProgress};

    // Owned copy of the area metadata: the runtime holds pointers into its
    // enemy and gun bank lists, which must outlive the area on a module change.
    MinigameSpec _spec;

    std::string _areaName;
    std::string _trackResRef;
    std::string _anchorSource;

    FirstPersonCamera *_camera {nullptr};

    // Player actors

    std::shared_ptr<scene::ModelSceneNode> _playerTrackNode;

    // Root of the models that stay fixed in the track frame (the hull).
    std::shared_ptr<scene::ModelSceneNode> _bodyRoot;
    std::vector<std::shared_ptr<scene::ModelSceneNode>> _bodyChildNodes;

    // Root of the models that follow the aim (the gun, the HUD).
    std::shared_ptr<scene::ModelSceneNode> _turretRoot;
    std::vector<std::shared_ptr<scene::ModelSceneNode>> _turretChildNodes;

    // Static model-space transform of the camera mount's "camerahook" node. The
    // mount model itself is not added to the scene.
    glm::mat4 _cameraHookLocal {1.0f};
    bool _haveCameraHook {false};

    // Cockpit HUD panes, picked out of the rotating model set by resref. The
    // health gauge and the radar are separate authored models, each driven by
    // overlay animation channels so their disjoint node sets can run at once.
    std::shared_ptr<scene::ModelSceneNode> _healthHud;
    std::shared_ptr<scene::ModelSceneNode> _radarHud;

    int _healthState {-1};    // last applied Health00..12, -1 = none yet
    int _headingState {-1};   // last applied HudRot degree, -1 = none yet
    bool _alarmActive {false};
    std::vector<bool> _contactAlive;

    std::vector<GunBank> _gunBanks;
    std::vector<Enemy> _enemies;
    std::vector<Bullet> _bullets;

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
    float _playerSphereRadius {0.0f};
    int _hitPoints {0};
    int _maxHitPoints {0};
    float _elapsed {0.0f};
    bool _firing {false};

    // Loading

    std::shared_ptr<scene::ModelSceneNode> loadModel(const std::string &resRef);
    std::shared_ptr<scene::ModelSceneNode> loadTrack(const std::string &resRef,
                                                     const glm::vec3 &position);
    bool loadPlayer(const MinigameSpec &spec);
    void loadEnemies(const MinigameSpec &spec);

    /**
     * Retire everything a fighter owns: its gun bank models, its model root
     * with the reference models, lights and emitters the loader built under it,
     * and its rail. Safe to call twice.
     */
    void releaseEnemyNodes(Enemy &enemy);
    void loadGunBanks(const std::vector<MinigameGunBankSpec> &specs,
                      scene::ModelSceneNode &mount,
                      std::vector<GunBank> &out);

    // Simulation

    void updateAnchor();
    void updatePlayerTransforms();
    void updateCamera();
    void updateEnemies(float dt);
    void updateBullets(float dt);
    void fire();
    void spawnBullet(const MinigameGunBankSpec &bank,
                     const glm::vec3 &origin,
                     const glm::quat &orientation,
                     bool fromPlayer);
    void damagePlayer(uint32_t amount);
    void damageEnemy(Enemy &enemy, uint32_t amount);
    void playSound(const std::string &resRef);

    // Hide the authored canopy glass shells for this session's cockpit only.
    void suppressCanopyGlass();

    // HUD

    void bindHudPanes();
    void resetHud();
    void updateHealthHud();
    void updateHeadingHud();
    void startContactHud();
    void killContactHud(size_t enemyIndex);
    void clearHud();
    void setAlarmActive(bool active);

    // END HUD

    void detachAll();
    std::shared_ptr<scene::ModelSceneNode> acquireBulletNode(const std::string &resRef);
    void releaseBulletNode(Bullet &bullet);
    void setCameraFieldOfView(float fovDegrees);

    // Input

    bool handleKeyDown(const input::KeyEvent &event);
    bool handleKeyUp(const input::KeyEvent &event);
    bool handleMouseMotion(const input::MouseMotionEvent &event);
    bool handleMouseButton(const input::MouseButtonEvent &event);

    float cameraAspect() const;
};

/**
 * A session is a success only when every enemy was destroyed. Abandoning a
 * session mid-run is neither a win nor a loss, so it must not be reported as
 * either - in particular it must not emit the victory-only completion state.
 */
inline bool turretSessionSucceeded(Turret::Outcome outcome) {
    return outcome == Turret::Outcome::Won;
}

/**
 * True when a session ended before reaching a win or a loss, i.e. the player
 * left through Escape or stopturret.
 */
inline bool turretSessionAborted(Turret::Outcome outcome) {
    return outcome == Turret::Outcome::InProgress;
}

inline const char *turretOutcomeName(Turret::Outcome outcome) {
    switch (outcome) {
    case Turret::Outcome::Won:
        return "won";
    case Turret::Outcome::Lost:
        return "lost";
    default:
        return "aborted";
    }
}

} // namespace game

} // namespace reone
