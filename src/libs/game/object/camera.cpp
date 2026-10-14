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

#include "reone/game/object/camera.h"

#include <algorithm>

#include "reone/game/game.h"
#include "reone/graphics/types.h"
#include "reone/scene/node/camera.h"

namespace reone {

namespace game {

// A view angle animation that replaces a running one starts from this angle.
static constexpr float kViewAngleAnimationReset = 45.0f;

// Keyboard turning: the rate in degrees per second, and the acceleration and
// braking that approach it.
static constexpr float kKeyboardTurnRate = 200.0f;
static constexpr float kKeyboardTurnAcceleration = 500.0f;
static constexpr float kKeyboardTurnDeceleration = 2000.0f;

// The longest frame an accelerated rate advances over at once.
static constexpr float kAcceleratedRateMaxStep = 0.2f;

// Mouse travel in a full-strength frame.
static constexpr float kMouseFullTravel = 100.0f;

static constexpr float kOneSixth = 1.0f / 6.0f;
static constexpr float kOneThird = 1.0f / 3.0f;

// The acceleration of a velocity under an input: the acceleration gain under
// an input, the braking gain without one, each pulling toward the rate.
static float rateAcceleration(float velocity, float input, float rate, float acceleration, float deceleration) {
    float gain = input == 0.0f ? deceleration : acceleration;
    return gain * input - (gain / rate) * velocity;
}

float stepAcceleratedRate(
    float &velocity, float &previousInput, float input, float dt,
    float rate, float acceleration, float deceleration) {
    auto accelerate = [&](float v, float u) {
        return rateAcceleration(v, u, rate, acceleration, deceleration);
    };
    float h = std::min(dt, kAcceleratedRateMaxStep);
    float midInput = (previousInput + input) * 0.5f;
    float v0 = velocity;

    float dv1 = accelerate(v0, previousInput) * h;
    float dx1 = h * v0;
    float v1 = (v0 + dv1) * 0.5f;
    float dv2 = accelerate(v1, midInput) * h;
    float dx2 = v1 * h;
    float v2 = (v0 + dv2) * 0.5f;
    float dv3 = accelerate(v2, midInput) * h;
    float dx3 = v2 * h;
    float v3 = v0 + dv3;
    float dv4 = accelerate(v3, input) * h;
    float dx4 = h * v3;

    velocity = v0 + (((dv2 * kOneThird + dv1 * kOneSixth) + dv3 * kOneThird) + dv4 * kOneSixth);
    previousInput = input;
    return ((dx2 * kOneThird + dx1 * kOneSixth) + dx3 * kOneThird) + dx4 * kOneSixth;
}

float stepKeyboardTurn(float &velocity, float &previousInput, float input, float dt) {
    return stepAcceleratedRate(velocity, previousInput, input, dt,
                               kKeyboardTurnRate, kKeyboardTurnAcceleration, kKeyboardTurnDeceleration);
}

float mouseFrameStrength(float travel) {
    return std::clamp(-travel / kMouseFullTravel, -1.0f, 1.0f);
}

void Camera::update(float dt) {
    updateViewAngle(dt);
    auto &options = _game.options().graphics;
    if (_projectionWidth != options.width || _projectionHeight != options.height) {
        rebuildProjection();
    }
}

void Camera::updateViewAngle(float dt) {
    // A pause returns a held view angle at once.
    if (_viewAngleHold && _game.isPaused()) {
        releaseViewAngleHold(_viewAngleHold->id);
        return;
    }
    if (!_viewAngleAnimation) {
        return;
    }
    auto &animation = *_viewAngleAnimation;
    animation.elapsed += dt;
    // Past its end the animation is dropped without a last write.
    if (animation.elapsed > animation.duration) {
        _viewAngleAnimation.reset();
        return;
    }
    const float t = animation.elapsed / animation.duration;
    setViewAngle((1.0f - t) * animation.start + t * animation.target);
}

float Camera::viewAngle() const {
    return _viewAngleOverride.value_or(glm::degrees(projectionFovy()));
}

void Camera::setViewAngle(float degrees) {
    _viewAngleOverride = degrees;
    rebuildProjection();
}

void Camera::beginViewAngleAnimation(float target, float seconds) {
    ViewAngleAnimation animation;
    if (_viewAngleAnimation) {
        setViewAngle(kViewAngleAnimationReset);
        animation.start = kViewAngleAnimationReset;
    } else {
        animation.start = viewAngle();
    }
    animation.duration = seconds;
    animation.target = target;
    _viewAngleAnimation = animation;
}

void Camera::endViewAngleAnimation() {
    _viewAngleAnimation.reset();
}

uint32_t Camera::beginViewAngleHold() {
    ViewAngleHold hold;
    hold.id = _nextViewAngleHold++;
    hold.restoreAngle = viewAngle();
    hold.dolly = setDollyZoom(true);
    _viewAngleHold = hold;
    return hold.id;
}

void Camera::releaseViewAngleHold(uint32_t id) {
    if (!holdsViewAngle(id)) {
        return;
    }
    const auto hold = *_viewAngleHold;
    _viewAngleHold.reset();
    endViewAngleAnimation();
    // The camera's own angle needs no override.
    if (hold.restoreAngle == glm::degrees(projectionFovy())) {
        _viewAngleOverride.reset();
        rebuildProjection();
    } else {
        setViewAngle(hold.restoreAngle);
    }
    if (hold.dolly) {
        setDollyZoom(false);
    }
}

void Camera::rebuildProjection() {
    auto &options = _game.options().graphics;
    float aspect = options.width / static_cast<float>(options.height);
    const float fovy = _viewAngleOverride ? glm::radians(*_viewAngleOverride) : projectionFovy();
    cameraSceneNode()->setPerspectiveProjection(fovy, aspect, graphics::kDefaultClipPlaneNear, graphics::kDefaultClipPlaneFar);
    _projectionWidth = options.width;
    _projectionHeight = options.height;
}

} // namespace game

} // namespace reone
