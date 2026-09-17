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

#include <cmath>
#include <limits>

#include "reone/graphics/animation.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/model.h"
#include "reone/graphics/modelnode.h"
#include "reone/graphics/options.h"
#include "reone/graphics/texture.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/mesh.h"
#include "reone/scene/node/model.h"
#include "reone/scene/render/pass.h"

#include "../fixtures/audio.h"
#include "../fixtures/graphics.h"
#include "../fixtures/resource.h"
#include "../fixtures/scene.h"

namespace reone::scene {

// Narrow peer for state that cannot arise through well-formed public transforms,
// e.g. an already-poisoned history or an independently corrupted cached inverse.
// Samples still run through MeshSceneNode::update and the real drawSaber route.
class SaberHistoryTestAccess {
public:
    static bool hasHistory(const MeshSceneNode &node) { return node._saber.hasHistory; }
    static glm::vec3 previousPosition(const MeshSceneNode &node) { return node._saber.prevWorldPos; }
    static glm::vec3 displacement(const MeshSceneNode &node) { return node._saber.displacement; }

    static void seed(MeshSceneNode &node, glm::vec3 previousPosition, glm::vec3 displacement) {
        node._saber.hasHistory = true;
        node._saber.prevWorldPos = previousPosition;
        node._saber.displacement = displacement;
    }

    static void setInverse(MeshSceneNode &node, const glm::mat4 &inverse) {
        node._absTransformInv = inverse;
    }
};

} // namespace reone::scene

using namespace reone;
using namespace reone::audio;
using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;

using testing::_;
using testing::Return;

namespace {

// Minimal creature-usage model with an arbitrary number of mesh nodes, built
// through the production scene graph so that MeshSceneNode::init and the
// setMainTexture propagation path are the real ones.
struct MeshFixture {
    GraphicsOptions graphicsOpt;
    MockRenderPipelineFactory pipelineFactory;
    TestGraphicsModule graphicsModule;
    TestAudioModule audioModule;
    TestResourceModule resourceModule;
    std::unique_ptr<SceneGraph> scene;
    std::shared_ptr<ModelNode> rootNode;
    std::vector<std::shared_ptr<ModelNode::TriangleMesh>> meshes;
    std::unique_ptr<Model> model;
    std::shared_ptr<ModelSceneNode> node;
    std::shared_ptr<Texture> texture;

    MeshFixture() {
        graphicsModule.init();
        audioModule.init();
        resourceModule.init();
        scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt,
                                             graphicsModule.services(),
                                             audioModule.services(),
                                             resourceModule.services());
        rootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f),
                                               glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
        texture = std::make_shared<Texture>("some_texture", TextureType::TwoDim, Texture::Properties());
        EXPECT_CALL(resourceModule.textures(), get(_, _)).WillRepeatedly(Return(texture));
    }

    // Appends a mesh node to the model root. An empty diffuseMap models the K2
    // creature bodies that defer their skin to appearance.2da.
    std::shared_ptr<ModelNode::TriangleMesh> addMesh(int number,
                                                     const std::string &name,
                                                     const std::string &diffuseMap,
                                                     bool render = true) {
        auto mesh = std::make_shared<ModelNode::TriangleMesh>();
        mesh->render = render;
        mesh->diffuseMap = diffuseMap;
        auto meshNode = std::make_shared<ModelNode>(number, name, glm::vec3(0.0f),
                                                    glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, rootNode.get());
        meshNode->setMesh(mesh);
        rootNode->addChild(meshNode);
        meshes.push_back(mesh);
        return mesh;
    }

    void build() {
        model = std::make_unique<Model>("some_model", 0, rootNode,
                                        std::vector<std::shared_ptr<Animation>>(), "", 1.0f);
        node = std::make_shared<ModelSceneNode>(*model, ModelUsage::Creature, *scene,
                                                graphicsModule.services(),
                                                audioModule.services(),
                                                resourceModule.services());
        node->init();
    }

    MeshSceneNode &meshNode(const std::string &name) {
        auto sceneNode = node->getNodeByName(name);
        return *static_cast<MeshSceneNode *>(sceneNode);
    }

    void addSaber(int number, const std::string &name) {
        auto mesh = addMesh(number, name, "synthetic_blade");
        mesh->saber = true;
        // Redistributable triangle, not a game asset. No GL context is needed:
        // the render spy below intercepts the production draw boundary.
        mesh->mesh = std::make_shared<Mesh>(
            std::vector<float> {-0.05f, 0.0f, 0.0f, 0.05f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f},
            Mesh::VertexLayoutBuilder().stride(3 * sizeof(float)).offPosition(0).build(),
            std::vector<Mesh::Face> {Mesh::Face({0, 1, 2})});
    }
};

