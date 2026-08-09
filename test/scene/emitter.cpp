#include <gtest/gtest.h>

#include "reone/graphics/animation.h"
#include "reone/graphics/model.h"
#include "reone/graphics/options.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/emitter.h"
#include "reone/scene/node/model.h"
#include "reone/scene/node/particle.h"
#include "../fixtures/audio.h"
#include "../fixtures/graphics.h"
#include "../fixtures/resource.h"
#include "../fixtures/scene.h"

using namespace reone;
using namespace reone::graphics;
using namespace reone::audio;
using namespace reone::scene;
using namespace reone::resource;

TEST(EmitterSceneNode, local_z_particle_basis_preserves_authored_winding) {
    auto orientation = glm::angleAxis(glm::half_pi<float>(), glm::normalize(glm::vec3(1.0f, 2.0f, 3.0f)));
    auto transform = glm::mat4_cast(orientation);

    auto basis = EmitterSceneNode::localZParticleBasis(transform);
    auto authoredRight = glm::vec3(transform[0]);
    auto authoredUp = glm::vec3(transform[1]);
    auto authoredForward = glm::normalize(glm::vec3(transform[2]));

    EXPECT_LT(glm::length(basis.right - authoredRight), 0.0001f);
    EXPECT_LT(glm::length(basis.up - authoredUp), 0.0001f);
    EXPECT_GT(glm::dot(glm::normalize(glm::cross(basis.right, basis.up)), authoredForward), 0.9999f);
}

TEST(EmitterSceneNode, particle_bounds_include_rendered_billboard_extent) {
    EmitterSceneNode::ParticleBasis basis {
        glm::vec3(2.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.5f, 0.0f),
    };

    auto bounds = EmitterSceneNode::particleBounds(
        glm::vec3(10.0f, 20.0f, 30.0f),
        glm::vec2(8.0f, 4.0f),
        basis);

    EXPECT_FLOAT_EQ(2.0f, bounds.min().x);
    EXPECT_FLOAT_EQ(19.0f, bounds.min().y);
    EXPECT_FLOAT_EQ(30.0f, bounds.min().z);
    EXPECT_FLOAT_EQ(18.0f, bounds.max().x);
    EXPECT_FLOAT_EQ(21.0f, bounds.max().y);
    EXPECT_FLOAT_EQ(30.0f, bounds.max().z);
}

TEST(EmitterSceneNode, billboard_intersecting_frustum_is_retained_when_center_is_outside) {
    auto graphicsOpt = GraphicsOptions();
    auto pipelineFactory = MockRenderPipelineFactory();
    auto graphicsModule = TestGraphicsModule();
    graphicsModule.init();
    auto audioModule = TestAudioModule();
    audioModule.init();
    auto resourceModule = TestResourceModule();
    resourceModule.init();

    auto scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt, graphicsModule.services(), audioModule.services(), resourceModule.services());
    auto cameraNode = scene->newCamera();
    cameraNode->setPerspectiveProjection(glm::half_pi<float>(), 1.0f, 0.25f, 10.0f);
    scene->setActiveCamera(cameraNode.get());

    auto emitterData = std::make_shared<ModelNode::Emitter>();
    emitterData->updateMode = ModelNode::Emitter::UpdateMode::Fountain;
    emitterData->renderMode = ModelNode::Emitter::RenderMode::Normal;
    auto modelNode = ModelNode(0, "emitter", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    modelNode.setEmitter(emitterData);
    auto emitter = scene->newEmitter(modelNode);
    emitter->init();

    auto particle = scene->newParticle(*emitter);
    particle->setLocalTransform(glm::translate(glm::mat4(1.0f), glm::vec3(5.5f, 0.0f, -5.0f)));
    particle->setSize(glm::vec2(2.0f));

    auto camera = cameraNode->camera();
    ASSERT_FALSE(camera->isInFrustum(particle->origin()));
    EXPECT_TRUE(camera->isInFrustum(emitter->particleBounds(*particle)));
}

TEST(ModelSceneNode, emitter_only_model_is_not_rejected_by_degenerate_root_bounds) {
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
    auto emitterData = std::make_shared<ModelNode::Emitter>();
    emitterData->updateMode = ModelNode::Emitter::UpdateMode::Fountain;
    emitterData->renderMode = ModelNode::Emitter::RenderMode::Normal;
    auto emitterNode = std::make_shared<ModelNode>(1, "emitter_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, rootNode.get());
    emitterNode->setEmitter(emitterData);
    rootNode->addChild(emitterNode);

    auto model = Model("emitter_only", 0, rootNode, std::vector<std::shared_ptr<Animation>>(), "", 1.0f);
    model.init();
    auto modelNode = scene->newModel(model, ModelUsage::GUI);
    modelNode->setLocalTransform(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -100.0f)));
    scene->addRoot(modelNode);

    auto cameraNode = scene->newCamera();
    cameraNode->setPerspectiveProjection(glm::radians(55.0f), 1.0f, 0.25f, 10.0f);
    scene->setActiveCamera(cameraNode.get());

    ASSERT_TRUE(modelNode->isPoint());
    ASSERT_TRUE(modelNode->hasActiveRenderableEmitters());

    scene->update(0.0f);

    EXPECT_FALSE(modelNode->isCulled());
}
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
