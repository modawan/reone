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

#include <cstddef>
#include <set>
#include <string>
#include <vector>

#include "reone/graphics/uniforms.h"

#include "../fixtures/glsl.h"

using namespace reone;
using namespace reone::graphics;

namespace {

struct CppMember {
    std::string glslName;
    size_t offset;
    size_t size;
};

// The C++ mirror of one GLSL block or struct, members in GLSL order.
struct CppLayout {
    std::string file;
    std::string glslName; /**< block or struct name */
    bool isStruct;
    size_t size;
    std::vector<CppMember> members;
};

#define MEMBER(glslName, Struct, member)                           \
    CppMember {                                                    \
        glslName, offsetof(Struct, member), sizeof(Struct::member) \
    }

std::vector<CppLayout> cppLayouts() {
    return {
        {"u_globals.glsl", "Light", true, sizeof(GlobalUniformsLight), {
                                                                           MEMBER("position", GlobalUniformsLight, position),
                                                                           MEMBER("color", GlobalUniformsLight, color),
                                                                           MEMBER("multiplier", GlobalUniformsLight, multiplier),
                                                                           MEMBER("radius", GlobalUniformsLight, radius),
                                                                           MEMBER("ambientOnly", GlobalUniformsLight, ambientOnly),
                                                                           MEMBER("dynamicType", GlobalUniformsLight, dynamicType),
                                                                       }},
        {"u_globals.glsl", "Globals", false, sizeof(GlobalUniforms), {
                                                                         MEMBER("uProjection", GlobalUniforms, projection),
                                                                         MEMBER("uProjectionInv", GlobalUniforms, projectionInv),
                                                                         MEMBER("uView", GlobalUniforms, view),
                                                                         MEMBER("uViewInv", GlobalUniforms, viewInv),
                                                                         MEMBER("uCameraPosition", GlobalUniforms, cameraPosition),
                                                                         MEMBER("uWorldAmbientColor", GlobalUniforms, worldAmbientColor),
                                                                         MEMBER("uLights", GlobalUniforms, lights),
                                                                         MEMBER("uShadowLightPosition", GlobalUniforms, shadowLightPosition),
                                                                         MEMBER("uShadowCascadeFarPlanes", GlobalUniforms, shadowCascadeFarPlanes),
                                                                         MEMBER("uShadowLightSpace", GlobalUniforms, shadowLightSpace),
                                                                         MEMBER("uFogColor", GlobalUniforms, fogColor),
                                                                         MEMBER("uClipNear", GlobalUniforms, clipNear),
                                                                         MEMBER("uClipFar", GlobalUniforms, clipFar),
                                                                         MEMBER("uNumLights", GlobalUniforms, numLights),
                                                                         MEMBER("uShadowStrength", GlobalUniforms, shadowStrength),
                                                                         MEMBER("uShadowRadius", GlobalUniforms, shadowRadius),
                                                                         MEMBER("uFogNear", GlobalUniforms, fogNear),
                                                                         MEMBER("uFogFar", GlobalUniforms, fogFar),
                                                                         MEMBER("uForceSight", GlobalUniforms, forceSight),
                                                                     }},
        {"u_locals.glsl", "Locals", false, sizeof(LocalUniforms), {
                                                                      MEMBER("uModel", LocalUniforms, model),
                                                                      MEMBER("uModelInv", LocalUniforms, modelInv),
                                                                      MEMBER("uUV", LocalUniforms, uv),
                                                                      MEMBER("uColor", LocalUniforms, color),
                                                                      MEMBER("uAmbientColor", LocalUniforms, ambientColor),
                                                                      MEMBER("uDiffuseColor", LocalUniforms, diffuseColor),
                                                                      MEMBER("uSelfIllumColor", LocalUniforms, selfIllumColor),
                                                                      MEMBER("uForceSightColor", LocalUniforms, forceSightColor),
                                                                      MEMBER("uSaberDisplacement", LocalUniforms, saberDisplacement),
                                                                      MEMBER("uFeatureMask", LocalUniforms, featureMask),
                                                                      MEMBER("uBumpMapFrame", LocalUniforms, bumpMapFrame),
                                                                      MEMBER("uBumpMapScale", LocalUniforms, bumpMapScale),
                                                                      MEMBER("uWaterAlpha", LocalUniforms, waterAlpha),
                                                                      MEMBER("uBillboardSize", LocalUniforms, billboardSize),
                                                                      MEMBER("uShellOffset", LocalUniforms, shellOffset),
                                                                      MEMBER("uEnvMapDerivedLayer", LocalUniforms, envMapDerivedLayer),
                                                                      MEMBER("uRoughness", LocalUniforms, roughness),
                                                                  }},
        {"u_bones.glsl", "Bones", false, sizeof(BoneUniforms), {
                                                                   MEMBER("uBones", BoneUniforms, bones),
                                                               }},
        {"u_dangly.glsl", "Dangly", false, sizeof(DanglyUniforms), {
                                                                       MEMBER("uDanglyPositions", DanglyUniforms, positions),
                                                                   }},
        {"u_particles.glsl", "Particle", true, sizeof(ParticleUniformsParticle), {
                                                                                     MEMBER("positionFrame", ParticleUniformsParticle, positionFrame),
                                                                                     MEMBER("right", ParticleUniformsParticle, right),
                                                                                     MEMBER("up", ParticleUniformsParticle, up),
                                                                                     MEMBER("color", ParticleUniformsParticle, color),
                                                                                     MEMBER("size", ParticleUniformsParticle, size),
                                                                                 }},
        {"u_particles.glsl", "Particles", false, sizeof(ParticleUniforms), {
                                                                               MEMBER("uGridSize", ParticleUniforms, gridSize),
                                                                               MEMBER("uParticles", ParticleUniforms, particles),
                                                                           }},
        {"u_grass.glsl", "GrassCluster", true, sizeof(GrassUniformsCluster), {
                                                                                 MEMBER("positionVariant", GrassUniformsCluster, positionVariant),
                                                                                 MEMBER("lightmapUV", GrassUniformsCluster, lightmapUV),
                                                                             }},
        {"u_grass.glsl", "Grass", false, sizeof(GrassUniforms), {
                                                                    MEMBER("uGrassQuadSize", GrassUniforms, quadSize),
                                                                    MEMBER("uGrassRadius", GrassUniforms, radius),
                                                                    MEMBER("uGrassClusters", GrassUniforms, clusters),
                                                                }},
        {"u_walkmesh.glsl", "Walkmesh", false, sizeof(WalkmeshUniforms), {
                                                                             MEMBER("uWalkmeshMaterials", WalkmeshUniforms, materials),
                                                                         }},
        {"u_text.glsl", "Character", true, sizeof(TextUniformsCharacter), {
                                                                              MEMBER("posScale", TextUniformsCharacter, posScale),
                                                                              MEMBER("uv", TextUniformsCharacter, uv),
                                                                          }},
        {"u_text.glsl", "Text", false, sizeof(TextUniforms), {
                                                                 MEMBER("uTextChars", TextUniforms, chars),
                                                             }},
        {"u_screeneffect.glsl", "ScreenEffect", false, sizeof(ScreenEffectUniforms), {
                                                                                         MEMBER("uProjection", ScreenEffectUniforms, projection),
                                                                                         MEMBER("uProjectionInv", ScreenEffectUniforms, projectionInv),
                                                                                         MEMBER("uScreenProjection", ScreenEffectUniforms, screenProjection),
                                                                                         MEMBER("uSSAOSamples", ScreenEffectUniforms, ssaoSamples),
                                                                                         MEMBER("uScreenResolution", ScreenEffectUniforms, screenResolution),
                                                                                         MEMBER("uScreenResolutionRcp", ScreenEffectUniforms, screenResolutionRcp),
                                                                                         MEMBER("uBlurDirection", ScreenEffectUniforms, blurDirection),
                                                                                         MEMBER("uClipNear", ScreenEffectUniforms, clipNear),
                                                                                         MEMBER("uClipFar", ScreenEffectUniforms, clipFar),
                                                                                         MEMBER("uSSAOSampleRadius", ScreenEffectUniforms, ssaoSampleRadius),
                                                                                         MEMBER("uSSAOBias", ScreenEffectUniforms, ssaoBias),
                                                                                         MEMBER("uSSRBias", ScreenEffectUniforms, ssrBias),
                                                                                         MEMBER("uSSRPixelStride", ScreenEffectUniforms, ssrPixelStride),
                                                                                         MEMBER("uSSRMaxSteps", ScreenEffectUniforms, ssrMaxSteps),
                                                                                         MEMBER("uSharpenAmount", ScreenEffectUniforms, sharpenAmount),
                                                                                         MEMBER("uSpeedBlurRatio", ScreenEffectUniforms, speedBlurRatio),
                                                                                         MEMBER("uVideoModulationR", ScreenEffectUniforms, videoModulationR),
                                                                                         MEMBER("uVideoModulationG", ScreenEffectUniforms, videoModulationG),
                                                                                         MEMBER("uVideoModulationB", ScreenEffectUniforms, videoModulationB),
                                                                                         MEMBER("uVideoSaturation", ScreenEffectUniforms, videoSaturation),
                                                                                         MEMBER("uVideoSaturationEnabled", ScreenEffectUniforms, videoSaturationEnabled),
                                                                                         MEMBER("uVideoScanNoiseEnabled", ScreenEffectUniforms, videoScanNoiseEnabled),
                                                                                     }},
        {"u_aabb.glsl", "AABB", false, sizeof(AABBUniforms), {
                                                                 MEMBER("uCorners", AABBUniforms, corners),
                                                             }},
        {"u_profiler.glsl", "Profiler", false, sizeof(ProfilerUniforms), {
                                                                             MEMBER("uSeriesColors", ProfilerUniforms, colors),
                                                                             CppMember {"uSeriesValues1", offsetof(ProfilerUniforms, values) + 0 * sizeof(ProfilerUniforms::values[0]), sizeof(ProfilerUniforms::values[0])},
                                                                             CppMember {"uSeriesValues2", offsetof(ProfilerUniforms, values) + 1 * sizeof(ProfilerUniforms::values[0]), sizeof(ProfilerUniforms::values[0])},
                                                                             CppMember {"uSeriesValues3", offsetof(ProfilerUniforms, values) + 2 * sizeof(ProfilerUniforms::values[0]), sizeof(ProfilerUniforms::values[0])},
                                                                             CppMember {"uSeriesValues4", offsetof(ProfilerUniforms, values) + 3 * sizeof(ProfilerUniforms::values[0]), sizeof(ProfilerUniforms::values[0])},
                                                                         }},
    };
}

#undef MEMBER

glsl::Std140Layout glslLayout(const CppLayout &cpp) {
    auto uniforms = glsl::parseUniforms(glsl::readShaderSource(cpp.file));
    if (cpp.isStruct) {
        auto it = uniforms.structs.find(cpp.glslName);
        if (it == uniforms.structs.end()) {
            throw std::runtime_error("struct " + cpp.glslName + " not found in " + cpp.file);
        }
        return glsl::std140Layout(it->second, uniforms.structs);
    }
    for (const auto &block : uniforms.blocks) {
        if (block.name == cpp.glslName) {
            return glsl::std140Layout(block.members, uniforms.structs);
        }
    }
    throw std::runtime_error("block " + cpp.glslName + " not found in " + cpp.file);
}

} // namespace