class SaberRenderSpy : public IRenderPass {
public:
    std::vector<glm::vec4> submitted;

    void drawSaber(Mesh &, Material &, const glm::mat4 &, const glm::mat4 &,
                   const glm::vec4 &displacement) override {
        submitted.push_back(displacement);
    }

    void draw(Mesh &, Material &, const glm::mat4 &, const glm::mat4 &) override { FAIL() << "Expected drawSaber"; }
    void drawSkinned(Mesh &, Material &, const glm::mat4 &, const glm::mat4 &, const std::vector<glm::mat4> &) override { FAIL() << "Expected drawSaber"; }
    void drawDangly(Mesh &, Material &, const glm::mat4 &, const glm::mat4 &, const std::vector<glm::vec4> &) override { FAIL() << "Expected drawSaber"; }
    void drawBillboard(Texture &, const glm::vec4 &, const glm::mat4 &, const glm::mat4 &, std::optional<float>) override { FAIL() << "Expected drawSaber"; }
    void drawParticles(Texture &, FaceCullMode, bool, const glm::ivec2 &, const std::vector<ParticleInstance> &) override { FAIL() << "Expected drawSaber"; }
    void drawGrass(float, float, Texture &, std::optional<std::reference_wrapper<Texture>> &, const std::vector<GrassInstance> &) override { FAIL() << "Expected drawSaber"; }
    void drawAABB(const std::vector<glm::vec4> &) override { FAIL() << "Expected drawSaber"; }
    void drawImage(Texture &, const glm::ivec2 &, const glm::ivec2 &, glm::vec4, glm::mat3x4, ImageAlphaMode) override { FAIL() << "Expected drawSaber"; }
};

glm::mat4 saberPose(glm::vec3 position, glm::vec3 scale = glm::vec3(1.0f)) {
    return glm::scale(glm::translate(glm::mat4(1.0f), position), scale);
}

void expectVectorNear(const glm::vec3 &actual, const glm::vec3 &expected, float tolerance = 1.0e-6f) {
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(std::isfinite(actual[i]));
        EXPECT_NEAR(actual[i], expected[i], tolerance);
    }
}

glm::vec3 renderedSaberDisplacement(MeshSceneNode &mesh) {
    SaberRenderSpy pass;
    mesh.render(pass);
    EXPECT_EQ(1u, pass.submitted.size());
    if (pass.submitted.empty()) {
        return glm::vec3(std::numeric_limits<float>::quiet_NaN());
    }
    const auto &submitted = pass.submitted.front();
    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(std::isfinite(submitted[i])) << "Nonfinite actual drawSaber argument at component " << i;
    }
    EXPECT_FLOAT_EQ(0.0f, submitted.w);
    auto stored = SaberHistoryTestAccess::displacement(mesh);
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(std::isfinite(stored[i]));
        EXPECT_FLOAT_EQ(stored[i], submitted[i]);
    }
    return glm::vec3(submitted);
}

void expectZeroSaber(MeshSceneNode &mesh) {
    expectVectorNear(renderedSaberDisplacement(mesh), glm::vec3(0.0f), 0.0f);
}

void sampleSaber(MeshSceneNode &mesh, const glm::mat4 &pose, float dt = 1.0f / 60.0f) {
    mesh.setLocalTransform(pose);
    mesh.update(dt);
}

