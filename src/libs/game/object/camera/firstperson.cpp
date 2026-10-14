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

#include "reone/game/object/camera/firstperson.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/graphics/types.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/camera.h"

using namespace reone::graphics;
using namespace reone::scene;

namespace reone {

namespace game {

static constexpr float kMovementSpeed = 4.0f;
static constexpr float kMouseMultiplier = glm::pi<float>() / 4000.0f;
// Free-look: the share of the mouse sensitivity a frame of horizontal travel
// turns by, and the most it may turn, before the rotate speed and the frame
// time scale it.
static constexpr float kFreeLookMouseTurnScale = 0.5f;
static constexpr float kFreeLookMaxTurn = 30.0f;
// In TSL vertical mouse travel tilts by a tenth of the mouse sensitivity.
static constexpr float kFreeLookTiltDivisorTSL = 10.0f;
// The keyboard tilt's rate in degrees per second, and the gain that
// approaches it and brakes from it.
static constexpr float kFreeLookTiltRate = 200.0f;
static constexpr float kFreeLookTiltGain = 500.0f;
// The look pitch wraps into a half turn either side of the horizontal.
static constexpr float kHalfTurn = 180.0f;
static constexpr float kFullTurn = 360.0f;

void FirstPersonCamera::load() {
    auto &scene = _services.scene.graphs.get(_sceneName);
    _sceneNode = scene.newCamera();
    rebuildProjection();
}

float FirstPersonCamera::projectionFovy() const {
    return _fovy;
}

bool FirstPersonCamera::handle(const input::Event &event) {
    if (isAttached()) {
        return handleAttached(event);
    }
    switch (event.type) {
    case input::EventType::MouseMotion:
        return handleMouseMotion(event.motion);
    case input::EventType::KeyDown:
        return handleKeyDown(event.key);
    case input::EventType::KeyUp:
        return handleKeyUp(event.key);
    default:
        return false;
    }
}

bool FirstPersonCamera::handleMouseMotion(const input::MouseMotionEvent &event) {
    _facing = glm::mod(
        _facing - event.xrel * kMouseMultiplier,
        glm::two_pi<float>());

    _pitch = glm::clamp(
        _pitch - event.yrel * kMouseMultiplier,
        -glm::quarter_pi<float>(),
        glm::quarter_pi<float>());

    updateSceneNode();

    return true;
}

void FirstPersonCamera::updateSceneNode() {
    glm::quat orientation(glm::vec3(glm::half_pi<float>(), 0.0f, 0.0f));
    orientation *= glm::quat(glm::vec3(_pitch, _facing, 0.0f));

    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, _position);
    transform *= glm::mat4_cast(orientation);

