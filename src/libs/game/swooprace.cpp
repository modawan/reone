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

#include "reone/game/swooprace.h"

#include "reone/game/game.h"
#include "reone/game/object/camera/firstperson.h"
#include "reone/graphics/types.h"
#include "reone/scene/node/camera.h"
#include "reone/scene/node/model.h"

namespace reone {

namespace game {

// Fallback forward speed when metadata movement is unspecified.
static constexpr float kFallbackMovePerSec = 10.0f;

// The forward target speed is reached in this many seconds, to avoid an
// instantaneous jump at race start.
static constexpr float kSpeedRampSeconds = 3.0f;

// Dev steering tuning. The parsed lateralAccel (e.g. 300) ramps the sideways
// velocity quickly; the velocity cap keeps it controllable while still snappy
// relative to the high forward speed. Values are tuned for testing feel, not
// vanilla accuracy.
static constexpr float kMaxLateralSpeed = 20.0f; // units per second
static constexpr float kLateralDecay = 40.0f;    // units per second^2 when not steering

// Lateral travel bounds (world units). The dev proxy moves the bike laterally
// in world space, so it needs a positional limit. Vanilla tunnel values are
// angular (deg) lean/rotation limits and do not directly bound lateral
// position (the track walkmesh does), so the tunnel X magnitude is reused here
// only as a per-module hint, safety-clamped to this range. A proper positional
// bound awaits track-rail following.
static constexpr float kMinLateralBound = 3.0f;
static constexpr float kMaxLateralBound = 8.0f;
static constexpr float kFallbackLateralBound = 8.0f;

// Chase camera placement relative to the bike.
static constexpr float kChaseDistance = 4.5f; // behind the bike
static constexpr float kChaseHeight = 2.5f;   // above the bike
static constexpr float kLookAtHeight = 1.0f;  // look-at point above the bike

// Maximum cosmetic bank applied while strafing. Purely visual: it does not
// affect movement, progress, or facing/heading.
static constexpr float kMaxLeanRadians = 0.20f; // ~11.5 degrees

// Engine default field of view, restored to the reused first-person camera on
// exit (mirrors Area's default camera FOV).
static constexpr float kDefaultCameraFovDegrees = 75.0f;

void SwoopRace::start(const MinigameSpec &spec,
                      FirstPersonCamera *camera,
                      std::vector<std::shared_ptr<scene::ModelSceneNode>> bikeNodes,
                      const glm::vec3 &startPosition,
                      float startFacing,
                      float finishProgress) {
    _type = spec.type;
    _movePerSec = spec.movementPerSec;
    _lateralAccel = spec.lateralAccel;
    _camFov = spec.cameraViewAngle;
    _trackResRef = spec.player.trackResRef;
    _modelCount = spec.player.modelResRefs.size();
    _finishProgress = finishProgress;

    computeLateralBounds(spec.player);

    _camera = camera;
    _bikeNodes = std::move(bikeNodes);

    // Build the track-relative frame once. The bike faces down-course and never
    // turns from input; engine facing convention is forward = (-sin f, cos f).
    _trackStart = startPosition;
    _facing = startFacing;
    _trackForward = glm::vec3(-glm::sin(_facing), glm::cos(_facing), 0.0f);
    _trackRight = glm::vec3(glm::cos(_facing), glm::sin(_facing), 0.0f);

    _speed = 0.0f;
    _progress = 0.0f;
    _lateralOffset = 0.0f;
    _lateralVel = 0.0f;
    _elapsed = 0.0f;
    _steerDir = 0;

    _active = true;

    setCameraFieldOfView(_camFov > 0.0f ? _camFov : kDefaultCameraFovDegrees);
    applyBikeTransform();
    applyChaseCamera();
}

void SwoopRace::stop() {
    // Restore the reused camera's field of view before releasing it.
    setCameraFieldOfView(kDefaultCameraFovDegrees);

    _active = false;
    _steerDir = 0;
    _camera = nullptr;
    _bikeNodes.clear();
}

void SwoopRace::computeLateralBounds(const MinigamePlayerSpec &player) {
    // Lateral axis is X: KotOR.js applies the steering force to the track's X
    // (forceVector.x = lateralForce), so tunnel X is the matching axis. The raw
    // tunnel X values are angular in vanilla; we only use their magnitude as a
    // per-module hint and safety-clamp it into a controllable positional range.
    float rawPos = glm::abs(player.tunnelXPos);
    float rawNeg = glm::abs(player.tunnelXNeg);
    if (rawPos > 0.0f || rawNeg > 0.0f) {
        _lateralRightBound = glm::clamp(rawPos > 0.0f ? rawPos : kFallbackLateralBound,
                                        kMinLateralBound, kMaxLateralBound);
        _lateralLeftBound = glm::clamp(rawNeg > 0.0f ? rawNeg : kFallbackLateralBound,
                                       kMinLateralBound, kMaxLateralBound);
        _lateralBoundSource = "tunnelX";
    } else {
        _lateralRightBound = kFallbackLateralBound;
        _lateralLeftBound = kFallbackLateralBound;
        _lateralBoundSource = "fallback";
    }
}

void SwoopRace::update(float dt) {
    if (!_active || dt <= 0.0f) {
        return;
    }
    _elapsed += dt;

    // Forward speed ramps toward the metadata movement rate.
    float targetSpeed = _movePerSec > 0.0f ? _movePerSec : kFallbackMovePerSec;
    if (_speed < targetSpeed) {
        _speed = glm::min(targetSpeed, _speed + (targetSpeed / kSpeedRampSeconds) * dt);
    }

    // Lateral strafe: parsed lateral acceleration drives a conservatively
    // clamped sideways velocity and offset. This is strafe-only - it never
    // changes the bike's heading.
    if (_steerDir != 0) {
        _lateralVel += static_cast<float>(_steerDir) * _lateralAccel * dt;
        _lateralVel = glm::clamp(_lateralVel, -kMaxLateralSpeed, kMaxLateralSpeed);
    } else if (_lateralVel != 0.0f) {
        float decay = kLateralDecay * dt;
        if (glm::abs(_lateralVel) <= decay) {
            _lateralVel = 0.0f;
        } else {
            _lateralVel -= glm::sign(_lateralVel) * decay;
        }
    }
    _lateralOffset += _lateralVel * dt;
    if (_lateralOffset > _lateralRightBound) {
        _lateralOffset = _lateralRightBound;
        _lateralVel = 0.0f;
    } else if (_lateralOffset < -_lateralLeftBound) {
        _lateralOffset = -_lateralLeftBound;
        _lateralVel = 0.0f;
    }

    // Advance forward progress down the fixed track frame.
    _progress += _speed * dt;

    applyBikeTransform();
    applyChaseCamera();
}

glm::vec3 SwoopRace::bikePosition() const {
    glm::vec3 centerline = _trackStart + _trackForward * _progress;
    return centerline + _trackRight * _lateralOffset;
}

void SwoopRace::applyBikeTransform() {
    if (_bikeNodes.empty()) {
        return;
    }
    glm::vec3 bikePos = bikePosition();

    // Down-course yaw (fixed), plus a small cosmetic bank into the strafe. The
    // lean is visual only: it does not affect heading, progress, or strafe.
    float lean = 0.0f;
    if (kMaxLateralSpeed > 0.0f) {
        lean = glm::clamp(_lateralVel / kMaxLateralSpeed, -1.0f, 1.0f) * kMaxLeanRadians;
    }

    glm::mat4 transform(1.0f);
    transform *= glm::translate(bikePos);
    transform *= glm::mat4_cast(glm::quat(glm::vec3(0.0f, 0.0f, _facing)));
    transform *= glm::mat4_cast(glm::quat(glm::vec3(0.0f, lean, 0.0f)));

    // All loaded models share the same bike transform for this dev slice.
    for (auto &node : _bikeNodes) {
        if (node) {
            node->setLocalTransform(transform);
        }
    }
}

void SwoopRace::applyChaseCamera() {
    if (!_camera) {
        return;
    }
    static const glm::vec3 up(0.0f, 0.0f, 1.0f);

    glm::vec3 bikePos = bikePosition();

    glm::vec3 cameraPos = bikePos - _trackForward * kChaseDistance;
    cameraPos.z += kChaseHeight;

    glm::vec3 target = bikePos;
    target.z += kLookAtHeight;

    glm::quat orientation(glm::quatLookAt(glm::normalize(target - cameraPos), up));

    glm::mat4 transform(1.0f);
    transform *= glm::translate(cameraPos);
    transform *= glm::mat4_cast(orientation);
    _camera->cameraSceneNode()->setLocalTransform(transform);
}

void SwoopRace::setCameraFieldOfView(float fovDegrees) {
    if (!_camera) {
        return;
    }
    _camera->cameraSceneNode()->setPerspectiveProjection(
        glm::radians(fovDegrees),
        cameraAspect(),
        graphics::kDefaultClipPlaneNear,
        graphics::kDefaultClipPlaneFar);
}

float SwoopRace::cameraAspect() const {
    const auto &graphicsOpts = _game.options().graphics;
    if (graphicsOpts.height <= 0) {
        return 1.0f;
    }
    return graphicsOpts.width / static_cast<float>(graphicsOpts.height);
}

bool SwoopRace::handle(const input::Event &event) {
    switch (event.type) {
    case input::EventType::KeyDown:
        return handleKeyDown(event.key);
    case input::EventType::KeyUp:
        return handleKeyUp(event.key);
    default:
        return false;
    }
}

bool SwoopRace::handleKeyDown(const input::KeyEvent &event) {
    switch (event.code) {
    case input::KeyCode::Left:
    case input::KeyCode::A:
        _steerDir = -1;
        return true;
    case input::KeyCode::Right:
    case input::KeyCode::D:
        _steerDir = 1;
        return true;
    case input::KeyCode::Space:
        // Jump is stubbed for the skeleton: accepted but has no gameplay effect.
        return true;
    case input::KeyCode::Escape:
        _game.exitSwoopRace();
        return true;
    default:
        return false;
    }
}

bool SwoopRace::handleKeyUp(const input::KeyEvent &event) {
    switch (event.code) {
    case input::KeyCode::Left:
    case input::KeyCode::A:
        if (_steerDir == -1) {
            _steerDir = 0;
        }
        return true;
    case input::KeyCode::Right:
    case input::KeyCode::D:
        if (_steerDir == 1) {
            _steerDir = 0;
        }
        return true;
    default:
        return false;
    }
}

} // namespace game

} // namespace reone
