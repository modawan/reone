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

namespace reone {

namespace game {

struct CameraStyle {
    std::string name;
    float distance {0.0f};
    float pitch {0.0f};
    float height {0.0f};
    float viewAngle {0.0f};
    /** How far the exploration view may tilt up, in degrees. */
    float tiltUp {0.0f};
    /** How far the exploration view may tilt down, in degrees. */
    float tiltDown {0.0f};
    /** The exploration view's tilt speed. */
    float tiltSpeed {0.0f};
    /** Free-look tilt speed, in degrees per second. */
    float freeLookTiltSpeed {60.0f};
    /** Free-look turn speed, in degrees per second. */
    float freeLookRotateSpeed {60.0f};
    /** How far free-look may pitch up, in degrees. */
    float freeLookUp {0.0f};
    /** How far free-look may pitch down, in degrees. */
    float freeLookDown {0.0f};
};

} // namespace game

} // namespace reone
