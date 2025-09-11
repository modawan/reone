/*
 * Copyright (c) 2025 The reone project contributors
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

#include <gtest/gtest.h>

#include "reone/graphics/keyframetrack.h"

using namespace reone;
using namespace reone::graphics;

TEST(KeyframeTrack, quat) {
    KeyframeTrack<glm::quat> track;
    glm::quat q0(1.0f, 0.0f, 0.0f, 0.0f);
    glm::quat q1(-1.0f, 0.5f, 0.0f, 0.0f);

    track.add(0.0f, q0);
    track.add(1.0f, q1);

    glm::quat result;
    bool found = track.valueAtTime(0.5f, result);
    ASSERT_TRUE(found);
    ASSERT_TRUE(glm::all(glm::equal(result, glm::slerp(q0, q1, 0.5f))));
}

TEST(KeyframeTrack, float) {
    KeyframeTrack<glm::vec3> track;
    glm::vec3 v0(0.0f);
    glm::vec3 v1(4.0f);

    track.add(0.0f, v0);
    track.add(1.0f, v1);

    glm::vec3 result;
    bool found = track.valueAtTime(0.5f, result);
    ASSERT_TRUE(found);
    ASSERT_TRUE(glm::all(glm::equal(result, glm::mix(v0, v1, 0.5f))));
}
