/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

// The minigame simulation, driven through a scripted host that stands in for
// the module scripts.

#include <gtest/gtest.h>

#include <cmath>
#include <functional>

#include <gtest/gtest.h>

#include "reone/game/minigameview.h"
#include "reone/graphics/animation.h"
#include "reone/graphics/model.h"
#include "reone/graphics/modelnode.h"
#include "reone/graphics/options.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/model.h"

#include "../fixtures/audio.h"
#include "../fixtures/graphics.h"
#include "../fixtures/resource.h"
#include "../fixtures/scene.h"

using namespace reone;
using namespace reone::audio;
using namespace reone::game;
using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;

// The swoop bike's lean is a pose held under the layers the scripts play: the
// gear clip goes on running while the lean changes clip, and a bike without a
// clip for the level pose just rests.
TEST(MinigameAnimator, a_pose_sits_under_the_script_layers_and_replaces_the_last_clip) {
    GraphicsOptions graphicsOpt;
    MockRenderPipelineFactory pipelineFactory;
    TestGraphicsModule graphicsModule;
    TestAudioModule audioModule;
    TestResourceModule resourceModule;
    graphicsModule.init();
    audioModule.init();
    resourceModule.init();
    SceneGraph scene("test", pipelineFactory, graphicsOpt, graphicsModule.services(), audioModule.services(), resourceModule.services());

    auto rootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    std::vector<std::shared_ptr<Animation>> animations;
    for (const char *name : {"gear1", "bankl_06", "bankr_05"}) {
        auto animRoot = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
        animations.push_back(std::make_shared<Animation>(name, 0.0f, 0.0f, "", animRoot, std::vector<Animation::Event>()));
    }
    Model model("bike", 0, rootNode, animations, "", 1.0f);
    ModelSceneNode bike(model, ModelUsage::Placeable, scene, graphicsModule.services(), audioModule.services(), resourceModule.services());
    bike.init();

    MinigameAnimator animator;
    ASSERT_TRUE(animator.play(bike, "gear1", true, false, true));

    // Scripts name animations in any case; the first clip has nothing to replace.
    EXPECT_TRUE(animator.pose(bike, "BankR_05", ""));
    EXPECT_EQ(bike.animationChannelCount(), 2u);

    // A new clip replaces the last, on either side.
    EXPECT_TRUE(animator.pose(bike, "BankL_06", "BankR_05"));
    EXPECT_EQ(bike.animationChannelCount(), 2u);
    EXPECT_FALSE(bike.isAnimationPlaying("bankr_05"));
    EXPECT_TRUE(bike.isAnimationPlaying("bankl_06"));

    // Level: the bike has no Ready_01, so the clip comes off and nothing replaces it.
    EXPECT_FALSE(animator.pose(bike, "Ready_01", "BankL_06"));
    EXPECT_EQ(bike.animationChannelCount(), 1u);

    // The script's layer ran through all of it.
    EXPECT_TRUE(bike.isAnimationPlaying("gear1"));
}