TEST(UniformLayout, every_cpp_block_mirror_matches_its_glsl_std140_layout) {
    for (const auto &cpp : cppLayouts()) {
        SCOPED_TRACE(cpp.file + " " + cpp.glslName);
        auto glsl = glslLayout(cpp);
        ASSERT_EQ(glsl.members.size(), cpp.members.size());
        for (size_t i = 0; i < cpp.members.size(); ++i) {
            const auto &g = glsl.members[i];
            const auto &c = cpp.members[i];
            EXPECT_EQ(g.name, c.glslName) << "member " << i;
            EXPECT_EQ(g.offset, c.offset) << g.name;
            EXPECT_EQ(g.size, c.size) << g.name;
        }
        // The C++ struct may end in alignment padding, never in an extra
        // member: it covers the block and stops within 16 bytes of its end.
        EXPECT_GE(cpp.size, glsl.size);
        EXPECT_LT(cpp.size, glsl.size + 16);
    }
}

TEST(UniformLayout, every_glsl_block_has_a_cpp_mirror) {
    std::set<std::string> mirrored;
    for (const auto &cpp : cppLayouts()) {
        if (!cpp.isStruct) {
            mirrored.insert(cpp.glslName);
        }
    }
    for (const auto &name : glsl::listShaderSources()) {
        for (const auto &block : glsl::parseUniforms(glsl::readShaderSource(name)).blocks) {
            EXPECT_EQ(1u, mirrored.count(block.name)) << name << " declares block " << block.name << " with no C++ layout check";
        }
    }
}

