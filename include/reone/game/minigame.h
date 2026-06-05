/*
 * Copyright (c) 2020-2023 The reone project contributors
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

struct MinigamePlayerScriptSpec {
    std::string onCreate;
    std::string onDeath;
    std::string onTrackLoop;
    std::string onDamage;
    std::string onAccelerate;
    std::string onHeartbeat;
};

struct MinigamePlayerSpec {
    std::string cameraResRef;
    std::string trackResRef;
    std::vector<std::string> modelResRefs;
    float minimumSpeed {0.0f};
    float maximumSpeed {0.0f};
    float accelSecs {0.0f};
    float sphereRadius {0.0f};
    uint32_t hitPoints {0};
    MinigamePlayerScriptSpec scripts;
};

struct MinigameEnemySpec {
    std::string trackResRef;
    std::vector<std::string> modelResRefs;
    uint32_t hitPoints {0};
    std::string onCreate;
};

struct MinigameObstacleSpec {
    std::string name;
    std::string onCreate;
};

// Passive data parsed from the .are MiniGame struct. Stored on Area.
// Not instantiated as a game object — that belongs to a future slice.
struct MinigameSpec {
    MinigameType type {MinigameType::None};
    float cameraViewAngle {0.0f};
    float lateralAccel {0.0f};
    float movementPerSec {0.0f};
    bool useInertia {false};
    MinigamePlayerSpec player;
    std::vector<MinigameEnemySpec> enemies;
    std::vector<MinigameObstacleSpec> obstacles;

    // Unique track resrefs referenced by player and enemies.
    // Derived at parse time; not stored separately in the .are format.
    std::vector<std::string> trackResRefs;
};

} // namespace game

} // namespace reone
