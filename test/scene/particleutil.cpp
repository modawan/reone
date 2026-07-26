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

#include <gtest/gtest.h>

#include "reone/graphics/animation.h"
#include "reone/graphics/model.h"
#include "reone/graphics/modelnode.h"
#include "reone/graphics/options.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/emitter.h"
#include "reone/scene/node/model.h"
#include "reone/scene/particleutil.h"

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

class AnimatedEmitterHarness {
public:
    AnimatedEmitterHarness(
        float initialBirthrate,
        float lifeExpectancy,
        std::vector<std::pair<float, float>> animatedBirthrate = {}) {

        _graphicsModule.init();
        _audioModule.init();
        _resourceModule.init();
        _scene = std::make_unique<SceneGraph>(
            "particle-test",
            _pipelineFactory,
            _graphicsOptions,
            _graphicsModule.services(),
            _audioModule.services(),
            _resourceModule.services());

        auto rootNode = std::make_shared<ModelNode>(
            0,
            "root",
            glm::vec3(0.0f),
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
            true);
        auto emitterNode = std::make_shared<ModelNode>(
            1,
            "emitter",
            glm::vec3(0.0f),
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
            true,
            rootNode.get());
        auto emitter = std::make_shared<ModelNode::Emitter>();
        emitter->updateMode = ModelNode::Emitter::UpdateMode::Fountain;
        emitterNode->setEmitter(emitter);
        emitterNode->floatTracks()[ControllerTypes::birthrate].add(0.0f, initialBirthrate);
        emitterNode->floatTracks()[ControllerTypes::lifeExp].add(0.0f, lifeExpectancy);
        emitterNode->floatTracks()[ControllerTypes::sizeStart].add(0.0f, 1.0f);
        emitterNode->floatTracks()[ControllerTypes::sizeMid].add(0.0f, 1.0f);
        emitterNode->floatTracks()[ControllerTypes::sizeEnd].add(0.0f, 1.0f);
        emitterNode->floatTracks()[ControllerTypes::alphaStart].add(0.0f, 1.0f);
        emitterNode->floatTracks()[ControllerTypes::alphaMid].add(0.0f, 1.0f);
        emitterNode->floatTracks()[ControllerTypes::alphaEnd].add(0.0f, 1.0f);
        emitterNode->vectorTracks()[ControllerTypes::colorStart].add(0.0f, glm::vec3(1.0f));
        emitterNode->vectorTracks()[ControllerTypes::colorMid].add(0.0f, glm::vec3(1.0f));
        emitterNode->vectorTracks()[ControllerTypes::colorEnd].add(0.0f, glm::vec3(1.0f));
        rootNode->addChild(emitterNode);

        std::vector<std::shared_ptr<Animation>> animations;
        if (!animatedBirthrate.empty()) {
            auto animationRoot = std::make_shared<ModelNode>(
                0,
                "root",
                glm::vec3(0.0f),
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                false);
            auto animationEmitter = std::make_shared<ModelNode>(
                1,
                "emitter",
                glm::vec3(0.0f),
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                false,
                animationRoot.get());
            for (const auto &[time, birthrate] : animatedBirthrate) {
                animationEmitter->floatTracks()[ControllerTypes::birthrate].add(time, birthrate);
            }
            animationRoot->addChild(animationEmitter);
            animations.push_back(std::make_shared<Animation>(
                "pulse",
                1.0f,
                0.0f,
                "root",
                animationRoot,
                std::vector<Animation::Event>()));
        }

        _model = std::make_unique<Model>(
            "particle-test",
            0,
            rootNode,
            animations,
            "",
            1.0f);
        _modelSceneNode = std::make_shared<ModelSceneNode>(
            *_model,
            ModelUsage::Projectile,
            *_scene,
            _graphicsModule.services(),
            _audioModule.services(),
            _resourceModule.services());
        _modelSceneNode->init();
        if (!animatedBirthrate.empty()) {
            _modelSceneNode->playAnimation(
                "pulse",
                nullptr,
                AnimationProperties::fromFlags(AnimationFlags::loop));
        }
    }

