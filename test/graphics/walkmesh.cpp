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

#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

#include "reone/graphics/walkmesh.h"

using namespace reone;
using namespace reone::graphics;

TEST(Walkmesh, should_find_ray_walkmesh_intersection__intersection_from_close) {
    // given
    auto walkmesh = Walkmesh();
    walkmesh.vertices = {
        glm::vec3(-1.0f, 0.0f, 0.0f),
        glm::vec3(-1.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(-1.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
    };
    walkmesh.normals = {
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
    };
    walkmesh.faces = {
        {0, 1, 2},
        {3, 4, 5},
        {6, 7, 8},
        {9, 10, 11},
    };
    walkmesh.materials = {0, 0, 0, 0};

    auto rootAabb = std::make_shared<Walkmesh::AABB>();
    rootAabb->value = AABB(glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f));
    rootAabb->left = std::make_shared<Walkmesh::AABB>();
    rootAabb->left->value = AABB(glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    rootAabb->left->left = std::make_shared<Walkmesh::AABB>();
    rootAabb->left->left->faceIdx = 0;
    rootAabb->left->right = std::make_shared<Walkmesh::AABB>();
    rootAabb->left->right->faceIdx = 1;
    rootAabb->right = std::make_shared<Walkmesh::AABB>();
    rootAabb->right->value = AABB(glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    rootAabb->right->left = std::make_shared<Walkmesh::AABB>();
    rootAabb->right->left->faceIdx = 2;
    rootAabb->right->right = std::make_shared<Walkmesh::AABB>();
    rootAabb->right->right->faceIdx = 3;
    walkmesh.setRootAABB(rootAabb);

    // when
    auto raycast = walkmesh.raycast(std::set<uint32_t> {0}, glm::vec3(-0.5f, 0.25, 1.0f), glm::vec3(0.0f, 0.0f, -1.0f), 10.0f, /*ignoreBackface=*/false);

    // then
    EXPECT_EQ(RAYCAST_OK, raycast.fail);
    EXPECT_EQ(0, raycast.face);
    EXPECT_NEAR(1.0f, raycast.distance, 1e-5);
}

TEST(Walkmesh, should_find_ray_walkmesh_intersection__intersection_from_far) {
    // given
    auto walkmesh = Walkmesh();
    walkmesh.vertices = {
        glm::vec3(-1.0f, 0.0f, 0.0f),
        glm::vec3(-1.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(-1.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
    };
    walkmesh.normals = {
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
    };
    walkmesh.faces = {
        {0, 1, 2},
        {3, 4, 5},
        {6, 7, 8},
        {9, 10, 11},
    };
    walkmesh.materials = {0, 0, 0, 0};

    auto rootAabb = std::make_shared<Walkmesh::AABB>();
    rootAabb->value = AABB(glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f));
    rootAabb->left = std::make_shared<Walkmesh::AABB>();
    rootAabb->left->value = AABB(glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    rootAabb->left->left = std::make_shared<Walkmesh::AABB>();
    rootAabb->left->left->faceIdx = 0;
    rootAabb->left->right = std::make_shared<Walkmesh::AABB>();
    rootAabb->left->right->faceIdx = 1;
    rootAabb->right = std::make_shared<Walkmesh::AABB>();
    rootAabb->right->value = AABB(glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    rootAabb->right->left = std::make_shared<Walkmesh::AABB>();
    rootAabb->right->left->faceIdx = 2;
    rootAabb->right->right = std::make_shared<Walkmesh::AABB>();
    rootAabb->right->right->faceIdx = 3;
    walkmesh.setRootAABB(rootAabb);

    // when
    auto raycast = walkmesh.raycast(std::set<uint32_t> {0}, glm::vec3(-0.5f, 0.25, 20.0f), glm::vec3(0.0f, 0.0f, -1.0f), 10.0f, /*ignoreBackface=*/false);

    // then
    EXPECT_EQ(RAYCAST_NO_INTERSECTION, raycast.fail);
}

TEST(Walkmesh, should_find_ray_walkmesh_intersection__no_intersection) {
    // given
    auto walkmesh = Walkmesh();
    walkmesh.vertices = {
        glm::vec3(-1.0f, 0.0f, 0.0f),
        glm::vec3(-1.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(-1.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
    };
    walkmesh.normals = {
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
    };
    walkmesh.faces = {
        {0, 1, 2},
        {3, 4, 5},
        {6, 7, 8},
        {9, 10, 11},
    };
    walkmesh.materials = {0, 0, 0, 0};

    auto rootAabb = std::make_shared<Walkmesh::AABB>();
    rootAabb->value = AABB(glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f));
    rootAabb->left = std::make_shared<Walkmesh::AABB>();
    rootAabb->left->value = AABB(glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    rootAabb->left->left = std::make_shared<Walkmesh::AABB>();
    rootAabb->left->left->faceIdx = 0;
    rootAabb->left->right = std::make_shared<Walkmesh::AABB>();
    rootAabb->left->right->faceIdx = 1;
    rootAabb->right = std::make_shared<Walkmesh::AABB>();
    rootAabb->right->value = AABB(glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    rootAabb->right->left = std::make_shared<Walkmesh::AABB>();
    rootAabb->right->left->faceIdx = 2;
    rootAabb->right->right = std::make_shared<Walkmesh::AABB>();
    rootAabb->right->right->faceIdx = 3;
    walkmesh.setRootAABB(rootAabb);

    // when
    auto raycast = walkmesh.raycast(std::set<uint32_t> {0}, glm::vec3(-0.5f, 0.25, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f), 10.0f, /*ignoreBackface=*/false);

    // then
    EXPECT_EQ(RAYCAST_NO_INTERSECTION, raycast.fail);
}

TEST(Walkmesh, should_find_ray_walkmesh_intersection__ignore_backface) {
    // given
    auto walkmesh = Walkmesh();
    walkmesh.vertices = {
        // Bottom triangle (facing up)
        glm::vec3(-1.0f, -1.0f, 0.0f),
        glm::vec3(1.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        // Top triangle (facing down)
        glm::vec3(-1.0f, -1.0f, 1.0f),
        glm::vec3(1.0f, -1.0f, 1.0f),
        glm::vec3(0.0f, 1.0f, 1.0f),
    };
    walkmesh.normals = {
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 0.0f, -1.0f)};
    walkmesh.faces = {
        {0, 1, 2},
        {3, 4, 5},
    };
    walkmesh.materials = {0, 0};

    // Root AABB covers both triangles.
    auto rootAabb = std::make_shared<Walkmesh::AABB>();
    rootAabb->value = AABB(glm::vec3(-1.0f, -1.0f, -0.2f), glm::vec3(1.0f, 1.0f, 1.2f));

    // Bottom AABB covers the bottom triangle.
    rootAabb->right = std::make_shared<Walkmesh::AABB>();
    rootAabb->right->value = AABB(glm::vec3(-1.0f, -1.0f, -0.2f), glm::vec3(1.0f, 1.0f, 0.2f));
    rootAabb->right->faceIdx = 0;

    // Top AABB covers the top triangle.
    rootAabb->left = std::make_shared<Walkmesh::AABB>();
    rootAabb->left->value = AABB(glm::vec3(-1.0f, -1.0f, 0.8f), glm::vec3(1.0f, 1.0f, 1.2f));
    rootAabb->left->faceIdx = 1;

    walkmesh.setRootAABB(rootAabb);

    // When ignoreBackface is false, we should hit the top face first
    // regardless of its orientation (normal is up or down).
    auto raycast = walkmesh.raycast(std::set<uint32_t> {0}, glm::vec3(0.0f, 0.0f, 2.0f), glm::vec3(0.0f, 0.0f, -1.0f), 10.0f, /*ignoreBackface=*/false);
    EXPECT_EQ(RAYCAST_OK, raycast.fail);
    EXPECT_EQ(1, raycast.face);
    EXPECT_NEAR(1.0f, raycast.distance, 1e-5);

    // When ignoreBackface is true, we should ignore the top face and
    // hit the bottom face.
    raycast = walkmesh.raycast(std::set<uint32_t> {0}, glm::vec3(0.0f, 0.0f, 2.0f), glm::vec3(0.0f, 0.0f, -1.0f), 10.0f, /*ignoreBackface=*/true);
    EXPECT_EQ(RAYCAST_OK, raycast.fail);
    EXPECT_EQ(0, raycast.face);
    EXPECT_NEAR(2.0f, raycast.distance, 1e-5);
}

TEST(Walkmesh, should_find_ray_walkmesh_intersection__multi_level) {
    // given
    auto walkmesh = Walkmesh();

    walkmesh.vertices = {
        // Bottom triangle (facing up)
        glm::vec3(-1.0f, -1.0f, 0.0f),
        glm::vec3(1.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        // Top triangle (facing up)
        glm::vec3(-1.0f, -1.0f, 1.0f),
        glm::vec3(1.0f, -1.0f, 1.0f),
        glm::vec3(0.0f, 1.0f, 1.0f),
    };
    walkmesh.normals = {
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 0.0f, 1.0f)};
    walkmesh.faces = {
        {0, 1, 2},
        {3, 4, 5},
    };
    walkmesh.materials = {0, 0};

    auto rootAabb = std::make_shared<Walkmesh::AABB>();
    rootAabb->value = AABB(glm::vec3(-1.0f, -1.0f, -0.2f), glm::vec3(1.0f, 1.0f, 1.2f));

    // Bottom AABB
    rootAabb->right = std::make_shared<Walkmesh::AABB>();
    rootAabb->right->value = AABB(glm::vec3(-1.0f, -1.0f, -0.2f), glm::vec3(1.0f, 1.0f, 0.2f));
    rootAabb->right->faceIdx = 0;

    // Top AABB
    rootAabb->left = std::make_shared<Walkmesh::AABB>();
    rootAabb->left->value = AABB(glm::vec3(-1.0f, -1.0f, 0.8f), glm::vec3(1.0f, 1.0f, 1.2f));
    rootAabb->left->faceIdx = 1;

    walkmesh.setRootAABB(rootAabb);

    // With both triangles are facing up, we should pick the one
    // closer to the origin.
    auto raycast = walkmesh.raycast(std::set<uint32_t> {0}, glm::vec3(0.0f, 0.0f, 1.1f), glm::vec3(0.0f, 0.0f, -1.0f), 10.0f, /*ignoreBackface=*/true);
    EXPECT_EQ(RAYCAST_OK, raycast.fail);
    EXPECT_EQ(1, raycast.face);
    EXPECT_NEAR(0.1f, raycast.distance, 1e-5);

    // Now move the origin closer to the bottom triangle. Raycast
    // should pick it instead of the top triangle.
    raycast = walkmesh.raycast(std::set<uint32_t> {0}, glm::vec3(0.0f, 0.0f, 0.2f), glm::vec3(0.0f, 0.0f, -1.0f), 10.0f, /*ignoreBackface=*/true);
    EXPECT_EQ(RAYCAST_OK, raycast.fail);
    EXPECT_EQ(0, raycast.face);
    EXPECT_NEAR(0.2f, raycast.distance, 1e-5);
}

namespace {

constexpr uint32_t kFloor = 0;
constexpr uint32_t kWall = 1;

const std::set<uint32_t> kFloors {kFloor};
const std::set<uint32_t> kWalls {kWall};
const std::set<uint32_t> kFloorsAndWalls {kFloor, kWall};

void addTriangle(Walkmesh &walkmesh, const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, uint32_t material) {
    const auto first = static_cast<uint32_t>(walkmesh.vertices.size());
    walkmesh.vertices.insert(walkmesh.vertices.end(), {a, b, c});
    walkmesh.faces.push_back({first, first + 1, first + 2});
    walkmesh.normals.push_back(glm::normalize(glm::cross(b - a, c - a)));
    walkmesh.materials.push_back(material);
}

// A level floor over the box from (x0, y0) to (x1, y1) at height z, as two
// faces facing up, the second sharing the first's diagonal.
void addFloor(Walkmesh &walkmesh, float x0, float y0, float x1, float y1, float z) {
    addTriangle(walkmesh, glm::vec3(x0, y0, z), glm::vec3(x1, y0, z), glm::vec3(x1, y1, z), kFloor);
    addTriangle(walkmesh, glm::vec3(x0, y0, z), glm::vec3(x1, y1, z), glm::vec3(x0, y1, z), kFloor);
}

// An upright wall at x from y0 to y1, between heights z0 and z1.
void addWall(Walkmesh &walkmesh, float x, float y0, float y1, float z0, float z1) {
    addTriangle(walkmesh, glm::vec3(x, y0, z0), glm::vec3(x, y1, z0), glm::vec3(x, y1, z1), kWall);
    addTriangle(walkmesh, glm::vec3(x, y0, z0), glm::vec3(x, y1, z1), glm::vec3(x, y0, z1), kWall);
}

// Links the two faces of each floor addFloor made, from the given face on,
// across their shared diagonal: the first face's third edge and the second's
// first.
void linkFloor(Walkmesh &walkmesh, int first) {
    walkmesh.adjacency.resize(walkmesh.faces.size() * 3, -1);
    walkmesh.adjacency[3 * first + 2] = 3 * (first + 1);
    walkmesh.adjacency[3 * (first + 1)] = 3 * first + 2;
}

bool hasFloorOverFloor(const Walkmesh &walkmesh) {
    return walkmesh.hasFloorOverFloor(walkmesh.floorNormals(kFloors));
}

} // namespace

TEST(Walkmesh, floors_meeting_only_at_a_wall_do_not_lie_one_over_the_other) {
    // given: a floor, a wall up from its edge and a floor on from the top of
    // the wall
    Walkmesh walkmesh;
    addFloor(walkmesh, 0.0f, 0.0f, 2.0f, 2.0f, 0.0f);
    addWall(walkmesh, 2.0f, 0.0f, 2.0f, 0.0f, 3.0f);
    addFloor(walkmesh, 2.0f, 0.0f, 4.0f, 2.0f, 3.0f);

    // when, then
    EXPECT_FALSE(hasFloorOverFloor(walkmesh));
}

TEST(Walkmesh, floors_overlapping_in_a_sliver_do_not_lie_one_over_the_other) {
    // given: a floor 3 over another, the two overlapping by half a centimetre
    Walkmesh walkmesh;
    addFloor(walkmesh, 0.0f, 0.0f, 2.0f, 2.0f, 0.0f);
    addFloor(walkmesh, 1.995f, 0.0f, 4.0f, 2.0f, 3.0f);

    // when, then
    EXPECT_FALSE(hasFloorOverFloor(walkmesh));
}

TEST(Walkmesh, a_floor_within_headroom_of_another_does_not_lie_over_it) {
    // given: a floor half a unit over another, over the same ground
    Walkmesh walkmesh;
    addFloor(walkmesh, 0.0f, 0.0f, 2.0f, 2.0f, 0.0f);
    addFloor(walkmesh, 0.0f, 0.0f, 2.0f, 2.0f, 0.5f);

    // when, then
    EXPECT_FALSE(hasFloorOverFloor(walkmesh));
}

TEST(Walkmesh, a_second_floor_lies_over_the_first) {
    // given: a floor 3 over another, over the same ground
    Walkmesh walkmesh;
    addFloor(walkmesh, 0.0f, 0.0f, 2.0f, 2.0f, 0.0f);
    addFloor(walkmesh, 0.5f, 0.5f, 1.5f, 1.5f, 3.0f);

    // when, then
    EXPECT_TRUE(hasFloorOverFloor(walkmesh));
}

TEST(Walkmesh, a_floor_of_another_walkmesh_lies_over_one_of_this) {
    // given: two walkmeshes with a floor each at their own ground, the second
    // placed 3 up
    Walkmesh lower;
    addFloor(lower, 0.0f, 0.0f, 2.0f, 2.0f, 0.0f);
    Walkmesh upper;
    addFloor(upper, 0.0f, 0.0f, 2.0f, 2.0f, 0.0f);
    const auto lowerNormals = lower.floorNormals(kFloors);
    const auto upperNormals = upper.floorNormals(kFloors);

    // when, then
    EXPECT_TRUE(lower.hasFloorOver(lowerNormals, upper, upperNormals, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 3.0f))));
    EXPECT_FALSE(lower.hasFloorOver(lowerNormals, upper, upperNormals, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.5f))));
}

TEST(Walkmesh, a_sweep_meets_the_nearer_of_two_walls) {
    // given: walls across the walk at 4 and, nearer, at 2
    Walkmesh walkmesh;
    addWall(walkmesh, 4.0f, -1.0f, 1.0f, 0.0f, 2.0f);
    addWall(walkmesh, 2.0f, -1.0f, 1.0f, 0.0f, 2.0f);

    // when
    const auto contact = walkmesh.sweep(kWalls, glm::vec3(0.0f), glm::vec3(6.0f, 0.0f, 0.0f), 0.5f, 0.0f, 2.0f);

    // then: the walker stops its radius, and a little more, short of the
    // nearer wall
    ASSERT_TRUE(contact.has_value());
    EXPECT_EQ(Walkmesh::ContactKind::Edge, contact->kind);
    EXPECT_NEAR(1.47f, contact->distance, 1e-4f);
    EXPECT_NEAR(1.0f, std::abs(contact->normal.x), 1e-5f);
}

TEST(Walkmesh, a_sweep_passes_under_a_wall_above_its_heights) {
    // given: a wall at 2 standing from 3 up, and one at 4 from the ground
    Walkmesh walkmesh;
    addWall(walkmesh, 2.0f, -1.0f, 1.0f, 3.0f, 5.0f);
    addWall(walkmesh, 4.0f, -1.0f, 1.0f, 0.0f, 2.0f);

    // when
    const auto contact = walkmesh.sweep(kWalls, glm::vec3(0.0f), glm::vec3(6.0f, 0.0f, 0.0f), 0.5f, 0.0f, 2.0f);

    // then
    ASSERT_TRUE(contact.has_value());
    EXPECT_NEAR(3.47f, contact->distance, 1e-4f);
}

TEST(Walkmesh, a_sweep_wide_of_a_wall_meets_nothing) {
    // given: a wall at 2 standing beside the walk
    Walkmesh walkmesh;
    addWall(walkmesh, 2.0f, 1.0f, 3.0f, 0.0f, 2.0f);

    // when, then
    EXPECT_FALSE(walkmesh.sweep(kWalls, glm::vec3(0.0f), glm::vec3(6.0f, 0.0f, 0.0f), 0.5f, 0.0f, 2.0f).has_value());
}

TEST(Walkmesh, a_point_under_a_second_floor_stands_on_the_first) {
    // given: a floor 3 over another
    Walkmesh walkmesh;
    addFloor(walkmesh, 0.0f, 0.0f, 2.0f, 2.0f, 0.0f);
    addFloor(walkmesh, 0.0f, 0.0f, 2.0f, 2.0f, 3.0f);

    // when
    const auto onLower = walkmesh.standingFace(kFloors, kFloors, glm::vec3(1.5f, 0.5f, 0.0f), 1000.0f, 2000.0f, /*stacked=*/true);
    const auto onUpper = walkmesh.standingFace(kFloors, kFloors, glm::vec3(1.5f, 0.5f, 3.0f), 1000.0f, 2000.0f, /*stacked=*/true);
    const auto unstacked = walkmesh.standingFace(kFloors, kFloors, glm::vec3(1.5f, 0.5f, 0.0f), 1000.0f, 2000.0f, /*stacked=*/false);

    // then: the lower floor's first face, the upper's first, and the first
    // face met from above on a walkmesh not taken as stacked
    ASSERT_TRUE(onLower.has_value());
    EXPECT_EQ(0u, onLower->face);
    EXPECT_NEAR(0.0f, onLower->height, 1e-5f);
    ASSERT_TRUE(onUpper.has_value());
    EXPECT_EQ(2u, onUpper->face);
    EXPECT_NEAR(3.0f, onUpper->height, 1e-5f);
    ASSERT_TRUE(unstacked.has_value());
    EXPECT_EQ(2u, unstacked->face);
}

TEST(Walkmesh, a_walk_across_faces_leaves_them_at_their_edge) {
    // given: a floor of two faces linked across their diagonal
    Walkmesh walkmesh;
    addFloor(walkmesh, 0.0f, 0.0f, 2.0f, 2.0f, 0.0f);
    linkFloor(walkmesh, 0);

    // when
    const auto inside = walkmesh.walkFaces(kFloors, kFloors, glm::vec3(0.5f, 1.0f, 0.0f), glm::vec3(1.5f, 1.0f, 0.0f), /*stacked=*/false);
    const auto out = walkmesh.walkFaces(kFloors, kFloors, glm::vec3(0.5f, 1.0f, 0.0f), glm::vec3(3.0f, 1.0f, 0.0f), /*stacked=*/false);

    // then: a walk to a point on the floor reaches it, and one past the floor
    // leaves it at x = 2, three fifths of the way
    ASSERT_TRUE(inside.has_value());
    EXPECT_FALSE(inside->exit.has_value());
    ASSERT_TRUE(out.has_value());
    ASSERT_TRUE(out->exit.has_value());
    EXPECT_NEAR(0.6f, out->exit->fraction, 1e-5f);
    EXPECT_NEAR(-1.0f, out->exit->normal.x, 1e-5f);
}

TEST(Walkmesh, a_walk_under_a_second_floor_keeps_to_the_first) {
    // given: a floor 3 over another, each of two linked faces
    Walkmesh walkmesh;
    addFloor(walkmesh, 0.0f, 0.0f, 2.0f, 2.0f, 0.0f);
    addFloor(walkmesh, 0.0f, 0.0f, 2.0f, 2.0f, 3.0f);
    linkFloor(walkmesh, 0);
    linkFloor(walkmesh, 2);

    // when
    const auto below = walkmesh.walkFaces(kFloors, kFloors, glm::vec3(0.5f, 1.0f, 0.0f), glm::vec3(1.5f, 1.0f, 0.0f), /*stacked=*/true);
    const auto above = walkmesh.walkFaces(kFloors, kFloors, glm::vec3(0.5f, 1.0f, 3.0f), glm::vec3(1.5f, 1.0f, 3.0f), /*stacked=*/true);

    // then
    ASSERT_TRUE(below.has_value());
    EXPECT_NEAR(0.0f, below->band.low, 1e-5f);
    EXPECT_NEAR(0.0f, below->band.high, 1e-5f);
    ASSERT_TRUE(above.has_value());
    EXPECT_NEAR(3.0f, above->band.low, 1e-5f);
    EXPECT_NEAR(3.0f, above->band.high, 1e-5f);
}
