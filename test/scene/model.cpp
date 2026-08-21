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

#include "reone/game/types.h"
#include "reone/graphics/animation.h"
#include "reone/graphics/camera/orthographic.h"
#include "reone/graphics/camera/perspective.h"
#include "reone/graphics/lipanimation.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/options.h"
#include "reone/gui/sceneinitializer.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/model.h"
#include "reone/scene/node/modelnode.h"

#include "../fixtures/audio.h"
#include "../fixtures/graphics.h"
#include "../fixtures/resource.h"
#include "../fixtures/scene.h"

using namespace reone;
using namespace reone::audio;
using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;

using testing::_;
using testing::ReturnRef;

class MockUser : public IUser {
public:
    ~MockUser() {}
};

TEST(SceneInitializer, defaults_to_orthographic_projection_and_default_depth) {
    auto graphicsOpt = GraphicsOptions();
    auto pipelineFactory = MockRenderPipelineFactory();
    auto graphicsModule = TestGraphicsModule();
    graphicsModule.init();
    auto audioModule = TestAudioModule();
    audioModule.init();
    auto resourceModule = TestResourceModule();
    resourceModule.init();
    auto scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt, graphicsModule.services(), audioModule.services(), resourceModule.services());

    auto rootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    auto model = Model("gui_model", 0, rootNode, std::vector<std::shared_ptr<Animation>>(), "", 1.0f);
    model.init();

    gui::SceneInitializer(*scene)
        .aspect(2.5f)
        .modelSupplier([&model](ISceneGraph &graph) {
            return graph.newModel(model, ModelUsage::GUI);
        })
        .invoke();

    auto maybeCameraNode = scene->camera();
    ASSERT_TRUE(maybeCameraNode);
    auto camera = maybeCameraNode->get().camera();
    ASSERT_TRUE(camera);
    EXPECT_EQ(CameraType::Orthographic, camera->type());
    EXPECT_FLOAT_EQ(kDefaultClipPlaneNear, camera->zNear());
    EXPECT_FLOAT_EQ(kDefaultClipPlaneFar, camera->zFar());
}

TEST(SceneInitializer, perspective_uses_requested_fov_aspect_and_depth) {
    auto graphicsOpt = GraphicsOptions();
    auto pipelineFactory = MockRenderPipelineFactory();
    auto graphicsModule = TestGraphicsModule();
    graphicsModule.init();
    auto audioModule = TestAudioModule();
    audioModule.init();
    auto resourceModule = TestResourceModule();
    resourceModule.init();
    auto scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt, graphicsModule.services(), audioModule.services(), resourceModule.services());

    auto rootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    auto model = Model("gui_model", 0, rootNode, std::vector<std::shared_ptr<Animation>>(), "", 1.0f);
    model.init();
    constexpr float kVerticalFov = glm::radians(35.0f);

    gui::SceneInitializer(*scene)
        .aspect(2.5f)
        .depth(0.1f, 10000.0f)
        .perspective(kVerticalFov)
        .modelSupplier([&model](ISceneGraph &graph) {
            return graph.newModel(model, ModelUsage::GUI);
        })
        .invoke();

    auto maybeCameraNode = scene->camera();
    ASSERT_TRUE(maybeCameraNode);
    auto camera = std::dynamic_pointer_cast<PerspectiveCamera>(maybeCameraNode->get().camera());
    ASSERT_TRUE(camera);
    EXPECT_FLOAT_EQ(kVerticalFov, camera->fovy());
    EXPECT_FLOAT_EQ(2.5f, camera->aspect());
    EXPECT_FLOAT_EQ(0.1f, camera->zNear());
    EXPECT_FLOAT_EQ(10000.0f, camera->zFar());
}

TEST(SceneInitializer, camera_tracks_authored_model_hook_after_initialization) {
    auto graphicsOpt = GraphicsOptions();
    auto pipelineFactory = MockRenderPipelineFactory();
    auto graphicsModule = TestGraphicsModule();
    graphicsModule.init();
    auto audioModule = TestAudioModule();
    audioModule.init();
    auto resourceModule = TestResourceModule();
    resourceModule.init();
    auto scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt, graphicsModule.services(), audioModule.services(), resourceModule.services());

    auto rootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    auto cameraHook = std::make_shared<ModelNode>(1, "camerahook", glm::vec3(3.0f, 4.0f, 5.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, rootNode.get());
    rootNode->addChild(cameraHook);
    auto model = Model("gui_model", 0, rootNode, std::vector<std::shared_ptr<Animation>>(), "", 1.0f);
    model.init();
    std::shared_ptr<ModelSceneNode> sceneModel;

    gui::SceneInitializer(*scene)
        .modelSupplier([&model, &sceneModel](ISceneGraph &graph) {
            sceneModel = graph.newModel(model, ModelUsage::GUI);
            return sceneModel;
        })
        .cameraFromModelNode("camerahook")
        .invoke();

    auto maybeCameraNode = scene->camera();
    ASSERT_TRUE(maybeCameraNode);
    auto camera = maybeCameraNode->get().camera();
    ASSERT_TRUE(camera);
    EXPECT_LT(glm::length(camera->position() - glm::vec3(3.0f, 4.0f, 5.0f)), 1e-5f);

    auto hookNode = sceneModel->getNodeByName("camerahook");
    ASSERT_TRUE(hookNode);
    hookNode->setLocalTransform(glm::translate(glm::mat4(1.0f), glm::vec3(6.0f, 7.0f, 8.0f)));
    EXPECT_LT(glm::length(camera->position() - glm::vec3(6.0f, 7.0f, 8.0f)), 1e-5f);
}

TEST(SceneInitializer, static_camera_hook_preserves_local_camera_transform) {
    auto graphicsOpt = GraphicsOptions();
    auto pipelineFactory = MockRenderPipelineFactory();
    auto graphicsModule = TestGraphicsModule();
    graphicsModule.init();
    auto audioModule = TestAudioModule();
    audioModule.init();
    auto resourceModule = TestResourceModule();
    resourceModule.init();
    auto scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt, graphicsModule.services(), audioModule.services(), resourceModule.services());

    auto rootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    auto cameraHook = std::make_shared<ModelNode>(1, "camerahook", glm::vec3(3.0f, 4.0f, 5.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, rootNode.get());
    rootNode->addChild(cameraHook);
    auto model = Model("gui_model", 0, rootNode, std::vector<std::shared_ptr<Animation>>(), "", 1.0f);
    model.init();

    gui::SceneInitializer(*scene)
        .modelSupplier([&model](ISceneGraph &graph) {
            return graph.newModel(model, ModelUsage::GUI);
        })
        .cameraFromModelNode("camerahook")
        .cameraTransform(glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f)))
        .invoke();

    auto maybeCameraNode = scene->camera();
    ASSERT_TRUE(maybeCameraNode);
    auto camera = maybeCameraNode->get().camera();
    ASSERT_TRUE(camera);
    EXPECT_LT(glm::length(camera->position() - glm::vec3(4.0f, 6.0f, 8.0f)), 1e-5f);
}

