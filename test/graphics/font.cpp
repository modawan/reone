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

#include "reone/graphics/font.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/shaderprogram.h"
#include "reone/graphics/texture.h"

#include "../fixtures/graphics.h"

using namespace reone;
using namespace reone::graphics;

using testing::_;
using testing::InSequence;
using testing::NiceMock;
using testing::Ref;
using testing::ReturnRef;

namespace {

// Uninitialized mesh, program and texture: no GL calls, so Font runs on mocks.
struct FontFixture {
    NiceMock<MockContext> context;
    NiceMock<MockMeshRegistry> meshes;
    NiceMock<MockShaderRegistry> shaders;
    NiceMock<MockUniforms> uniforms;

    ShaderProgram program {std::vector<std::shared_ptr<Shader>> {}};
    Mesh quad {
        std::vector<Mesh::Vertex> {
            Mesh::VertexBuilder().position({0.0f, 0.0f, 0.0f}).build(),
            Mesh::VertexBuilder().position({1.0f, 0.0f, 0.0f}).build(),
            Mesh::VertexBuilder().position({0.0f, 1.0f, 0.0f}).build()},
        Mesh::VertexLayoutBuilder().stride(3 * sizeof(float)).offPosition(0).build(),
        std::vector<Mesh::Face> {Mesh::Face({0, 1, 2})}};
    Font font {context, meshes, shaders, uniforms};

    FontFixture() {
        ON_CALL(shaders, get(_)).WillByDefault(ReturnRef(program));
        ON_CALL(meshes, get(std::string(MeshName::quad))).WillByDefault(ReturnRef(quad));

        auto texture = std::make_shared<Texture>("font", TextureType::TwoDim, Texture::Properties {});
        texture->setPixels(1, 1, PixelFormat::RGBA8, Texture::Layer {std::make_shared<ByteBuffer>(4, 0)});
        Texture::Features features;
        features.fontHeight = 0.12f;
        features.numChars = 256;
        features.upperLeftCoords.assign(256, glm::vec3(0.0f, 1.0f, 0.0f));
        features.lowerRightCoords.assign(256, glm::vec3(0.5f, 0.0f, 0.0f));
        texture->setFeatures(std::move(features));
        font.load(texture);
    }
};

} // namespace

TEST(Font, line_draws_one_quad_instance_per_glyph_through_context) {
    FontFixture f;
    {
        InSequence seq;
        EXPECT_CALL(f.uniforms, setText(_));
        EXPECT_CALL(f.context, drawMesh(Ref(f.quad), 4));
    }
    f.font.render("abcd", glm::vec3(0.0f));
}

TEST(Font, text_longer_than_one_block_draws_one_instanced_quad_per_block) {
    FontFixture f;
    {
        InSequence seq;
        EXPECT_CALL(f.uniforms, setText(_));
        EXPECT_CALL(f.context, drawMesh(Ref(f.quad), kMaxTextChars));
        EXPECT_CALL(f.uniforms, setText(_));
        EXPECT_CALL(f.context, drawMesh(Ref(f.quad), 3));
    }
    f.font.render(std::string(kMaxTextChars + 3, 'a'), glm::vec3(0.0f));
}
