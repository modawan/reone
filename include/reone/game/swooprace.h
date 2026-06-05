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

#include "reone/input/event.h"

#include "minigame.h"

namespace reone {

namespace game {

class Game;
class FirstPersonCamera;

/**
 * Developer skeleton for the swoop racing minigame. Drives a simple forward
 * movement plus left/right steering, consuming passive metadata parsed from
 * the area's .are MiniGame struct (see MinigameSpec). Intentionally minimal:
 * no obstacles, enemies, boost, lap/finish logic, HUD, or scripts.
 *
 * The race reuses the area's first-person camera as a temporary chase/hood
 * camera. A dedicated model-hook camera and a spawned bike model are left for
 * a later slice.
 */
class SwoopRace {
public:
    SwoopRace(Game &game) :
        _game(game) {
    }

    /**
     * Initialize and activate the race from the given metadata, anchored at the
     * supplied world position and facing. The camera may be null, in which case
     * movement state still advances but nothing is driven visually.
     */
    void start(const MinigameSpec &spec,
               FirstPersonCamera *camera,
               const glm::vec3 &startPosition,
               float startFacing);

    void stop();

    void update(float dt);
    bool handle(const input::Event &event);

    bool isActive() const { return _active; }

    // Diagnostics

    MinigameType type() const { return _type; }
    const std::string &trackResRef() const { return _trackResRef; }
    size_t modelCount() const { return _modelCount; }
    float movePerSec() const { return _movePerSec; }
    float lateralAccel() const { return _lateralAccel; }
    float cameraViewAngle() const { return _camFov; }
    float elapsed() const { return _elapsed; }
    float speed() const { return _speed; }

    // END Diagnostics

private:
    Game &_game;

    bool _active {false};

    // Tuning copied from metadata at start

    MinigameType _type {MinigameType::None};
    float _movePerSec {0.0f};
    float _lateralAccel {0.0f};
    float _camFov {0.0f};
    std::string _trackResRef;
    size_t _modelCount {0};

    // Runtime state

    float _speed {0.0f};
    float _lateralOffset {0.0f};
    float _lateralVel {0.0f};
    float _elapsed {0.0f};
    glm::vec3 _position {0.0f};
    float _facing {0.0f};
    int _steerDir {0}; // -1 left, +1 right, 0 none

    FirstPersonCamera *_camera {nullptr};

    bool handleKeyDown(const input::KeyEvent &event);
    bool handleKeyUp(const input::KeyEvent &event);

    void applyToCamera();
};

} // namespace game

} // namespace reone
