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

#include "reone/graphics/walkmesh.h"

using namespace reone;
using namespace reone::graphics;

TEST(Walkmesh, should_find_ray_walkmesh_intersection__intersection_from_close) {
    // given
    auto walkmesh = Walkmesh();
    walkmesh.add(Walkmesh::Face {0, 0, std::vector<glm::vec3> {glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(-1.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f)}, glm::vec3(1.0f, 0.0f, 0.0f)});
    walkmesh.add(Walkmesh::Face {1, 0, std::vector<glm::vec3> {glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(-1.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f)}, glm::vec3(1.0f, 0.0f, 0.0f)});
    walkmesh.add(Walkmesh::Face {2, 0, std::vector<glm::vec3> {glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f, -1.0f, 0.0f)}, glm::vec3(1.0f, 0.0f, 0.0f)});
    walkmesh.add(Walkmesh::Face {3, 0, std::vector<glm::vec3> {glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, -1.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)}, glm::vec3(1.0f, 0.0f, 0.0f)});
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
    float distance = -1.0f;
    auto face = walkmesh.raycast(std::set<uint32_t> {0}, glm::vec3(-0.5f, 0.25, 1.0f), glm::vec3(0.0f, 0.0f, -1.0f), 10.0f, /*ignoreBackface=*/false, distance);

    // then
    EXPECT_TRUE(static_cast<bool>(face));
    EXPECT_EQ(0, face->index);
    EXPECT_NEAR(1.0f, distance, 1e-5);
}

TEST(Walkmesh, should_find_ray_walkmesh_intersection__intersection_from_far) {
    // given
    auto walkmesh = Walkmesh();
    walkmesh.add(Walkmesh::Face {0, 0, std::vector<glm::vec3> {glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(-1.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f)}, glm::vec3(1.0f, 0.0f, 0.0f)});
    walkmesh.add(Walkmesh::Face {1, 0, std::vector<glm::vec3> {glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(-1.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f)}, glm::vec3(1.0f, 0.0f, 0.0f)});
    walkmesh.add(Walkmesh::Face {2, 0, std::vector<glm::vec3> {glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f, -1.0f, 0.0f)}, glm::vec3(1.0f, 0.0f, 0.0f)});
    walkmesh.add(Walkmesh::Face {3, 0, std::vector<glm::vec3> {glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, -1.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)}, glm::vec3(1.0f, 0.0f, 0.0f)});
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
    float distance = -1.0f;
    auto face = walkmesh.raycast(std::set<uint32_t> {0}, glm::vec3(-0.5f, 0.25, 20.0f), glm::vec3(0.0f, 0.0f, -1.0f), 10.0f, /*ignoreBackface=*/false, distance);

    // then
    EXPECT_TRUE(!static_cast<bool>(face));
}

TEST(Walkmesh, should_find_ray_walkmesh_intersection__no_intersection) {
    // given
    auto walkmesh = Walkmesh();
    walkmesh.add(Walkmesh::Face {0, 0, std::vector<glm::vec3> {glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(-1.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f)}, glm::vec3(1.0f, 0.0f, 0.0f)});
    walkmesh.add(Walkmesh::Face {1, 0, std::vector<glm::vec3> {glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(-1.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f)}, glm::vec3(1.0f, 0.0f, 0.0f)});
    walkmesh.add(Walkmesh::Face {2, 0, std::vector<glm::vec3> {glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f, -1.0f, 0.0f)}, glm::vec3(1.0f, 0.0f, 0.0f)});
    walkmesh.add(Walkmesh::Face {3, 0, std::vector<glm::vec3> {glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, -1.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)}, glm::vec3(1.0f, 0.0f, 0.0f)});
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
    float distance = -1.0f;
    auto face = walkmesh.raycast(std::set<uint32_t> {0}, glm::vec3(-0.5f, 0.25, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f), 10.0f, /*ignoreBackface=*/false, distance);

    // then
    EXPECT_TRUE(!static_cast<bool>(face));
}

TEST(Walkmesh, should_find_ray_walkmesh_intersection__ignore_backface) {
    // given
    auto walkmesh = Walkmesh();

    // Bottom triangle (facing up)
    walkmesh.add(Walkmesh::Face {0, 0, std::vector<glm::vec3> {glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(1.0f, -1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)}, glm::vec3(0.0f, 0.0f, 1.0f)});

    // Top triangle (facing down)
    walkmesh.add(Walkmesh::Face {1, 0, std::vector<glm::vec3> {glm::vec3(-1.0f, -1.0f, 1.0f), glm::vec3(1.0f, -1.0f, 1.0f), glm::vec3(0.0f, 1.0f, 1.0f)}, glm::vec3(0.0f, 0.0f, -1.0f)});

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
    float distance = -1.0f;
    auto face = walkmesh.raycast(std::set<uint32_t> {0}, glm::vec3(0.0f, 0.0f, 2.0f), glm::vec3(0.0f, 0.0f, -1.0f), 10.0f, /*ignoreBackface=*/false, distance);
    EXPECT_TRUE(face);
    EXPECT_EQ(1, face->index);
    EXPECT_NEAR(1.0f, distance, 1e-5);

    // When ignoreBackface is true, we should ignore the top face and
    // hit the bottom face.
    distance = -1.0f;
    face = walkmesh.raycast(std::set<uint32_t> {0}, glm::vec3(0.0f, 0.0f, 2.0f), glm::vec3(0.0f, 0.0f, -1.0f), 10.0f, /*ignoreBackface=*/true, distance);
    EXPECT_TRUE(face);
    EXPECT_EQ(0, face->index);
    EXPECT_NEAR(2.0f, distance, 1e-5);
}

TEST(Walkmesh, should_find_ray_walkmesh_intersection__multi_level) {
    // given
    auto walkmesh = Walkmesh();

    // Bottom triangle (facing up)
    walkmesh.add(Walkmesh::Face {0, 0, std::vector<glm::vec3> {glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(1.0f, -1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)}, glm::vec3(0.0f, 0.0f, 1.0f)});

    // Top triangle (facing up)
    walkmesh.add(Walkmesh::Face {1, 0, std::vector<glm::vec3> {glm::vec3(-1.0f, -1.0f, 1.0f), glm::vec3(1.0f, -1.0f, 1.0f), glm::vec3(0.0f, 1.0f, 1.0f)}, glm::vec3(0.0f, 0.0f, 1.0f)});

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
    float distance = -1.0f;
    auto face = walkmesh.raycast(std::set<uint32_t> {0}, glm::vec3(0.0f, 0.0f, 1.1f), glm::vec3(0.0f, 0.0f, -1.0f), 10.0f, /*ignoreBackface=*/true, distance);
    EXPECT_TRUE(face);
    EXPECT_EQ(1, face->index);
    EXPECT_NEAR(0.1f, distance, 1e-5);

    // Now move the origin closer to the bottom triangle. Raycast
    // should pick it instead of the top triangle.
    distance = -1.0f;
    face = walkmesh.raycast(std::set<uint32_t> {0}, glm::vec3(0.0f, 0.0f, 0.2f), glm::vec3(0.0f, 0.0f, -1.0f), 10.0f, /*ignoreBackface=*/true, distance);
    EXPECT_TRUE(face);
    EXPECT_EQ(0, face->index);
    EXPECT_NEAR(0.2f, distance, 1e-5);
}
