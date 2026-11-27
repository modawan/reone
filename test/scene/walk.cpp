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

#include <limits>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "reone/graphics/format/bwmreader.h"
#include "reone/graphics/options.h"
#include "reone/graphics/walkmesh.h"
#include "reone/scene/collision.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/walkmesh.h"
#include "reone/system/stream/memoryinput.h"

#include "../fixtures/audio.h"
#include "../fixtures/graphics.h"
#include "../fixtures/resource.h"
#include "../fixtures/scene.h"

using namespace reone;
using namespace reone::audio;
using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;
using testing::NiceMock;

namespace {
class WalkmeshUser : public IUser {
};

struct WokFace {
    glm::vec3 a;
    glm::vec3 b;
    glm::vec3 c;
    uint32_t material;
};

struct WokAabbNode {
    glm::vec3 min;
    glm::vec3 max;
    int32_t face;
    uint32_t left;
    uint32_t right;
};

// Adds the AABB tree node over faces [first, last) and those below it, in
// pre-order, and returns its index: a leaf for a single face, else a node
// whose two children split the faces in half.
uint32_t addAabbNode(std::vector<WokAabbNode> &nodes, const std::vector<WokFace> &faces, uint32_t first, uint32_t last) {
    const auto index = static_cast<uint32_t>(nodes.size());
    nodes.push_back({});
    WokAabbNode node {glm::vec3(std::numeric_limits<float>::max()), glm::vec3(-std::numeric_limits<float>::max()), -1, 0, 0};
    for (uint32_t i = first; i < last; ++i) {
        for (const auto &vertex : {faces[i].a, faces[i].b, faces[i].c}) {
            node.min = glm::min(node.min, vertex);
            node.max = glm::max(node.max, vertex);
        }
    }
    if (last - first == 1) {
        node.face = static_cast<int32_t>(first);
    } else {
        const uint32_t middle = (first + last) / 2;
        node.left = addAabbNode(nodes, faces, first, middle);
        node.right = addAabbNode(nodes, faces, middle, last);
    }
    nodes[index] = node;
    return index;
}

// A room walkmesh (WOK) of the faces given, with an AABB tree over them, read
// as the game reads one.
std::shared_ptr<Walkmesh> loadRoomWalkmesh(const std::vector<WokFace> &faces) {
    std::string bytes;
    auto u32 = [&bytes](uint32_t value) { bytes.append(reinterpret_cast<const char *>(&value), 4); };
    auto f32 = [&bytes](float value) { bytes.append(reinterpret_cast<const char *>(&value), 4); };
    auto vec = [&f32](const glm::vec3 &v) { f32(v.x); f32(v.y); f32(v.z); };
    const auto count = static_cast<uint32_t>(faces.size());
    std::vector<WokAabbNode> nodes;
    addAabbNode(nodes, faces, 0, count);
    const auto numAabb = static_cast<uint32_t>(nodes.size());
    const uint32_t offVertices = 136;
    const uint32_t offFaces = offVertices + count * 36;
    const uint32_t offMaterials = offFaces + count * 12;
    const uint32_t offNormals = offMaterials + count * 4;
    const uint32_t offAabb = offNormals + count * 12;
    bytes.append("BWM V1.0");
    u32(1); // WOK
    for (int i = 0; i < 15; ++i) {
        f32(0.0f); // use positions and position
    }
    for (uint32_t value : {3 * count, offVertices, count, offFaces, offMaterials, offNormals, 0u, numAabb, offAabb, 0u}) {
        u32(value); // counts and offsets; no planar distances
    }
    for (int i = 0; i < 6; ++i) {
        u32(0); // adjacencies, edges and perimeters: none
    }
    for (const auto &face : faces) {
        vec(face.a);
        vec(face.b);
        vec(face.c);
    }
    for (uint32_t i = 0; i < 3 * count; ++i) {
        u32(i);
    }
    for (const auto &face : faces) {
        u32(face.material);
    }
    for (const auto &face : faces) {
        vec(glm::normalize(glm::cross(face.b - face.a, face.c - face.a)));
    }
    for (const auto &node : nodes) {
        vec(node.min);
        vec(node.max);
        u32(static_cast<uint32_t>(node.face));
        u32(0); // unknown
        u32(0); // most significant plane
        u32(node.left);
        u32(node.right);
    }
    auto stream = MemoryInputStream(bytes);
    auto reader = BwmReader(stream);
    reader.load();
    return reader.walkmesh();
}

// A quad from x = -1 to 1 between the two heights at the two values of y,
// facing up or toward -y.
void addQuad(std::vector<WokFace> &faces, float y0, float z0, float y1, float z1, uint32_t material) {
    const glm::vec3 a {-1.0f, y0, z0};
    const glm::vec3 b {1.0f, y0, z0};
    const glm::vec3 c {1.0f, y1, z1};
    const glm::vec3 d {-1.0f, y1, z1};
    faces.push_back({a, b, c, material});
    faces.push_back({a, c, d, material});
}

} // namespace

