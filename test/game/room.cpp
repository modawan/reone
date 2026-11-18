/* Copyright (c) 2026 The reone project contributors */

#include <gtest/gtest.h>

#include "reone/game/room.h"
#include "reone/graphics/model.h"
#include "reone/graphics/modelnode.h"
#include "reone/graphics/options.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/model.h"

#include "../fixtures/audio.h"
#include "../fixtures/graphics.h"
#include "../fixtures/resource.h"
#include "../fixtures/scene.h"

using namespace reone;
using namespace reone::game;
using namespace reone::graphics;
using namespace reone::scene;

namespace {

// A room model named "room" whose root holds its walls and, when animated, an
// animation root holding a screen.
struct RoomModelFixture {
    GraphicsOptions graphicsOpt;
    MockRenderPipelineFactory pipelineFactory;
    TestGraphicsModule graphicsModule;
    audio::TestAudioModule audioModule;
    resource::TestResourceModule resourceModule;
    std::unique_ptr<SceneGraph> scene;
    std::shared_ptr<ModelSceneNode> model;

    explicit RoomModelFixture(bool animated) {
        graphicsModule.init();
        audioModule.init();
        resourceModule.init();
        scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt, graphicsModule.services(), audioModule.services(), resourceModule.services());
        const glm::quat identity {1.0f, 0.0f, 0.0f, 0.0f};
        auto root = std::make_shared<ModelNode>(0, "room", glm::vec3(0.0f), identity, false);
        root->addChild(std::make_shared<ModelNode>(1, "walls", glm::vec3(0.0f), identity, false, root.get()));
        if (animated) {
            auto animationRoot = std::make_shared<ModelNode>(2, "rooma", glm::vec3(0.0f), identity, true, root.get());
            animationRoot->addChild(std::make_shared<ModelNode>(3, "screen", glm::vec3(0.0f), identity, true, animationRoot.get()));
            root->addChild(animationRoot);
        }
        auto roomModel = std::make_shared<Model>("room", 0, root, std::vector<std::shared_ptr<Animation>>(), "", 1.0f);
        roomModel->init();
        model = scene->newModel(roomModel, ModelUsage::Room);
    }
};

} // namespace

TEST(Room, nodes_outside_the_animation_root_are_static) {
    RoomModelFixture fixture(/*animated=*/true);
    Room room {"room", glm::vec3(0.0f), fixture.model, nullptr};

    EXPECT_TRUE(fixture.model->getNodeByName("room")->isStatic());
    EXPECT_TRUE(fixture.model->getNodeByName("walls")->isStatic());
    EXPECT_FALSE(fixture.model->getNodeByName("rooma")->isStatic());
    EXPECT_FALSE(fixture.model->getNodeByName("screen")->isStatic());
}

TEST(RoomEnvironment, env_audio_names_the_eax_room_types_in_order) {
    EXPECT_EQ(environmentOfEnvAudio(0), audio::Environment::Generic);
    EXPECT_EQ(environmentOfEnvAudio(2), audio::Environment::Room);
    EXPECT_EQ(environmentOfEnvAudio(22), audio::Environment::Underwater);
}

TEST(RoomEnvironment, env_audio_23_repeats_generic) {
    EXPECT_EQ(environmentOfEnvAudio(23), audio::Environment::Generic);
}

TEST(RoomEnvironment, env_audio_past_23_or_below_0_names_none) {
    // 40 and 92 are among the values K1's areas use.
    EXPECT_EQ(environmentOfEnvAudio(24), std::nullopt);
    EXPECT_EQ(environmentOfEnvAudio(40), std::nullopt);
    EXPECT_EQ(environmentOfEnvAudio(92), std::nullopt);
    EXPECT_EQ(environmentOfEnvAudio(-1), std::nullopt);
}

TEST(RoomEnvironment, only_room_types_past_generic_reverberate) {
    EXPECT_FALSE(envAudioReverberates(0));
    EXPECT_TRUE(envAudioReverberates(1));
    EXPECT_TRUE(envAudioReverberates(22));
    EXPECT_FALSE(envAudioReverberates(23));
    EXPECT_FALSE(envAudioReverberates(40));
    EXPECT_FALSE(envAudioReverberates(-1));
}