TEST(UniformLayout, std140_rules_pad_vec3_arrays_matrices_and_structs) {
    auto uniforms = glsl::parseUniforms(
        "struct S { vec3 a; float b; vec2 c; };\n"
        "layout(std140) uniform B {\n"
        "    float f;\n"
        "    vec3 v3;\n"
        "    float g;\n"
        "    vec2 v2;\n"
        "    float arr[3];\n"
        "    mat3 m;\n"
        "    S s;\n"
        "    int last;\n"
        "};\n");
    ASSERT_EQ(1u, uniforms.blocks.size());
    auto layout = glsl::std140Layout(uniforms.blocks[0].members, uniforms.structs);
    std::vector<std::pair<size_t, size_t>> offsetsAndSizes;
    for (const auto &member : layout.members) {
        offsetsAndSizes.emplace_back(member.offset, member.size);
    }
    EXPECT_EQ((std::vector<std::pair<size_t, size_t>> {
                  {0, 4},    // float
                  {16, 12},  // vec3 aligns to 16
                  {28, 4},   // float packs after vec3
                  {32, 8},   // vec2
                  {48, 48},  // float[3], stride 16
                  {96, 48},  // mat3, three 16-byte columns
                  {144, 32}, // struct, size rounded up to 16
                  {176, 4},  // int
              }),
              offsetsAndSizes);
    EXPECT_EQ(180u, layout.size);
}
