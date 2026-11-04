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

#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>

#include "reone/game/screenpoint.h"

using namespace reone::game;

namespace {

// A 90-degree camera at the origin looking down -Y, Z up, into 800x600.
struct Camera {
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 projection = glm::perspective(glm::radians(90.0f), 800.0f / 600.0f, 0.1f, 100.0f);
};

} // namespace

TEST(WorldToWindowPoint, a_point_straight_ahead_is_the_window_centre) {
    Camera camera;
    auto point = worldToWindowPoint({0.0f, -10.0f, 0.0f}, camera.view, camera.projection, 800, 600);
    ASSERT_TRUE(point.has_value());
    EXPECT_EQ(*point, glm::ivec2(400, 300));
}

TEST(WorldToWindowPoint, up_in_the_world_is_up_on_screen) {
    Camera camera;
    // Mouse coordinates grow downwards, so a point above the view axis has a
    // smaller window y than the centre.
    auto point = worldToWindowPoint({0.0f, -10.0f, 2.0f}, camera.view, camera.projection, 800, 600);
    ASSERT_TRUE(point.has_value());
    EXPECT_EQ(point->x, 400);
    EXPECT_LT(point->y, 300);
}

TEST(WorldToWindowPoint, a_point_behind_the_camera_has_none) {
    Camera camera;
    EXPECT_FALSE(worldToWindowPoint({0.0f, 10.0f, 0.0f}, camera.view, camera.projection, 800, 600).has_value());
}

TEST(WorldToWindowPoint, a_point_beyond_the_far_plane_has_none) {
    Camera camera;
    EXPECT_FALSE(worldToWindowPoint({0.0f, -200.0f, 0.0f}, camera.view, camera.projection, 800, 600).has_value());
}

TEST(BoxWindowPoints, should_spread_over_the_box_nearest_the_centre_first) {
    Camera camera;
    // A 2x2x2 box ten metres ahead.
    auto points = boxWindowPoints(glm::vec3(-1.0f), glm::vec3(1.0f),
                                  glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -10.0f, 0.0f)),
                                  camera.view, camera.projection, 800, 600);
    ASSERT_EQ(points.size(), 25u);
    // The middle of the grid is the window centre.
    EXPECT_NEAR(points.front().x, 400, 1);
    EXPECT_NEAR(points.front().y, 300, 1);
    for (size_t i = 1; i < points.size(); ++i) {
        auto previous = glm::length(glm::vec2(points[i - 1]) - glm::vec2(400, 300));
        auto current = glm::length(glm::vec2(points[i]) - glm::vec2(400, 300));
        EXPECT_LE(previous, current + 1.0f) << i;
    }
    // Every point lies within the box's projection, here within 100 pixels
    // of the centre for a box this size and distance.
    for (const auto &point : points) {
        EXPECT_LT(std::abs(point.x - 400), 100);
        EXPECT_LT(std::abs(point.y - 300), 100);
    }
}

TEST(BoxWindowPoints, a_box_behind_the_camera_has_none) {
    Camera camera;
    auto points = boxWindowPoints(glm::vec3(-1.0f), glm::vec3(1.0f),
                                  glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 10.0f, 0.0f)),
                                  camera.view, camera.projection, 800, 600);
    EXPECT_TRUE(points.empty());
}

TEST(WorldToWindowPoint, a_point_outside_the_view_has_none) {
    Camera camera;
    EXPECT_FALSE(worldToWindowPoint({50.0f, -10.0f, 0.0f}, camera.view, camera.projection, 800, 600).has_value());
}
