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

#include "reone/game/object/camera/thirdperson.h"

#include <algorithm>
#include <cmath>

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/scene/collision.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/camera.h"

using namespace reone::graphics;
using namespace reone::scene;

namespace reone {

namespace game {

static constexpr float kMinRotationSpeed = 1.0f;
static constexpr float kMaxRotationSpeed = 2.5f;
static constexpr float kRotationAcceleration = 1.0f;
static constexpr float kMouseRotationSpeed = 0.001f;
static constexpr float kTargetPadding = 0.05f;

// Clearance kept around the combat camera, and the extra lift it may take.
static constexpr float kCombatPersonalSpace = 0.35f;
static constexpr float kCombatLiftAllowance = 0.15f;
// Look-at swing: the target is held this far off centre, the swing lets go
// once it lies this close to the view line (metres), and the approach rate
// ramps between these bounds.
static constexpr float kLookAtOffset = 15.0f;
static constexpr float kLookAtRelease = 0.866f;
static constexpr float kSwingRateMin = 2.5f;
static constexpr float kSwingRateMax = 7.5f;
static constexpr float kSwingRateRise = 3.0f;
static constexpr float kSwingRateFall = 5.0f;

// Death orbit: heading rate (degrees per second), the distance it closes on,
// the tilt below the level it turns to (degrees), the per-update easing of
// heading, tilt, distance and aim height, the aim height it starts from, and
// the clearance kept from what the view ray hits.
static constexpr float kDeathOrbitRate = 30.0f;
static constexpr float kDeathOrbitDistance = 3.0f;
static constexpr float kDeathOrbitTilt = 90.0f;
static constexpr float kDeathYawEasing = 0.5f;
static constexpr float kDeathTiltEasing = 0.01f;
static constexpr float kDeathDistanceEasing = 0.5f;
static constexpr float kDeathLiftEasing = 0.01f;
static constexpr float kDeathStartLift = 0.75f;
static constexpr float kDeathClearance = 0.25f;

// Heading and tilt of a view, in degrees, read from its up axis: heading 0
// looks along +Y, and tilt 0 looks straight down.
static float viewHeading(const glm::quat &orientation) {
    const glm::vec3 up {orientation * glm::vec3(0.0f, 1.0f, 0.0f)};
    if (up.x == 0.0f && up.y == 0.0f) {
        return 0.0f;
    }
    return glm::degrees(std::atan2(-up.x, up.y));
}

static float viewTilt(const glm::quat &orientation) {
    const glm::vec3 up {orientation * glm::vec3(0.0f, 1.0f, 0.0f)};
    return glm::degrees(std::atan2(up.z, std::sqrt(up.x * up.x + up.y * up.y)));
}

static glm::quat viewOrientation(float heading, float tilt) {
    return glm::angleAxis(glm::radians(heading), glm::vec3(0.0f, 0.0f, 1.0f)) *
           glm::angleAxis(glm::radians(tilt), glm::vec3(1.0f, 0.0f, 0.0f));
}

static float wrapRadians(float angle) {
    angle = std::fmod(angle + glm::pi<float>(), glm::two_pi<float>());
    if (angle < 0.0f) {
        angle += glm::two_pi<float>();
    }
    return angle - glm::pi<float>();
}

void ThirdPersonCamera::load() {
    auto &scene = _services.scene.graphs.get(_sceneName);
    _sceneNode = scene.newCamera();
    rebuildProjection();
}

float ThirdPersonCamera::projectionFovy() const {
    return glm::radians(_style.viewAngle);
}

bool ThirdPersonCamera::handle(const input::Event &event) {
    switch (event.type) {
    case input::EventType::KeyDown:
        return handleKeyDown(event.key);
    case input::EventType::KeyUp:
        return handleKeyUp(event.key);
    case input::EventType::MouseButtonDown:
        return handleMouseButtonDown(event.button);
    case input::EventType::MouseButtonUp:
        return handleMouseButtonUp(event.button);
    case input::EventType::MouseMotion:
        return handleMouseMotion(event.motion);
    default:
        return false;
    }
}

// The turn keys work whether or not the mouse turns the camera. Either Ctrl
// key, like the right button, turns the camera with the mouse while held.
bool ThirdPersonCamera::handleKeyDown(const input::KeyEvent &event) {
    switch (event.code) {
    case input::KeyCode::A:
        if (!event.repeat) {
            _rotateCCW = true;
            _rotateCW = false;
            _rotationSpeed = kMinRotationSpeed;
            return true;
        }
        break;
    case input::KeyCode::D:
        if (!event.repeat) {
            _rotateCCW = false;
            _rotateCW = true;
            _rotationSpeed = kMinRotationSpeed;
            return true;
        }
        break;
    case input::KeyCode::LeftControl:
    case input::KeyCode::RightControl:
        _mouseLookHeld = true;
        return true;
    default:
        break;
    }

    return false;
}

bool ThirdPersonCamera::handleKeyUp(const input::KeyEvent &event) {
    switch (event.code) {
    case input::KeyCode::A:
        _rotateCCW = false;
        return true;
    case input::KeyCode::D:
        _rotateCW = false;
        return true;
    case input::KeyCode::LeftControl:
    case input::KeyCode::RightControl:
        _mouseLookHeld = false;
        return true;
    default:
        break;
    }

    return false;
}

bool ThirdPersonCamera::handleMouseMotion(const input::MouseMotionEvent &event) {
    const bool mouseLook = isMouseLookMode();
    if (mouseLook && event.xrel != 0) {
        releaseLookAt();
    }
    // The combat camera turns once a frame by the travel gathered here.
    if (mouseLook && _combat) {
        _mouseTurn += static_cast<float>(event.xrel);
        return false;
    }
    // A held turn key wins over the mouse.
    if (mouseLook && !_rotateCW && !_rotateCCW) {
        _facing -= kMouseRotationSpeed * event.xrel;
        _facing = glm::mod(_facing, glm::two_pi<float>());
        updateSceneNode();
    }
    return false;
}

// The right button and the Ctrl keys share one hold: any press starts it and
// any release ends it.
bool ThirdPersonCamera::handleMouseButtonDown(const input::MouseButtonEvent &event) {
    if (event.button == input::MouseButton::Right) {
        _mouseLookHeld = true;
        return true;
    }
    return false;
}

bool ThirdPersonCamera::handleMouseButtonUp(const input::MouseButtonEvent &event) {
    if (event.button == input::MouseButton::Right) {
        _mouseLookHeld = false;
        return true;
    }
    return false;
}

void ThirdPersonCamera::update(float dt) {
    Camera::update(dt);

    if (_deathOrbit) {
        updateDeathOrbit(dt);
        return;
    }
    if (_combat) {
        updateCombatTurn(dt);
        return;
    }
    // The dolly distance follows the view angle every frame.
    if (_dollyZoom) {
        updateSceneNode();
    }
    if (_rotateCW || _rotateCCW) {
        releaseLookAt();
    }
    updateLookAtSwing(dt);
    if (!_rotateCW && !_rotateCCW)
        return;

    _rotationSpeed += kRotationAcceleration * dt;

    if (_rotationSpeed > kMaxRotationSpeed) {
        _rotationSpeed = kMaxRotationSpeed;
    }
    _facing += (_rotateCCW ? 1.0f : -1.0f) * _rotationSpeed * dt;
    _facing = glm::mod(_facing, glm::two_pi<float>());

    updateSceneNode();
}

float ThirdPersonCamera::integrateCombatTurn(float input, float dt) {
    return stepKeyboardTurn(_turnVelocity, _turnInput, input, dt);
}

// Held turn keys win. Otherwise a mouse look moved this frame turns by its
// travel at once and leaves that turn as a velocity to coast off; with
// neither, the turn brakes.
void ThirdPersonCamera::updateCombatTurn(float dt) {
    const float input = _rotateCCW ? 1.0f : (_rotateCW ? -1.0f : 0.0f);
    float turned;
    const float mouse = isMouseLookMode()
                            ? mouseFrameStrength(_mouseTurn) * mouseSensitivity(_game.options().game.mouse.sensitivity)
                            : 0.0f;
    _mouseTurn = 0.0f;
    if (input == 0.0f && mouse != 0.0f) {
        _turnVelocity = mouse;
        turned = mouse;
    } else {
        turned = integrateCombatTurn(input, dt);
    }
    _facing = glm::mod(_facing + glm::radians(turned), glm::two_pi<float>());
    updateSceneNode();
}

// The combat camera sits the style distance behind the target at the style
// height, pitched by the style. When the view from the target toward it is
// obstructed it is pulled in by the deepest intrusion and lifted in proportion.
void ThirdPersonCamera::updateCombatSceneNode() {
    static const glm::vec3 up {0.0f, 0.0f, 1.0f};

    const glm::vec3 dir {glm::sin(_facing), -glm::cos(_facing), 0.0f};
    const glm::vec3 eye {_eyePosition};
    glm::vec3 desired {_targetPosition + _style.distance * dir};
    desired.z += _style.height;

    auto &scene = _services.scene.graphs.get(_sceneName);
    const glm::vec3 side {glm::normalize(glm::cross(eye - desired, up))};
    float intrusion = 0.0f;
    bool obstructed = false;
    for (const auto &end : {desired + kCombatPersonalSpace * side, desired - kCombatPersonalSpace * side,
                            desired + kCombatPersonalSpace * up, desired - kCombatPersonalSpace * up}) {
        Collision collision;
        if (scene.testLineOfSight(eye, end, collision)) {
            obstructed = true;
            intrusion = std::max(intrusion, glm::distance(collision.intersection, end));
        }
    }
    glm::vec3 cameraPos {desired};
    if (obstructed) {
        const float length = glm::distance(desired, eye);
        cameraPos -= glm::normalize(desired - eye) * intrusion;
        cameraPos += up * (intrusion / length) * (kCombatPersonalSpace + kCombatLiftAllowance);
    }

    // Pitch 90 looks level; lower values look down.
    const float tilt = glm::radians(90.0f - _style.pitch);
    const glm::vec3 forward {glm::normalize(-dir * glm::cos(tilt) - up * glm::sin(tilt))};
    glm::mat4 transform {1.0f};
    transform *= glm::translate(cameraPos);
    transform *= glm::mat4_cast(glm::quatLookAt(forward, up));
    _sceneNode->setLocalTransform(std::move(transform));
}

void ThirdPersonCamera::updateSceneNode() {
    static glm::vec3 up {0.0f, 0.0f, 1.0f};

    // The death orbit alone places the camera.
    if (_deathOrbit) {
        return;
    }
    if (_combat) {
        updateCombatSceneNode();
        return;
    }

    glm::vec3 dir(
        glm::sin(_facing),
        -glm::cos(_facing),
        0.0f);

    glm::vec3 targetPos(_targetPosition);
    targetPos += kTargetPadding * dir;

    // The dolly zoom keeps the subject's size: the style distance scaled by
    // the tangent of the style's half angle over the current half angle.
    const float distance = _dollyZoom
                               ? _style.distance * std::tan(glm::radians(_style.viewAngle) * 0.5f) / std::tan(glm::radians(viewAngle()) * 0.5f)
                               : _style.distance;
    glm::vec3 cameraPos(_targetPosition);
    cameraPos += distance * dir;
    cameraPos.z += _style.height;

    Collision collision;
    auto &scene = _services.scene.graphs.get(_sceneName);
    if (scene.testLineOfSight(targetPos, cameraPos, collision)) {
        cameraPos = collision.intersection;
    }

    glm::quat orientation(glm::quatLookAt(glm::normalize(targetPos - cameraPos), up));

    glm::mat4 transform(1.0f);
    transform *= glm::translate(cameraPos);
    transform *= glm::mat4_cast(orientation);
    _sceneNode->setLocalTransform(std::move(transform));
}

void ThirdPersonCamera::stopMovement() {
    _rotateCCW = false;
    _rotateCW = false;
    _mouseLookHeld = false;
}

void ThirdPersonCamera::setTargetPosition(glm::vec3 position) {
    _targetPosition = std::move(position);
    updateSceneNode();
}

void ThirdPersonCamera::setFacing(float facing) {
    _facing = facing;
    updateSceneNode();
}

bool ThirdPersonCamera::setDollyZoom(bool enabled) {
    if (enabled && _combat) {
        return false;
    }
    _dollyZoom = enabled;
    updateSceneNode();
    return true;
}

void ThirdPersonCamera::setStyle(CameraStyle style) {
    _style = std::move(style);
    // A new style sets its own view angle.
    _viewAngleOverride.reset();
    rebuildProjection();
    updateSceneNode();
}

void ThirdPersonCamera::setLookAtTarget(const std::shared_ptr<Object> &target) {
    // The combat camera does not swing.
    if (_combat) {
        return;
    }
    _lookAtTarget = RuntimeObjectRef<Object>(target);
}

// Turning by hand lets go of the target, and the view takes the facing it was swinging to.
void ThirdPersonCamera::releaseLookAt() {
    if (!_lookAtTarget.resolve() && !_swinging) {
        return;
    }
    _lookAtTarget.reset();
    if (_swinging) {
        _facing = _desiredFacing;
        _swinging = false;
        updateSceneNode();
    }
}

// The desired facing aims 15 degrees past the target, on the side it is on,
// and the view closes on it at a rate that rises while the camera is more
// than a metre from its desired spot and falls once it is within one.
void ThirdPersonCamera::updateLookAtSwing(float dt) {
    bool swinging = false;
    if (auto target = _lookAtTarget.resolve()) {
        const glm::vec3 toTarget {target->position() - _targetPosition};
        const glm::vec2 forward {-std::sin(_desiredFacing), std::cos(_desiredFacing)};
        const float lateral = glm::dot(glm::vec2(toTarget), glm::vec2(-forward.y, forward.x));
        const float offset = lateral < 0.0f ? -kLookAtOffset : kLookAtOffset;
        if (std::abs(lateral) < kLookAtRelease) {
            _lookAtTarget.reset();
        }
        _desiredFacing = std::atan2(-toTarget.x, toTarget.y) - glm::radians(offset);
        swinging = true;
    } else {
        _lookAtTarget.reset();
    }
    if (!swinging) {
        // Without a target the view holds its desired facing.
        if (_swinging) {
            _facing = glm::mod(_desiredFacing, glm::two_pi<float>());
            updateSceneNode();
        }
        _swinging = false;
        _swingRate = 0.0f;
        _desiredFacing = _facing;
        return;
    }
    _swinging = true;
    const float delta = wrapRadians(_desiredFacing - _facing);
    const float chord = 2.0f * _style.distance * std::sin(std::abs(delta) / 2.0f);
    if (chord * chord > 1.0f) {
        _swingRate += kSwingRateRise * dt;
    }
    if (chord * chord < 1.0f) {
        _swingRate -= kSwingRateFall * dt;
    }
    _swingRate = std::min(std::max(_swingRate, kSwingRateMin), kSwingRateMax);
    _facing = glm::mod(_facing + delta * _swingRate * dt, glm::two_pi<float>());
    updateSceneNode();
}

void ThirdPersonCamera::setCombat(bool combat) {
    if (combat) {
        _lookAtTarget.reset();
        _swinging = false;
    }
    _combat = combat;
    _turnVelocity = 0.0f;
    _turnInput = 0.0f;
    _mouseTurn = 0.0f;
    updateSceneNode();
}

void ThirdPersonCamera::resetCombatTurn() {
    _turnVelocity = 0.0f;
    _turnInput = 0.0f;
    _mouseTurn = 0.0f;
}

void ThirdPersonCamera::setCombatAnchor(glm::vec3 base, glm::vec3 eye) {
    _eyePosition = std::move(eye);
    setTargetPosition(std::move(base));
}

void ThirdPersonCamera::faceAlong(const glm::vec3 &from, const glm::vec3 &to) {
    // The camera sits behind "from" and looks toward "to".
    const glm::vec2 behind {glm::vec2(from) - glm::vec2(to)};
    setFacing(glm::mod(std::atan2(behind.x, -behind.y), glm::two_pi<float>()));
}

void ThirdPersonCamera::startDeathOrbit(const std::shared_ptr<Object> &target, const glm::mat4 &view) {
    _rotateCCW = false;
    _rotateCW = false;
    _lookAtTarget.reset();
    _swinging = false;
    _deathOrbit = true;
    _deathTarget = RuntimeObjectRef<Object>(target);
    _deathPosition = glm::vec3(view[3]);
    _deathOrientation = glm::quat_cast(glm::mat3(view));
    // The orbit starts on the heading from the camera to the body.
    glm::vec3 toTarget {target->position() - _deathPosition};
    _deathYaw = toTarget.x == 0.0f && toTarget.y == 0.0f
                    ? 0.0f
                    : glm::degrees(std::atan2(-toTarget.x, toTarget.y));
    _deathLift = kDeathStartLift;
}

// Every update eases the view by a fixed share toward the orbit: half-way to
// its heading and distance, a hundredth of the way to looking straight down
// and to aiming at the body itself. The view ray from the aim point keeps a
// quarter metre off whatever it hits.
void ThirdPersonCamera::updateDeathOrbit(float dt) {
    auto target = _deathTarget.resolve();
    if (!target) {
        return;
    }

    _deathLift += (0.0f - _deathLift) * kDeathLiftEasing;
    _deathYaw += dt * kDeathOrbitRate;
    if (_deathYaw < 0.0f) {
        _deathYaw += 360.0f;
    } else if (_deathYaw >= 360.0f) {
        _deathYaw -= 360.0f;
    }

    // The current heading counts up by whole turns until the orbit's heading
    // lies within one turn above it.
    float heading = viewHeading(_deathOrientation);
    for (float next = heading + 360.0f; _deathYaw > next; next += 360.0f) {
        heading = next;
    }
    // Straight down is tilt 0; a view past it counts up by whole turns too.
    const float tiltGoal = 90.0f - kDeathOrbitTilt;
    float tilt = viewTilt(_deathOrientation);
    while (tiltGoal > tilt) {
        tilt += 360.0f;
    }

    const float newHeading = heading + (_deathYaw - heading) * kDeathYawEasing;
    const float newTilt = tilt + (tiltGoal - tilt) * kDeathTiltEasing;
    _deathOrientation = viewOrientation(newHeading, newTilt);

    const glm::vec3 body {target->position()};
    const float distance = glm::distance(body, _deathPosition);
    const float reach = distance + (kDeathOrbitDistance - distance) * kDeathDistanceEasing;
    const glm::vec3 anchor {body + glm::vec3(0.0f, 0.0f, _deathLift)};
    const glm::vec3 back {_deathOrientation * glm::vec3(0.0f, 0.0f, 1.0f)};
    glm::vec3 placed {anchor + back * reach};

    Collision collision;
    auto &scene = _services.scene.graphs.get(_sceneName);
    if (scene.testLineOfSight(anchor, placed + back * kDeathClearance, collision)) {
        placed = collision.intersection + collision.normal * kDeathClearance;
    }
    _deathPosition = placed;

    glm::mat4 transform {1.0f};
    transform *= glm::translate(placed);
    transform *= glm::mat4_cast(_deathOrientation);
    _sceneNode->setLocalTransform(std::move(transform));
}

} // namespace game

} // namespace reone
