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
#include "reone/graphics/shaderprogram.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/texture.h"
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
