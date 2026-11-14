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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "reone/graphics/options.h"
#include "reone/graphics/walkmesh.h"
#include "reone/scene/collision.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/walkmesh.h"

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

    Walkmesh walkmesh;
    walkmesh.vertices = {
        {12.0f, -1.0f, -1.0f},
        {12.0f, 1.0f, -1.0f},
        {12.0f, 1.0f, 1.0f},
        {12.0f, -1.0f, -1.0f},
        {12.0f, 1.0f, 1.0f},
        {12.0f, -1.0f, 1.0f},
    };
    walkmesh.faces = {{0, 2, 1}, {3, 5, 4}};
    walkmesh.normals = {{-1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}};
    walkmesh.materials = {0, 0};

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
