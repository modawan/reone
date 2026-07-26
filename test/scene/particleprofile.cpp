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

#include "reone/graphics/model.h"
#include "reone/graphics/modelnode.h"
#include "reone/graphics/options.h"
#include "reone/graphics/uniforms.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/emitter.h"
#include "reone/scene/node/model.h"
#include "reone/scene/render/pass.h"

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

class ParticleProfileHarness {
public:
    ParticleProfileHarness() {
        _graphicsModule.init();
        _audioModule.init();
        _resourceModule.init();
        _scene = std::make_unique<SceneGraph>(
            "particle-profile-test",
            _pipelineFactory,
            _graphicsOptions,
            _graphicsModule.services(),
            _audioModule.services(),
            _resourceModule.services());
    }

    std::shared_ptr<ModelSceneNode> newModel(const std::string &name) {
        auto rootNode = std::make_shared<ModelNode>(
            0,
            "root",
            glm::vec3(0.0f),
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
            true);
        auto hookNode = std::make_shared<ModelNode>(
            1,
            "hook",
            glm::vec3(0.0f),
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
            true,
            rootNode.get());
        auto emitterNode = std::make_shared<ModelNode>(
            2,
            "emitter",
            glm::vec3(0.0f),
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
            true,
            rootNode.get());
        emitterNode->setEmitter(std::make_shared<ModelNode::Emitter>());
        rootNode->addChild(hookNode);
        rootNode->addChild(emitterNode);

        auto model = std::make_unique<Model>(
            name,
            0,
            rootNode,
            std::vector<std::shared_ptr<Animation>>(),
            "",
            1.0f);
        auto modelNode = _scene->newModel(*model, ModelUsage::Projectile);
        _models.push_back(std::move(model));
        return modelNode;
    }

    static const ParticleRenderProfile &profile(ModelSceneNode &model) {
        return static_cast<EmitterSceneNode *>(model.getNodeByName("emitter"))->renderProfile();
    }

private:
    GraphicsOptions _graphicsOptions;
    MockRenderPipelineFactory _pipelineFactory;
    TestGraphicsModule _graphicsModule;
    TestAudioModule _audioModule;
    TestResourceModule _resourceModule;
    std::unique_ptr<SceneGraph> _scene;
    std::vector<std::unique_ptr<Model>> _models;
};

void expectPolicyEqual(const ParticleRenderPolicy &expected, const ParticleUniforms &actual) {
    EXPECT_EQ(static_cast<int>(expected.reconstruction), actual.reconstructionMode);
    EXPECT_EQ(static_cast<int>(expected.alpha), actual.alphaMode);
    EXPECT_EQ(static_cast<int>(expected.trail), actual.trailMode);
    EXPECT_EQ(static_cast<int>(expected.diagnostic), actual.diagnosticMode);
    EXPECT_FLOAT_EQ(expected.reconstructionStrength, actual.reconstructionStrength);
    EXPECT_FLOAT_EQ(expected.alphaExponent, actual.alphaExponent);
    EXPECT_FLOAT_EQ(expected.trailCoreIntensity, actual.trailCoreIntensity);
}

} // namespace

TEST(ParticleProfile, should_default_to_legacy_single_sample_and_lighten_policy) {
    ParticleRenderProfile profile;
    ParticleUniforms uniforms;
    uniforms.reconstructionMode = 42;
    uniforms.alphaMode = 42;
    uniforms.trailMode = 42;
    uniforms.diagnosticMode = 42;
    uniforms.reconstructionStrength = 42.0f;
    uniforms.alphaExponent = 42.0f;
    uniforms.trailCoreIntensity = 42.0f;

    populateParticleUniforms(
        uniforms,
        profile.policy,
        false,
        glm::ivec2(4, 4),
        {});

    EXPECT_EQ(ParticleReconstruction::Legacy, profile.policy.reconstruction);
    EXPECT_EQ(ParticleAlphaMode::Legacy, profile.policy.alpha);
    EXPECT_EQ(ParticleTrailMode::Legacy, profile.policy.trail);
    EXPECT_FLOAT_EQ(1.0f, profile.largeParticleScale);
    EXPECT_FLOAT_EQ(1.0f, profile.opacity);
    EXPECT_EQ(glm::vec3(1.0f), profile.colorTint);
    expectPolicyEqual(profile.policy, uniforms);
    EXPECT_EQ(0, uniforms.motionBlur);
}

