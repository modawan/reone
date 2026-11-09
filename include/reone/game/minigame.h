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
 * Minigame mouse-axis bindings. Axis IDs are retained as opaque values;
 * only the flip flags currently affect input.
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

} // namespace game

} // namespace reone
