#include <gtest/gtest.h>

#include "reone/graphics/animation.h"
#include "reone/graphics/model.h"
#include "reone/graphics/options.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/emitter.h"
#include "reone/scene/node/model.h"
#include "reone/scene/node/particle.h"
#include "reone/gui/sceneinitializer.h"
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

namespace {

/** The engine modules and scene graph a GUI model scene needs to exist. */
struct PrewarmScene {
    GraphicsOptions graphicsOpt;
    MockRenderPipelineFactory pipelineFactory;
    TestGraphicsModule graphicsModule;
    TestAudioModule audioModule;
    TestResourceModule resourceModule;
    std::unique_ptr<SceneGraph> graph;
    std::shared_ptr<ModelSceneNode> model;

    PrewarmScene() {
        graphicsModule.init();
        audioModule.init();
        resourceModule.init();
        graph = std::make_unique<SceneGraph>(
            "test",
            pipelineFactory,
            graphicsOpt,
            graphicsModule.services(),
            audioModule.services(),
            resourceModule.services());
    }

    /** Build the GUI scene and hand back its one emitter. */
    EmitterSceneNode *build(Model &source, bool prewarm) {
        gui::SceneInitializer initializer(*graph);
        initializer.modelSupplier([this, &source](ISceneGraph &graph) {
            model = graph.newModel(source, ModelUsage::GUI);
            return model;
        });
        if (prewarm) {
            initializer.prewarmEmitters();
        }
        initializer.invoke();
        return static_cast<EmitterSceneNode *>(model->getNodeByName("emitter"));
    }
};

/**
 * A model carrying one emitter of the given mode, emitting at the given
 * birthrate for the given particle life expectancy.
 */
std::shared_ptr<Model> newEmitterModel(
    const std::string &name,
    ModelNode::Emitter::UpdateMode updateMode,
    float birthrate,
    float lifeExpectancy) {

    auto rootNode = std::make_shared<ModelNode>(
        0, "root", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);

    auto emitter = std::make_shared<ModelNode::Emitter>();
    emitter->updateMode = updateMode;
    emitter->renderMode = ModelNode::Emitter::RenderMode::Normal;

    auto emitterNode = std::make_shared<ModelNode>(
        1, "emitter", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, rootNode.get());
    emitterNode->setEmitter(emitter);
    emitterNode->floatTracks()[ControllerTypes::birthrate].add(0.0f, birthrate);
    emitterNode->floatTracks()[ControllerTypes::lifeExp].add(0.0f, lifeExpectancy);
    // Authored appearance over a particle's life, so a seeded particle can be
    // checked against the point in that life it claims to be at.
    emitterNode->floatTracks()[ControllerTypes::frameStart].add(0.0f, 0.0f);
    emitterNode->floatTracks()[ControllerTypes::frameEnd].add(0.0f, 10.0f);
    emitterNode->floatTracks()[ControllerTypes::sizeStart].add(0.0f, 1.0f);
    emitterNode->floatTracks()[ControllerTypes::sizeMid].add(0.0f, 2.0f);
    emitterNode->floatTracks()[ControllerTypes::sizeEnd].add(0.0f, 3.0f);
    emitterNode->floatTracks()[ControllerTypes::alphaStart].add(0.0f, 1.0f);
    emitterNode->floatTracks()[ControllerTypes::alphaMid].add(0.0f, 0.5f);
    emitterNode->floatTracks()[ControllerTypes::alphaEnd].add(0.0f, 0.0f);
    emitterNode->vectorTracks()[ControllerTypes::colorStart].add(0.0f, glm::vec3(1.0f, 0.0f, 0.0f));
    emitterNode->vectorTracks()[ControllerTypes::colorMid].add(0.0f, glm::vec3(0.0f, 1.0f, 0.0f));
    emitterNode->vectorTracks()[ControllerTypes::colorEnd].add(0.0f, glm::vec3(0.0f, 0.0f, 1.0f));
    rootNode->addChild(emitterNode);

    auto model = std::make_shared<Model>(
        name, 0, rootNode, std::vector<std::shared_ptr<Animation>>(), "", 1.0f);
    model->init();
    return model;
}

std::vector<float> particleLifetimes(const EmitterSceneNode &emitter) {
    std::vector<float> lifetimes;
    for (auto &child : emitter.children()) {
        if (child->type() != SceneNodeType::Particle) {
            continue;
        }
        lifetimes.push_back(static_cast<const ParticleSceneNode *>(child)->lifetime());
    }
    std::sort(lifetimes.begin(), lifetimes.end());
    return lifetimes;
}

const ParticleSceneNode *oldestParticle(const EmitterSceneNode &emitter) {
    const ParticleSceneNode *oldest = nullptr;
    for (auto &child : emitter.children()) {
        if (child->type() != SceneNodeType::Particle) {
            continue;
        }
        auto particle = static_cast<const ParticleSceneNode *>(child);
        if (!oldest || particle->lifetime() > oldest->lifetime()) {
            oldest = particle;
        }
    }
    return oldest;
}

} // namespace