void expectSaberReseedsAndMoves(MeshSceneNode &mesh) {
    sampleSaber(mesh, saberPose(glm::vec3(0.25f, 0.0f, 0.0f)));
    EXPECT_TRUE(SaberHistoryTestAccess::hasHistory(mesh));
    expectZeroSaber(mesh);
    sampleSaber(mesh, saberPose(glm::vec3(0.375f, 0.0f, 0.0f)));
    EXPECT_GT(renderedSaberDisplacement(mesh).x, 0.1f);
}

struct SaberFixture : MeshFixture {
    SaberFixture() {
        addSaber(1, "blade");
        build();
    }

    MeshSceneNode &blade() { return meshNode("blade"); }
};

} // namespace

TEST(MeshSceneNode, should_render_mesh_with_embedded_diffuse_texture) {
    // given
    MeshFixture fixture;
    fixture.addMesh(1, "body_g", "c_mk1_drd");
    fixture.build();

    // when
    auto &mesh = fixture.meshNode("body_g");

    // then
    EXPECT_TRUE(mesh.shouldRender());
}

TEST(MeshSceneNode, should_cull_mesh_without_any_diffuse_texture) {
    // given
    MeshFixture fixture;
    fixture.addMesh(1, "body_g", "");
    fixture.build();

    // when
    auto &mesh = fixture.meshNode("body_g");

    // then
    EXPECT_FALSE(mesh.shouldRender());
}

TEST(MeshSceneNode, should_render_mesh_whose_diffuse_texture_is_supplied_at_runtime) {
    // given
    MeshFixture fixture;
    fixture.addMesh(1, "body_g", "");
    fixture.build();
    auto &mesh = fixture.meshNode("body_g");
    ASSERT_FALSE(mesh.shouldRender());

    // when
    fixture.node->setMainTexture(fixture.texture.get());

    // then
    EXPECT_TRUE(mesh.shouldRender());
}

TEST(MeshSceneNode, should_cull_mesh_with_render_flag_off_despite_runtime_texture) {
    // given
    MeshFixture fixture;
    fixture.addMesh(1, "body_g", "", false);
    fixture.build();

    // when
    fixture.node->setMainTexture(fixture.texture.get());

    // then
    EXPECT_FALSE(fixture.meshNode("body_g").shouldRender());
}

TEST(MeshSceneNode, should_cull_aabb_mesh_despite_runtime_texture) {
    // given
    MeshFixture fixture;
    auto mesh = fixture.addMesh(1, "walkmesh_g", "");
    mesh->aabbTree = std::make_shared<ModelNode::AABBTree>();
    fixture.build();

    // when
    fixture.node->setMainTexture(fixture.texture.get());

    // then
    EXPECT_FALSE(fixture.meshNode("walkmesh_g").shouldRender());
}

TEST(MeshSceneNode, should_propagate_runtime_texture_to_every_mesh_of_a_model) {
    // given
    MeshFixture fixture;
    fixture.addMesh(1, "torso_g", "n_rodian01");
    fixture.addMesh(2, "head_g", "");
    fixture.build();
    ASSERT_TRUE(fixture.meshNode("torso_g").shouldRender());
    ASSERT_FALSE(fixture.meshNode("head_g").shouldRender());

    // when
    fixture.node->setMainTexture(fixture.texture.get());

    // then
    EXPECT_TRUE(fixture.meshNode("torso_g").shouldRender());
    EXPECT_TRUE(fixture.meshNode("head_g").shouldRender());
}

TEST(SaberHistory, should_recover_off_to_on_without_teleport) {
    SaberFixture fixture;
    auto &blade = fixture.blade();
    sampleSaber(blade, saberPose(glm::vec3(0.1f, 0.0f, 0.0f)));
    auto off = saberPose(glm::vec3(0.2f, 0.0f, 0.0f), glm::vec3(0.0f));
    sampleSaber(blade, off);
    EXPECT_FALSE(SaberHistoryTestAccess::hasHistory(blade));
    expectZeroSaber(blade);
    // The consumer must not "repair" authored off scale or the cached inverse.
    EXPECT_EQ(off, blade.localTransform());
    EXPECT_FLOAT_EQ(0.0f, blade.absoluteTransform()[0][0]);
    EXPECT_FALSE(std::isfinite(blade.absoluteTransformInverse()[0][0]));

    sampleSaber(blade, saberPose(glm::vec3(0.3f, 0.0f, 0.0f)));
    expectZeroSaber(blade);
    EXPECT_TRUE(SaberHistoryTestAccess::hasHistory(blade));
    for (int frame = 1; frame <= 8; ++frame) {
        sampleSaber(blade, saberPose(glm::vec3(0.3f + frame * 0.0625f, 0.0f, 0.0f)));
        EXPECT_GT(renderedSaberDisplacement(blade).x, 0.05f) << "frame " << frame;
    }
}