    _sceneNode->setLocalTransform(transform);
}

bool FirstPersonCamera::handleKeyDown(const input::KeyEvent &event) {
    switch (event.code) {
    case input::KeyCode::D:
        _moveDir = MovementDirection::Right;
        return true;

    case input::KeyCode::A:
        _moveDir = MovementDirection::Left;
        return true;

    case input::KeyCode::W:
        _moveDir = MovementDirection::Forward;
        return true;

    case input::KeyCode::S:
        _moveDir = MovementDirection::Back;
        return true;

    case input::KeyCode::Q:
        _moveDir = MovementDirection::Up;
        return true;

    case input::KeyCode::Z:
        _moveDir = MovementDirection::Down;
        return true;

    case input::KeyCode::LeftShift:
        _multiplier = 2.0f;
        return true;

    default:
        return false;
    }
}

bool FirstPersonCamera::handleKeyUp(const input::KeyEvent &event) {
    switch (event.code) {
    case input::KeyCode::D:
        if (_moveDir == MovementDirection::Right) {
            _moveDir = MovementDirection::None;
        }
        return true;

    case input::KeyCode::A:
        if (_moveDir == MovementDirection::Left) {
            _moveDir = MovementDirection::None;
        }
        return true;

    case input::KeyCode::W:
        if (_moveDir == MovementDirection::Forward) {
            _moveDir = MovementDirection::None;
        }
        return true;

    case input::KeyCode::S:
        if (_moveDir == MovementDirection::Back) {
            _moveDir = MovementDirection::None;
        }
        return true;

    case input::KeyCode::Q:
        if (_moveDir == MovementDirection::Up) {
            _moveDir = MovementDirection::None;
        }
        return true;

    case input::KeyCode::Z:
        if (_moveDir == MovementDirection::Down) {
            _moveDir = MovementDirection::None;
        }
        return true;

    case input::KeyCode::LeftShift:
        _multiplier = 1.0f;
        return true;

    default:
        return false;
    }
}

void FirstPersonCamera::attach(const std::shared_ptr<Creature> &creature, const CameraStyle &style) {
    _attached = creature;
    _style = style;
    _lookPitch = 0.0f;
    _mouseX = _mouseY = _mouseTilt = 0.0f;
    _turnLeft = _turnRight = false;
    _turnVelocity = _turnInput = 0.0f;
    _pitchUp = _pitchDown = false;
    _tiltVelocity = _tiltInput = 0.0f;
    _moveDir = MovementDirection::None;
}

bool FirstPersonCamera::isAttached() const {
    return static_cast<bool>(_attached.resolve());
}

void FirstPersonCamera::detach() {
    _attached.reset();
    _turnLeft = _turnRight = false;
    _pitchUp = _pitchDown = false;
}

// Mouse travel is taken at the next update. A and D turn the creature; W and S
// are held for the keyboard tilt.
bool FirstPersonCamera::handleAttached(const input::Event &event) {
    switch (event.type) {
    case input::EventType::MouseMotion:
        _mouseX += static_cast<float>(event.motion.xrel);
        _mouseY += static_cast<float>(event.motion.yrel);
        return true;
    case input::EventType::KeyDown:
    case input::EventType::KeyUp: {
        const bool down = event.type == input::EventType::KeyDown;
        switch (event.key.code) {
        case input::KeyCode::A:
            _turnLeft = down;
            return true;
        case input::KeyCode::D:
            _turnRight = down;
            return true;
        case input::KeyCode::W:
            _pitchUp = down;
            return true;
        case input::KeyCode::S:
            _pitchDown = down;
            return true;
        default:
            return false;
        }
    }
    default:
        return false;
    }
}

void FirstPersonCamera::updateAttached(Creature &creature, float dt) {
    bool tsl = _game.isTSL();
    // Held keys turn the creature with the keyboard profile. Otherwise the
    // mouse turns it by its travel.
    const float input = _turnLeft ? 1.0f : (_turnRight ? -1.0f : 0.0f);
    float turned = stepKeyboardTurn(_turnVelocity, _turnInput, input, dt);
    const float strengthX = mouseFrameStrength(_mouseX);
    const float strengthY = mouseFrameStrength(_mouseY);
    _mouseX = _mouseY = 0.0f;
    if (input == 0.0f) {
        const float delta = -kDefaultMouseSensitivity * strengthX;
        turned -= glm::clamp(delta * kFreeLookMouseTurnScale, -kFreeLookMaxTurn, kFreeLookMaxTurn) * dt * _style.freeLookRotateSpeed;
    }
    if (turned != 0.0f) {
        creature.setFacing(glm::mod(creature.getFacing() + glm::radians(turned), glm::two_pi<float>()));
    }
    // Vertical travel sets the tilt. TSL reads it only on frames without a
    // turn key, so the last tilt lasts while one is held, and there downward
    // travel looks up. KotOR reads it every frame, and downward travel looks
    // down.
    if (!tsl) {
        _mouseTilt = kDefaultMouseSensitivity * strengthY;
    } else if (input == 0.0f) {
        _mouseTilt = -kDefaultMouseSensitivity * strengthY / kFreeLookTiltDivisorTSL;
    }
    // A mouse tilt pitches directly and stops the keyboard tilt. Otherwise W
    // and S pitch with the keyboard profile, up and down, in KotOR only.
    if (_mouseTilt != 0.0f) {
        _lookPitch += _mouseTilt * dt * _style.freeLookTiltSpeed;
        _tiltVelocity = _tiltInput = 0.0f;
    } else {
        const float keys = tsl ? 0.0f : (_pitchUp ? 1.0f : 0.0f) - (_pitchDown ? 1.0f : 0.0f);
        _lookPitch += stepAcceleratedRate(_tiltVelocity, _tiltInput, keys, dt, kFreeLookTiltRate, kFreeLookTiltGain, kFreeLookTiltGain);
    }
    if (_lookPitch < -kHalfTurn) {
        _lookPitch += kFullTurn;
    }
    if (_lookPitch > kHalfTurn) {
        _lookPitch -= kFullTurn;
    }
    _lookPitch = std::min(_style.freeLookUp, std::max(-_style.freeLookDown, _lookPitch));

    _position = creature.freeLookPoint();
    _facing = creature.getFacing();
    _pitch = glm::radians(_lookPitch);
    updateSceneNode();
}

void FirstPersonCamera::update(float dt) {
    Camera::update(dt);
    if (auto creature = _attached.resolve()) {
        updateAttached(*creature, dt);
        return;
    }

    float facingSin = glm::sin(_facing) * _multiplier * kMovementSpeed * dt;
    float facingCos = glm::cos(_facing) * _multiplier * kMovementSpeed * dt;
    float pitchSin = glm::sin(_pitch) * _multiplier * kMovementSpeed * dt;
    float pitchCos = glm::cos(_pitch) * _multiplier * kMovementSpeed * dt;
    bool positionChanged = false;

    switch (_moveDir) {
    case MovementDirection::Right:
        _position.x += facingCos;
        _position.y += facingSin;
        positionChanged = true;
        break;
    case MovementDirection::Left:
        _position.x -= facingCos;
        _position.y -= facingSin;
        positionChanged = true;
        break;
    case MovementDirection::Forward:
        _position.x -= facingSin;
        _position.y += facingCos;
        _position.z += pitchSin;
        positionChanged = true;
        break;
    case MovementDirection::Back:
        _position.x += facingSin;
        _position.y -= facingCos;
        _position.z -= pitchSin;
        positionChanged = true;
        break;
    case MovementDirection::Up:
        _position.x += facingSin * pitchSin;
        _position.y -= facingCos * pitchSin;
        _position.z += pitchCos;
        positionChanged = true;
        break;
    case MovementDirection::Down:
        _position.x -= facingSin * pitchSin;
        _position.y += facingCos * pitchSin;
        _position.z -= pitchCos;
        positionChanged = true;
        break;
    default:
        break;
    }
    if (positionChanged) {
        updateSceneNode();
    }
}

void FirstPersonCamera::stopMovement() {
    _moveDir = MovementDirection::None;
}

void FirstPersonCamera::setPosition(const glm::vec3 &pos) {
    _position = pos;
    updateSceneNode();
}

void FirstPersonCamera::setFacing(float facing) {
    _facing = facing;
    updateSceneNode();
}

void FirstPersonCamera::setLookAt(const glm::vec3 &target) {
    glm::vec3 direction = target - _position;
    float length = glm::length(direction);
    if (length <= glm::epsilon<float>()) {
        throw std::invalid_argument("Camera look target must differ from its position");
    }
    direction /= length;
    _facing = glm::atan(-direction.x, direction.y);
    _pitch = glm::clamp(glm::asin(direction.z), -glm::quarter_pi<float>(), glm::quarter_pi<float>());
    updateSceneNode();
}

} // namespace game

} // namespace reone
