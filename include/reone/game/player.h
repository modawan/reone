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

#include <memory>
#include <optional>

#include "reone/input/event.h"

namespace reone {

namespace game {

class Camera;
class Creature;
class Module;
class Party;

/**
 * Encapsulates third-person player controls. The movement input drives the
 * leader: it gives the leader a drive as the input starts and a new one each
 * drive update while it lasts, clearing the leader's orders each time, and
 * steers the drive in between.
 */
class Player : boost::noncopyable {
public:
    Player(Module &module, Camera &camera, const Party &party) :
        _module(module),
        _camera(camera),
        _party(party) {
    }

    bool handle(const input::Event &event);
    void update(float dt);

    void stopMovement();

    bool isMovementRequested() const;
    /** Whether the player drives the leader. */
    bool isMoving() const;

private:
    Module &_module;
    Camera &_camera;
    const Party &_party;

    bool _moveForward {false};
    bool _moveLeft {false};
    bool _moveBackward {false};
    bool _moveRight {false};
    bool _leftPressedInMouseLook {false};
    bool _walk {false};
    /** Time left until the next drive update while the movement input lasts. */
    float _driveUpdateDelay {0.0f};
    /** The creature the last drive update went to. */
    std::weak_ptr<Creature> _driven;

    /** The facing the movement input steers the leader toward, if any. */
    std::optional<float> steeredFacing() const;

    bool handleKeyDown(const input::KeyEvent &event);
    bool handleKeyUp(const input::KeyEvent &event);
    bool handleMouseButtonDown(const input::MouseButtonEvent &event);
    bool handleMouseButtonUp(const input::MouseButtonEvent &event);
};

} // namespace game

} // namespace reone
