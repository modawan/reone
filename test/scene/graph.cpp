/* Copyright (c) 2026 The reone project contributors */

#include <gtest/gtest.h>

#include "reone/graphics/options.h"
#include "reone/graphics/walkmesh.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/walkmesh.h"
#include "reone/system/threadutil.h"

#include "../fixtures/audio.h"
#include "../fixtures/graphics.h"
#include "../fixtures/resource.h"
#include "../fixtures/scene.h"

using namespace reone;
using namespace reone::audio;
using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;

namespace {

struct GraphFixture {
    GraphicsOptions graphicsOpt;
    MockRenderPipelineFactory pipelineFactory;
    TestGraphicsModule graphicsModule;
    TestAudioModule audioModule;
    TestResourceModule resourceModule;
    std::unique_ptr<SceneGraph> scene;
    std::shared_ptr<Walkmesh> walkmesh {std::make_shared<Walkmesh>()};

    GraphFixture() {
        // The graph's update checks it runs on the main thread.
        markMainThread();
        graphicsModule.init();
        audioModule.init();
        resourceModule.init();
        scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt,
                                             graphicsModule.services(),
                                             audioModule.services(),
                                             resourceModule.services());
    }

    // The revision after an update, which is when the graph looks again.
    uint32_t revisionAfterUpdate() {
        scene->update(0.0f);
        return scene->walkmeshRevision();
    }
};

} // namespace

TEST(SceneGraph, walkmesh_revision_moves_on_when_a_walkmesh_comes_goes_switches_or_moves) {
    GraphFixture fixture;
    // Made directly, as newWalkmesh would also upload a mesh to draw.
    auto node = std::make_shared<WalkmeshSceneNode>(fixture.walkmesh, *fixture.scene,
                                                    fixture.graphicsModule.services(),
                                                    fixture.audioModule.services(),
                                                    fixture.resourceModule.services());
    fixture.scene->addRoot(node);
    uint32_t revision = fixture.revisionAfterUpdate();
    EXPECT_EQ(fixture.revisionAfterUpdate(), revision);

    node->setEnabled(false);
    EXPECT_NE(fixture.revisionAfterUpdate(), revision);
    revision = fixture.scene->walkmeshRevision();

    node->setLocalTransform(glm::translate(glm::vec3(1.0f, 0.0f, 0.0f)));
    EXPECT_NE(fixture.revisionAfterUpdate(), revision);
    revision = fixture.scene->walkmeshRevision();

    fixture.scene->removeRoot(*node);
    EXPECT_NE(fixture.revisionAfterUpdate(), revision);
    revision = fixture.scene->walkmeshRevision();

    fixture.scene->setLineOfSightSurfaces({1});
    EXPECT_NE(fixture.scene->walkmeshRevision(), revision);
}