TEST(SaberHistory, should_recover_already_poisoned_history_with_valid_current_transform) {
    for (bool poisonPreviousPosition : {false, true}) {
        SCOPED_TRACE(poisonPreviousPosition);
        SaberFixture fixture;
        auto &blade = fixture.blade();
        const auto nan = std::numeric_limits<float>::quiet_NaN();
        SaberHistoryTestAccess::seed(blade,
                                    poisonPreviousPosition ? glm::vec3(nan) : glm::vec3(0.125f, 0.0f, 0.0f),
                                    poisonPreviousPosition ? glm::vec3(0.5f) : glm::vec3(nan));
        // Current input is entirely usable: checking only this input cannot
        // remove the pre-existing poison. This update must seed, not integrate.
        sampleSaber(blade, saberPose(glm::vec3(0.25f, 0.0f, 0.0f)));
        expectZeroSaber(blade);
        EXPECT_TRUE(SaberHistoryTestAccess::hasHistory(blade));
        expectVectorNear(SaberHistoryTestAccess::previousPosition(blade), glm::vec3(0.25f, 0.0f, 0.0f), 0.0f);
        sampleSaber(blade, saberPose(glm::vec3(0.375f, 0.0f, 0.0f)));
        EXPECT_GT(renderedSaberDisplacement(blade).x, 0.1f);
    }
}

TEST(SaberHistory, should_discard_nonfinite_world_transform_and_reseed) {
    const float nonfinite[] {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()};
    for (auto badValue : nonfinite) {
        for (int column : {0, 3}) {
            SCOPED_TRACE(column);
            SaberFixture fixture;
            auto &blade = fixture.blade();
            sampleSaber(blade, saberPose(glm::vec3(0.0f)));
            auto invalid = saberPose(glm::vec3(0.125f, 0.0f, 0.0f));
            invalid[column][0] = badValue;
            sampleSaber(blade, invalid);
            EXPECT_FALSE(SaberHistoryTestAccess::hasHistory(blade));
            expectZeroSaber(blade);
            expectSaberReseedsAndMoves(blade);
        }
    }
}

TEST(SaberHistory, should_validate_inverse_even_when_position_is_finite_and_stationary) {
    const float nonfinite[] {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()};
    for (auto badValue : nonfinite) {
        for (int column : {0, 3}) {
            SCOPED_TRACE(column);
            SaberFixture fixture;
            auto &blade = fixture.blade();
            sampleSaber(blade, saberPose(glm::vec3(0.0f)));
            auto inverse = glm::mat4(1.0f);
            inverse[column][0] = badValue;
            SaberHistoryTestAccess::setInverse(blade, inverse);
            blade.update(1.0f / 60.0f);
            EXPECT_FALSE(SaberHistoryTestAccess::hasHistory(blade));
            expectZeroSaber(blade);
            expectSaberReseedsAndMoves(blade);
        }
    }
}

TEST(SaberHistory, should_discard_nonfinite_timestep) {
    const float invalidDt[] {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -0.01f};
    for (auto dt : invalidDt) {
        SaberFixture fixture;
        auto &blade = fixture.blade();
        sampleSaber(blade, saberPose(glm::vec3(0.0f)));
        sampleSaber(blade, saberPose(glm::vec3(0.125f, 0.0f, 0.0f)), dt);
        EXPECT_FALSE(SaberHistoryTestAccess::hasHistory(blade));
        expectZeroSaber(blade);
        expectSaberReseedsAndMoves(blade);
    }
}