TEST(ModelSceneNode, should_build_from_model) {
    // given
    auto graphicsOpt = GraphicsOptions();
    auto pipelineFactory = MockRenderPipelineFactory();

    auto graphicsModule = TestGraphicsModule();
    graphicsModule.init();

    auto audioModule = TestAudioModule();
    audioModule.init();

    auto resourceModule = TestResourceModule();
    resourceModule.init();

    auto scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt, graphicsModule.services(), audioModule.services(), resourceModule.services());

    auto rootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);

    auto mesh = std::make_shared<ModelNode::TriangleMesh>();
    auto meshNode = std::make_shared<ModelNode>(1, "mesh_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, rootNode.get());
    meshNode->setMesh(mesh);
    rootNode->addChild(meshNode);

    auto light = std::make_shared<ModelNode::Light>();
    auto lightNode = std::make_shared<ModelNode>(2, "light_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, rootNode.get());
    lightNode->setLight(light);
    rootNode->addChild(lightNode);

    auto emitter = std::make_shared<ModelNode::Emitter>();
    auto emitterNode = std::make_shared<ModelNode>(3, "emitter_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, rootNode.get());
    emitterNode->setEmitter(emitter);
    rootNode->addChild(emitterNode);

    auto model = Model("some_model", 0, rootNode, std::vector<std::shared_ptr<Animation>>(), "", 1.0f);
    auto modelSceneNode = std::make_shared<ModelSceneNode>(
        model,
        ModelUsage::Creature,
        *scene,
        graphicsModule.services(),
        audioModule.services(),
        resourceModule.services());

    // when
    modelSceneNode->init();

    // then
    EXPECT_EQ(1ll, modelSceneNode->children().size());

    auto rootNodeSceneNode = modelSceneNode->getNodeByName("root_node");
    EXPECT_TRUE(static_cast<bool>(rootNodeSceneNode));
    EXPECT_EQ(static_cast<int>(SceneNodeType::Dummy), static_cast<int>(rootNodeSceneNode->type()));

    EXPECT_EQ(3ll, rootNodeSceneNode->children().size());

    auto meshSceneNode = modelSceneNode->getNodeByName("mesh_node");
    EXPECT_TRUE(static_cast<bool>(meshSceneNode));
    EXPECT_EQ(static_cast<int>(SceneNodeType::Mesh), static_cast<int>(meshSceneNode->type()));

    auto lightSceneNode = modelSceneNode->getNodeByName("light_node");
    EXPECT_TRUE(static_cast<bool>(lightSceneNode));
    EXPECT_EQ(static_cast<int>(SceneNodeType::Light), static_cast<int>(lightSceneNode->type()));

    auto emitterSceneNode = modelSceneNode->getNodeByName("emitter_node");
    EXPECT_TRUE(static_cast<bool>(emitterSceneNode));
    EXPECT_EQ(static_cast<int>(SceneNodeType::Emitter), static_cast<int>(emitterSceneNode->type()));
}

namespace {

// A model carrying several zero-length animations on disjoint nodes, which is
// the shape of the shipped minigame HUD: one heading pose plus a contact loop
// per fighter, all needing to run at once.
struct OverlayModelFixture {
    GraphicsOptions graphicsOpt;
    MockRenderPipelineFactory pipelineFactory;
    TestGraphicsModule graphicsModule;
    TestAudioModule audioModule;
    TestResourceModule resourceModule;
    std::unique_ptr<SceneGraph> scene;
    std::shared_ptr<ModelNode> rootNode;
    std::vector<std::shared_ptr<Animation>> animations;
    std::unique_ptr<Model> model;
    std::shared_ptr<ModelSceneNode> node;

    explicit OverlayModelFixture(const std::vector<std::string> &names) {
        graphicsModule.init();
        audioModule.init();
        resourceModule.init();
        scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt,
                                             graphicsModule.services(),
                                             audioModule.services(),
                                             resourceModule.services());
        rootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f),
                                               glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
        for (const auto &name : names) {
            auto animRoot = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f),
                                                        glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
            animations.push_back(std::make_shared<Animation>(
                name, 0.0f, 0.0f, "", animRoot, std::vector<Animation::Event>()));
        }
        model = std::make_unique<Model>("hud_model", 0, rootNode, animations, "", 1.0f);
        node = std::make_shared<ModelSceneNode>(*model, ModelUsage::Placeable, *scene,
                                                graphicsModule.services(),
                                                audioModule.services(),
                                                resourceModule.services());
        node->init();
    }

    void overlay(const std::string &name) {
        node->playAnimation(name, nullptr,
                            AnimationProperties::fromFlags(AnimationFlags::loopOverlay));
    }
};

} // namespace

TEST(ModelSceneNode, overlay_animations_on_disjoint_nodes_run_together) {
    OverlayModelFixture fixture({"contact01", "contact02", "heading000"});

    fixture.overlay("contact01");
    fixture.overlay("contact02");
    fixture.overlay("heading000");

    EXPECT_EQ(fixture.node->animationChannelCount(), 3u);
    EXPECT_TRUE(fixture.node->isAnimationPlaying("contact01"));
    EXPECT_TRUE(fixture.node->isAnimationPlaying("heading000"));
}

TEST(ModelSceneNode, removing_one_overlay_leaves_the_others_running) {
    OverlayModelFixture fixture({"contact01", "contact02", "heading000"});
    fixture.overlay("contact01");
    fixture.overlay("contact02");
    fixture.overlay("heading000");

    EXPECT_TRUE(fixture.node->removeAnimation("contact01"));

    EXPECT_EQ(fixture.node->animationChannelCount(), 2u);
    EXPECT_FALSE(fixture.node->isAnimationPlaying("contact01"));
    EXPECT_TRUE(fixture.node->isAnimationPlaying("contact02"));
    EXPECT_TRUE(fixture.node->isAnimationPlaying("heading000"));
}

