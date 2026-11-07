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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "reone/graphics/material.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/options.h"
#include "reone/graphics/pbrtextures.h"
#include "reone/graphics/shaderprogram.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/texture.h"
#include "reone/graphics/uniforms.h"
#include "reone/scene/render/pass/pbr.h"
#include "reone/scene/render/pass/retro.h"

#include "../fixtures/graphics.h"

using namespace reone;
using namespace reone::graphics;
using namespace reone::scene;

using testing::_;
using testing::InSequence;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;
using testing::Truly;

namespace {

// None of these objects are initialized, so their constructors and
// destructors make no GL calls and the passes can run against mocks alone.
struct RenderPassFixture {
    GraphicsOptions options;
    NiceMock<MockContext> context;
    NiceMock<MockShaderRegistry> shaders;
    NiceMock<MockMeshRegistry> meshes;
    NiceMock<MockPBRTextures> pbrTextures;
    NiceMock<MockTextureRegistry> textures;
    NiceMock<MockUniforms> uniforms;

    ShaderProgram program {std::vector<std::shared_ptr<Shader>> {}};
    Texture mainTex {"main", TextureType::TwoDim, Texture::Properties {}};
    Mesh mesh {
        std::vector<Mesh::Vertex> {
            Mesh::VertexBuilder().position({0.0f, 0.0f, 0.0f}).build(),
            Mesh::VertexBuilder().position({1.0f, 0.0f, 0.0f}).build(),
            Mesh::VertexBuilder().position({0.0f, 1.0f, 0.0f}).build()},
        Mesh::VertexLayoutBuilder().stride(3 * sizeof(float)).offPosition(0).build(),
        std::vector<Mesh::Face> {Mesh::Face({0, 1, 2})}};
    Mesh billboard {
        std::vector<Mesh::Vertex> {
            Mesh::VertexBuilder().position({0.0f, 0.0f, 0.0f}).build(),
            Mesh::VertexBuilder().position({1.0f, 0.0f, 0.0f}).build(),
            Mesh::VertexBuilder().position({0.0f, 1.0f, 0.0f}).build()},
        Mesh::VertexLayoutBuilder().stride(3 * sizeof(float)).offPosition(0).build(),
        std::vector<Mesh::Face> {Mesh::Face({0, 1, 2})}};
    Mesh aabb {
        std::vector<Mesh::Vertex> {
            Mesh::VertexBuilder().position({0.0f, 0.0f, 0.0f}).build(),
            Mesh::VertexBuilder().position({1.0f, 0.0f, 0.0f}).build(),
            Mesh::VertexBuilder().position({0.0f, 1.0f, 0.0f}).build()},
        Mesh::VertexLayoutBuilder().stride(3 * sizeof(float)).offPosition(0).build(),
        std::vector<Mesh::Face> {Mesh::Face({0, 1, 2})}};
    Mesh grass {
        std::vector<Mesh::Vertex> {
            Mesh::VertexBuilder().position({0.0f, 0.0f, 0.0f}).build(),
            Mesh::VertexBuilder().position({1.0f, 0.0f, 0.0f}).build(),
            Mesh::VertexBuilder().position({0.0f, 1.0f, 0.0f}).build()},
        Mesh::VertexLayoutBuilder().stride(3 * sizeof(float)).offPosition(0).build(),
        std::vector<Mesh::Face> {Mesh::Face({0, 1, 2})}};

    RenderPassFixture() {
        ON_CALL(context, blendMode()).WillByDefault(Return(BlendMode::Normal));
        ON_CALL(context, faceCullMode()).WillByDefault(Return(FaceCullMode::Back));
        ON_CALL(context, polygonMode()).WillByDefault(Return(PolygonMode::Fill));
        ON_CALL(shaders, get(_)).WillByDefault(ReturnRef(program));
        ON_CALL(meshes, get(std::string(MeshName::billboard))).WillByDefault(ReturnRef(billboard));
        ON_CALL(meshes, get(std::string(MeshName::grass))).WillByDefault(ReturnRef(grass));
        ON_CALL(meshes, get(std::string(MeshName::aabb))).WillByDefault(ReturnRef(aabb));
    }

