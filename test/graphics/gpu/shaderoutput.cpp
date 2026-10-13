/* Copyright (c) 2026 The reone project contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include <array>
#include <cmath>
#include <glm/gtx/component_wise.hpp>

#include "reone/graphics/di/module.h"
#include "reone/graphics/framebuffer.h"
#include "reone/graphics/material.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/texture.h"
#include "reone/resource/provider/shaders.h"
#include "reone/resource/resources.h"
#include "reone/scene/render/pass/pbr.h"
#include "reone/scene/render/pass/retro.h"
#include "reone/system/threadutil.h"
#include "modelfixture.h"
using namespace shader_output;

using namespace reone;
using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;

namespace {

constexpr int kSize = 16;
// Float targets are half precision. Allow two half-precision ULPs near 1,
// and two final RGBA8 quantization steps; never demand GPU bit identity.
constexpr float kAccumTolerance = 0.002f;
constexpr float kOutputTolerance = 2.0f / 255.0f;

struct Sample {
    glm::vec4 texel {0.05f, 0.15f, 0.9f, 0.2f};
    glm::vec4 tint {1.0f};
    glm::vec3 background {0.03f};
    glm::vec3 selfIllum {0.0f};
    float ambient {0.0f};
    float dynamicLight {0.0f};
    float waterAlpha {-1.0f};
    bool saber {true};
    bool additive {true};
    bool lightmapped {false};
    bool zeroNormals {false};
};

struct Pixels {
    glm::vec4 accum;
    float weight;
    glm::vec4 resolved;
    std::vector<float> weightImage;
    std::vector<float> colorImage;
};

struct Programs {
    Resources resources;
    ShaderRegistry registry;
    std::unique_ptr<Shaders> shaders;
};

class Harness {
public:
    Harness() {
        markMainThread();
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            throw std::runtime_error(SDL_GetError());
        }
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        _window = SDL_CreateWindow("reone shader output tests", kSize, kSize, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        if (!_window || !(_gl = SDL_GL_CreateContext(_window))) {
            throw std::runtime_error(SDL_GetError());
        }
        _graphics = std::make_unique<GraphicsModule>(_options);
        _graphics->init();
        std::cout << "GPU: " << glGetString(GL_VENDOR) << "; " << glGetString(GL_RENDERER)
                  << "; " << glGetString(GL_VERSION) << '\n';
        glDisable(GL_FRAMEBUFFER_SRGB);
        glDisable(GL_DITHER);
        _graphics->context().pushViewport({0, 0, kSize, kSize});
        _graphics->context().pushDepthTestMode(DepthTestMode::None);

        for (const std::string name : {"current", "pre343", "post343", "dedicated-only", "lighting-only", "global-fullbright", "wrong-geometry"}) {
            auto programs = std::make_unique<Programs>();
            auto path = std::filesystem::path(REONE_GPU_SHADER_DIR) / name / "shaderpack.erf";
            programs->resources.addERF(path);
            programs->shaders = std::make_unique<Shaders>(_options, programs->registry, programs->resources);
            programs->shaders->init();
            _programs.emplace(name, std::move(programs));
            std::cout << "Loaded: " << path << '\n';
        }
        // Shader provider initializes programs outside Context; clear its cache.
        _graphics->context().resetProgram();

        _source = texture("sample", PixelFormat::RGBA16F, 1);
        _background = texture("background", PixelFormat::RGBA8, 1);
        _hilights = texture("no_hilights", PixelFormat::RGBA8, 1);
        _lightmap = texture("lightmap", PixelFormat::RGBA16F, 1);
        upload(*_hilights, glm::vec4(0.0f));
        upload(*_lightmap, {0.2f, 0.3f, 0.4f, 1.0f});
        _accum = texture("accum", PixelFormat::RGBA16F);
        _weight = texture("weight", PixelFormat::R16F);
        _output = texture("resolved", PixelFormat::RGBA8);
        _transparent.attachColorsDepth({_accum, _weight}, nullptr);
        _transparent.init();
        _resolved.attachColorDepth(_output, nullptr);
        _resolved.init();
        _graphics->context().resetDrawFramebuffer();
        _graphics->context().resetReadFramebuffer();
        _mesh = mesh(false);
        _zeroNormalMesh = mesh(true);
        _modelPlane = modelPlane();
        _zeroNormalModelPlane = modelPlane(true);
    }

    ~Harness() {
        // GL-owned members must die while the context is current.
        _mesh.reset();
        _zeroNormalMesh.reset();
        _modelPlane.reset();
        _zeroNormalModelPlane.reset();
        _transparent.deinit();
        _resolved.deinit();
        _source.reset(); _background.reset(); _hilights.reset(); _lightmap.reset();
        _accum.reset(); _weight.reset(); _output.reset();
        _programs.clear();
        _graphics.reset();
        SDL_GL_DestroyContext(_gl);
        SDL_DestroyWindow(_window);
        SDL_Quit();
    }

    Pixels render(const std::string &variant, bool pbr, const Sample &sample,
                  const std::function<void(IRenderPass &)> &renderModel = {}) {
        auto &g = *_graphics;
        auto &context = g.context();
        auto &registry = _programs.at(variant)->registry;
        _options.pbr = pbr;
        upload(*_source, sample.texel);
        upload(*_background, glm::vec4(sample.background, 1.0f));
        Texture::Features features;
        features.blending = sample.additive ? Texture::Blending::Additive : Texture::Blending::Default;
        features.waterAlpha = sample.waterAlpha;
        _source->setFeatures(features);
        Material material;
        material.type = MaterialType::TransparentModel;
        material.textures.emplace(TextureUnits::mainTex, *_source);
        if (sample.lightmapped) {
            material.textures.emplace(TextureUnits::lightmap, *_lightmap);
        }
        material.color = sample.tint;
        material.selfIllumColor = sample.selfIllum;
        material.faceCulling = FaceCullMode::None;
        g.uniforms().setGlobals([&](auto &globals) {
            globals.reset();
            globals.worldAmbientColor = glm::vec4(sample.ambient);
            globals.numLights = sample.dynamicLight > 0.0f ? 1 : 0;
            globals.lights[0].position = {0.0f, 0.0f, 2.0f, 1.0f};
            globals.lights[0].color = glm::vec4(1.0f);
            globals.lights[0].multiplier = sample.dynamicLight;
            globals.lights[0].radius = 10.0f;
            globals.lights[0].ambientOnly = 0;
        });
        ObservedUniforms observedUniforms(g.uniforms());
        ObservedShaders observedShaders(registry);
        RetroRenderPass retro(_options, context, observedShaders, g.statistic(), g.meshRegistry(), g.textureRegistry(), observedUniforms);
        PBRRenderPass physical(_options, context, observedShaders, g.statistic(), g.meshRegistry(), g.pbrTextures(), g.textureRegistry(), observedUniforms);
        IRenderPass &pass = pbr ? static_cast<IRenderPass &>(physical) : static_cast<IRenderPass &>(retro);

        context.bindDrawFramebuffer(_transparent, {0, 1});
        _transparent.checkCompleteness();
        // Same target formats, clear, blend and depth-write policy as both
        // production pipelines' TransparentGeometry pass.
        context.clearColor({0.0f, 0.0f, 0.0f, 1.0f});
        context.withBlendMode(BlendMode::OIT_Transparent, [&] {
            context.withDepthMask(false, [&] {
                auto &geometry = sample.zeroNormals ? *_zeroNormalMesh : *_mesh;
                if (renderModel) {
                    renderModel(pass);
                } else if (sample.saber) {
                    // No synthetic feature-mask assignment: the real pass
                    // derives additive flags and sets FEATURE_SABER itself.
                    pass.drawSaber(geometry, material, glm::mat4(1.0f), glm::mat4(1.0f), glm::vec4(0.0f));
                } else {
                    pass.draw(geometry, material, glm::mat4(1.0f), glm::mat4(1.0f));
                }
            });
        });
        Pixels result;
        context.bindReadFramebuffer(_transparent, 0);
        result.accum = readRGBA();
        context.bindReadFramebuffer(_transparent, 1);
        result.weight = readRGBA().r;
        result.weightImage.resize(kSize * kSize);
        glReadPixels(0, 0, kSize, kSize, GL_RED, GL_FLOAT, result.weightImage.data());
        lastFeatures = observedUniforms.features;
        lastPrograms = observedShaders.requested;

        context.bindDrawFramebuffer(_resolved, {0});
        _resolved.checkCompleteness();
        context.useProgram(registry.get(ShaderProgramId::oitBlend));
        context.bindTexture(*_background, TextureUnits::mainTex);
        context.bindTexture(*_hilights, TextureUnits::hilights);
        context.bindTexture(*_accum, TextureUnits::oitAccum);
        context.bindTexture(*_weight, TextureUnits::oitRevealage);
        context.clearColor();
        g.meshRegistry().get(MeshName::quadNDC).draw(g.statistic());
        context.bindReadFramebuffer(_resolved, 0);
        result.resolved = readRGBA();
        result.colorImage.resize(kSize * kSize * 4);
        glReadPixels(0, 0, kSize, kSize, GL_RGBA, GL_FLOAT, result.colorImage.data());
        EXPECT_EQ(GL_NO_ERROR, glGetError());
        return result;
    }

    std::vector<DrawRecord> lastDraws;
    std::vector<int> lastFeatures;
    std::vector<std::string> lastPrograms;

    // Selection 0=ordinary plane, 1=dedicated plane, 2=both. The authored
    // model/scene path selects material and draw method; no test-side flags.
    Pixels renderComposite(const std::string &variant, bool pbr, const Sample &sample,
                           int selection, float yaw = 0.0f, bool opaque = false) {
        auto oldFeatures = _source->features();
        auto oldVertices = _modelPlane->vertexCoords();
        auto oldFaces = _modelPlane->faces();
        auto result = render(variant, pbr, sample, [&](IRenderPass &pass) {
            // A nonzero stale displacement makes accidental SABER reuse on
            // ordinary vertices observable, even without an animation system.
            auto &program = _programs.at(variant)->registry.get(ShaderProgramId::oitModel);
            _graphics->context().useProgram(program);
            program.setUniform("uSaberDisplacement", glm::vec4(1.0f, 0.4f, 0.0f, 0.0f));
            auto features = _source->features();
            features.decal = true;
            _source->setFeatures(features);
            NodeInput ordinary;
            ordinary.mesh = sample.zeroNormals ? _zeroNormalModelPlane : _modelPlane;
            ordinary.texture = _source;
            ordinary.opacity = sample.tint.a;
            ordinary.selfIllum = sample.selfIllum;
            ordinary.lightmap = sample.lightmapped ? _lightmap : nullptr;
            ordinary.transform = glm::rotate(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
            NodeInput dedicated = ordinary;
            dedicated.dedicatedGeometry = true;
            dedicated.transform = glm::rotate(yaw + glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
            if (opaque) {
                ordinary.texture = texture("opaque_control", PixelFormat::RGB8, 1);
                upload(*ordinary.texture, sample.texel);
            }
            ModelFixture fixture(_options, _graphics->services(), {ordinary, dedicated});
            if (opaque) {
                // The opaque consumer really has an additive sibling in the
                // same model, even when only its own attachment is measured.
                EXPECT_FALSE(fixture.node(0).isTransparent());
                EXPECT_TRUE(fixture.node(1).isTransparent());
            }
            ObservedPass observed(pass);
            std::vector<size_t> indices;
            if (selection == 2) indices = {0, 1};
            else if (selection == 3) indices = {0, 0};
            else if (selection == 4) indices = {1, 0};
            else indices = {static_cast<size_t>(selection)};
            if (opaque) {
                _graphics->context().withBlendMode(BlendMode::None, [&] { fixture.render(observed, indices); });
            } else {
                fixture.render(observed, indices);
            }
            lastDraws = observed.records;
            // Rendering must never mutate a cached source resource according
            // to which model is currently using it.
            EXPECT_EQ(_source->features().blending, features.blending);
            EXPECT_EQ(_source->features().waterAlpha, features.waterAlpha);
            EXPECT_EQ(_source->features().decal, features.decal);
            EXPECT_EQ(_modelPlane->vertexCoords(), oldVertices);
            ASSERT_EQ(_modelPlane->faces().size(), oldFaces.size());
            for (size_t i = 0; i < oldFaces.size(); ++i) {
                EXPECT_EQ(_modelPlane->faces()[i].vertices, oldFaces[i].vertices);
            }
        });
        _source->setFeatures(oldFeatures);
        return result;
    }

private:
    GraphicsOptions _options;
    SDL_Window *_window {nullptr};
    SDL_GLContext _gl {nullptr};
    std::unique_ptr<GraphicsModule> _graphics;
    std::map<std::string, std::unique_ptr<Programs>> _programs;
    std::shared_ptr<Texture> _source, _background, _hilights, _lightmap, _accum, _weight, _output;
    Framebuffer _transparent, _resolved;
    std::unique_ptr<Mesh> _mesh, _zeroNormalMesh;
    std::shared_ptr<Mesh> _modelPlane, _zeroNormalModelPlane;

    static std::shared_ptr<Texture> texture(const std::string &name, PixelFormat format, int size = kSize) {
        Texture::Properties properties;
        properties.minFilter = Texture::Filtering::Nearest;
        properties.magFilter = Texture::Filtering::Nearest;
        properties.wrap = Texture::Wrapping::ClampToEdge;
        auto result = std::make_shared<Texture>(name, TextureType::TwoDim, properties);
        result->clear(size, size, format);
        result->init();
        return result;
    }

    static void upload(Texture &texture, glm::vec4 texel) {
        texture.bind();
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_FLOAT, &texel[0]);
    }

    static std::unique_ptr<Mesh> mesh(bool zeroNormals) {
        auto normal = zeroNormals ? glm::vec3(0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
        std::vector<Mesh::Vertex> vertices;
        for (glm::vec3 position : {glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(1.0f, -1.0f, 0.0f),
                                  glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(-1.0f, 1.0f, 0.0f)}) {
            vertices.push_back(Mesh::VertexBuilder().position(position).normal(normal).uv1({0.5f, 0.5f}).uv2({0.5f, 0.5f}).build());
        }
        auto layout = Mesh::VertexLayoutBuilder().stride(10 * sizeof(float)).offPosition(0)
                          .offNormals(3 * sizeof(float)).offUV1(6 * sizeof(float)).offUV2(8 * sizeof(float)).build();
        auto result = std::make_unique<Mesh>(std::move(vertices), layout,
                                            std::vector<Mesh::Face> {Mesh::Face({0, 1, 2}), Mesh::Face({2, 3, 0})});
        result->init();
        return result;
    }

    static std::shared_ptr<Mesh> modelPlane(bool zeroNormals = false) {
        // Visible ordinary geometry uses indices 84..87. Unlike a four-vertex
        // quad at indices 0..3, it would move under the saber vertex formula.
        auto normal = zeroNormals ? glm::vec3(0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
        auto dummy = Mesh::VertexBuilder().position({0.0f, 0.0f, 0.0f}).normal(normal)
                         .uv1({0.5f, 0.5f}).uv2({0.5f, 0.5f}).build();
        std::vector<Mesh::Vertex> vertices(84, dummy);
        for (glm::vec3 p : {glm::vec3(-0.55f, -0.7f, 0.0f), glm::vec3(0.55f, -0.7f, 0.0f),
                           glm::vec3(0.55f, 0.7f, 0.0f), glm::vec3(-0.55f, 0.7f, 0.0f)}) {
            auto vertex = dummy;
            vertex.position = p;
            vertices.push_back(vertex);
        }
        auto layout = Mesh::VertexLayoutBuilder().stride(10 * sizeof(float)).offPosition(0)
                          .offNormals(3 * sizeof(float)).offUV1(6 * sizeof(float)).offUV2(8 * sizeof(float)).build();
        auto result = std::make_shared<Mesh>(vertices, layout,
                                            std::vector<Mesh::Face>{Mesh::Face({84,85,86}), Mesh::Face({86,87,84})});
        result->init();
        return result;
    }

    static glm::vec4 readRGBA() {
        std::array<float, kSize * kSize * 4> pixels {};
        glReadPixels(0, 0, kSize, kSize, GL_RGBA, GL_FLOAT, pixels.data());
        EXPECT_TRUE(std::all_of(pixels.begin(), pixels.end(), [](float value) { return std::isfinite(value); }));
        auto center = 4 * ((kSize / 2) * kSize + kSize / 2);
        return {pixels[center], pixels[center + 1], pixels[center + 2], pixels[center + 3]};
    }
};

std::unique_ptr<Harness> gpu;
std::string candidate = "current";

void expectSame(const Pixels &actual, const Pixels &reference) {
    for (int i = 0; i < 4; ++i) {
        EXPECT_NEAR(actual.accum[i], reference.accum[i], kAccumTolerance) << "accum channel " << i;
        EXPECT_NEAR(actual.resolved[i], reference.resolved[i], kOutputTolerance) << "resolved channel " << i;
    }
    EXPECT_NEAR(actual.weight, reference.weight, kAccumTolerance);
}

float outputDifference(const Pixels &a, const Pixels &b) {
    return glm::compMax(glm::abs(glm::vec3(a.resolved) - glm::vec3(b.resolved)));
}

TEST(SaberFramebuffer, MatchesPre343ColoursAlphaTintOpacityAndBackgrounds) {
    for (bool pbr : {false, true}) {
        for (glm::vec3 color : {glm::vec3(0.03f, 0.12f, 0.9f), glm::vec3(0.95f, 0.04f, 0.01f), glm::vec3(0.95f)}) {
            for (float alpha : {0.0f, 0.2f, 1.0f}) {
                for (float background : {0.02f, 0.75f}) {
                    for (float lighting : {0.0f, 1.0f}) {
                        SCOPED_TRACE(::testing::Message() << "PBR=" << pbr << " RGB=" << color.r << ',' << color.g << ',' << color.b
                                                         << " alpha=" << alpha << " background=" << background << " lighting=" << lighting);
                        Sample sample;
                        sample.texel = glm::vec4(color, alpha);
                        sample.tint = {0.7f, 0.55f, 0.9f, 0.45f};
                        sample.background = glm::vec3(background);
                        sample.ambient = lighting;
                        sample.dynamicLight = lighting;
                        expectSame(gpu->render(candidate, pbr, sample), gpu->render("pre343", pbr, sample));
                    }
                }
            }
        }
    }
}

TEST(SaberFramebuffer, SceneLightingIndependentButBackgroundAndMaterialOpacityMatter) {
    for (bool pbr : {false, true}) {
        Sample sample;
        auto dark = gpu->render(candidate, pbr, sample);
        sample.ambient = 1.0f;
        sample.dynamicLight = 2.0f;
        auto bright = gpu->render(candidate, pbr, sample);
        expectSame(dark, bright);
        EXPECT_GT(glm::compMax(glm::vec3(dark.accum)), 0.2f);
        sample.background = glm::vec3(0.75f);
        auto lightBackground = gpu->render(candidate, pbr, sample);
        EXPECT_GT(outputDifference(bright, lightBackground), 0.1f);
        EXPECT_NEAR(bright.weight, lightBackground.weight, kAccumTolerance);
        sample.tint.a = 0.35f;
        auto translucent = gpu->render(candidate, pbr, sample);
        EXPECT_LT(translucent.weight, 0.5f * lightBackground.weight);
        EXPECT_GT(translucent.accum.a, lightBackground.accum.a);
    }
}

TEST(SaberFramebuffer, ZeroNearZeroOpacityAndUnusedNormalsAreFinite) {
    for (bool pbr : {false, true}) {
        for (bool saber : {false, true}) {
        for (float level : {0.0f, 0.000001f, 0.00005f, 0.0002f}) {
            Sample sample;
            sample.saber = saber;
            sample.texel = {level, level, level, 0.0f};
            sample.zeroNormals = true;
            auto actual = gpu->render(candidate, pbr, sample);
            auto reference = gpu->render("pre343", pbr, sample);
            expectSame(actual, reference);
            // The general half-float tolerance must not swallow these small
            // signals. Use two half-float subnormal steps or 0.3% of the
            // reference, whichever is larger, and require positive coverage.
            auto nearBlackTolerance = [](float expected) {
                return std::max(2.0f * 5.960464477539063e-8f, std::abs(expected) * 0.003f);
            };
            EXPECT_NEAR(actual.weight, reference.weight, nearBlackTolerance(reference.weight));
            for (int channel = 0; channel < 3; ++channel) {
                EXPECT_NEAR(actual.accum[channel], reference.accum[channel], nearBlackTolerance(reference.accum[channel]));
            }
            if (level > 0.0f) {
                EXPECT_GT(actual.weight, 0.0f);
                if (level >= 0.00005f) {
                    EXPECT_GT(actual.accum.r, 0.0f);
                }
            } else {
                EXPECT_FLOAT_EQ(actual.weight, 0.0f);
                EXPECT_FLOAT_EQ(actual.accum.r, 0.0f);
            }
        }
        Sample invisible;
        invisible.saber = saber;
        invisible.tint.a = 0.0f;
        auto result = gpu->render(candidate, pbr, invisible);
        EXPECT_FLOAT_EQ(result.weight, 0.0f);
        EXPECT_FLOAT_EQ(result.accum.a, 1.0f);
        EXPECT_NEAR(result.resolved.r, invisible.background.r, kOutputTolerance);
        }
    }
}

TEST(MaterialFramebuffer, RestoresAdditiveContributionAndRetainsLitAlphaMaterials) {
    for (bool pbr : {false, true}) {
        for (bool additive : {false, true}) {
            for (float alpha : {0.0f, 0.25f, 1.0f}) {
                for (float lighting : {0.0f, 0.4f, 1.0f}) {
                    for (float selfIllum : {0.0f, 0.5f}) {
                        Sample sample;
                        sample.saber = false;
                        sample.additive = additive;
                        sample.texel = {0.6f, 0.2f, 0.75f, alpha};
                        sample.tint = {0.8f, 0.6f, 0.7f, 0.55f};
                        sample.ambient = lighting;
                        sample.dynamicLight = lighting;
                        sample.selfIllum = glm::vec3(selfIllum);
                        SCOPED_TRACE(::testing::Message() << "PBR=" << pbr << " additive=" << additive << " alpha=" << alpha
                                                         << " lighting=" << lighting << " selfIllum=" << selfIllum);
                        expectSame(gpu->render(candidate, pbr, sample), gpu->render(additive ? "pre343" : "post343", pbr, sample));
                    }
                }
            }
        }
        Sample lit;
        lit.saber = false;
        lit.additive = false;
        auto dark = gpu->render(candidate, pbr, lit);
        lit.ambient = 1.0f;
        auto bright = gpu->render(candidate, pbr, lit);
        EXPECT_GT(outputDifference(dark, bright), 0.1f);
        lit.ambient = 0.0f;
        lit.selfIllum = glm::vec3(0.8f);
        EXPECT_GT(outputDifference(dark, gpu->render(candidate, pbr, lit)), 0.1f);
    }
}

TEST(MaterialFramebuffer, LightmapWaterAndNonAdditiveRetainTheirSupportedPaths) {
    for (bool pbr : {false, true}) {
        for (int kind = 0; kind < 4; ++kind) {
            Sample sample;
            sample.additive = kind != 0;
            sample.waterAlpha = kind == 1 || kind == 3 ? 0.4f : -1.0f;
            sample.lightmapped = kind == 2 || kind == 3;
            sample.ambient = 0.4f;
            expectSame(gpu->render(candidate, pbr, sample), gpu->render(sample.additive ? "pre343" : "post343", pbr, sample));
        }
    }
}

float maximumDifference(const std::vector<float> &a, const std::vector<float> &b) {
    if (a.size() != b.size()) {
        return std::numeric_limits<float>::infinity();
    }
    float maximum = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        maximum = std::max(maximum, std::abs(a[i] - b[i]));
    }
    return maximum;
}

int coverageDifference(const Pixels &a, const Pixels &b) {
    int difference = 0;
    for (size_t i = 0; i < a.weightImage.size(); ++i) {
        difference += (a.weightImage[i] > 0.0001f) != (b.weightImage.at(i) > 0.0001f);
    }
    return difference;
}

TEST(ModelMaterialRouting, OrdinaryDedicatedAndWholeCrossPlanesMatchAdditiveReference) {
    for (bool pbr : {false, true}) {
        for (glm::vec3 color : {glm::vec3(0.03f, 0.12f, 0.9f), glm::vec3(0.95f, 0.04f, 0.01f), glm::vec3(0.04f, 0.9f, 0.12f)}) {
            for (float yaw : {0.0f, glm::quarter_pi<float>(), glm::half_pi<float>()}) {
                for (int selection : {0, 1, 2}) {
                    Sample sample;
                    sample.texel = glm::vec4(color, 0.05f);
                    sample.ambient = 0.2f;
                    sample.tint.a = 0.55f;
                    auto reference = gpu->renderComposite("pre343", pbr, sample, selection, yaw);
                    auto actual = gpu->renderComposite(candidate, pbr, sample, selection, yaw);
                    SCOPED_TRACE(::testing::Message() << "PBR=" << pbr << " selection=" << selection << " yaw=" << yaw);
                    EXPECT_LE(maximumDifference(actual.weightImage, reference.weightImage), kAccumTolerance);
                    EXPECT_LE(maximumDifference(actual.colorImage, reference.colorImage), kOutputTolerance);
                    ASSERT_EQ(gpu->lastDraws.size(), selection == 2 ? 2 : 1);
                    ASSERT_EQ(gpu->lastFeatures.size(), gpu->lastDraws.size());
                    ASSERT_EQ(gpu->lastPrograms.size(), gpu->lastDraws.size());
                    for (size_t i = 0; i < gpu->lastDraws.size(); ++i) {
                        bool dedicated = selection == 1 || (selection == 2 && i == 1);
                        EXPECT_EQ(gpu->lastDraws[i].route, dedicated ? DrawRecord::Route::Saber : DrawRecord::Route::Ordinary);
                        EXPECT_EQ(gpu->lastFeatures[i] & UniformsFeatureFlags::saber, dedicated ? UniformsFeatureFlags::saber : 0);
                        EXPECT_NE(gpu->lastFeatures[i] & UniformsFeatureFlags::premulalpha, 0);
                        EXPECT_EQ(gpu->lastDraws[i].material, MaterialType::TransparentModel);
                        EXPECT_EQ(gpu->lastPrograms[i], ShaderProgramId::oitModel);
                        EXPECT_FLOAT_EQ(gpu->lastDraws[i].color.a, sample.tint.a);
                    }
                    if (selection == 2) {
                        EXPECT_EQ(gpu->lastDraws[0].texture, gpu->lastDraws[1].texture);
                    }
                }
            }
        }
    }
}

TEST(ModelMaterialRouting, AuthoredOpacityZeroNormalsAndDuplicateLayerOrder) {
    for (bool pbr : {false, true}) {
        for (float opacity : {0.0f, 0.35f, 1.0f}) {
            Sample sample;
            sample.tint.a = opacity;
            sample.texel = {0.15f, 0.35f, 0.65f, 0.0f};
            sample.zeroNormals = true;
            auto reference = gpu->renderComposite("pre343", pbr, sample, 0);
            auto actual = gpu->renderComposite(candidate, pbr, sample, 0);
            EXPECT_LE(maximumDifference(actual.weightImage, reference.weightImage), kAccumTolerance);
            EXPECT_LE(maximumDifference(actual.colorImage, reference.colorImage), kOutputTolerance);
            if (opacity == 0.0f) {
                EXPECT_TRUE(gpu->lastDraws.empty());
                EXPECT_FLOAT_EQ(actual.weight, 0.0f);
            } else {
                EXPECT_GT(actual.weight, 0.0f);
                auto duplicate = gpu->renderComposite(candidate, pbr, sample, 3);
                EXPECT_NEAR(duplicate.weight, 2.0f * actual.weight, kAccumTolerance);
                EXPECT_LT(duplicate.accum.a, actual.accum.a);
            }
            auto forward = gpu->renderComposite(candidate, pbr, sample, 2, glm::quarter_pi<float>());
            auto reverse = gpu->renderComposite(candidate, pbr, sample, 4, glm::quarter_pi<float>());
            EXPECT_LE(maximumDifference(forward.weightImage, reverse.weightImage), kAccumTolerance);
            EXPECT_LE(maximumDifference(forward.colorImage, reverse.colorImage), kOutputTolerance);
        }
    }
}

TEST(ModelMaterialRouting, OrdinaryGeometryIgnoresStaleSaberDisplacement) {
    for (bool pbr : {false, true}) {
        Sample sample;
        sample.ambient = 1.0f;
        sample.texel.a = 1.0f;
        auto reference = gpu->renderComposite("pre343", pbr, sample, 0);
        auto actual = gpu->renderComposite(candidate, pbr, sample, 0);
        EXPECT_EQ(coverageDifference(actual, reference), 0);
        ASSERT_EQ(gpu->lastFeatures.size(), 1);
        EXPECT_EQ(gpu->lastFeatures[0] & UniformsFeatureFlags::saber, 0);
        EXPECT_EQ(gpu->lastDraws[0].route, DrawRecord::Route::Ordinary);
        auto wrong = gpu->renderComposite("wrong-geometry", pbr, sample, 0);
        EXPECT_GT(coverageDifference(wrong, reference), 4);
    }
}

TEST(ModelMaterialRouting, OpaqueSiblingAndSharedResourceConsumersKeepTheirRoutes) {
    for (bool pbr : {false, true}) {
        Sample sample;
        sample.texel = {0.6f, 0.2f, 0.75f, 1.0f};
        sample.ambient = 0.6f;
        auto opaqueReference = gpu->renderComposite("post343", pbr, sample, 0, 0.0f, true);
        auto opaque = gpu->renderComposite(candidate, pbr, sample, 0, 0.0f, true);
        ASSERT_EQ(gpu->lastDraws.size(), 1);
        EXPECT_EQ(gpu->lastDraws[0].material, MaterialType::OpaqueModel);
        EXPECT_EQ(gpu->lastDraws[0].route, DrawRecord::Route::Ordinary);
        EXPECT_EQ(gpu->lastPrograms[0], pbr ? ShaderProgramId::pbrOpaqueModel : ShaderProgramId::retroOpaqueModel);
        EXPECT_EQ(gpu->lastFeatures[0] & (UniformsFeatureFlags::saber | UniformsFeatureFlags::premulalpha), 0);
        for (int channel = 0; channel < 4; ++channel) {
            EXPECT_NEAR(opaque.accum[channel], opaqueReference.accum[channel], kAccumTolerance);
        }
        // The same cached texture and mesh are exercised by multiple nodes,
        // then by an independent ordinary draw. The fixture also checks the
        // original texture properties, vertices and indices after rendering.
        sample.saber = false;
        auto before = gpu->render(candidate, pbr, sample);
        gpu->renderComposite(candidate, pbr, sample, 2, glm::quarter_pi<float>());
        auto after = gpu->render(candidate, pbr, sample);
        expectSame(after, before);
    }
}

TEST(ModelMaterialRouting, NonAdditiveAuthoredAlphaAndSelfIlluminationRemainLit) {
    for (bool pbr : {false, true}) {
        Sample sample;
        sample.saber = false;
        sample.additive = false;
        sample.texel = {0.6f, 0.3f, 0.75f, 0.65f};
        sample.tint.a = 0.35f;
        auto dark = gpu->renderComposite(candidate, pbr, sample, 0);
        sample.selfIllum = glm::vec3(0.6f);
        auto reference = gpu->renderComposite("post343", pbr, sample, 0);
        auto emissive = gpu->renderComposite(candidate, pbr, sample, 0);
        EXPECT_LE(maximumDifference(emissive.colorImage, reference.colorImage), kOutputTolerance);
        EXPECT_GT(maximumDifference(emissive.colorImage, dark.colorImage), 0.05f);
        ASSERT_EQ(gpu->lastFeatures.size(), 1);
        EXPECT_EQ(gpu->lastFeatures[0] & (UniformsFeatureFlags::saber | UniformsFeatureFlags::premulalpha), 0);
        EXPECT_FLOAT_EQ(gpu->lastDraws[0].color.a, 0.35f);
        sample.selfIllum = glm::vec3(0.0f);
        sample.ambient = 0.8f;
        auto lit = gpu->renderComposite(candidate, pbr, sample, 0);
        EXPECT_GT(maximumDifference(lit.colorImage, dark.colorImage), 0.05f);
        sample.texel.a = 0.0f;
        auto transparent = gpu->renderComposite(candidate, pbr, sample, 0);
        EXPECT_FLOAT_EQ(transparent.weight, 0.0f);
    }
}

TEST(ModelMaterialRouting, IncompleteDAndCurrentShaderFailOrdinaryPlaneRecovery) {
    for (bool pbr : {false, true}) {
        Sample sample;
        sample.texel = {0.05f, 0.15f, 0.95f, 0.05f};
        auto reference = gpu->renderComposite("pre343", pbr, sample, 0);
        for (const std::string wrong : {"post343", "dedicated-only", "lighting-only"}) {
            auto actual = gpu->renderComposite(wrong, pbr, sample, 0);
            EXPECT_GT(maximumDifference(reference.colorImage, actual.colorImage), 0.3f) << wrong;
        }
    }
}

TEST(ShaderDiscrimination, RejectsUnchangedLightingOnlyWholeRollbackAndFullbright) {
    for (bool pbr : {false, true}) {
        Sample saber;
        saber.texel = {0.7f, 0.8f, 0.95f, 0.05f};
        auto reference = gpu->render("pre343", pbr, saber);
        EXPECT_GT(outputDifference(reference, gpu->render("post343", pbr, saber)), 0.3f);
        EXPECT_GT(outputDifference(reference, gpu->render("lighting-only", pbr, saber)), 0.3f);
        Sample ordinary;
        ordinary.saber = false;
        ordinary.additive = false;
        ordinary.texel = {0.7f, 0.8f, 0.95f, 0.8f};
        auto accepted = gpu->render("post343", pbr, ordinary);
        EXPECT_GT(outputDifference(accepted, gpu->render("pre343", pbr, ordinary)), 0.3f);
        EXPECT_GT(outputDifference(accepted, gpu->render("global-fullbright", pbr, ordinary)), 0.3f);
    }
}

TEST(FramebufferEvidence, PrintsRepresentativeReadbacks) {
    std::cout << "GPU_SAMPLE,variant,renderer,sample,accumR,accumG,accumB,revealage,weight,outputR,outputG,outputB,outputA\n";
    for (bool pbr : {false, true}) {
        for (const std::string variant : {"current", "pre343", "post343", "dedicated-only", "lighting-only", "global-fullbright", "wrong-geometry"}) {
            for (const std::string name : {"blue-dark", "core-alpha-sensitive", "ordinary-dark", "ordinary-lit", "lit-alpha-dark", "lit-alpha-lit"}) {
                Sample sample;
                if (name == "core-alpha-sensitive") {
                    sample.texel = {0.7f, 0.8f, 0.95f, 0.05f};
                    sample.tint = {0.8f, 0.6f, 0.9f, 0.55f};
                } else if (name == "ordinary-dark" || name == "ordinary-lit" || name == "lit-alpha-dark" || name == "lit-alpha-lit") {
                    sample.saber = false;
                    sample.texel = {0.7f, 0.8f, 0.95f, 0.8f};
                    sample.ambient = name == "ordinary-lit" || name == "lit-alpha-lit" ? 0.7f : 0.0f;
                    sample.additive = name != "lit-alpha-dark" && name != "lit-alpha-lit";
                }
                auto pixel = gpu->render(variant, pbr, sample);
                std::cout << "GPU_SAMPLE," << variant << ',' << (pbr ? "PBR" : "Retro") << ',' << name;
                for (int channel = 0; channel < 4; ++channel) {
                    std::cout << ',' << pixel.accum[channel];
                }
                std::cout << ',' << pixel.weight;
                for (int channel = 0; channel < 4; ++channel) {
                    std::cout << ',' << pixel.resolved[channel];
                }
                std::cout << '\n';
            }
        }
    }
}

} // namespace

int main(int argc, char **argv) {
    // Useful for explicit negative runs: the same acceptance cases must fail
    // for these packaged wrong variants, rather than merely asserting strings.
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg.rfind("--candidate=", 0) == 0) {
            candidate = arg.substr(12);
        }
    }
    ::testing::InitGoogleTest(&argc, argv);
    try {
        gpu = std::make_unique<Harness>();
        int result = RUN_ALL_TESTS();
        gpu.reset();
        return result;
    } catch (const std::exception &error) {
        std::cerr << "GPU regression runner failed (not skipped): " << error.what() << '\n';
        gpu.reset();
        return 2;
    }
}