TEST(ModelSceneNode, removing_an_animation_that_is_not_playing_is_harmless) {
    OverlayModelFixture fixture({"contact01", "heading000"});
    fixture.overlay("contact01");

    EXPECT_FALSE(fixture.node->removeAnimation("heading000"));
    EXPECT_FALSE(fixture.node->removeAnimation("no_such_animation"));

    EXPECT_EQ(fixture.node->animationChannelCount(), 1u);
    EXPECT_TRUE(fixture.node->isAnimationPlaying("contact01"));
}

TEST(ModelSceneNode, repeated_removal_is_idempotent) {
    OverlayModelFixture fixture({"contact01"});
    fixture.overlay("contact01");

    EXPECT_TRUE(fixture.node->removeAnimation("contact01"));
    EXPECT_FALSE(fixture.node->removeAnimation("contact01"));
    EXPECT_FALSE(fixture.node->removeAnimation("contact01"));

    EXPECT_EQ(fixture.node->animationChannelCount(), 0u);
}

TEST(ModelSceneNode, replacing_a_pose_keeps_the_channel_count_bounded) {
    // Swapping one zero-length heading pose for the next, as the turret does
    // every time its yaw crosses a whole degree, must not accumulate channels.
    std::vector<std::string> names {"contact01"};
    for (int i = 0; i < 8; ++i) {
        names.push_back(str(boost::format("heading%03d") % i));
    }
    OverlayModelFixture fixture(names);
    fixture.overlay("contact01");

    std::string previous;
    for (int i = 0; i < 8; ++i) {
        std::string next = str(boost::format("heading%03d") % i);
        if (!previous.empty()) {
            fixture.node->removeAnimation(previous);
        }
        fixture.overlay(next);
        previous = next;
        EXPECT_EQ(fixture.node->animationChannelCount(), 2u) << "after " << next;
    }

    EXPECT_TRUE(fixture.node->isAnimationPlaying("contact01"));
    EXPECT_TRUE(fixture.node->isAnimationPlaying("heading007"));
}

TEST(ModelSceneNode, should_play_single_fire_forget_animation) {
    // given
    auto graphicsOpt = GraphicsOptions();
    auto pipelineFactory = MockRenderPipelineFactory();

    auto graphicsModule = TestGraphicsModule();
    graphicsModule.init();

    auto audioModule = TestAudioModule();
    audioModule.init();

    auto resourceModule = TestResourceModule();
    resourceModule.init();

    auto scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt, graphicsModule.services(), audioModule.services(), resourceModule.services());

    auto rootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);

    auto animRootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false, nullptr);
    animRootNode->vectorTracks()[ControllerTypes::position].add(0.0f, glm::vec3(0.0f));
    animRootNode->vectorTracks()[ControllerTypes::position].add(1.0f, glm::vec3(1.0f, 2.0f, 3.0f));

    auto animations = std::vector<std::shared_ptr<Animation>> {
        std::make_shared<Animation>("some_animation", 1.0f, 0.5f, "root_node", animRootNode, std::vector<Animation::Event>())};

    auto model = Model("some_model", 0, rootNode, animations, "", 1.0f);

    auto modelSceneNode = std::make_shared<ModelSceneNode>(
        model,
        ModelUsage::Creature,
        *scene,
        graphicsModule.services(),
        audioModule.services(),
        resourceModule.services());

    // when
    modelSceneNode->init();
    modelSceneNode->playAnimation("some_animation", nullptr, AnimationProperties::fromFlags(AnimationFlags::fireForget));
    modelSceneNode->update(1.25f);

    // then
    auto &channels = modelSceneNode->animationChannels();
    EXPECT_EQ(1ll, channels.size());
    EXPECT_EQ(1.0f, channels[0].time);
    EXPECT_TRUE(channels[0].finished);
    auto rootSceneNode = modelSceneNode->getNodeByName("root_node");
    EXPECT_TRUE(static_cast<bool>(rootSceneNode));
    auto &rootPosition = rootSceneNode->localTransform()[3];
    EXPECT_NEAR(1.0f, rootPosition.x, 1e-5);
    EXPECT_NEAR(2.0f, rootPosition.y, 1e-5);
    EXPECT_NEAR(3.0f, rootPosition.z, 1e-5);
}


TEST(ModelSceneNode, should_retarget_external_animation_root_to_live_model_root) {
    // given: retail stunt animations carry placement on a proxy model root,
    // while the live participant uses its appearance model's root name.
    auto graphicsOpt = GraphicsOptions();
    auto pipelineFactory = MockRenderPipelineFactory();

    auto graphicsModule = TestGraphicsModule();
    graphicsModule.init();

    auto audioModule = TestAudioModule();
    audioModule.init();

    auto resourceModule = TestResourceModule();
    resourceModule.init();

    auto scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt, graphicsModule.services(), audioModule.services(), resourceModule.services());

    auto liveRoot = std::make_shared<ModelNode>(
        0, "pmbbs", glm::vec3(0.0f),
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    auto proxyRoot = std::make_shared<ModelNode>(
        0, "m12aa_c01_char01", glm::vec3(0.0f),
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false, nullptr);
    proxyRoot->vectorTracks()[ControllerTypes::position].add(0.0f, glm::vec3(0.0f));
    proxyRoot->vectorTracks()[ControllerTypes::position].add(1.0f, glm::vec3(4.0f, 5.0f, 6.0f));

    auto external = std::make_shared<Animation>(
        "cut003w", 1.0f, 0.0f, "", proxyRoot,
        std::vector<Animation::Event>());
    auto model = Model(
        "pmbbs", 0, liveRoot,
        std::vector<std::shared_ptr<Animation>>(), "", 1.0f);
    auto modelSceneNode = std::make_shared<ModelSceneNode>(
        model,
        ModelUsage::Creature,
        *scene,
        graphicsModule.services(),
        audioModule.services(),
        resourceModule.services());

    // when
    modelSceneNode->init();
    modelSceneNode->playAnimation(
        *external, nullptr,
        AnimationProperties::fromFlags(AnimationFlags::retargetRoot));
    modelSceneNode->update(1.0f);

    // then
    auto rootSceneNode = modelSceneNode->getNodeByName("pmbbs");
    ASSERT_TRUE(static_cast<bool>(rootSceneNode));
    const auto &rootPosition = rootSceneNode->localTransform()[3];
    EXPECT_NEAR(4.0f, rootPosition.x, 1e-5);
    EXPECT_NEAR(5.0f, rootPosition.y, 1e-5);
    EXPECT_NEAR(6.0f, rootPosition.z, 1e-5);
}