    ModelSceneNode &model() {
        return *_modelSceneNode;
    }

    EmitterSceneNode &emitter() {
        return *static_cast<EmitterSceneNode *>(_modelSceneNode->getNodeByName("emitter"));
    }

private:
    GraphicsOptions _graphicsOptions;
    MockRenderPipelineFactory _pipelineFactory;
    TestGraphicsModule _graphicsModule;
    TestAudioModule _audioModule;
    TestResourceModule _resourceModule;
    std::unique_ptr<SceneGraph> _scene;
    std::unique_ptr<Model> _model;
    std::shared_ptr<ModelSceneNode> _modelSceneNode;
};

} // namespace

TEST(ParticleUtil, should_transform_motion_blur_velocity_to_world_space) {
    // The tar_m05aa waterfall emitters rotate local +Z to world -Z.
    auto emitterTransform = glm::rotate(glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));

    auto basis = particleutil::buildMotionBlurBasis(
        emitterTransform,
        glm::vec3(0.0f, 0.0f, 10.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        1.0f,
        1.0f);

    EXPECT_NEAR(1.0f, basis.right.x, 1e-5f);
    EXPECT_NEAR(0.0f, basis.right.y, 1e-5f);
    EXPECT_NEAR(0.0f, basis.right.z, 1e-5f);
    EXPECT_NEAR(0.0f, basis.up.x, 1e-5f);
    EXPECT_NEAR(0.0f, basis.up.y, 1e-5f);
    EXPECT_NEAR(-1.0f, basis.up.z, 1e-5f);
    EXPECT_NEAR(3.5f, basis.lengthScale, 1e-5f);
}