    void makeOpaque(Material &material, std::optional<BlendMode> blending = std::nullopt) {
        material.type = MaterialType::OpaqueModel;
        material.textures.insert({TextureUnits::mainTex, mainTex});
        material.blending = blending;
    }

    RetroRenderPass retro() {
        return RetroRenderPass(options, context, shaders, meshes, textures, uniforms);
    }

    PBRRenderPass pbr() {
        return PBRRenderPass(options, context, shaders, meshes, pbrTextures, textures, uniforms);
    }
};

template <class Pass>
void expectOpaqueDraw(RenderPassFixture &f, const char *programId, Pass &&pass) {
    Material material;
    f.makeOpaque(material);
    {
        InSequence seq;
        EXPECT_CALL(f.shaders, get(std::string(programId))).WillOnce(ReturnRef(f.program));
        EXPECT_CALL(f.context, useProgram(Ref(f.program)));
        EXPECT_CALL(f.context, bindTexture(Ref(f.mainTex), TextureUnits::mainTex));
        EXPECT_CALL(f.uniforms, setLocals(_));
        EXPECT_CALL(f.context, drawMesh(Ref(f.mesh), 1));
    }
    pass.draw(f.mesh, material, glm::mat4(1.0f), glm::mat4(1.0f));
}

template <class Pass>
void expectAdditiveDraw(RenderPassFixture &f, Pass &&pass) {
    Material material;
    f.makeOpaque(material, BlendMode::Additive);
    {
        InSequence seq;
        EXPECT_CALL(f.context, pushBlendMode(BlendMode::Additive));
        EXPECT_CALL(f.context, drawMesh(Ref(f.mesh), 1));
        EXPECT_CALL(f.context, popBlendMode());
    }
    pass.draw(f.mesh, material, glm::mat4(1.0f), glm::mat4(1.0f));
}

template <class Pass>
void expectInstancedParticles(RenderPassFixture &f, Pass &&pass) {
    std::vector<ParticleInstance> particles(5);
    {
        InSequence seq;
        EXPECT_CALL(f.shaders, get(std::string(ShaderProgramId::oitParticles))).WillOnce(ReturnRef(f.program));
        EXPECT_CALL(f.context, useProgram(Ref(f.program)));
        EXPECT_CALL(f.context, bindTexture(Ref(f.mainTex), TextureUnits::mainTex));
        EXPECT_CALL(f.uniforms, setParticles(_));
        EXPECT_CALL(f.context, drawMesh(Ref(f.billboard), 5));
    }
    pass.drawParticles(f.mainTex, FaceCullMode::Back, false, glm::ivec2(1, 1), particles);
}

template <class Pass>
void expectInstancedGrass(RenderPassFixture &f, const char *programId, Pass &&pass) {
    std::vector<GrassInstance> instances(7);
    std::optional<std::reference_wrapper<Texture>> lightmap;
    {
        InSequence seq;
        EXPECT_CALL(f.shaders, get(std::string(programId))).WillOnce(ReturnRef(f.program));
        EXPECT_CALL(f.context, useProgram(Ref(f.program)));
        EXPECT_CALL(f.context, bindTexture(Ref(f.mainTex), TextureUnits::mainTex));
        EXPECT_CALL(f.uniforms, setGrass(_));
        EXPECT_CALL(f.context, drawMesh(Ref(f.grass), 7));
    }
    pass.drawGrass(1.0f, 0.5f, f.mainTex, lightmap, instances);
}

// Uniform setters take a block that edits the persistent CPU-side copy;
// running it against our own copy shows what the pass would upload.
template <class Block>
auto applyTo(Block &block) {
    return [&block](const std::function<void(Block &)> &edit) { edit(block); };
}

template <class Pass>
void expectSaberDisplacementInLocals(RenderPassFixture &f, Pass &&pass) {
    Material material;
    f.makeOpaque(material);
    glm::vec4 displacement {0.25f, -0.5f, 2.0f, 0.0f};
    LocalUniforms locals;
    {
        InSequence seq;
        EXPECT_CALL(f.uniforms, setLocals(_)).WillOnce(applyTo(locals));
        EXPECT_CALL(f.context, drawMesh(Ref(f.mesh), 1));
    }
    pass.drawSaber(f.mesh, material, glm::mat4(1.0f), glm::mat4(1.0f), displacement);
    EXPECT_EQ(displacement, locals.saberDisplacement);
    EXPECT_NE(0, locals.featureMask & UniformsFeatureFlags::saber);
}

template <class Pass>
void expectAABBCornersInBlock(RenderPassFixture &f, const char *programId, Pass &&pass) {
    std::vector<glm::vec4> corners;
    for (int i = 0; i < kNumAABBCorners; ++i) {
        corners.emplace_back(static_cast<float>(i), 1.0f, -static_cast<float>(i), 1.0f);
    }
    AABBUniforms aabb;
    {
        InSequence seq;
        EXPECT_CALL(f.shaders, get(std::string(programId))).WillOnce(ReturnRef(f.program));
        EXPECT_CALL(f.context, useProgram(Ref(f.program)));
        EXPECT_CALL(f.uniforms, setAABB(_)).WillOnce(applyTo(aabb));
        EXPECT_CALL(f.context, withDepthMask(false, _)).WillOnce([](bool, const std::function<void()> &block) { block(); });
        EXPECT_CALL(f.context, withPolygonMode(PolygonMode::Line, _)).WillOnce([](PolygonMode, const std::function<void()> &block) { block(); });
        EXPECT_CALL(f.context, drawMesh(Ref(f.aabb), 1));
    }
    pass.drawAABB(corners);
    for (int i = 0; i < kNumAABBCorners; ++i) {
        EXPECT_EQ(corners[i], aabb.corners[i]) << "corner " << i;
    }
}

template <class Pass>
void expectFewerAABBCornersClearTheRest(RenderPassFixture &f, Pass &&pass) {
    std::vector<glm::vec4> corners(4, glm::vec4(1.0f, 2.0f, 3.0f, 1.0f));
    AABBUniforms aabb;
    std::fill(std::begin(aabb.corners), std::end(aabb.corners), glm::vec4(9.0f));
    EXPECT_CALL(f.uniforms, setAABB(_)).WillOnce(applyTo(aabb));
    pass.drawAABB(corners);
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(corners[i], aabb.corners[i]) << "corner " << i;
    }
    for (int i = 4; i < kNumAABBCorners; ++i) {
        EXPECT_EQ(glm::vec4(0.0f), aabb.corners[i]) << "corner " << i;
    }
}

} // namespace