TEST(SaberHistory, should_discard_overflowing_world_delta_or_length) {
    for (bool overflowSubtraction : {false, true}) {
        SCOPED_TRACE(overflowSubtraction);
        SaberFixture fixture;
        auto &blade = fixture.blade();
        auto max = std::numeric_limits<float>::max();
        auto previous = overflowSubtraction ? glm::vec3(-max) : glm::vec3(0.0f);
        SaberHistoryTestAccess::seed(blade, previous, glm::vec3(0.25f));
        // Both stored and current positions are finite. Either subtraction or
        // the magnitude computation overflows before the teleport comparison.
        sampleSaber(blade, saberPose(glm::vec3(0.5f * max)));
        EXPECT_FALSE(SaberHistoryTestAccess::hasHistory(blade));
        expectZeroSaber(blade);
        expectSaberReseedsAndMoves(blade);
    }
}

TEST(SaberHistory, should_discard_overflowing_local_delta) {
    SaberFixture fixture;
    auto &blade = fixture.blade();
    sampleSaber(blade, saberPose(glm::vec3(0.0f)));
    blade.setLocalTransform(saberPose(glm::vec3(0.5f)));
    auto inverse = glm::mat4(1.0f);
    inverse[0][0] = inverse[1][0] = inverse[2][0] = std::numeric_limits<float>::max();
    SaberHistoryTestAccess::setInverse(blade, inverse);
    blade.update(1.0f / 60.0f);
    EXPECT_FALSE(SaberHistoryTestAccess::hasHistory(blade));
    expectZeroSaber(blade);
    expectSaberReseedsAndMoves(blade);
}

TEST(SaberHistory, should_validate_all_components_of_local_conversion) {
    SaberFixture fixture;
    auto &blade = fixture.blade();
    sampleSaber(blade, saberPose(glm::vec3(0.0f)));
    blade.setLocalTransform(saberPose(glm::vec3(0.5f)));
    auto inverse = glm::mat4(1.0f);
    // xyz of the conversion remains finite, but its homogeneous component
    // overflows. Validate the consumed vec4 before dropping that component.
    inverse[0][3] = inverse[1][3] = inverse[2][3] = std::numeric_limits<float>::max();
    SaberHistoryTestAccess::setInverse(blade, inverse);
    blade.update(1.0f / 60.0f);
    EXPECT_FALSE(SaberHistoryTestAccess::hasHistory(blade));
    expectZeroSaber(blade);
    expectSaberReseedsAndMoves(blade);
}

TEST(SaberHistory, should_discard_overflowing_accumulation) {
    SaberFixture fixture;
    auto &blade = fixture.blade();
    auto max = std::numeric_limits<float>::max();
    SaberHistoryTestAccess::seed(blade, glm::vec3(0.0f), glm::vec3(0.9f * max, 0.0f, 0.0f));
    blade.setLocalTransform(saberPose(glm::vec3(0.5f, 0.0f, 0.0f)));
    auto inverse = glm::mat4(1.0f);
    inverse[0][0] = max;
    SaberHistoryTestAccess::setInverse(blade, inverse);
    blade.update(1.0f / 60.0f);
    EXPECT_FALSE(SaberHistoryTestAccess::hasHistory(blade));
    expectZeroSaber(blade);
    expectSaberReseedsAndMoves(blade);
}

TEST(SaberHistory, should_discard_overflowing_damping_factor) {
    SaberFixture fixture;
    auto &blade = fixture.blade();
    sampleSaber(blade, saberPose(glm::vec3(0.0f)));
    sampleSaber(blade, saberPose(glm::vec3(0.125f, 0.0f, 0.0f)));
    blade.update(std::numeric_limits<float>::max());
    EXPECT_FALSE(SaberHistoryTestAccess::hasHistory(blade));
    expectZeroSaber(blade);
    expectSaberReseedsAndMoves(blade);
}

