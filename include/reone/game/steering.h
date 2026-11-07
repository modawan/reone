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

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace reone {

namespace game {

/** What a player at the keys does this frame to walk toward a point. */
struct SteeringKeys {
    /** +1 holds A (camera turns counter-clockwise), -1 holds D, 0 neither. */
    int turn {0};
    /** Holds W: the player runs where the camera faces. */
    bool forward {false};
    /** Close enough: nothing to hold. */
    bool arrived {false};
};

/**
 * Steering toward \p target from \p position with the camera facing
 * \p cameraFacing (radians). W runs along (-sin f, cos f) for camera facing
 * f, so the facing that runs straight at an offset d is atan2(-d.x, d.y).
 * The player turns while more than \p turnTolerance off that facing, and
 * runs while within \p runTolerance of it, so a slight error is corrected on
 * the move rather than by stopping.
 */
inline SteeringKeys steerToward(
    const glm::vec2 &position,
    const glm::vec2 &target,
    float cameraFacing,
    float arriveDistance = 1.0f,
    float turnTolerance = 0.1f,
    float runTolerance = 0.6f) {

    SteeringKeys keys;
    glm::vec2 offset = target - position;
    if (glm::length(offset) < arriveDistance) {
        keys.arrived = true;
        return keys;
    }
    float wanted = glm::atan(-offset.x, offset.y);
    float delta = wanted - cameraFacing;
    // The shorter way round.
    while (delta > glm::pi<float>()) {
        delta -= glm::two_pi<float>();
    }
    while (delta <= -glm::pi<float>()) {
        delta += glm::two_pi<float>();
    }
    if (delta > turnTolerance) {
        keys.turn = 1;
    } else if (delta < -turnTolerance) {
        keys.turn = -1;
    }
    keys.forward = glm::abs(delta) < runTolerance;
    return keys;
}

} // namespace game

} // namespace reone
