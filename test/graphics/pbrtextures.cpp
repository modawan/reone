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

#include "reone/graphics/mesh.h"
#include "reone/graphics/pbrtextures.h"
#include "reone/graphics/shaderprogram.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/texture.h"
#include "reone/graphics/uniforms.h"
#include "reone/system/threadutil.h"

#include "../fixtures/graphics.h"

using namespace reone;
using namespace reone::graphics;

using testing::_;
using testing::NiceMock;
using testing::ReturnRef;

namespace {

// PBRTextures creates its render targets itself. These stubs stand in for
// the GL calls that does, so the passes run against mocks without a
// driver. Every stub is a no-op, except that names are non-zero.
class StubbedTargetsGL {
public:
    StubbedTargetsGL() {
        stub(glad_glGenTextures, _genTextures, genNames);
        stub(glad_glGenRenderbuffers, _genRenderbuffers, genNames);
        stub(glad_glGenFramebuffers, _genFramebuffers, genNames);
        stub(glad_glDeleteTextures, _deleteTextures, [](auto...) {});
        stub(glad_glDeleteRenderbuffers, _deleteRenderbuffers, [](auto...) {});
        stub(glad_glDeleteFramebuffers, _deleteFramebuffers, [](auto...) {});
        stub(glad_glBindTexture, _bindTexture, [](auto...) {});
        stub(glad_glBindRenderbuffer, _bindRenderbuffer, [](auto...) {});
        stub(glad_glBindFramebuffer, _bindFramebuffer, [](auto...) {});
        stub(glad_glTexParameteri, _texParameteri, [](auto...) {});
        stub(glad_glTexParameterf, _texParameterf, [](auto...) {});
        stub(glad_glTexParameterfv, _texParameterfv, [](auto...) {});
        stub(glad_glTexImage2D, _texImage2D, [](auto...) {});
        stub(glad_glTexImage3D, _texImage3D, [](auto...) {});
        stub(glad_glGenerateMipmap, _generateMipmap, [](auto...) {});
        stub(glad_glRenderbufferStorage, _renderbufferStorage, [](auto...) {});
        stub(glad_glFramebufferTexture, _framebufferTexture, [](auto...) {});
        stub(glad_glFramebufferTexture2D, _framebufferTexture2D, [](auto...) {});
        stub(glad_glFramebufferTextureLayer, _framebufferTextureLayer, [](auto...) {});
        stub(glad_glFramebufferRenderbuffer, _framebufferRenderbuffer, [](auto...) {});
    }

    ~StubbedTargetsGL() {
        for (auto it = _restore.rbegin(); it != _restore.rend(); ++it) {
            (*it)();
        }
    }

private:
    static void genNames(GLsizei n, GLuint *names) {
        for (GLsizei i = 0; i < n; ++i) {
            names[i] = static_cast<GLuint>(i + 1);
        }
    }

    template <class Fn, class Stub>
    void stub(Fn &glad, Fn &saved, Stub stubFn) {
        saved = glad;
        glad = stubFn;
        _restore.push_back([&glad, &saved]() { glad = saved; });
    }

    std::vector<std::function<void()>> _restore;

    PFNGLGENTEXTURESPROC _genTextures {nullptr};
    PFNGLGENRENDERBUFFERSPROC _genRenderbuffers {nullptr};
    PFNGLGENFRAMEBUFFERSPROC _genFramebuffers {nullptr};
    PFNGLDELETETEXTURESPROC _deleteTextures {nullptr};
    PFNGLDELETERENDERBUFFERSPROC _deleteRenderbuffers {nullptr};
    PFNGLDELETEFRAMEBUFFERSPROC _deleteFramebuffers {nullptr};
    PFNGLBINDTEXTUREPROC _bindTexture {nullptr};
    PFNGLBINDRENDERBUFFERPROC _bindRenderbuffer {nullptr};
    PFNGLBINDFRAMEBUFFERPROC _bindFramebuffer {nullptr};
    PFNGLTEXPARAMETERIPROC _texParameteri {nullptr};
    PFNGLTEXPARAMETERFPROC _texParameterf {nullptr};
    PFNGLTEXPARAMETERFVPROC _texParameterfv {nullptr};
    PFNGLTEXIMAGE2DPROC _texImage2D {nullptr};
    PFNGLTEXIMAGE3DPROC _texImage3D {nullptr};
    PFNGLGENERATEMIPMAPPROC _generateMipmap {nullptr};
    PFNGLRENDERBUFFERSTORAGEPROC _renderbufferStorage {nullptr};
    PFNGLFRAMEBUFFERTEXTUREPROC _framebufferTexture {nullptr};
    PFNGLFRAMEBUFFERTEXTURE2DPROC _framebufferTexture2D {nullptr};
    PFNGLFRAMEBUFFERTEXTURELAYERPROC _framebufferTextureLayer {nullptr};
    PFNGLFRAMEBUFFERRENDERBUFFERPROC _framebufferRenderbuffer {nullptr};
};

} // namespace

TEST(PBRTextures, prefilter_renders_each_mip_at_its_own_roughness) {
    markMainThread();
    StubbedTargetsGL gl;

    NiceMock<MockContext> context;
    NiceMock<MockMeshRegistry> meshes;
    NiceMock<MockShaderRegistry> shaders;
    NiceMock<MockUniforms> uniforms;

    ShaderProgram otherProgram {std::vector<std::shared_ptr<Shader>> {}};
    ShaderProgram prefilterProgram {std::vector<std::shared_ptr<Shader>> {}};
    Mesh mesh {
        std::vector<Mesh::Vertex> {
            Mesh::VertexBuilder().position({0.0f, 0.0f, 0.0f}).build(),
            Mesh::VertexBuilder().position({1.0f, 0.0f, 0.0f}).build(),
            Mesh::VertexBuilder().position({0.0f, 1.0f, 0.0f}).build()},
        Mesh::VertexLayoutBuilder().stride(3 * sizeof(float)).offPosition(0).build(),
        std::vector<Mesh::Face> {Mesh::Face({0, 1, 2})}};
    ON_CALL(shaders, get(_)).WillByDefault(ReturnRef(otherProgram));
    ON_CALL(shaders, get(std::string(ShaderProgramId::pbrPrefilter))).WillByDefault(ReturnRef(prefilterProgram));
    ON_CALL(meshes, get(_)).WillByDefault(ReturnRef(mesh));

    ShaderProgram *current = nullptr;
    ON_CALL(context, useProgram(_)).WillByDefault([&current](ShaderProgram &program) { current = &program; });
    ON_CALL(context, withViewport(_, _)).WillByDefault([](glm::ivec4, const std::function<void()> &block) { block(); });

    // Roughness in effect for each prefilter draw.
    LocalUniforms locals;
    std::vector<float> drawRoughness;
    ON_CALL(uniforms, setLocals(_)).WillByDefault([&locals](const std::function<void(LocalUniforms &)> &edit) { edit(locals); });
    ON_CALL(context, drawMesh(_, _)).WillByDefault([&](Mesh &, int) {
        if (current == &prefilterProgram) {
            drawRoughness.push_back(locals.roughness);
        }
    });

    Texture envMap {"envmap", TextureType::CubeMap, Texture::Properties {}};
    {
        PBRTextures pbrTextures {context, meshes, shaders, uniforms};
        pbrTextures.requestEnvMapDerived({envMap});
        pbrTextures.refresh();
    }

    std::vector<float> expected;
    for (float roughness : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        expected.insert(expected.end(), kNumCubeFaces, roughness);
    }
    EXPECT_EQ(expected, drawRoughness);
}
