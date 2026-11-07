#include <gtest/gtest.h>

#include <list>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "reone/resource/provider/shaders.h"

#include "../fixtures/glsl.h"

using namespace reone;
using namespace reone::resource;

namespace {

// SDL_GPU allows each shader stage at most this many uniform buffers and
// samplers.
constexpr int kMaxUniformBlocksPerStage = 4;
constexpr int kMaxSamplersPerStage = 16;

std::string readShader(std::string_view name) {
    return glsl::readShaderSource(std::string(name));
}

std::string composeStage(const std::string &resRef) {
    auto sources = composeShaderSources(resRef, [](const std::string &rr) {
        auto source = readShader(rr + ".glsl");
        if (source.empty()) {
            throw std::runtime_error("Shader source not found: " + rr);
        }
        return source;
    });
    std::string composed;
    for (auto &source : sources) {
        composed += source;
    }
    return composed;
}

std::vector<std::string> looseNames(const glsl::Uniforms &uniforms) {
    std::vector<std::string> names;
    for (const auto &var : uniforms.loose) {
        names.push_back(var.type + " " + var.name);
    }
    return names;
}

std::string bulleted(const std::vector<std::string> &lines) {
    std::ostringstream msg;
    for (const auto &line : lines) {
        msg << "\n  " << line;
    }
    return msg.str();
}

} // namespace

TEST(ShaderSources, declare_no_loose_non_sampler_uniforms_and_only_std140_blocks) {
    std::vector<std::string> offenders;
    for (const auto &name : glsl::listShaderSources()) {
        auto uniforms = glsl::parseUniforms(readShader(name));
        for (const auto &var : uniforms.loose) {
            offenders.push_back(name + ": uniform " + var.type + " " + var.name);
        }
        for (const auto &block : uniforms.blocks) {
            if (!block.std140) {
                offenders.push_back(name + ": block " + block.name + " is not std140");
            }
        }
    }
    EXPECT_TRUE(offenders.empty()) << "Move these into a std140 uniform block:" << bulleted(offenders);
}

TEST(ShaderSources, loose_uniform_lint_ignores_samplers_blocks_and_comments) {
    auto uniforms = glsl::parseUniforms(
        "uniform sampler2D sMainTex;\n"
        "uniform isampler2DArray sIndices;\n"
        "uniform lowp sampler2D sLowp;\n"
        "layout(std140) uniform Locals {\n"
        "    vec4 uColor;\n"
        "};\n"
        "// uniform float uCommented;\n"
        "/* uniform int uAlsoCommented;\n"
        "   uniform int uStillCommented; */\n"
        "bool isEnabled(int flag) {\n"
        "    return flag != 0;\n"
        "}\n");
    EXPECT_TRUE(uniforms.loose.empty()) << bulleted(looseNames(uniforms));
    EXPECT_EQ(3u, uniforms.samplers.size());
}

TEST(ShaderSources, loose_uniform_lint_catches_every_declaration_shape) {
    auto uniforms = glsl::parseUniforms(
        "uniform vec4 uCorners[8];\n"
        "  uniform float uRoughness;\n"
        "layout(location = 3) uniform mat4 uModel;\n"
        "uniform int uA; uniform int uB;\n"
        "uniform float uC, uD[2];\n"
        "uniform\n"
        "    highp vec3\n"
        "    uSplit;\n");
    EXPECT_EQ((std::vector<std::string> {
                  "vec4 uCorners",
                  "float uRoughness",
                  "mat4 uModel",
                  "int uA",
                  "int uB",
                  "float uC",
                  "float uD",
                  "vec3 uSplit"}),
              looseNames(uniforms));
}

TEST(ShaderSources, every_program_stage_fits_sdl_gpu_binding_limits) {
    const auto &specs = shaderProgramSpecs();
    ASSERT_FALSE(specs.empty());
    std::vector<std::string> offenders;
    for (const auto &spec : specs) {
        for (const auto &resRef : {spec.vertex, spec.geometry, spec.fragment}) {
            if (resRef.empty()) {
                continue;
            }
            auto uniforms = glsl::parseUniforms(composeStage(resRef));
            // A cross-compiler gathers loose non-sampler uniforms into an
            // implicit block, which takes a uniform buffer slot of its own.
            bool defaultBlock = !uniforms.loose.empty();
            int numBlocks = static_cast<int>(uniforms.blocks.size()) + (defaultBlock ? 1 : 0);
            if (numBlocks > kMaxUniformBlocksPerStage) {
                std::string names;
                for (const auto &block : uniforms.blocks) {
                    names += " " + block.name;
                }
                if (defaultBlock) {
                    names += " <loose uniforms>";
                }
                offenders.push_back(spec.id + "/" + resRef + ": " + std::to_string(numBlocks) + " uniform blocks:" + names);
            }
            if (uniforms.samplerCount() > kMaxSamplersPerStage) {
                offenders.push_back(spec.id + "/" + resRef + ": " + std::to_string(uniforms.samplerCount()) + " samplers");
            }
        }
    }
    EXPECT_TRUE(offenders.empty()) << "Stages over SDL_GPU's per-stage limits:" << bulleted(offenders);
}