TEST(ModelSceneNode, should_apply_external_stunt_tracks_to_an_attached_appearance_head) {
    // given: Game 5's cut003w proxy has no local clip on pmhb05, but carries
    // matching facial nodes whose tracks close the player's eyes.
    auto graphicsOpt = GraphicsOptions();
    auto pipelineFactory = MockRenderPipelineFactory();

    auto graphicsModule = TestGraphicsModule();
    graphicsModule.init();
    auto audioModule = TestAudioModule();
    audioModule.init();
    auto resourceModule = TestResourceModule();
    resourceModule.init();

    auto scene = std::make_unique<SceneGraph>(
        "test", pipelineFactory, graphicsOpt,
        graphicsModule.services(), audioModule.services(), resourceModule.services());

    auto bodyRoot = std::make_shared<ModelNode>(
        0, "pmbbs", glm::vec3(0.0f),
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    auto headHook = std::make_shared<ModelNode>(
        1, "headhook", glm::vec3(0.0f),
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false, bodyRoot.get());
    bodyRoot->addChild(headHook);
    auto accessoryHook = std::make_shared<ModelNode>(
        2, "accessoryhook", glm::vec3(0.0f),
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false, bodyRoot.get());
    bodyRoot->addChild(accessoryHook);

    auto headRoot = std::make_shared<ModelNode>(
        0, "pmhb05", glm::vec3(0.0f),
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    auto eyelid = std::make_shared<ModelNode>(
        1, "eyeLid", glm::vec3(0.0f),
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, headRoot.get());
    headRoot->addChild(eyelid);

    auto proxyRoot = std::make_shared<ModelNode>(
        0, "m12aa_c01_char01", glm::vec3(0.0f),
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false, nullptr);
    proxyRoot->vectorTracks()[ControllerTypes::position].add(1.0f, glm::vec3(4.0f, 5.0f, 6.0f));
    auto proxyEyelid = std::make_shared<ModelNode>(
        1, "eyeLid", glm::vec3(0.0f),
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false, proxyRoot.get());
    proxyEyelid->vectorTracks()[ControllerTypes::position].add(1.0f, glm::vec3(0.0f, 0.0f, -0.25f));
    proxyRoot->addChild(proxyEyelid);

    auto external = std::make_shared<Animation>(
        "cut003w", 1.0f, 0.0f, "", proxyRoot,
        std::vector<Animation::Event>());
    auto bodyModel = Model(
        "pmbbs", 0, bodyRoot,
        std::vector<std::shared_ptr<Animation>>(), "", 1.0f);
    auto headModel = Model(
        "pmhb05", 0, headRoot,
        std::vector<std::shared_ptr<Animation>>(), "", 1.0f);
    auto accessoryRoot = std::make_shared<ModelNode>(
        0, "unrelated_root", glm::vec3(0.0f),
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    auto accessoryAnimRoot = std::make_shared<ModelNode>(
        0, "unrelated_root", glm::vec3(0.0f),
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false, nullptr);
    auto accessoryOverlay = std::make_shared<Animation>(
        "local_overlay", 1.0f, 0.0f, "", accessoryAnimRoot,
        std::vector<Animation::Event>());
    auto accessoryModel = Model(
        "unrelated_creature_attachment", 0, accessoryRoot,
        std::vector<std::shared_ptr<Animation>> {accessoryOverlay}, "", 1.0f);
    auto body = std::make_shared<ModelSceneNode>(
        bodyModel, ModelUsage::Creature, *scene,
        graphicsModule.services(), audioModule.services(), resourceModule.services());
    auto head = std::make_shared<ModelSceneNode>(
        headModel, ModelUsage::Creature, *scene,
        graphicsModule.services(), audioModule.services(), resourceModule.services());
    auto accessory = std::make_shared<ModelSceneNode>(
        accessoryModel, ModelUsage::Creature, *scene,
        graphicsModule.services(), audioModule.services(), resourceModule.services());

    // when
    body->init();
    head->init();
    accessory->init();
    body->attach("headhook", *head);
    body->attach("accessoryhook", *accessory);
    accessory->playAnimation(
        "local_overlay", nullptr,
        AnimationProperties::fromFlags(AnimationFlags::loopOverlay));
    body->playAnimation(
        *external, nullptr,
        AnimationProperties::fromFlags(AnimationFlags::propagate | AnimationFlags::retargetRoot));
    body->update(1.0f);

    // then
    auto bodyRootNode = body->getNodeByName("pmbbs");
    auto headRootNode = head->getNodeByName("pmhb05");
    auto eyelidNode = head->getNodeByName("eyeLid");
    ASSERT_TRUE(static_cast<bool>(bodyRootNode));
    ASSERT_TRUE(static_cast<bool>(headRootNode));
    ASSERT_TRUE(static_cast<bool>(eyelidNode));
    EXPECT_NEAR(4.0f, bodyRootNode->localTransform()[3].x, 1e-5);
    EXPECT_NEAR(5.0f, bodyRootNode->localTransform()[3].y, 1e-5);
    EXPECT_NEAR(6.0f, bodyRootNode->localTransform()[3].z, 1e-5);
    EXPECT_NEAR(0.0f, headRootNode->localTransform()[3].x, 1e-5);
    EXPECT_NEAR(-0.25f, eyelidNode->localTransform()[3].z, 1e-5);
    EXPECT_EQ("local_overlay", accessory->activeAnimationName());
    EXPECT_EQ(1u, accessory->animationChannels().size());
}

TEST(ModelSceneNode, should_play_single_looping_animation) {
    // given
    auto graphicsOpt = GraphicsOptions();
    auto pipelineFactory = MockRenderPipelineFactory();

    auto graphicsModule = TestGraphicsModule();
    graphicsModule.init();

    auto audioModule = TestAudioModule();
    audioModule.init();

    auto resourceModule = TestResourceModule();
    resourceModule.init();

    auto scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt, graphicsModule.services(), audioModule.services(), resourceModule.services());

    auto rootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);

    auto animRootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false, nullptr);
    animRootNode->vectorTracks()[ControllerTypes::position].add(0.0f, glm::vec3(0.0f));
    animRootNode->vectorTracks()[ControllerTypes::position].add(1.0f, glm::vec3(1.0f, 2.0f, 3.0f));

    auto animations = std::vector<std::shared_ptr<Animation>> {
        std::make_shared<Animation>("some_animation", 1.0f, 0.5f, "root_node", animRootNode, std::vector<Animation::Event>())};

    auto model = Model("some_model", 0, rootNode, animations, "", 1.0f);
    auto modelSceneNode = std::make_shared<ModelSceneNode>(
        model,
        ModelUsage::Creature,
        *scene,
        graphicsModule.services(),
        audioModule.services(),
        resourceModule.services());

    // when
    modelSceneNode->init();
    modelSceneNode->playAnimation("some_animation", nullptr, AnimationProperties::fromFlags(AnimationFlags::loop));
    modelSceneNode->update(1.25f);

    // then
    auto &channels = modelSceneNode->animationChannels();
    EXPECT_EQ(1ll, channels.size());
    EXPECT_NEAR(0.0f, channels[0].time, 1e-5);
    EXPECT_TRUE(!channels[0].finished);
    auto rootSceneNode = modelSceneNode->getNodeByName("root_node");
    EXPECT_TRUE(static_cast<bool>(rootSceneNode));
    auto &rootPosition = rootSceneNode->localTransform()[3];
    EXPECT_NEAR(1.0f, rootPosition.x, 1e-5);
    EXPECT_NEAR(2.0f, rootPosition.y, 1e-5);
    EXPECT_NEAR(3.0f, rootPosition.z, 1e-5);
}