TEST(RetroRenderPass, opaque_draw_binds_program_and_texture_then_draws_once_through_context) {
    RenderPassFixture f;
    expectOpaqueDraw(f, ShaderProgramId::retroOpaqueModel, f.retro());
}

TEST(RetroRenderPass, additive_material_brackets_draw_with_blend_mode) {
    RenderPassFixture f;
    expectAdditiveDraw(f, f.retro());
}

TEST(RetroRenderPass, particles_upload_then_draw_billboard_instanced_through_context) {
    RenderPassFixture f;
    expectInstancedParticles(f, f.retro());
}

TEST(PBRRenderPass, opaque_draw_binds_program_and_texture_then_draws_once_through_context) {
    RenderPassFixture f;
    expectOpaqueDraw(f, ShaderProgramId::pbrOpaqueModel, f.pbr());
}

TEST(PBRRenderPass, additive_material_brackets_draw_with_blend_mode) {
    RenderPassFixture f;
    expectAdditiveDraw(f, f.pbr());
}

TEST(PBRRenderPass, particles_upload_then_draw_billboard_instanced_through_context) {
    RenderPassFixture f;
    expectInstancedParticles(f, f.pbr());
}

TEST(RetroRenderPass, grass_uploads_clusters_then_draws_one_instance_per_cluster) {
    RenderPassFixture f;
    expectInstancedGrass(f, ShaderProgramId::retroGrass, f.retro());
}