TEST(ShaderSources, block_count_sees_every_layout_qualifier_form) {
    auto uniforms = glsl::parseUniforms(
        "layout(std140) uniform A { vec4 a; };\n"
        "layout(std140, binding = 0) uniform B { vec4 b; };\n"
        "layout(set=1, binding=0, std140) uniform C { vec4 c; };\n"
        "layout(\n"
        "    std140) uniform D\n"
        "{\n"
        "    vec4 d;\n"
        "} instance;\n"
        "uniform E { vec4 e; };\n");
    ASSERT_EQ(5u, uniforms.blocks.size());
    std::vector<std::string> names;
    for (const auto &block : uniforms.blocks) {
        names.push_back(block.name);
        EXPECT_EQ(block.name != "E", block.std140) << block.name;
    }
    EXPECT_EQ((std::vector<std::string> {"A", "B", "C", "D", "E"}), names);
    EXPECT_TRUE(uniforms.loose.empty()) << bulleted(looseNames(uniforms));
}

TEST(ShaderSources, sampler_count_resolves_constant_arrays_and_comma_lists) {
    auto uniforms = glsl::parseUniforms(
        "#define NUM_CASCADES 4\n"
        "const int NUM_FACES = 6;\n"
        "uniform sampler2D sOne;\n"
        "uniform sampler2D sThree[3];\n"
        "uniform sampler2D sCascades[NUM_CASCADES];\n"
        "uniform samplerCube sFaces[NUM_FACES / 2];\n"
        "uniform sampler2D sA, sB[2];\n");
    EXPECT_EQ(1 + 3 + 4 + 3 + 1 + 2, uniforms.samplerCount());
    EXPECT_TRUE(uniforms.loose.empty()) << bulleted(looseNames(uniforms));
}

TEST(ShaderSources, sampler_array_with_unknown_size_is_an_error) {
    EXPECT_THROW(glsl::parseUniforms("uniform sampler2D sArr[UNKNOWN];\n"), std::runtime_error);
}

TEST(ShaderSources, stage_limit_counts_loose_uniforms_as_an_extra_block) {
    // Loose uniforms are what put v_model over the limit before they moved
    // into Locals.
    auto uniforms = glsl::parseUniforms(
        "layout(std140) uniform A { vec4 a; };\n"
        "uniform vec4 uLoose;\n");
    EXPECT_EQ(1u, uniforms.blocks.size());
    EXPECT_EQ(1u, uniforms.loose.size());
}

TEST(ShaderSources, compose_splices_nested_includes_before_their_includer) {
    std::map<std::string, std::string> files {
        {"main", "#include \"a.glsl\"\n#include \"b.glsl\"\nMAIN\n"},
        {"a", "A\n"},
        {"b", "#include \"c.glsl\"\nB\n"},
        {"c", "C\n"}};
    auto sources = composeShaderSources("main", [&files](const std::string &rr) { return files.at(rr); });
    EXPECT_EQ((std::list<std::string> {"A\n", "C\n", "B\n", "MAIN\n"}), sources);
}

TEST(OITModelShader, transparent_material_keeps_authored_alpha_and_lighting) {
    auto shader = readShader("f_oit_model.glsl");
    ASSERT_FALSE(shader.empty());

    auto textureAlpha = shader.find("float diffuseAlpha = mainTexSample.a;");
    auto applyTextureAlpha = shader.find("objectAlpha *= diffuseAlpha;");
    auto lightLoop = shader.find("for (int i = 0; i < uNumLights; ++i)");
    auto litColor = shader.find("vec3 objectColor = lighting * uColor.rgb * diffuseColor;");
    auto applyAuthoredAlpha = shader.find("objectColor *= objectAlpha;");
    auto encodeContribution = shader.find("objectAlpha = clamp(rgbToLuma(objectColor), 0.0, 1.0);");

    EXPECT_NE(std::string::npos, textureAlpha);
    EXPECT_NE(std::string::npos, applyTextureAlpha);
    EXPECT_NE(std::string::npos, lightLoop);
    EXPECT_NE(std::string::npos, litColor);
    EXPECT_NE(std::string::npos, applyAuthoredAlpha);
    EXPECT_NE(std::string::npos, encodeContribution);
    EXPECT_LT(textureAlpha, applyTextureAlpha);
    EXPECT_LT(lightLoop, litColor);
    EXPECT_LT(litColor, applyAuthoredAlpha);
    EXPECT_LT(applyAuthoredAlpha, encodeContribution);

    // The old path replaced texture alpha with RGB luminance before lighting,
    // which made a blue additive texture visible even under zero light.
    EXPECT_EQ(std::string::npos, shader.find("diffuseAlpha = rgbToLuma(mainTexSample.rgb);"));
}