TEST(ModelSceneNode, should_restart_a_completed_queued_animation_explicitly) {
    auto graphicsOpt = GraphicsOptions();
    auto pipelineFactory = MockRenderPipelineFactory();
    auto graphicsModule = TestGraphicsModule();
    graphicsModule.init();
    auto audioModule = TestAudioModule();
    audioModule.init();
    auto resourceModule = TestResourceModule();
    resourceModule.init();
    auto scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt, graphicsModule.services(), audioModule.services(), resourceModule.services());

    auto rootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    auto animRootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false, nullptr);
    animRootNode->vectorTracks()[ControllerTypes::position].add(0.0f, glm::vec3(0.0f));
    animRootNode->vectorTracks()[ControllerTypes::position].add(1.0f, glm::vec3(4.0f, 0.0f, 0.0f));
    auto animation = std::make_shared<Animation>("presentation", 1.0f, 0.0f, "root_node", animRootNode, std::vector<Animation::Event>());
    auto model = Model("animated_model", 0, rootNode, std::vector<std::shared_ptr<Animation>> {animation}, "", 1.0f);
    model.init();

    auto modelSceneNode = scene->newModel(model, ModelUsage::GUI);
    modelSceneNode->init();
    modelSceneNode->playAnimation("presentation");
    modelSceneNode->update(1.0f);
    ASSERT_TRUE(modelSceneNode->isAnimationFinished());

    modelSceneNode->playAnimation("presentation");
    ASSERT_TRUE(modelSceneNode->isAnimationFinished());
    ASSERT_FLOAT_EQ(1.0f, modelSceneNode->animationChannels().front().time);

    ASSERT_TRUE(modelSceneNode->restartAnimation("presentation"));
    ASSERT_FALSE(modelSceneNode->isAnimationFinished());
    ASSERT_FLOAT_EQ(0.0f, modelSceneNode->animationChannels().front().time);
    ASSERT_TRUE(modelSceneNode->animationChannels().front().stateByNodeNumber.empty());

    modelSceneNode->update(0.25f);
    EXPECT_FALSE(modelSceneNode->isAnimationFinished());
    EXPECT_FLOAT_EQ(0.25f, modelSceneNode->animationChannels().front().time);
    EXPECT_NEAR(1.0f, modelSceneNode->getNodeByName("root_node")->localTransform()[3].x, 1e-5);
}

TEST(ModelSceneNode, should_not_restart_an_animation_that_is_not_queued) {
    auto graphicsOpt = GraphicsOptions();
    auto pipelineFactory = MockRenderPipelineFactory();
    auto graphicsModule = TestGraphicsModule();
    graphicsModule.init();
    auto audioModule = TestAudioModule();
    audioModule.init();
    auto resourceModule = TestResourceModule();
    resourceModule.init();
    auto scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt, graphicsModule.services(), audioModule.services(), resourceModule.services());

    auto rootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    auto queued = std::make_shared<Animation>("queued", 1.0f, 0.0f, "root_node", nullptr, std::vector<Animation::Event>());
    auto notQueued = std::make_shared<Animation>("not_queued", 1.0f, 0.0f, "root_node", nullptr, std::vector<Animation::Event>());
    auto model = Model("animated_model", 0, rootNode, std::vector<std::shared_ptr<Animation>> {queued, notQueued}, "", 1.0f);
    model.init();

    auto modelSceneNode = scene->newModel(model, ModelUsage::GUI);
    modelSceneNode->init();
    modelSceneNode->playAnimation("queued");
    modelSceneNode->update(0.25f);

    ASSERT_FALSE(modelSceneNode->restartAnimation("not_queued"));
    ASSERT_EQ(1, modelSceneNode->animationChannels().size());
    EXPECT_EQ(queued.get(), modelSceneNode->animationChannels().front().anim);
    EXPECT_FLOAT_EQ(0.25f, modelSceneNode->animationChannels().front().time);
}

TEST(ModelSceneNode, should_restart_only_the_requested_overlay_channel) {
    auto graphicsOpt = GraphicsOptions();
    auto pipelineFactory = MockRenderPipelineFactory();
    auto graphicsModule = TestGraphicsModule();
    graphicsModule.init();
    auto audioModule = TestAudioModule();
    audioModule.init();
    auto resourceModule = TestResourceModule();
    resourceModule.init();
    auto scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt, graphicsModule.services(), audioModule.services(), resourceModule.services());

    auto rootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    auto first = std::make_shared<Animation>("first", 2.0f, 0.0f, "root_node", nullptr, std::vector<Animation::Event>());
    auto second = std::make_shared<Animation>("second", 2.0f, 0.0f, "root_node", nullptr, std::vector<Animation::Event>());
    auto model = Model("animated_model", 0, rootNode, std::vector<std::shared_ptr<Animation>> {first, second}, "", 1.0f);
    model.init();

    auto modelSceneNode = scene->newModel(model, ModelUsage::GUI);
    modelSceneNode->init();
    auto overlay = AnimationProperties::fromFlags(AnimationFlags::loopOverlay);
    modelSceneNode->playAnimation("first", nullptr, overlay);
    modelSceneNode->playAnimation("second", nullptr, overlay);
    modelSceneNode->update(0.5f);

    ASSERT_TRUE(modelSceneNode->restartAnimation("first"));
    ASSERT_EQ(2, modelSceneNode->animationChannels().size());
    EXPECT_EQ(second.get(), modelSceneNode->animationChannels()[0].anim);
    EXPECT_FLOAT_EQ(0.5f, modelSceneNode->animationChannels()[0].time);
    EXPECT_EQ(first.get(), modelSceneNode->animationChannels()[1].anim);
    EXPECT_FLOAT_EQ(0.0f, modelSceneNode->animationChannels()[1].time);
}