TEST(SaberHistory, should_seed_first_sample_without_spawn_smear) {
    // Include a spawn less than one unit from the default zero anchor: the
    // legacy teleport threshold alone does not protect that initial sample.
    for (auto start : {0.25f, 10000.0f}) {
        SCOPED_TRACE(start);
        SaberFixture fixture;
        auto &blade = fixture.blade();
        EXPECT_FALSE(SaberHistoryTestAccess::hasHistory(blade));
        sampleSaber(blade, saberPose(glm::vec3(start, 0.0f, 0.0f)));
        expectZeroSaber(blade);
        EXPECT_TRUE(SaberHistoryTestAccess::hasHistory(blade));
        sampleSaber(blade, saberPose(glm::vec3(start + 0.125f, 0.0f, 0.0f)));
        expectVectorNear(renderedSaberDisplacement(blade), glm::vec3(0.108333334f, 0.0f, 0.0f));
    }
}

TEST(SaberHistory, should_generate_nonzero_motion_after_seed) {
    // Explicit mutation discriminator: reset-every-frame/always-zero cannot
    // pass by merely guaranteeing finite values.
    SaberFixture fixture;
    auto &blade = fixture.blade();
    sampleSaber(blade, saberPose(glm::vec3(0.0f)));
    sampleSaber(blade, saberPose(glm::vec3(0.125f, 0.0f, 0.0f)));
    expectVectorNear(renderedSaberDisplacement(blade), glm::vec3(0.108333334f, 0.0f, 0.0f));
    sampleSaber(blade, saberPose(glm::vec3(0.25f, 0.0f, 0.0f)));
    expectVectorNear(renderedSaberDisplacement(blade), glm::vec3(0.202222228f, 0.0f, 0.0f));
}

TEST(SaberHistory, should_preserve_valid_motion_recurrence_with_rotation_and_scale) {
    const glm::vec3 scales[] {{1.0f, 1.0f, 1.0f}, {0.25f, 2.0f, 0.5f}, {-0.5f, 2.0f, 1.0f}};
    for (const auto &scale : scales) {
        SaberFixture fixture;
        auto &blade = fixture.blade();
        const float dt = 1.0f / 60.0f;
        glm::vec3 previous(0.0f);
        glm::vec3 expected(0.0f);
        sampleSaber(blade, saberPose(previous, scale), dt);
        for (int frame = 1; frame <= 24; ++frame) {
            SCOPED_TRACE(frame);
            glm::vec3 position(0.03125f * frame, 0.125f * std::sin(0.2f * frame), 0.0f);
            auto pose = glm::translate(glm::mat4(1.0f), position);
            pose = glm::rotate(pose, 0.1f * frame, glm::vec3(0.0f, 0.0f, 1.0f));
            pose = glm::scale(pose, scale);
            // Characterization oracle of the PREVIOUS finite recurrence, not
            // a substitute for exercising the implementation and draw route.
            glm::vec3 deltaLocal = glm::inverse(pose) * glm::vec4(position - previous, 0.0f);
            expected += deltaLocal;
            expected -= expected * glm::min(8.0f * dt, 1.0f);
            sampleSaber(blade, pose, dt);
            // Same float operations; 2e-6 absorbs harmless transform product
            // rounding through the fixture's identity-parent scene hierarchy.
            expectVectorNear(renderedSaberDisplacement(blade), expected, 2.0e-6f);
            EXPECT_TRUE(SaberHistoryTestAccess::hasHistory(blade));
            previous = position;
        }
    }
}

TEST(SaberHistory, should_accept_tiny_invertible_scale_without_magic_epsilon) {
    for (float scale : {1.0e-4f, 1.0e-8f, -1.0e-8f}) {
        SCOPED_TRACE(scale);
        SaberFixture fixture;
        auto &blade = fixture.blade();
        sampleSaber(blade, saberPose(glm::vec3(0.0f), glm::vec3(scale)));
        EXPECT_TRUE(SaberHistoryTestAccess::hasHistory(blade));
        sampleSaber(blade, saberPose(glm::vec3(0.015625f, 0.0f, 0.0f), glm::vec3(scale)));
        auto expected = (0.015625f / scale) * (1.0f - 8.0f / 60.0f);
        expectVectorNear(renderedSaberDisplacement(blade), glm::vec3(expected, 0.0f, 0.0f), std::abs(expected) * 2.0e-6f);
        EXPECT_TRUE(SaberHistoryTestAccess::hasHistory(blade));
    }
}

