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

#include "reone/resource/provider/shaders.h"

#include "reone/graphics/options.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/uniforms.h"
#include "reone/resource/resources.h"
#include "reone/system/logutil.h"
#include "reone/system/stream/memoryinput.h"
#include "reone/system/stringbuilder.h"
#include "reone/system/textreader.h"

using namespace reone::graphics;

namespace reone {

namespace resource {

const std::vector<ShaderProgramSpec> &shaderProgramSpecs() {
    static const std::vector<ShaderProgramSpec> kSpecs {
        {ShaderProgramId::aabbColor, "v_aabb", "", "f_color"},
        {ShaderProgramId::billboard, "v_billboard", "", "f_texture"},
        {ShaderProgramId::retroOpaqueModel, "v_model", "", "f_rtr_opaqmodel"},
        {ShaderProgramId::retroGrass, "v_grass", "", "f_rtr_grass"},
        {ShaderProgramId::retroAABB, "v_aabb", "", "f_rtr_aabb"},
        {ShaderProgramId::retroWalkmesh, "v_walkmesh", "", "f_rtr_walkmesh"},
        {ShaderProgramId::pbrAABB, "v_aabb", "", "f_pbr_aabb"},
        {ShaderProgramId::pbrCombine, "v_passthrough", "", "f_pbr_combine"},
        {ShaderProgramId::pbrGrass, "v_grass", "", "f_pbr_grass"},
        {ShaderProgramId::pbrOpaqueModel, "v_model", "", "f_pbr_opaqmodel"},
        {ShaderProgramId::pbrSSAO, "v_passthrough", "", "f_pbr_ssao"},
        {ShaderProgramId::pbrSSR, "v_passthrough", "", "f_pbr_ssr"},
        {ShaderProgramId::pbrWalkmesh, "v_walkmesh", "", "f_pbr_walkmesh"},
        {ShaderProgramId::dirLightShadows, "v_shadows", "g_dirlightshadow", "f_null"},
        {ShaderProgramId::mvpColor, "v_mvp", "", "f_color"},
        {ShaderProgramId::mvpIcon, "v_mvp", "", "f_icon"},
        {ShaderProgramId::mvpTexture, "v_mvp", "", "f_texture"},
        {ShaderProgramId::ndcTexture, "v_passthrough", "", "f_texnoper"},
        {ShaderProgramId::oitBlend, "v_passthrough", "", "f_oit_blend"},
        {ShaderProgramId::oitModel, "v_model", "", "f_oit_model"},
        {ShaderProgramId::oitParticles, "v_particles", "", "f_oit_particles"},
        {ShaderProgramId::oitProjectedBeam, "v_mvp", "", "f_oit_projbeam"},
        {ShaderProgramId::pointLightShadows, "v_shadows", "g_ptlightshadow", "f_ptlightshadow"},
        {ShaderProgramId::postBoxBlur4, "v_passthrough", "", "f_pp_boxblur4"},
        {ShaderProgramId::postFXAA, "v_passthrough", "", "f_pp_fxaa"},
        {ShaderProgramId::postGaussianBlur13, "v_passthrough", "", "f_pp_gausblur13"},
        {ShaderProgramId::postGaussianBlur9, "v_passthrough", "", "f_pp_gausblur9"},
        {ShaderProgramId::postMedianFilter3, "v_passthrough", "", "f_pp_medianfilt3"},
        {ShaderProgramId::postMedianFilter5, "v_passthrough", "", "f_pp_medianfilt5"},
        {ShaderProgramId::postSharpen, "v_passthrough", "", "f_pp_sharpen"},
        {ShaderProgramId::postSpeedBlur, "v_passthrough", "", "f_pp_speedblur"},
        {ShaderProgramId::postVideoEffect, "v_passthrough", "", "f_pp_videofx"},
        {ShaderProgramId::postDistortionOverlay, "v_passthrough", "", "f_pp_distortion"},
        {ShaderProgramId::text, "v_text", "", "f_text"},
        {ShaderProgramId::textBillboard, "v_textBillboard", "", "f_text"},
        {ShaderProgramId::pbrIrradiance, "v_mvp", "", "f_pbr_irradiance"},
        {ShaderProgramId::brdfLUT, "v_mvp", "", "f_pbr_brdf"},
        {ShaderProgramId::pbrPrefilter, "v_mvp", "", "f_pbr_prefilter"},
        {ShaderProgramId::profiler, "v_mvp", "", "f_profiler"},
    };
    return kSpecs;
}

std::list<std::string> composeShaderSources(const std::string &resRef,
                                            const std::function<std::string(const std::string &)> &loadSource) {
    static const std::regex kInclude("^#include \"([\\d\\w_]+)\\.glsl\"$");

    std::list<std::string> sources;
    std::list<std::string> resRefs;
    resRefs.push_back(resRef);
    while (!resRefs.empty()) {
        auto rr = resRefs.back();
        resRefs.pop_back();
        auto data = loadSource(rr);
        auto source = StringBuilder();
        auto stream = MemoryInputStream(data);
        auto reader = TextReader(stream);
        while (auto line = reader.readLine()) {
            std::smatch match;
            if (std::regex_search(*line, match, kInclude)) {
                resRefs.push_back(match[1].str());
                continue;
            }
            source.append(*line);
            source.append("\n");
        }
        sources.push_front(source.string());
    }
    return sources;
}

void Shaders::init() {
    if (_inited) {
        return;
    }

    std::map<std::pair<ShaderType, std::string>, std::shared_ptr<Shader>> shaders;
    auto getShader = [this, &shaders](ShaderType type, const std::string &resRef) {
        auto key = std::make_pair(type, resRef);
        auto it = shaders.find(key);
        if (it == shaders.end()) {
            it = shaders.insert({key, initShader(type, resRef)}).first;
        }
        return it->second;
    };
    for (const auto &spec : shaderProgramSpecs()) {
        std::vector<std::shared_ptr<Shader>> stages;
        stages.push_back(getShader(ShaderType::Vertex, spec.vertex));
        if (!spec.geometry.empty()) {
            stages.push_back(getShader(ShaderType::Geometry, spec.geometry));
        }
        stages.push_back(getShader(ShaderType::Fragment, spec.fragment));
        _shaderRegistry.add(spec.id, initShaderProgram(std::move(stages)));
    }

    _inited = true;
}

void Shaders::deinit() {
    if (!_inited) {
        return;
    }
    _inited = false;
}

std::shared_ptr<Shader> Shaders::initShader(ShaderType type, std::string resRef) {
    debug(
        str(boost::format("Initializing shader: type=%d resRef='%s'") % static_cast<int>(type) % resRef),
        LogChannel::Graphics);

    auto sources = composeShaderSources(resRef, [this](const std::string &rr) {
        if (_sourceResRefToData.count(rr) == 0) {
            auto res = _resources.get(ResourceId(rr, ResType::Glsl));
            _sourceResRefToData[rr] = std::move(res.data);
        }
        const auto &data = _sourceResRefToData.at(rr);
        return std::string(data.begin(), data.end());
    });

    // Prepend preprocessor directives
    auto defines = StringBuilder();
    if (_graphicsOpt.ssr) {
        defines.append("#define R_SSR\n");
    }
    if (_graphicsOpt.ssao) {
        defines.append("#define R_SSAO\n");
    }
    if (!defines.empty()) {
        defines.append("\n");
        sources.push_front(defines.string());
    }
    sources.push_front("#version 400 core\n\n");

    auto shader = std::make_unique<Shader>(type, std::move(sources));
    shader->init();
    return shader;
}

std::shared_ptr<ShaderProgram> Shaders::initShaderProgram(std::vector<std::shared_ptr<Shader>> shaders) {
    auto program = std::make_unique<ShaderProgram>(std::move(shaders));
    program->init();
    program->use();

    // Samplers
    program->setSamplerUnit("sMainTex", TextureUnits::mainTex);
    program->setSamplerUnit("sLightmap", TextureUnits::lightmap);
    program->setSamplerUnit("sEnvMap", TextureUnits::envMap);
    program->setSamplerUnit("sNormalMap", TextureUnits::normalMap);
    program->setSamplerUnit("sGBufSelfIllum", TextureUnits::gBufSelfIllum);
    program->setSamplerUnit("sGBufDepth", TextureUnits::gBufDepth);
    program->setSamplerUnit("sGBufEyeNormal", TextureUnits::gBufEyeNormal);
    program->setSamplerUnit("sSSAO", TextureUnits::ssao);
    program->setSamplerUnit("sSSR", TextureUnits::ssr);
    program->setSamplerUnit("sHilights", TextureUnits::hilights);
    program->setSamplerUnit("sOITAccum", TextureUnits::oitAccum);
    program->setSamplerUnit("sOITRevealage", TextureUnits::oitRevealage);
    program->setSamplerUnit("sNoise", TextureUnits::noise);
    program->setSamplerUnit("sEnvMapCube", TextureUnits::envMapCube);
    program->setSamplerUnit("sShadowMapCube", TextureUnits::shadowMapCube);
    program->setSamplerUnit("sBumpMapArray", TextureUnits::bumpMapArray);
    program->setSamplerUnit("sShadowMap", TextureUnits::shadowMapArray);
    program->setSamplerUnit("sBRDFLUT", TextureUnits::brdfLUT);
    program->setSamplerUnit("sIrradianceMapArray", TextureUnits::irradianceMapArray);
    program->setSamplerUnit("sPrefilteredEnvMapArray", TextureUnits::prefilteredEnvMapArray);

    // Uniform Blocks
    program->bindUniformBlock("Globals", UniformBlockBindingPoints::globals);
    program->bindUniformBlock("Locals", UniformBlockBindingPoints::locals);
    program->bindUniformBlock("Bones", UniformBlockBindingPoints::bones);
    program->bindUniformBlock("Dangly", UniformBlockBindingPoints::dangly);
    program->bindUniformBlock("Particles", UniformBlockBindingPoints::particles);
    program->bindUniformBlock("Grass", UniformBlockBindingPoints::grass);
    program->bindUniformBlock("Walkmesh", UniformBlockBindingPoints::walkmesh);
    program->bindUniformBlock("Text", UniformBlockBindingPoints::text);
    program->bindUniformBlock("ScreenEffect", UniformBlockBindingPoints::screenEffect);
    program->bindUniformBlock("AABB", UniformBlockBindingPoints::aabb);
    program->bindUniformBlock("Profiler", UniformBlockBindingPoints::profiler);

    return program;
}

} // namespace resource

} // namespace reone
