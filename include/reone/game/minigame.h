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

namespace reone {

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

    // Vanilla "RotatingModel": models flagged here follow the player/enemy
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

struct MinigamePlayerScriptSpec {
    std::string onCreate;
    std::string onDeath;
    std::string onTrackLoop;
    std::string onDamage;
    std::string onAccelerate;
    std::string onHeartbeat;
    std::string onFire;
    std::string onHitBullet;
    std::string onHitFollower;
    std::string onHitObstacle;
};

struct MinigamePlayerSpec {
    std::string cameraResRef;
    std::string trackResRef;
    std::vector<MinigameModelSpec> models;
    std::vector<MinigameGunBankSpec> gunBanks;
    float minimumSpeed {0.0f};
    float maximumSpeed {0.0f};
    float accelSecs {0.0f};
    float sphereRadius {0.0f};
    uint32_t hitPoints {0};
    uint32_t maxHitPoints {0};
    float invincePeriod {0.0f};
    int bumpDamage {0};
    int numLoops {0};
    bool cameraRotate {false};
    MinigameSoundSpec sounds;
    MinigamePlayerScriptSpec scripts;

    // Placement of the player actor relative to its track hook, and of the
    // aim/target point relative to the actor.
    glm::vec3 startOffset {0.0f};
    glm::vec3 targetOffset {0.0f};

    // Tunnel bounds, as stored in the .are. For the swoop race these are the
    // angular limits (degrees) vanilla applies to bike lean/rotation per axis;
    // for the turret they are the aim limits (X = pitch, Z = yaw).
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

struct MinigameEnemyScriptSpec {
    std::string onCreate;
    std::string onDeath;
    std::string onDamage;
    std::string onHeartbeat;
    std::string onTrackLoop;
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
    MinigameEnemyScriptSpec scripts;

    // Kept for call sites that only need the creation hook.
    std::string onCreate;
};

struct MinigameObstacleSpec {
    std::string name;
    std::string onCreate;
};

// Vanilla mouse axis binding for the minigame. The axis identifiers are opaque
// engine constants; only the flip flags have a confirmed meaning.
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
    std::string music;
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

} // namespace game

} // namespace reone