TEST(SaberHistory, should_keep_stationary_history_zero_and_decay_when_motion_stops) {
    for (int hz : {30, 60, 144}) {
        SCOPED_TRACE(hz);
        SaberFixture fixture;
        auto &blade = fixture.blade();
        const float dt = 1.0f / hz;
        auto stationary = saberPose(glm::vec3(0.25f, 0.0f, 0.0f));
        sampleSaber(blade, stationary, dt);
        for (int frame = 0; frame < hz; ++frame) {
            sampleSaber(blade, stationary, dt);
            expectZeroSaber(blade);
        }
        stationary = saberPose(glm::vec3(0.5f, 0.0f, 0.0f));
        sampleSaber(blade, stationary, dt);
        auto expected = renderedSaberDisplacement(blade);
        auto initial = expected.x;
        ASSERT_GT(initial, 0.0f);
        for (int frame = 0; frame < hz; ++frame) {
            auto before = expected.x;
            expected -= expected * glm::min(8.0f * dt, 1.0f);
            sampleSaber(blade, stationary, dt);
            auto actual = renderedSaberDisplacement(blade);
            expectVectorNear(actual, expected);
            EXPECT_LT(actual.x, before);
        }
        EXPECT_LT(renderedSaberDisplacement(blade).x, initial * 0.001f);
    }
}

TEST(SaberHistory, should_preserve_origin_only_rotation_behavior) {
    SaberFixture fixture;
    auto &blade = fixture.blade();
    sampleSaber(blade, glm::mat4(1.0f));
    for (int frame = 1; frame <= 12; ++frame) {
        sampleSaber(blade, glm::rotate(glm::mat4(1.0f), 0.1f * frame, glm::vec3(0.0f, 1.0f, 0.0f)));
        expectZeroSaber(blade);
    }
}

TEST(SaberHistory, should_preserve_zero_timestep_and_damping_cap) {
    SaberFixture fixture;
    auto &blade = fixture.blade();
    sampleSaber(blade, saberPose(glm::vec3(0.0f)), 0.0f);
    sampleSaber(blade, saberPose(glm::vec3(0.125f, 0.0f, 0.0f)), 0.0f);
    expectVectorNear(renderedSaberDisplacement(blade), glm::vec3(0.125f, 0.0f, 0.0f), 0.0f);
    sampleSaber(blade, saberPose(glm::vec3(0.25f, 0.0f, 0.0f)), 0.125f);
    expectZeroSaber(blade);
    sampleSaber(blade, saberPose(glm::vec3(0.375f, 0.0f, 0.0f)), 0.5f);
    expectZeroSaber(blade);
    EXPECT_TRUE(SaberHistoryTestAccess::hasHistory(blade));
}

TEST(SaberHistory, should_recover_across_repeated_off_on_cycles) {
    SaberFixture fixture;
    auto &blade = fixture.blade();
    sampleSaber(blade, saberPose(glm::vec3(0.0f)));
    for (int cycle = 0; cycle < 8; ++cycle) {
        SCOPED_TRACE(cycle);
        auto position = glm::vec3(0.0625f * cycle, 0.0f, 0.0f);
        sampleSaber(blade, saberPose(position, glm::vec3(0.0f)));
        EXPECT_FALSE(SaberHistoryTestAccess::hasHistory(blade));
        expectZeroSaber(blade);
        sampleSaber(blade, saberPose(position + glm::vec3(0.125f, 0.0f, 0.0f)));
        expectZeroSaber(blade);
        sampleSaber(blade, saberPose(position + glm::vec3(0.25f, 0.0f, 0.0f)));
        EXPECT_GT(renderedSaberDisplacement(blade).x, 0.1f);
    }
}

