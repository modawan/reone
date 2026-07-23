/*
 * Copyright (c) 2026 The reone project contributors
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

#include "reone/scene/particleutil.h"

using namespace reone::scene;

TEST(ParticleUtil, should_transform_motion_blur_velocity_to_world_space) {
    // The tar_m05aa waterfall emitters rotate local +Z to world -Z.
    auto emitterTransform = glm::rotate(glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));

    auto basis = particleutil::buildMotionBlurBasis(
        emitterTransform,
        glm::vec3(0.0f, 0.0f, 10.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        1.0f,
        1.0f);

    EXPECT_NEAR(1.0f, basis.right.x, 1e-5f);
    EXPECT_NEAR(0.0f, basis.right.y, 1e-5f);
    EXPECT_NEAR(0.0f, basis.right.z, 1e-5f);
    EXPECT_NEAR(0.0f, basis.up.x, 1e-5f);
    EXPECT_NEAR(0.0f, basis.up.y, 1e-5f);
    EXPECT_NEAR(-1.0f, basis.up.z, 1e-5f);
    EXPECT_NEAR(3.5f, basis.lengthScale, 1e-5f);
}

TEST(ParticleUtil, should_add_motion_trail_length_in_world_units) {
    auto ion = particleutil::buildMotionBlurBasis(
        glm::mat4(1.0f),
        glm::vec3(0.0f, 0.0f, 15.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        3.0f,
        1.0f);
    auto thermal = particleutil::buildMotionBlurBasis(
        glm::mat4(1.0f),
        glm::vec3(0.0f, 0.0f, 20.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        4.0f,
        1.0f);

    EXPECT_NEAR(6.75f, 3.0f * ion.lengthScale, 1e-5f);
    EXPECT_NEAR(9.0f, 4.0f * thermal.lengthScale, 1e-5f);
}

TEST(ParticleUtil, should_fall_back_to_camera_axes_without_motion) {
    auto basis = particleutil::buildMotionBlurBasis(
        glm::mat4(1.0f),
        glm::vec3(0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        3.0f,
        1.0f);

    EXPECT_EQ(glm::vec3(1.0f, 0.0f, 0.0f), basis.right);
    EXPECT_EQ(glm::vec3(0.0f, 1.0f, 0.0f), basis.up);
    EXPECT_EQ(1.0f, basis.lengthScale);
}

TEST(ParticleUtil, should_fall_back_to_camera_axes_for_camera_depth_motion) {
    auto basis = particleutil::buildMotionBlurBasis(
        glm::mat4(1.0f),
        glm::vec3(0.0f, 0.0f, 10.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        1.0f,
        10.0f);

    EXPECT_EQ(glm::vec3(1.0f, 0.0f, 0.0f), basis.right);
    EXPECT_EQ(glm::vec3(0.0f, 1.0f, 0.0f), basis.up);
    EXPECT_EQ(1.0f, basis.lengthScale);
}