TEST(PBRRenderPass, grass_uploads_clusters_then_draws_one_instance_per_cluster) {
    RenderPassFixture f;
    expectInstancedGrass(f, ShaderProgramId::pbrGrass, f.pbr());
}

TEST(RetroRenderPass, saber_displacement_is_uploaded_with_locals) {
    RenderPassFixture f;
    expectSaberDisplacementInLocals(f, f.retro());
}

TEST(PBRRenderPass, saber_displacement_is_uploaded_with_locals) {
    RenderPassFixture f;
    expectSaberDisplacementInLocals(f, f.pbr());
}

TEST(RetroRenderPass, aabb_corners_are_uploaded_through_the_aabb_block) {
    RenderPassFixture f;
    expectAABBCornersInBlock(f, ShaderProgramId::retroAABB, f.retro());
}

TEST(PBRRenderPass, aabb_corners_are_uploaded_through_the_aabb_block) {
    RenderPassFixture f;
    expectAABBCornersInBlock(f, ShaderProgramId::pbrAABB, f.pbr());
}

TEST(PBRRenderPass, derived_env_map_layer_is_uploaded_with_locals) {
    RenderPassFixture f;
    f.options.pbr = true;
    Texture envMap {"envmap", TextureType::CubeMap, Texture::Properties {}};
    Material material;
    f.makeOpaque(material);
    material.textures.insert({TextureUnits::envMapCube, envMap});
    LocalUniforms locals;
    EXPECT_CALL(f.pbrTextures, findEnvMapDerivedLayer(std::string("envmap"))).WillOnce(Return(std::optional<int>(3)));
    EXPECT_CALL(f.pbrTextures, requestEnvMapDerived(_)).Times(0);
    EXPECT_CALL(f.uniforms, setLocals(_)).WillOnce(applyTo(locals));

    f.pbr().draw(f.mesh, material, glm::mat4(1.0f), glm::mat4(1.0f));

    EXPECT_EQ(3, locals.envMapDerivedLayer);
}

TEST(PBRRenderPass, missing_derived_env_map_is_requested_and_uploads_layer_zero) {
    RenderPassFixture f;
    f.options.pbr = true;
    Texture envMap {"envmap", TextureType::CubeMap, Texture::Properties {}};
    Material material;
    f.makeOpaque(material);
    material.textures.insert({TextureUnits::envMapCube, envMap});
    LocalUniforms locals;
    locals.envMapDerivedLayer = 7;
    EXPECT_CALL(f.pbrTextures, findEnvMapDerivedLayer(std::string("envmap"))).WillOnce(Return(std::nullopt));
    EXPECT_CALL(f.pbrTextures, requestEnvMapDerived(Truly([&envMap](const EnvMapDerivedRequest &request) {
                    return &request.texture == &envMap;
                })));
    EXPECT_CALL(f.uniforms, setLocals(_)).WillOnce(applyTo(locals));

    f.pbr().draw(f.mesh, material, glm::mat4(1.0f), glm::mat4(1.0f));

    EXPECT_EQ(0, locals.envMapDerivedLayer);
}

TEST(RetroRenderPass, fewer_aabb_corners_leave_no_stale_corners_behind) {
    RenderPassFixture f;
    expectFewerAABBCornersClearTheRest(f, f.retro());
}

TEST(PBRRenderPass, fewer_aabb_corners_leave_no_stale_corners_behind) {
    RenderPassFixture f;
    expectFewerAABBCornersClearTheRest(f, f.pbr());
}
