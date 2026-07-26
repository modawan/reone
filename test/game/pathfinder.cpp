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

#include <array>
#include <gtest/gtest.h>

#include "reone/game/pathfinder.h"

using namespace reone;
using namespace reone::game;

void checkAdjecent(const Uniface &face, std::array<uint32_t, 3> ref) {
    for (uint32_t i = 0; i < 3; ++i) {
        EXPECT_EQ(face.adjecent[i], ref[i]);
    }
}

TEST(Pathfinder, should_find_shortest_path) {
    // Build a square subdivided into 8 triangles.
    Pathfinder pf;

    pf.uni.vertices = {
        // Center.
        {1.0f, 1.0f, 0.0f}, // 0
        // Vertices in clockwise order starting from the bottom left.
        {0.0f, 0.0f, 0.0f}, // 1
        {0.0f, 1.0f, 0.0f}, // 2
        {0.0f, 2.0f, 0.0f}, // 3
        {1.0f, 2.0f, 0.0f}, // 4
        {2.0f, 2.0f, 0.0f}, // 5
        {2.0f, 1.0f, 0.0f}, // 6
        {2.0f, 0.0f, 0.0f}, // 7
        {1.0f, 0.0f, 0.0f}, // 8
    };

    // Faces in clockwise order starting from the bottom left.
    pf.uni.faces = {
        {{0, 1, 2}}, // 0
        {{0, 2, 3}}, // 1
        {{0, 3, 4}}, // 2
        {{0, 4, 5}}, // 3
        {{0, 5, 6}}, // 4
        {{0, 6, 7}}, // 5
        {{0, 7, 8}}, // 6
        {{0, 8, 1}}, // 7
    };

    // Single room for all faces.
    pf.uni.rooms = {{0, 8, {0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 0.0f}}};

    // Build adjecency lists.
    for (Uniface &face : pf.uni.faces) {
        for (uint32_t i = 0; i < 3; ++i) {
            face.adjecent[i] = UINT32_MAX;
        }
    }
    uniwalkFinalize(pf.uni);

    checkAdjecent(pf.uni.faces[0], {7, UINT32_MAX, 1});
    checkAdjecent(pf.uni.faces[1], {0, UINT32_MAX, 2});
    checkAdjecent(pf.uni.faces[2], {1, UINT32_MAX, 3});
    checkAdjecent(pf.uni.faces[3], {2, UINT32_MAX, 4});
    checkAdjecent(pf.uni.faces[4], {3, UINT32_MAX, 5});
    checkAdjecent(pf.uni.faces[5], {4, UINT32_MAX, 6});
    checkAdjecent(pf.uni.faces[6], {5, UINT32_MAX, 7});
    checkAdjecent(pf.uni.faces[7], {6, UINT32_MAX, 0});

    pf.paths.resize(1);

    // Find a path from face 0 to face 5.
    glm::vec3 current = {0.2f, 0.8f, 0.0f};
    glm::vec3 dest = {1.8f, 0.8f, 0.0f};
    std::optional<Path> path = createPath(pf, current, dest);
    EXPECT_TRUE(path);

    // The funnel algorithm should determine that there is a straight line from
    // current to dest.
    glm::vec3 v0 = getNextPathPoint(pf, *path);
    EXPECT_EQ(v0, dest);

    // The actual path is a sequence of faces.
    AStarPath &astarPath = pf.paths[path->index];
    std::vector<uint32_t> expectedPath = {
        0, 7, 6, 5};
    EXPECT_EQ(astarPath.faces, expectedPath);
}
