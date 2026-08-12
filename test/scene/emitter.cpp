#include <gtest/gtest.h>

#include "reone/graphics/animation.h"
#include "reone/graphics/model.h"
#include "reone/graphics/options.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/emitter.h"
#include "reone/scene/node/model.h"
#include "../fixtures/audio.h"
#include "../fixtures/graphics.h"
#include "../fixtures/resource.h"
#include "../fixtures/scene.h"

using namespace reone;
using namespace reone::graphics;
using namespace reone::audio;
using namespace reone::scene;
using namespace reone::resource;

TEST(EmitterSceneNode, looping_model_animation_rearms_single_emitter_after_particle_expires) {
    auto graphicsOpt = GraphicsOptions();
    auto pipelineFactory = MockRenderPipelineFactory();

    auto graphicsModule = TestGraphicsModule();
    graphicsModule.init();
    auto audioModule = TestAudioModule();
    audioModule.init();
    auto resourceModule = TestResourceModule();
    resourceModule.init();

    auto scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt, graphicsModule.services(), audioModule.services(), resourceModule.services());
    auto rootNode = std::make_shared<ModelNode>(0, "root", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    auto emitter = std::make_shared<ModelNode::Emitter>();
    emitter->updateMode = ModelNode::Emitter::UpdateMode::Single;
    emitter->renderMode = ModelNode::Emitter::RenderMode::Normal;
    auto emitterNode = std::make_shared<ModelNode>(1, "single", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, rootNode.get());
    emitterNode->setEmitter(emitter);
    emitterNode->floatTracks()[ControllerTypes::lifeExp].add(0.0f, 0.05f);
    rootNode->addChild(emitterNode);

    std::vector<std::shared_ptr<Animation>> animations;
    animations.push_back(std::make_shared<Animation>("default", 0.0f, 0.0f, "", nullptr, std::vector<Animation::Event>()));
    auto model = Model("single_loop", 0, rootNode, animations, "", 1.0f);
    model.init();
    auto sceneModel = scene->newModel(model, ModelUsage::GUI);
    sceneModel->init();
    scene->addRoot(sceneModel);

    auto single = static_cast<EmitterSceneNode *>(sceneModel->getNodeByName("single"));
    ASSERT_TRUE(single);

    scene->update(0.0f);
    ASSERT_EQ(1, single->children().size());

    scene->update(0.1f);
    EXPECT_EQ(1, single->children().size());

    scene->update(0.0f);
    EXPECT_EQ(1, single->children().size());
}

TEST(EmitterSceneNode, non_looping_model_animation_does_not_rearm_single_emitter) {
    auto graphicsOpt = GraphicsOptions();
    auto pipelineFactory = MockRenderPipelineFactory();

    auto graphicsModule = TestGraphicsModule();
    graphicsModule.init();
    auto audioModule = TestAudioModule();
    audioModule.init();
    auto resourceModule = TestResourceModule();
    resourceModule.init();

    auto scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt, graphicsModule.services(), audioModule.services(), resourceModule.services());
    auto rootNode = std::make_shared<ModelNode>(0, "root", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    auto emitter = std::make_shared<ModelNode::Emitter>();
    emitter->updateMode = ModelNode::Emitter::UpdateMode::Single;
    emitter->renderMode = ModelNode::Emitter::RenderMode::Normal;
    auto emitterNode = std::make_shared<ModelNode>(1, "single", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, rootNode.get());
    emitterNode->setEmitter(emitter);
    emitterNode->floatTracks()[ControllerTypes::lifeExp].add(0.0f, 0.05f);
    rootNode->addChild(emitterNode);

    auto animation = std::make_shared<Animation>("once", 0.1f, 0.0f, "", nullptr, std::vector<Animation::Event>());
    auto model = Model("single_once", 0, rootNode, std::vector<std::shared_ptr<Animation>> {animation}, "", 1.0f);
    model.init();
    auto sceneModel = scene->newModel(model, ModelUsage::GUI);
    sceneModel->init();
    sceneModel->playAnimation(*animation, nullptr, AnimationProperties());
    scene->addRoot(sceneModel);

    auto single = static_cast<EmitterSceneNode *>(sceneModel->getNodeByName("single"));
    ASSERT_TRUE(single);

    scene->update(0.0f);
    ASSERT_EQ(1, single->children().size());

    scene->update(0.1f);
    scene->update(0.0f);
    EXPECT_TRUE(single->children().empty());
}