TEST(SceneWalk, should_test_a_distant_object_walkmesh_whose_geometry_reaches_the_walk) {
    GraphicsOptions options;
    NiceMock<MockRenderPipelineFactory> pipelineFactory;
    TestGraphicsModule graphicsModule;
    graphicsModule.init();
    TestAudioModule audioModule;
    audioModule.init();
    TestResourceModule resourceModule;
    resourceModule.init();

    auto walkmesh = std::make_shared<Walkmesh>();
    walkmesh->vertices = {
        {12.0f, -1.0f, -1.0f},
        {12.0f, 1.0f, -1.0f},
        {12.0f, 1.0f, 1.0f},
        {12.0f, -1.0f, -1.0f},
        {12.0f, 1.0f, 1.0f},
        {12.0f, -1.0f, 1.0f},
    };
    walkmesh->faces = {{0, 2, 1}, {3, 5, 4}};
    walkmesh->normals = {{-1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}};
    walkmesh->materials = {0, 0};

    SceneGraph graph(
        "test",
        pipelineFactory,
        options,
        graphicsModule.services(),
        audioModule.services(),
        resourceModule.services());
    auto node = std::make_shared<WalkmeshSceneNode>(
        walkmesh,
        graph,
        graphicsModule.services(),
        audioModule.services(),
        resourceModule.services());
    ASSERT_FALSE(node->aabb().isDegenerate());
    WalkmeshUser user;
    node->setUser(user);
    graph.addRoot(node);
    graph.setWalkcheckSurfaces({0});

    Collision collision;
    EXPECT_TRUE(graph.testWalk({10.0f, 0.0f, 0.0f}, {14.0f, 0.0f, 0.0f}, nullptr, collision));
    EXPECT_NEAR(12.0f, collision.intersection.x, 1e-5f);
}

// A room floor sunk 0.22 m under the passage next to it, with a walkable sill
// rising over 0.15 m. A walk line, tested a tenth of a metre above the
// ground, met the sill as a wall going up and passed over it going down. A
// steep, low walkable sill met from above is stepped up; a higher one, a
// gentle slope, an unwalkable sill, and a wall past the sill, still stop the
// line.
TEST(SceneWalk, a_walk_line_climbs_a_walkable_sill_steeper_than_its_lift) {
    GraphicsOptions options;
    NiceMock<MockRenderPipelineFactory> pipelineFactory;
    TestGraphicsModule graphicsModule;
    graphicsModule.init();
    TestAudioModule audioModule;
    audioModule.init();
    TestResourceModule resourceModule;
    resourceModule.init();

    // given: a floor at height 0 up to y = 2, a sill rising 0.22 m by
    // y = 2.15, a floor at 0.22 from there, and a wall across it at y = 3.5;
    // the sill walkable (material 1) or not (material 7)
    const auto room = [](uint32_t sill, float rise = 0.22f) {
        std::vector<WokFace> faces;
        addQuad(faces, 0.0f, 0.0f, 2.0f, 0.0f, 1);
        addQuad(faces, 2.0f, 0.0f, 2.15f, rise, sill);
        addQuad(faces, 2.15f, rise, 3.5f, rise, 1);
        addQuad(faces, 3.5f, rise, 3.5f, 4.0f, 7);
        return loadRoomWalkmesh(faces);
    };
    const auto testWalk = [&](const std::shared_ptr<Walkmesh> &walkmesh, const glm::vec3 &from, const glm::vec3 &to,
                              Collision &collision) {
        SceneGraph graph(
            "test",
            pipelineFactory,
            options,
            graphicsModule.services(),
            audioModule.services(),
            resourceModule.services());
        auto node = std::make_shared<WalkmeshSceneNode>(
            walkmesh,
            graph,
            graphicsModule.services(),
            audioModule.services(),
            resourceModule.services());
        WalkmeshUser user;
        node->setUser(user);
        graph.addRoot(node);
        graph.setWalkcheckSurfaces({1, 7});
        graph.setWalkableSurfaces({1});
        const glm::vec3 lift {0.0f, 0.0f, 0.1f};
        return graph.testWalk(from + lift, to + lift, nullptr, collision);
    };
    const auto walkable = room(1);
    ASSERT_TRUE(walkable->isAreaWalkmesh());
    const auto unwalkable = room(7);

    Collision collision;
    // when, then: a step up the walkable sill, from its foot and from partway
    // up it, is clear, as is a step down it
    EXPECT_FALSE(testWalk(walkable, {0.0f, 1.95f, 0.0f}, {0.0f, 2.1f, 0.0f}, collision));
    EXPECT_FALSE(testWalk(walkable, {0.0f, 2.04f, 0.06f}, {0.0f, 2.2f, 0.06f}, collision));
    EXPECT_FALSE(testWalk(walkable, {0.0f, 2.3f, 0.22f}, {0.0f, 1.9f, 0.22f}, collision));
    // the wall past the sill still stops a line over it
    ASSERT_TRUE(testWalk(walkable, {0.0f, 1.0f, 0.0f}, {0.0f, 3.8f, 0.0f}, collision));
    EXPECT_NEAR(3.5f, collision.intersection.y, 1e-4f);
    EXPECT_EQ(7, collision.material);
    // a walkable rise higher than a step still stops a step up it
    const auto high = room(1, 0.5f);
    EXPECT_TRUE(testWalk(high, {0.0f, 1.95f, 0.0f}, {0.0f, 2.1f, 0.0f}, collision));
    // a gentle walkable slope (0.25 m over 1.5 m) still stops a line that
    // meets it, as before
    std::vector<WokFace> slopeFaces;
    addQuad(slopeFaces, 0.0f, 0.0f, 2.0f, 0.0f, 1);
    addQuad(slopeFaces, 2.0f, 0.0f, 3.5f, 0.25f, 1);
    const auto slope = loadRoomWalkmesh(slopeFaces);
    EXPECT_TRUE(testWalk(slope, {0.0f, 1.0f, 0.0f}, {0.0f, 3.4f, 0.0f}, collision));
    // an unwalkable sill stops a step up it at its foot
    ASSERT_TRUE(testWalk(unwalkable, {0.0f, 1.95f, 0.0f}, {0.0f, 2.1f, 0.0f}, collision));
    EXPECT_EQ(7, collision.material);
    EXPECT_LT(collision.intersection.y, 2.1f);
}