TEST(ModelSceneNode, should_play_two_overlayed_animations) {
    // given
    auto graphicsOpt = GraphicsOptions();
    auto pipelineFactory = MockRenderPipelineFactory();

    auto graphicsModule = TestGraphicsModule();
    graphicsModule.init();

    auto audioModule = TestAudioModule();
    audioModule.init();

    auto resourceModule = TestResourceModule();
    resourceModule.init();

    auto scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt, graphicsModule.services(), audioModule.services(), resourceModule.services());

    auto rootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    auto dummyNode = std::make_shared<ModelNode>(1, "dummy_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, rootNode.get());
    rootNode->addChild(dummyNode);

    auto anim1RootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false, nullptr);
    anim1RootNode->vectorTracks()[ControllerTypes::position].add(0.0f, glm::vec3(0.0f));
    anim1RootNode->vectorTracks()[ControllerTypes::position].add(1.0f, glm::vec3(1.0f, 2.0f, 3.0f));

    auto anim2RootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false, nullptr);
    auto anim2DummyNode = std::make_shared<ModelNode>(1, "dummy_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false, anim2RootNode.get());
    anim2DummyNode->vectorTracks()[ControllerTypes::position].add(0.0f, glm::vec3(0.0f));
    anim2DummyNode->vectorTracks()[ControllerTypes::position].add(2.0f, glm::vec3(4.0f, 5.0f, 6.0f));
    anim2RootNode->addChild(anim2DummyNode);

    auto animations = std::vector<std::shared_ptr<Animation>> {
        std::make_shared<Animation>("animation1", 1.0f, 0.5f, "root_node", anim1RootNode, std::vector<Animation::Event>()),
        std::make_shared<Animation>("animation2", 2.0f, 0.5f, "root_node", anim2RootNode, std::vector<Animation::Event>())};

    auto model = Model("some_model", 0, rootNode, animations, "", 1.0f);
    auto modelSceneNode = std::make_shared<ModelSceneNode>(
        model,
        ModelUsage::Creature,
        *scene,
        graphicsModule.services(),
        audioModule.services(),
        resourceModule.services());

    // when
    modelSceneNode->init();
    modelSceneNode->playAnimation("animation1", nullptr, AnimationProperties::fromFlags(AnimationFlags::loopOverlay));
    modelSceneNode->playAnimation("animation2", nullptr, AnimationProperties::fromFlags(AnimationFlags::loopOverlay));
    modelSceneNode->update(1.25f);

    // then
    auto &channels = modelSceneNode->animationChannels();
    EXPECT_EQ(2ll, channels.size());
    EXPECT_NEAR(1.25f, channels[0].time, 1e-5);
    EXPECT_NEAR(0.0f, channels[1].time, 1e-5);
    EXPECT_TRUE(!channels[0].finished);
    EXPECT_TRUE(!channels[1].finished);
    auto rootSceneNode = modelSceneNode->getNodeByName("root_node");
    EXPECT_TRUE(static_cast<bool>(rootSceneNode));
    auto &rootPosition = rootSceneNode->localTransform()[3];
    EXPECT_NEAR(1.0f, rootPosition.x, 1e-5);
    EXPECT_NEAR(2.0f, rootPosition.y, 1e-5);
    EXPECT_NEAR(3.0f, rootPosition.z, 1e-5);
    auto dummySceneNode = modelSceneNode->getNodeByName("dummy_node");
    EXPECT_TRUE(static_cast<bool>(dummySceneNode));
    auto &dummyPosition = dummySceneNode->localTransform()[3];
    EXPECT_NEAR(2.5f, dummyPosition.x, 1e-5);
    EXPECT_NEAR(3.125f, dummyPosition.y, 1e-5);
    EXPECT_NEAR(3.75f, dummyPosition.z, 1e-5);
}

TEST(ModelSceneNode, hould_transition_between_two_animations) {
    // given
    auto graphicsOpt = GraphicsOptions();
    auto pipelineFactory = MockRenderPipelineFactory();

    auto graphicsModule = TestGraphicsModule();
    graphicsModule.init();

    auto audioModule = TestAudioModule();
    audioModule.init();

    auto resourceModule = TestResourceModule();
    resourceModule.init();

    auto scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt, graphicsModule.services(), audioModule.services(), resourceModule.services());

    auto rootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);

    auto anim1RootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false, nullptr);
    anim1RootNode->vectorTracks()[ControllerTypes::position].add(0.0f, glm::vec3(0.0f));
    anim1RootNode->vectorTracks()[ControllerTypes::position].add(1.0f, glm::vec3(1.0f, 2.0f, 3.0f));

    auto anim2RootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false, nullptr);
    anim2RootNode->vectorTracks()[ControllerTypes::position].add(0.0f, glm::vec3(0.0f));
    anim2RootNode->vectorTracks()[ControllerTypes::position].add(2.0f, glm::vec3(4.0f, 5.0f, 6.0f));

    auto animations = std::vector<std::shared_ptr<Animation>> {
        std::make_shared<Animation>("animation1", 1.0f, 0.5f, "root_node", anim1RootNode, std::vector<Animation::Event>()),
        std::make_shared<Animation>("animation2", 2.0f, 0.5f, "root_node", anim2RootNode, std::vector<Animation::Event>())};

    auto model = Model("some_model", 0, rootNode, animations, "", 1.0f);
    auto modelSceneNode = std::make_shared<ModelSceneNode>(
        model,
        ModelUsage::Creature,
        *scene,
        graphicsModule.services(),
        audioModule.services(),
        resourceModule.services());

    // when
    modelSceneNode->init();
    modelSceneNode->playAnimation("animation1", nullptr, AnimationProperties::fromFlags(AnimationFlags::loopBlend));
    modelSceneNode->playAnimation("animation2", nullptr, AnimationProperties::fromFlags(AnimationFlags::loopBlend));
    modelSceneNode->update(1.25f);

    // then
    auto &channels = modelSceneNode->animationChannels();
    EXPECT_EQ(2ll, channels.size());
    EXPECT_NEAR(1.5f, channels[0].time, 1e-5);
    EXPECT_NEAR(0.0f, channels[1].time, 1e-5);
    EXPECT_TRUE(!channels[0].finished);
    EXPECT_TRUE(!channels[1].finished);
    auto rootSceneNode = modelSceneNode->getNodeByName("root_node");
    EXPECT_TRUE(static_cast<bool>(rootSceneNode));
    auto &rootPosition = rootSceneNode->localTransform()[3];
    EXPECT_NEAR(3.0f, rootPosition.x, 1e-5);
    EXPECT_NEAR(3.75f, rootPosition.y, 1e-5);
    EXPECT_NEAR(4.5f, rootPosition.z, 1e-5);
}

