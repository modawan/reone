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

#include "reone/graphics/animation.h"
#include "reone/graphics/material.h"
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

namespace {

// Records the material of every mesh drawn.
class MaterialRecordingRenderPass : public IRenderPass {
public:
    struct MeshDraw {
        Material::TextureUnitToTexture textures;
        int mainTexFrame {0};
    };

    void draw(Mesh &, Material &material, const glm::mat4 &, const glm::mat4 &) override {
        meshDraws.push_back({material.textures, material.mainTexFrame});
    }

    void drawSkinned(Mesh &, Material &, const glm::mat4 &, const glm::mat4 &, const std::vector<glm::mat4> &) override {}
    void drawDangly(Mesh &, Material &, const glm::mat4 &, const glm::mat4 &, const std::vector<glm::vec4> &) override {}
    void drawSaber(Mesh &, Material &, const glm::mat4 &, const glm::mat4 &, const glm::vec4 &) override {}
    void drawBillboard(Texture &, const glm::vec4 &, const glm::mat4 &, const glm::mat4 &, std::optional<float>) override {}
    void drawParticles(Texture &, FaceCullMode, bool, const glm::ivec2 &, const std::vector<ParticleInstance> &) override {}
    void drawProjectedBeam(Mesh &, const glm::mat4 &, const glm::mat4 &, const glm::vec4 &) override {}
    void drawGrass(float, float, Texture &, std::optional<std::reference_wrapper<Texture>> &, const std::vector<GrassInstance> &) override {}
    void drawAABB(const std::vector<glm::vec4> &) override {}
    void drawImage(Texture &, const glm::ivec2 &, const glm::ivec2 &, glm::vec4, glm::mat3x4, ImageAlphaMode) override {}

    std::vector<MeshDraw> meshDraws;
};

std::shared_ptr<Mesh> triangleMesh() {
    std::vector<Mesh::Vertex> vertices {
        Mesh::VertexBuilder().position(glm::vec3(0.0f, 0.0f, 0.0f)).build(),
        Mesh::VertexBuilder().position(glm::vec3(1.0f, 0.0f, 0.0f)).build(),
        Mesh::VertexBuilder().position(glm::vec3(0.0f, 1.0f, 0.0f)).build()};
    Mesh::VertexLayout layout;
    layout.stride = 3 * sizeof(float);
    layout.offPosition = 0;
    std::vector<Mesh::Face> faces {Mesh::Face(std::array<uint16_t, 3> {0, 1, 2})};
    return std::make_shared<Mesh>(std::move(vertices), std::move(layout), std::move(faces));
}

} // namespace

TEST(MeshSceneNode, should_draw_cycling_diffuse_texture_from_texture_array_at_its_current_frame) {
    // given: door_field, the Endar Spire force field, 2x2 frames at 16 fps
    MeshFixture fixture;
    fixture.texture->setType(TextureType::TwoDimArray);
    Texture::Features features;
    features.blending = Texture::Blending::Additive;
    features.procedureType = Texture::ProcedureType::Cycle;
    features.numX = 2;
    features.numY = 2;
    features.fps = 16;
    features.decal = true;
    fixture.texture->setFeatures(features);
    fixture.addMesh(1, "plane_g", "door_field")->mesh = triangleMesh();
    fixture.build();
    // 0.1875 s into its 0.25 s cycle of four frames
    fixture.scene->update(0.1875f);
    MaterialRecordingRenderPass pass;

    // when
    fixture.meshNode("plane_g").render(pass);

    // then
    ASSERT_EQ(1ll, pass.meshDraws.size());
    const auto &draw = pass.meshDraws.front();
    EXPECT_EQ(0ll, draw.textures.count(TextureUnits::mainTex));
    ASSERT_EQ(1ll, draw.textures.count(TextureUnits::mainTexArray));
    EXPECT_EQ(fixture.texture.get(), &draw.textures.at(TextureUnits::mainTexArray).get());
    EXPECT_EQ(2, draw.mainTexFrame);
}

TEST(MeshSceneNode, should_draw_single_image_diffuse_texture_from_main_texture_unit) {
    // given
    MeshFixture fixture;
    fixture.addMesh(1, "plane_g", "some_texture")->mesh = triangleMesh();
    fixture.build();
    fixture.scene->update(0.1875f);
    MaterialRecordingRenderPass pass;

    // when
    fixture.meshNode("plane_g").render(pass);

    // then
    ASSERT_EQ(1ll, pass.meshDraws.size());
    const auto &draw = pass.meshDraws.front();
    EXPECT_EQ(0ll, draw.textures.count(TextureUnits::mainTexArray));
    ASSERT_EQ(1ll, draw.textures.count(TextureUnits::mainTex));
    EXPECT_EQ(fixture.texture.get(), &draw.textures.at(TextureUnits::mainTex).get());
    EXPECT_EQ(0, draw.mainTexFrame);
}