TEST(EmitterPrewarm, a_gui_scene_that_does_not_opt_in_starts_its_fountain_cold) {
    PrewarmScene scene;
    auto model = newEmitterModel("cold", ModelNode::Emitter::UpdateMode::Fountain, 10.0f, 2.0f);

    auto emitter = scene.build(*model, false);

    ASSERT_TRUE(emitter);
    EXPECT_TRUE(emitter->children().empty());
}

TEST(EmitterPrewarm, opting_in_seeds_a_fountain_at_its_steady_state_population) {
    PrewarmScene scene;
    // Ten a second, each living two seconds: twenty alive once it is running.
    auto model = newEmitterModel("warm", ModelNode::Emitter::UpdateMode::Fountain, 10.0f, 2.0f);

    auto emitter = scene.build(*model, true);

    ASSERT_TRUE(emitter);
    EXPECT_EQ(20u, emitter->children().size());
}

TEST(EmitterPrewarm, seeded_particle_ages_are_spread_across_the_authored_lifetime) {
    PrewarmScene scene;
    auto model = newEmitterModel("spread", ModelNode::Emitter::UpdateMode::Fountain, 2.0f, 2.0f);

    auto emitter = scene.build(*model, true);
    ASSERT_TRUE(emitter);

    // Four particles over a two second life: one per half-second birth interval.
    auto lifetimes = particleLifetimes(*emitter);
    ASSERT_EQ(4u, lifetimes.size());
    EXPECT_FLOAT_EQ(0.0f, lifetimes[0]);
    EXPECT_FLOAT_EQ(0.5f, lifetimes[1]);
    EXPECT_FLOAT_EQ(1.0f, lifetimes[2]);
    EXPECT_FLOAT_EQ(1.5f, lifetimes[3]);
    // None of them is already at the end of its life.
    EXPECT_LT(lifetimes.back(), 2.0f);
}

TEST(EmitterPrewarm, a_seeded_particle_looks_like_one_that_lived_that_long) {
    PrewarmScene scene;
    auto model = newEmitterModel("aged", ModelNode::Emitter::UpdateMode::Fountain, 1.0f, 2.0f);

    auto emitter = scene.build(*model, true);
    ASSERT_TRUE(emitter);
    ASSERT_EQ(2u, emitter->children().size());

    // The older of the two is halfway through its life, so it carries the
    // authored midpoint of every property that varies over a particle's life.
    auto oldest = oldestParticle(*emitter);
    ASSERT_TRUE(oldest);
    EXPECT_FLOAT_EQ(1.0f, oldest->lifetime());
    EXPECT_EQ(5, oldest->frame());
    EXPECT_FLOAT_EQ(2.0f, oldest->size().x);
    EXPECT_FLOAT_EQ(0.5f, oldest->alpha());
    EXPECT_LT(glm::length(oldest->color() - glm::vec3(0.0f, 1.0f, 0.0f)), 1e-5f);
}

TEST(EmitterPrewarm, prewarming_leaves_a_single_emitter_alone) {
    PrewarmScene scene;
    auto model = newEmitterModel("single", ModelNode::Emitter::UpdateMode::Single, 10.0f, 2.0f);

    auto emitter = scene.build(*model, true);

    ASSERT_TRUE(emitter);
    // Its one particle is still the ordinary lifecycle's to create.
    EXPECT_TRUE(emitter->children().empty());

    scene.graph->update(0.0f);
    EXPECT_EQ(1u, emitter->children().size());
}