TEST(ModelSceneNode, pick_model_at) {
    // given
    auto graphicsOpt = GraphicsOptions();
    auto pipelineFactory = MockRenderPipelineFactory();

    auto graphicsModule = TestGraphicsModule();
    graphicsModule.init();

    auto audioModule = TestAudioModule();
    audioModule.init();

    auto resourceModule = TestResourceModule();
    resourceModule.init();

    auto scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt, graphicsModule.services(), audioModule.services(), resourceModule.services());

    auto rootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    auto dummyNode = std::make_shared<ModelNode>(1, "dummy", glm::vec3(0.0f, 1.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, rootNode.get());
    rootNode->addChild(dummyNode);

    std::vector<std::shared_ptr<Animation>> animations;
    auto dummyModel = Model("dummy1", 0, rootNode, animations, "", 1.0f);

    auto dummy1 = std::make_shared<ModelSceneNode>(
        dummyModel,
        ModelUsage::Creature,
        *scene,
        graphicsModule.services(),
        audioModule.services(),
        resourceModule.services());

    auto dummy2 = std::make_shared<ModelSceneNode>(
        dummyModel,
        ModelUsage::Creature,
        *scene,
        graphicsModule.services(),
        audioModule.services(),
        resourceModule.services());

    scene->addRoot(dummy1);
    scene->addRoot(dummy2);

    dummy1->setLocalTransform(glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
    dummy2->setLocalTransform(glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f)));

    dummy1->setPickable(true);
    dummy2->setPickable(true);

    MockUser user1;
    MockUser user2;

    dummy1->setUser(user1);
    dummy2->setUser(user2);

    std::shared_ptr<CameraSceneNode> camera = scene->newCamera();

    glm::mat4 view(
        glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                    glm::vec3(1.0f, 0.0f, 0.0f),
                    glm::vec3(0.0f, 1.0f, 0.0f)));
    camera->setLocalTransform(view);
    camera->setOrthographicProjection(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, -3.0f);
    scene->setActiveCamera(camera.get());

    ModelSceneNode *picked2 =
        scene->pickModelAt(graphicsOpt.width / 2, graphicsOpt.height / 2, dummy1->user());
    EXPECT_EQ(dummy2.get(), picked2);

    ModelSceneNode *picked1 =
        scene->pickModelAt(graphicsOpt.width / 2, graphicsOpt.height / 2, nullptr);
    EXPECT_EQ(dummy1.get(), picked1);
}