TEST(SaberHistory, should_preserve_teleport_threshold_and_reset_without_stale_fan) {
    SaberFixture fixture;
    auto &blade = fixture.blade();
    sampleSaber(blade, saberPose(glm::vec3(0.0f)));
    sampleSaber(blade, saberPose(glm::vec3(1.0f, 0.0f, 0.0f)));
    EXPECT_GT(renderedSaberDisplacement(blade).x, 0.8f); // Exactly 1 is still motion.
    sampleSaber(blade, saberPose(glm::vec3(2.125f, 0.0f, 0.0f)));
    expectZeroSaber(blade); // Strictly greater than 1 resets, without changing the threshold.
    EXPECT_TRUE(SaberHistoryTestAccess::hasHistory(blade));
    sampleSaber(blade, saberPose(glm::vec3(2.25f, 0.0f, 0.0f)));
    expectVectorNear(renderedSaberDisplacement(blade), glm::vec3(0.108333334f, 0.0f, 0.0f));
}

TEST(SaberHistory, should_start_recreated_node_with_independent_empty_history) {
    SaberFixture fixture;
    auto &oldBlade = fixture.blade();
    sampleSaber(oldBlade, saberPose(glm::vec3(0.0f)));
    sampleSaber(oldBlade, saberPose(glm::vec3(0.125f, 0.0f, 0.0f)));
    EXPECT_GT(renderedSaberDisplacement(oldBlade).x, 0.1f);
    oldBlade.parent()->removeChild(oldBlade);
    fixture.node.reset();
    fixture.build(); // Same authored model data/name, genuinely new scene nodes.
    auto &newBlade = fixture.blade();
    EXPECT_FALSE(SaberHistoryTestAccess::hasHistory(newBlade));
    sampleSaber(newBlade, saberPose(glm::vec3(0.25f, 0.0f, 0.0f)));
    expectZeroSaber(newBlade);
    sampleSaber(newBlade, saberPose(glm::vec3(0.375f, 0.0f, 0.0f)));
    EXPECT_GT(renderedSaberDisplacement(newBlade).x, 0.1f);
}

TEST(SaberHistory, should_isolate_hands_and_double_blade_node_history) {
    MeshFixture fixture;
    const std::string names[] {"right_hand_blade", "left_hand_blade", "double_blade_a", "double_blade_b"};
    for (int i = 0; i < 4; ++i) {
        fixture.addSaber(i + 1, names[i]);
    }
    fixture.build();
    for (const auto &name : names) {
        sampleSaber(fixture.meshNode(name), saberPose(glm::vec3(0.0f)));
    }
    auto &right = fixture.meshNode(names[0]);
    auto &left = fixture.meshNode(names[1]);
    auto &doubleA = fixture.meshNode(names[2]);
    auto &doubleB = fixture.meshNode(names[3]);
    sampleSaber(right, saberPose(glm::vec3(0.125f, 0.0f, 0.0f)));
    sampleSaber(left, saberPose(glm::vec3(-0.25f, 0.0f, 0.0f)));
    sampleSaber(doubleA, saberPose(glm::vec3(0.0f, 0.375f, 0.0f)));
    sampleSaber(doubleB, saberPose(glm::vec3(0.0f, -0.5f, 0.0f)));
    auto leftBefore = renderedSaberDisplacement(left);
    auto doubleABefore = renderedSaberDisplacement(doubleA);
    auto doubleBBefore = renderedSaberDisplacement(doubleB);
    EXPECT_GT(renderedSaberDisplacement(right).x, 0.1f);
    EXPECT_LT(leftBefore.x, -0.2f);
    EXPECT_GT(doubleABefore.y, 0.3f);
    EXPECT_LT(doubleBBefore.y, -0.4f);

    sampleSaber(right, saberPose(glm::vec3(0.25f, 0.0f, 0.0f), glm::vec3(0.0f)));
    expectZeroSaber(right);
    EXPECT_FALSE(SaberHistoryTestAccess::hasHistory(right));
    expectVectorNear(renderedSaberDisplacement(left), leftBefore, 0.0f);
    expectVectorNear(renderedSaberDisplacement(doubleA), doubleABefore, 0.0f);
    expectVectorNear(renderedSaberDisplacement(doubleB), doubleBBefore, 0.0f);
    EXPECT_TRUE(SaberHistoryTestAccess::hasHistory(left));
    EXPECT_TRUE(SaberHistoryTestAccess::hasHistory(doubleA));
    EXPECT_TRUE(SaberHistoryTestAccess::hasHistory(doubleB));
    expectSaberReseedsAndMoves(right);
}