TEST(EmitterPrewarm, emission_carries_on_at_the_authored_cadence_after_prewarming) {
    PrewarmScene scene;
    auto model = newEmitterModel("cadence", ModelNode::Emitter::UpdateMode::Fountain, 2.0f, 2.0f);

    auto emitter = scene.build(*model, true);
    ASSERT_TRUE(emitter);
    ASSERT_EQ(4u, emitter->children().size());

    // A tick well short of the half-second birth interval must not add one: a
    // birth timer left at zero by prewarming would fire straight away.
    scene.graph->update(0.1f);
    EXPECT_EQ(4u, emitter->children().size());

    // Reaching the interval emits exactly one more, and nothing has expired yet.
    scene.graph->update(0.4f);
    EXPECT_EQ(5u, emitter->children().size());
}

TEST(EmitterPrewarm, seeded_ages_follow_the_birth_interval_not_an_even_share_of_the_lifetime) {
    PrewarmScene scene;
    // Two a second for three quarters of a second: the product is 1.5, so an
    // even share of the lifetime and the authored cadence disagree. A running
    // emitter holds the newborn and the one emitted half a second before it;
    // sharing the lifetime evenly would instead claim an age of 0.375, which no
    // emission at this cadence could ever have produced.
    auto model = newEmitterModel("cadence_phase", ModelNode::Emitter::UpdateMode::Fountain, 2.0f, 0.75f);

    auto emitter = scene.build(*model, true);
    ASSERT_TRUE(emitter);

    auto lifetimes = particleLifetimes(*emitter);
    ASSERT_EQ(2u, lifetimes.size());
    EXPECT_FLOAT_EQ(0.0f, lifetimes[0]);
    EXPECT_FLOAT_EQ(0.5f, lifetimes[1]);
    EXPECT_LT(lifetimes.back(), 0.75f);
}

TEST(EmitterPrewarm, a_fractional_birthrate_still_seeds_whole_birth_intervals) {
    PrewarmScene scene;
    // Two and a half a second for a second: three alive, spaced four tenths
    // apart, the oldest still short of the end of its life.
    auto model = newEmitterModel("fractional", ModelNode::Emitter::UpdateMode::Fountain, 2.5f, 1.0f);

    auto emitter = scene.build(*model, true);
    ASSERT_TRUE(emitter);

    auto lifetimes = particleLifetimes(*emitter);
    ASSERT_EQ(3u, lifetimes.size());
    EXPECT_FLOAT_EQ(0.0f, lifetimes[0]);
    EXPECT_FLOAT_EQ(0.4f, lifetimes[1]);
    EXPECT_FLOAT_EQ(0.8f, lifetimes[2]);
    EXPECT_LT(lifetimes.back(), 1.0f);
}

TEST(EmitterPrewarm, no_seeded_particle_is_born_already_expired) {
    // Across products either side of a whole number, the oldest seeded particle
    // must still be short of the life expectancy, or it would be removed on the
    // very first update and the field would open a particle short.
    const std::vector<std::pair<float, float>> cases {
        {2.0f, 2.0f},  // exactly 4
        {2.0f, 0.75f}, // 1.5
        {4.0f, 1.1f},  // 4.4
        {2.5f, 1.0f},  // 2.5
        {3.0f, 1.0f},  // exactly 3
        {7.0f, 0.3f}   // 2.1
    };
    for (const auto &[birthrate, lifeExpectancy] : cases) {
        PrewarmScene scene;
        auto model = newEmitterModel("expiry", ModelNode::Emitter::UpdateMode::Fountain, birthrate, lifeExpectancy);

        auto emitter = scene.build(*model, true);
        ASSERT_TRUE(emitter);

        auto lifetimes = particleLifetimes(*emitter);
        ASSERT_FALSE(lifetimes.empty()) << "birthrate " << birthrate << " life " << lifeExpectancy;
        EXPECT_LT(lifetimes.back(), lifeExpectancy)
            << "birthrate " << birthrate << " life " << lifeExpectancy;

        // Nothing is dropped by the first update, so the field it opens with is
        // the field it keeps.
        auto seeded = emitter->children().size();
        scene.graph->update(0.0f);
        EXPECT_EQ(seeded, emitter->children().size())
            << "birthrate " << birthrate << " life " << lifeExpectancy;
    }
}