TEST(ModelSceneNode, should_propagate_animation_object_to_creature_attachment) {
    // given
    auto graphicsOpt = GraphicsOptions();
    auto pipelineFactory = MockRenderPipelineFactory();

    auto graphicsModule = TestGraphicsModule();
    graphicsModule.init();

    auto audioModule = TestAudioModule();
    audioModule.init();

    auto resourceModule = TestResourceModule();
    resourceModule.init();

    auto scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt, graphicsModule.services(), audioModule.services(), resourceModule.services());

    // body model, with a head hook and no node of its own to animate
    auto bodyRootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    auto headHookNode = std::make_shared<ModelNode>(1, "headhook", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, bodyRootNode.get());
    bodyRootNode->addChild(headHookNode);

    // body animation, targeting a node that only exists in the head model
    auto animRootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false, nullptr);
    auto animEyelidNode = std::make_shared<ModelNode>(1, "eyelid_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false, animRootNode.get());
    animEyelidNode->vectorTracks()[ControllerTypes::position].add(0.0f, glm::vec3(0.0f));
    animEyelidNode->vectorTracks()[ControllerTypes::position].add(1.0f, glm::vec3(1.0f, 2.0f, 3.0f));
    animRootNode->addChild(animEyelidNode);

    auto bodyAnimations = std::vector<std::shared_ptr<Animation>> {
        std::make_shared<Animation>("some_animation", 1.0f, 0.5f, "root_node", animRootNode, std::vector<Animation::Event>())};

    auto bodyModel = Model("some_body", 0, bodyRootNode, bodyAnimations, "", 1.0f);

    // head model, without any animation of its own
    auto headRootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    auto eyelidNode = std::make_shared<ModelNode>(1, "eyelid_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, headRootNode.get());
    headRootNode->addChild(eyelidNode);

    auto headModel = Model("some_head", 0, headRootNode, std::vector<std::shared_ptr<Animation>>(), "", 1.0f);

    auto bodySceneNode = std::make_shared<ModelSceneNode>(
        bodyModel,
        ModelUsage::Creature,
        *scene,
        graphicsModule.services(),
        audioModule.services(),
        resourceModule.services());

    auto headSceneNode = std::make_shared<ModelSceneNode>(
        headModel,
        ModelUsage::Creature,
        *scene,
        graphicsModule.services(),
        audioModule.services(),
        resourceModule.services());

    // when
    bodySceneNode->init();
    headSceneNode->init();
    bodySceneNode->attach("headhook", *headSceneNode);
    bodySceneNode->playAnimation("some_animation", nullptr, AnimationProperties::fromFlags(AnimationFlags::loop | AnimationFlags::propagate));
    bodySceneNode->update(1.0f);

    // then: the head plays the body animation object, not a lookup by name
    auto &headChannels = headSceneNode->animationChannels();
    ASSERT_EQ(1ll, headChannels.size());
    EXPECT_EQ(bodyModel.getAnimation("some_animation").get(), headChannels[0].anim);

    // then: the body controller reaches the node that only the head owns
    auto eyelidSceneNode = headSceneNode->getNodeByName("eyelid_node");
    ASSERT_TRUE(static_cast<bool>(eyelidSceneNode));
    auto &eyelidPosition = eyelidSceneNode->localTransform()[3];
    EXPECT_NEAR(1.0f, eyelidPosition.x, 1e-5);
    EXPECT_NEAR(2.0f, eyelidPosition.y, 1e-5);
    EXPECT_NEAR(3.0f, eyelidPosition.z, 1e-5);
}

TEST(ModelSceneNode, should_keep_equipment_attachment_animation_local) {
    // given
    auto graphicsOpt = GraphicsOptions();
    auto pipelineFactory = MockRenderPipelineFactory();

    auto graphicsModule = TestGraphicsModule();
    graphicsModule.init();

    auto audioModule = TestAudioModule();
    audioModule.init();

    auto resourceModule = TestResourceModule();
    resourceModule.init();

    auto scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt, graphicsModule.services(), audioModule.services(), resourceModule.services());

    // body model, with a right hand hook
    auto bodyRootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    auto handHookNode = std::make_shared<ModelNode>(1, "rhand", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, bodyRootNode.get());
    bodyRootNode->addChild(handHookNode);

    // body animation, targeting the blade node of the weapon model
    auto animRootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false, nullptr);
    auto animBladeNode = std::make_shared<ModelNode>(1, "blade_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false, animRootNode.get());
    animBladeNode->vectorTracks()[ControllerTypes::position].add(0.0f, glm::vec3(0.0f));
    animBladeNode->vectorTracks()[ControllerTypes::position].add(1.0f, glm::vec3(1.0f, 2.0f, 3.0f));
    animRootNode->addChild(animBladeNode);

    auto bodyAnimations = std::vector<std::shared_ptr<Animation>> {
        std::make_shared<Animation>("some_animation", 1.0f, 0.5f, "root_node", animRootNode, std::vector<Animation::Event>())};

    auto bodyModel = Model("some_body", 0, bodyRootNode, bodyAnimations, "", 1.0f);

    // weapon model, with a local retracted blade animation
    auto weaponRootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    auto bladeNode = std::make_shared<ModelNode>(1, "blade_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, weaponRootNode.get());
    weaponRootNode->addChild(bladeNode);

    auto offAnimRootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false, nullptr);
    auto offAnimBladeNode = std::make_shared<ModelNode>(1, "blade_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false, offAnimRootNode.get());
    offAnimBladeNode->vectorTracks()[ControllerTypes::position].add(0.0f, glm::vec3(-1.0f, -2.0f, -3.0f));
    offAnimRootNode->addChild(offAnimBladeNode);

    auto weaponAnimations = std::vector<std::shared_ptr<Animation>> {
        std::make_shared<Animation>("off", 1.0f, 0.5f, "root_node", offAnimRootNode, std::vector<Animation::Event>())};

    auto weaponModel = Model("some_weapon", 0, weaponRootNode, weaponAnimations, "", 1.0f);

    auto bodySceneNode = std::make_shared<ModelSceneNode>(
        bodyModel,
        ModelUsage::Creature,
        *scene,
        graphicsModule.services(),
        audioModule.services(),
        resourceModule.services());

    auto weaponSceneNode = std::make_shared<ModelSceneNode>(
        weaponModel,
        ModelUsage::Equipment,
        *scene,
        graphicsModule.services(),
        audioModule.services(),
        resourceModule.services());

    // when
    bodySceneNode->init();
    weaponSceneNode->init();
    bodySceneNode->attach("rhand", *weaponSceneNode);
    weaponSceneNode->playAnimation("off", nullptr, AnimationProperties::fromFlags(AnimationFlags::loop));
    bodySceneNode->playAnimation("some_animation", nullptr, AnimationProperties::fromFlags(AnimationFlags::loop | AnimationFlags::propagate));
    bodySceneNode->update(1.0f);

    // then: the weapon has no animation of that name and keeps its local state
    EXPECT_EQ("off", weaponSceneNode->activeAnimationName());
    auto &weaponChannels = weaponSceneNode->animationChannels();
    ASSERT_EQ(1ll, weaponChannels.size());
    EXPECT_EQ(weaponModel.getAnimation("off").get(), weaponChannels[0].anim);

    // then: the body controller does not reach the weapon blade
    auto bladeSceneNode = weaponSceneNode->getNodeByName("blade_node");
    ASSERT_TRUE(static_cast<bool>(bladeSceneNode));
    auto &bladePosition = bladeSceneNode->localTransform()[3];
    EXPECT_NEAR(-1.0f, bladePosition.x, 1e-5);
    EXPECT_NEAR(-2.0f, bladePosition.y, 1e-5);
    EXPECT_NEAR(-3.0f, bladePosition.z, 1e-5);
}

TEST(ModelSceneNode, should_keep_lip_animation_alive_for_channel_lifetime) {
    // given
    auto graphicsOpt = GraphicsOptions();
    auto pipelineFactory = MockRenderPipelineFactory();

    auto graphicsModule = TestGraphicsModule();
    graphicsModule.init();

    auto audioModule = TestAudioModule();
    audioModule.init();

    auto resourceModule = TestResourceModule();
    resourceModule.init();

    auto scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt, graphicsModule.services(), audioModule.services(), resourceModule.services());

    auto rootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);

    auto animRootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false, nullptr);
    animRootNode->vectorTracks()[ControllerTypes::position].add(0.0f, glm::vec3(0.0f));
    animRootNode->vectorTracks()[ControllerTypes::position].add(1.0f, glm::vec3(1.0f, 2.0f, 3.0f));

    auto animations = std::vector<std::shared_ptr<Animation>> {
        std::make_shared<Animation>("talk", 1.0f, 0.5f, "root_node", animRootNode, std::vector<Animation::Event>())};

    auto model = Model("some_model", 0, rootNode, animations, "", 1.0f);
    auto modelSceneNode = std::make_shared<ModelSceneNode>(
        model,
        ModelUsage::Creature,
        *scene,
        graphicsModule.services(),
        audioModule.services(),
        resourceModule.services());

    auto lipAnim = std::make_shared<LipAnimation>(
        "talk", 1.0f, std::vector<LipAnimation::Keyframe> {{0.0f, 0}, {1.0f, 1}});

    // when
    modelSceneNode->init();
    modelSceneNode->playAnimation("talk", lipAnim, AnimationProperties::fromFlags(AnimationFlags::loop));

    // then: the channel co-owns the lip animation
    ASSERT_EQ(1ll, modelSceneNode->animationChannels().size());
    EXPECT_EQ(2l, lipAnim.use_count());

    // when: the external owner releases while the looping channel is still active
    modelSceneNode->update(0.5f);
    lipAnim.reset();
    modelSceneNode->update(0.5f);

    // then: the lip animation is still alive, kept by the channel, and usable
    auto &channels = modelSceneNode->animationChannels();
    ASSERT_EQ(1ll, channels.size());
    ASSERT_TRUE(static_cast<bool>(channels[0].lipAnim));
    EXPECT_EQ(1l, channels[0].lipAnim.use_count());
    EXPECT_NEAR(1.0f, channels[0].lipAnim->length(), 1e-5);
}