TEST(EmitterSceneNode, unrelated_looping_overlay_does_not_rearm_single_emitter) {
    auto graphicsOpt = GraphicsOptions();
    auto pipelineFactory = MockRenderPipelineFactory();

    auto graphicsModule = TestGraphicsModule();
    graphicsModule.init();
    auto audioModule = TestAudioModule();
    audioModule.init();
    auto resourceModule = TestResourceModule();
    resourceModule.init();

    auto scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt, graphicsModule.services(), audioModule.services(), resourceModule.services());
    auto rootNode = std::make_shared<ModelNode>(0, "root", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    auto loopingBranch = std::make_shared<ModelNode>(1, "looping_branch", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, rootNode.get());
    auto singleBranch = std::make_shared<ModelNode>(2, "single_branch", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, rootNode.get());
    rootNode->addChild(loopingBranch);
    rootNode->addChild(singleBranch);

    auto emitter = std::make_shared<ModelNode::Emitter>();
    emitter->updateMode = ModelNode::Emitter::UpdateMode::Single;
    emitter->renderMode = ModelNode::Emitter::RenderMode::Normal;
    auto emitterNode = std::make_shared<ModelNode>(3, "single", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, singleBranch.get());
    emitterNode->setEmitter(emitter);
    emitterNode->floatTracks()[ControllerTypes::lifeExp].add(0.0f, 0.05f);
    singleBranch->addChild(emitterNode);

    auto nonLooping = std::make_shared<Animation>("single_state", 1.0f, 0.0f, "single_branch", nullptr, std::vector<Animation::Event>());
    auto looping = std::make_shared<Animation>("unrelated_loop", 0.05f, 0.0f, "looping_branch", nullptr, std::vector<Animation::Event>());
    auto model = Model("concurrent_single", 0, rootNode, std::vector<std::shared_ptr<Animation>> {nonLooping, looping}, "", 1.0f);
    model.init();
    auto sceneModel = scene->newModel(model, ModelUsage::GUI);
    sceneModel->init();
    sceneModel->playAnimation(*nonLooping, nullptr, AnimationProperties::fromFlags(AnimationFlags::overlay));
    sceneModel->playAnimation(*looping, nullptr, AnimationProperties::fromFlags(AnimationFlags::loopOverlay));
    scene->addRoot(sceneModel);

    auto single = static_cast<EmitterSceneNode *>(sceneModel->getNodeByName("single"));
    ASSERT_TRUE(single);

    scene->update(0.0f);
    ASSERT_EQ(2, sceneModel->animationChannels().size());
    ASSERT_EQ(1, single->children().size());

    scene->update(0.1f);
    scene->update(0.0f);
    EXPECT_TRUE(single->children().empty());
}

TEST(EmitterSceneNode, rearming_single_does_not_affect_fountain_emission) {
    auto graphicsOpt = GraphicsOptions();
    auto pipelineFactory = MockRenderPipelineFactory();

    auto graphicsModule = TestGraphicsModule();
    graphicsModule.init();
    auto audioModule = TestAudioModule();
    audioModule.init();
    auto resourceModule = TestResourceModule();
    resourceModule.init();

    auto scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt, graphicsModule.services(), audioModule.services(), resourceModule.services());
    auto rootNode = std::make_shared<ModelNode>(0, "root", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    auto emitter = std::make_shared<ModelNode::Emitter>();
    emitter->updateMode = ModelNode::Emitter::UpdateMode::Fountain;
    emitter->renderMode = ModelNode::Emitter::RenderMode::Normal;
    auto emitterNode = std::make_shared<ModelNode>(1, "fountain", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, rootNode.get());
    emitterNode->setEmitter(emitter);
    emitterNode->floatTracks()[ControllerTypes::birthrate].add(0.0f, 10.0f);
    emitterNode->floatTracks()[ControllerTypes::lifeExp].add(0.0f, 1.0f);
    rootNode->addChild(emitterNode);

    auto animation = std::make_shared<Animation>("loop", 0.05f, 0.0f, "root", nullptr, std::vector<Animation::Event>());
    auto model = Model("fountain_loop", 0, rootNode, std::vector<std::shared_ptr<Animation>> {animation}, "", 1.0f);
    model.init();
    auto sceneModel = scene->newModel(model, ModelUsage::GUI);
    sceneModel->init();
    sceneModel->playAnimation(*animation, nullptr, AnimationProperties::fromFlags(AnimationFlags::loop));
    scene->addRoot(sceneModel);

    auto fountain = static_cast<EmitterSceneNode *>(sceneModel->getNodeByName("fountain"));
    ASSERT_TRUE(fountain);

    scene->update(0.05f);
    ASSERT_EQ(1, fountain->children().size());
    scene->update(0.05f);
    ASSERT_EQ(1, fountain->children().size());
    scene->update(0.05f);
    EXPECT_EQ(2, fountain->children().size());
}