TEST(ParticleProfile, should_pack_the_same_non_default_policy_for_retro_and_pbr) {
    ParticleRenderPolicy policy;
    policy.reconstruction = ParticleReconstruction::Cubic;
    policy.alpha = ParticleAlphaMode::AlphaAndLuminance;
    policy.trail = ParticleTrailMode::AnalyticCore;
    policy.diagnostic = ParticleDiagnosticMode::AlphaOnly;
    policy.reconstructionStrength = 0.75f;
    policy.alphaExponent = 1.25f;
    policy.trailCoreIntensity = 0.08f;
    ParticleUniforms retroUniforms;
    ParticleUniforms pbrUniforms;

    populateParticleUniforms(
        retroUniforms,
        policy,
        true,
        glm::ivec2(8, 2),
        {});
    populateParticleUniforms(
        pbrUniforms,
        policy,
        true,
        glm::ivec2(8, 2),
        {});

    expectPolicyEqual(policy, retroUniforms);
    expectPolicyEqual(policy, pbrUniforms);
    EXPECT_EQ(retroUniforms.gridSize, pbrUniforms.gridSize);
    EXPECT_EQ(retroUniforms.motionBlur, pbrUniforms.motionBlur);
}

TEST(ParticleProfile, should_reset_policy_values_for_the_next_emitter_draw) {
    ParticleRenderPolicy enhanced;
    enhanced.reconstruction = ParticleReconstruction::Cubic;
    enhanced.alpha = ParticleAlphaMode::AlphaAndLuminance;
    enhanced.trail = ParticleTrailMode::AnalyticCore;
    enhanced.diagnostic = ParticleDiagnosticMode::TextureOnly;
    enhanced.reconstructionStrength = 0.75f;
    enhanced.alphaExponent = 1.25f;
    enhanced.trailCoreIntensity = 0.08f;
    ParticleUniforms uniforms;
    populateParticleUniforms(uniforms, enhanced, true, glm::ivec2(8, 2), {});

    ParticleRenderPolicy defaults;
    populateParticleUniforms(uniforms, defaults, false, glm::ivec2(1, 1), {});

    expectPolicyEqual(defaults, uniforms);
    EXPECT_EQ(0, uniforms.motionBlur);
    EXPECT_EQ(glm::ivec2(1, 1), uniforms.gridSize);
}

TEST(ParticleProfile, should_propagate_to_existing_nested_model_attachments) {
    ParticleProfileHarness harness;
    auto parent = harness.newModel("parent");
    auto child = harness.newModel("child");
    auto grandchild = harness.newModel("grandchild");
    child->attach("hook", *grandchild);
    parent->attach("hook", *child);

    ParticleRenderProfile profile;
    profile.opacity = 0.4f;
    profile.policy.reconstruction = ParticleReconstruction::Cubic;
    parent->setParticleRenderProfile(profile);

    EXPECT_FLOAT_EQ(0.4f, ParticleProfileHarness::profile(*parent).opacity);
    EXPECT_FLOAT_EQ(0.4f, ParticleProfileHarness::profile(*child).opacity);
    EXPECT_FLOAT_EQ(0.4f, ParticleProfileHarness::profile(*grandchild).opacity);
    EXPECT_EQ(
        ParticleReconstruction::Cubic,
        ParticleProfileHarness::profile(*grandchild).policy.reconstruction);
}

TEST(ParticleProfile, should_propagate_to_model_attachments_added_later) {
    ParticleProfileHarness harness;
    auto parent = harness.newModel("parent");
    ParticleRenderProfile profile;
    profile.motionOpacity = 0.6f;
    profile.policy.trail = ParticleTrailMode::AnalyticCore;
    parent->setParticleRenderProfile(profile);

    auto child = harness.newModel("child");
    parent->attach("hook", *child);

    EXPECT_FLOAT_EQ(0.6f, ParticleProfileHarness::profile(*child).motionOpacity);
    EXPECT_EQ(
        ParticleTrailMode::AnalyticCore,
        ParticleProfileHarness::profile(*child).policy.trail);
}