TEST(ParticleUtil, should_add_motion_trail_length_in_world_units) {
    auto ion = particleutil::buildMotionBlurBasis(
        glm::mat4(1.0f),
        glm::vec3(0.0f, 0.0f, 15.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        3.0f,
        1.0f);
    auto thermal = particleutil::buildMotionBlurBasis(
        glm::mat4(1.0f),
        glm::vec3(0.0f, 0.0f, 20.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        4.0f,
        1.0f);

    EXPECT_NEAR(6.75f, 3.0f * ion.lengthScale, 1e-5f);
    EXPECT_NEAR(9.0f, 4.0f * thermal.lengthScale, 1e-5f);
}

TEST(ParticleUtil, should_fall_back_to_camera_axes_without_motion) {
    auto basis = particleutil::buildMotionBlurBasis(
        glm::mat4(1.0f),
        glm::vec3(0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        3.0f,
        1.0f);

    EXPECT_EQ(glm::vec3(1.0f, 0.0f, 0.0f), basis.right);
    EXPECT_EQ(glm::vec3(0.0f, 1.0f, 0.0f), basis.up);
    EXPECT_EQ(1.0f, basis.lengthScale);
}

TEST(ParticleUtil, should_fall_back_to_camera_axes_for_camera_depth_motion) {
    auto basis = particleutil::buildMotionBlurBasis(
        glm::mat4(1.0f),
        glm::vec3(0.0f, 0.0f, 10.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        1.0f,
        10.0f);

    EXPECT_EQ(glm::vec3(1.0f, 0.0f, 0.0f), basis.right);
    EXPECT_EQ(glm::vec3(0.0f, 1.0f, 0.0f), basis.up);
    EXPECT_EQ(1.0f, basis.lengthScale);
}

TEST(ParticleUtil, should_fall_back_to_camera_axes_for_degenerate_particle_length) {
    auto basis = particleutil::buildMotionBlurBasis(
        glm::mat4(1.0f),
        glm::vec3(0.0f, 0.0f, 10.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        0.0f,
        1.0f);

    EXPECT_EQ(glm::vec3(1.0f, 0.0f, 0.0f), basis.right);
    EXPECT_EQ(glm::vec3(0.0f, 1.0f, 0.0f), basis.up);
    EXPECT_EQ(1.0f, basis.lengthScale);
}

TEST(ParticleUtil, should_spawn_at_the_same_rate_across_frame_rates) {
    float accumulator60Hz = 0.0f;
    int particles60Hz = 0;
    for (int i = 0; i < 60; ++i) {
        particles60Hz += particleutil::advanceSpawnAccumulator(400.0f, 1.0f / 60.0f, accumulator60Hz);
    }

    float accumulator144Hz = 0.0f;
    int particles144Hz = 0;
    for (int i = 0; i < 144; ++i) {
        particles144Hz += particleutil::advanceSpawnAccumulator(400.0f, 1.0f / 144.0f, accumulator144Hz);
    }

    EXPECT_EQ(400, particles60Hz);
    EXPECT_EQ(400, particles144Hz);
    EXPECT_NEAR(accumulator60Hz, accumulator144Hz, 1e-4f);
}

TEST(ParticleUtil, should_clear_spawn_remainder_when_emitter_stops) {
    float accumulator = 0.75f;

    EXPECT_EQ(0, particleutil::advanceSpawnAccumulator(0.0f, 1.0f / 60.0f, accumulator));
    EXPECT_EQ(0.0f, accumulator);
}

TEST(ParticleUtil, should_drop_spawn_catch_up_after_a_discontinuous_frame) {
    float accumulator = 0.75f;

    EXPECT_EQ(0, particleutil::advanceSpawnAccumulator(400.0f, 1.0f, accumulator));
    EXPECT_EQ(0.0f, accumulator);
}

TEST(ParticleUtil, should_cap_spawn_work_without_carrying_integer_debt) {
    float accumulator = 0.0f;

    EXPECT_EQ(256, particleutil::advanceSpawnAccumulator(1'000'000.0f, 1.0f / 60.0f, accumulator));
    EXPECT_GE(accumulator, 0.0f);
    EXPECT_LT(accumulator, 1.0f);
}

TEST(ParticleUtil, should_preserve_the_particle_uniform_batch_boundary) {
    EXPECT_EQ(64, kMaxParticles);
}

TEST(ParticleUtil, should_keep_reconstruction_samples_inside_the_selected_atlas_frame) {
    auto bounds = particleutil::atlasFrameBounds(
        glm::ivec2(256, 128),
        glm::ivec2(4, 2),
        1);

    EXPECT_NEAR(0.25f + 0.5f / 256.0f, bounds.minUV.x, 1e-6f);
    EXPECT_NEAR(0.50f - 0.5f / 256.0f, bounds.maxUV.x, 1e-6f);
    EXPECT_NEAR(0.5f / 128.0f, bounds.minUV.y, 1e-6f);
    EXPECT_NEAR(0.50f - 0.5f / 128.0f, bounds.maxUV.y, 1e-6f);

    auto below = particleutil::clampAtlasUV(bounds, glm::vec2(-10.0f));
    auto above = particleutil::clampAtlasUV(bounds, glm::vec2(10.0f));
    EXPECT_EQ(bounds.minUV, below);
    EXPECT_EQ(bounds.maxUV, above);
}

TEST(ParticleUtil, should_make_degenerate_atlas_grids_safe) {
    auto onePixel = particleutil::atlasFrameBounds(
        glm::ivec2(1, 1),
        glm::ivec2(0, 0),
        -10);
    EXPECT_EQ(glm::vec2(0.5f), onePixel.minUV);
    EXPECT_EQ(glm::vec2(0.5f), onePixel.maxUV);

    auto overDivided = particleutil::atlasFrameBounds(
        glm::ivec2(2, 1),
        glm::ivec2(100, 100),
        10'000);
    EXPECT_LE(overDivided.minUV.x, overDivided.maxUV.x);
    EXPECT_LE(overDivided.minUV.y, overDivided.maxUV.y);
    EXPECT_GE(overDivided.minUV.x, 0.0f);
    EXPECT_LE(overDivided.maxUV.x, 1.0f);
}

TEST(ParticleUtil, should_normalize_the_cubic_reconstruction_kernel) {
    for (float fraction : {0.0f, 0.1f, 0.5f, 0.9f, 1.0f}) {
        auto weights = particleutil::cubicReconstructionWeights(fraction);
        float sum = 0.0f;
        for (float weight : weights) {
            sum += weight;
        }
        EXPECT_NEAR(1.0f, sum, 1e-6f);
    }
}

TEST(ParticleUtil, should_use_stored_alpha_for_white_lighten_sprites) {
    auto decoded = particleutil::decodeParticleSample(
        glm::vec4(1.0f, 1.0f, 1.0f, 0.15f),
        ParticleAlphaMode::AlphaAndLuminance,
        true,
        1.0f);

    EXPECT_EQ(glm::vec3(1.0f), decoded.color);
    EXPECT_NEAR(0.15f, decoded.alpha, 1e-6f);
}

TEST(ParticleUtil, should_use_luminance_for_black_background_intensity_sprites) {
    glm::vec3 authoredColor(0.20f, 0.05f, 0.0f);
    auto decoded = particleutil::decodeParticleSample(
        glm::vec4(authoredColor, 1.0f),
        ParticleAlphaMode::AlphaAndLuminance,
        true,
        1.0f);

    EXPECT_GT(decoded.alpha, 0.0f);
    EXPECT_LT(decoded.alpha, 0.20f);
    EXPECT_NEAR(authoredColor.r, decoded.color.r * decoded.alpha, 1e-6f);
    EXPECT_NEAR(authoredColor.g, decoded.color.g * decoded.alpha, 1e-6f);
    EXPECT_NEAR(authoredColor.b, decoded.color.b * decoded.alpha, 1e-6f);
}

TEST(ParticleUtil, should_not_square_correlated_alpha_and_luminance) {
    auto decoded = particleutil::decodeParticleSample(
        glm::vec4(0.20f, 0.20f, 0.20f, 0.20f),
        ParticleAlphaMode::AlphaAndLuminance,
        true,
        1.0f);

    EXPECT_NEAR(0.20f, decoded.alpha, 1e-6f);
    EXPECT_NEAR(1.0f, decoded.color.r, 1e-6f);
    EXPECT_NEAR(1.0f, decoded.color.g, 1e-6f);
    EXPECT_NEAR(1.0f, decoded.color.b, 1e-6f);
}

TEST(ParticleUtil, should_preserve_authored_alpha_for_non_lighten_particles) {
    glm::vec4 authored(0.8f, 0.1f, 0.05f, 0.35f);
    auto decoded = particleutil::decodeParticleSample(
        authored,
        ParticleAlphaMode::Luminance,
        false,
        2.0f);

    EXPECT_EQ(glm::vec3(authored), decoded.color);
    EXPECT_NEAR(authored.a, decoded.alpha, 1e-6f);
}

TEST(ParticleUtil, should_bound_the_analytic_motion_trail_core) {
    EXPECT_EQ(
        0.0f,
        particleutil::analyticTrailEnvelope(glm::vec2(0.5f), false, 1.0f));
    EXPECT_EQ(
        0.0f,
        particleutil::analyticTrailEnvelope(glm::vec2(0.0f, 0.5f), true, 1.0f));
    EXPECT_NEAR(
        0.8f,
        particleutil::analyticTrailEnvelope(glm::vec2(0.5f), true, 0.8f),
        1e-6f);
    EXPECT_LE(
        particleutil::analyticTrailEnvelope(glm::vec2(0.5f), true, 10.0f),
        1.0f);
    EXPECT_NEAR(
        1e-7f,
        particleutil::analyticTrailEnvelope(glm::vec2(0.5f), true, 1e-7f),
        1e-10f);
}

TEST(ParticleUtil, should_cap_high_velocity_motion_trails_in_world_units) {
    EXPECT_NEAR(
        8.0f,
        particleutil::clampMotionTrailLength(4.0f, 100.0f, 1.0f, 8.0f),
        1e-6f);
    EXPECT_NEAR(
        4.0f,
        particleutil::clampMotionTrailLength(4.0f, 1.0f, 1.0f, 8.0f),
        1e-6f);
    EXPECT_EQ(
        0.0f,
        particleutil::clampMotionTrailLength(0.0f, 100.0f, 1.0f, 8.0f));
}

TEST(ParticleUtil, should_read_animated_emitter_controllers) {
    ModelNode animationNode(
        1,
        "spark",
        glm::vec3(0.0f),
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        true);
    animationNode.floatTracks()[ControllerTypes::birthrate].add(0.0f, 400.0f);
    animationNode.floatTracks()[ControllerTypes::birthrate].add(0.3f, 0.0f);
    animationNode.floatTracks()[ControllerTypes::xSize].add(0.0f, 0.0f);
    animationNode.floatTracks()[ControllerTypes::xSize].add(0.3f, 600.0f);
    animationNode.vectorTracks()[ControllerTypes::colorStart].add(
        0.0f,
        glm::vec3(0.2f, 0.4f, 1.0f));

    auto state = EmitterSceneNode::animationStateAt(animationNode, 0.15f);

    EXPECT_FALSE(state.empty());
    ASSERT_TRUE(state.birthrate);
    EXPECT_NEAR(200.0f, *state.birthrate, 1e-5f);
    ASSERT_TRUE(state.xSize);
    EXPECT_NEAR(300.0f, *state.xSize, 1e-5f);
    ASSERT_TRUE(state.colorStart);
    EXPECT_EQ(glm::vec3(0.2f, 0.4f, 1.0f), *state.colorStart);
    EXPECT_FALSE(state.lifeExpectancy);
}

TEST(ParticleUtil, should_apply_emitter_controllers_before_spawning_in_the_same_frame) {
    AnimatedEmitterHarness harness(
        0.0f,
        1.0f,
        {{0.0f, 0.0f}, {0.1f, 10.0f}});

    harness.model().update(0.1f);

    EXPECT_EQ(1u, harness.emitter().children().size());
}

TEST(ParticleUtil, should_advance_culled_animation_without_hidden_particles_or_spawn_debt) {
    AnimatedEmitterHarness harness(
        20.0f,
        1.0f,
        {{0.0f, 20.0f}, {1.0f, 20.0f}});
    harness.model().setCulled(true);

    for (int i = 0; i < 5; ++i) {
        harness.model().update(0.1f);
    }

    ASSERT_EQ(1u, harness.model().animationChannels().size());
    EXPECT_NEAR(0.5f, harness.model().animationChannels().front().time, 1e-5f);
    EXPECT_TRUE(harness.emitter().children().empty());

    harness.model().setCulled(false);
    harness.model().update(0.05f);

    EXPECT_EQ(1u, harness.emitter().children().size());
}

TEST(ParticleUtil, should_cap_particles_per_emitter_and_reuse_the_pool) {
    AnimatedEmitterHarness harness(2560.0f, 0.1f);

    harness.model().update(0.1f);
    ASSERT_EQ(256u, harness.emitter().children().size());
    std::unordered_set<const SceneNode *> firstGeneration(
        harness.emitter().children().begin(),
        harness.emitter().children().end());

    harness.model().update(0.1f);

    ASSERT_EQ(256u, harness.emitter().children().size());
    for (auto *particle : harness.emitter().children()) {
        EXPECT_EQ(1u, firstGeneration.count(particle));
    }
}
