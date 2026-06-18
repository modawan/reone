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

#pragma once

#include <array>

#include "framebuffer.h"
#include "renderbuffer.h"
#include "texture.h"

namespace reone {

namespace graphics {

struct EnvMapDerivedRequest {
    Texture &texture;

    EnvMapDerivedRequest(Texture &texture) :
        texture(texture) {
    }
};

static constexpr int kMaxEnvMapDerivedLayers = 16;
static constexpr int kMaxEnvMapDerivedCubemapLayers = 4;

} // namespace graphics

} // namespace reone

template <>
struct std::less<reone::graphics::EnvMapDerivedRequest> {
    bool operator()(const reone::graphics::EnvMapDerivedRequest &lhs,
                    const reone::graphics::EnvMapDerivedRequest &rhs) const {
        return lhs.texture.name() < rhs.texture.name();
    }
};

namespace reone {

namespace graphics {

class IContext;
class IMeshRegistry;
class IShaderRegistry;
class IStatistic;
class IUniforms;

class IPBRTextures {
public:
    virtual ~IPBRTextures() = default;

    virtual void refresh() = 0;
    virtual void requestEnvMapDerived(EnvMapDerivedRequest request) = 0;
    virtual std::optional<int> findEnvMapDerivedLayer(const std::string &name) = 0;
    virtual void bindEnvMapDerived(IContext &context) = 0;

    virtual Texture &brdf() = 0;
};

class PBRTextures : public IPBRTextures, boost::noncopyable {
public:
    PBRTextures(IContext &context,
                IMeshRegistry &meshRegistry,
                IShaderRegistry &shaderRegistry,
                IStatistic &statistic,
                IUniforms &uniforms) :
        _context(context),
        _meshRegistry(meshRegistry),
        _shaderRegistry(shaderRegistry),
        _statistic(statistic),
        _uniforms(uniforms) {
    }

    void refresh();

    void requestEnvMapDerived(EnvMapDerivedRequest request);

    std::optional<int> findEnvMapDerivedLayer(const std::string &name) {
        auto it = _envMapToDerivedLayer.find(name);
        if (it == _envMapToDerivedLayer.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    void bindEnvMapDerived(IContext &context);

    Texture &brdf() {
        return *_brdfLUT;
    }

    Texture &irradianceMapArray() {
        return *_irradianceMapArray;
    }

    Texture &prefilteredEnvMapArray() {
        return *_prefilteredEnvMapArray;
    }

private:
    IContext &_context;
    IMeshRegistry &_meshRegistry;
    IShaderRegistry &_shaderRegistry;
    IStatistic &_statistic;
    IUniforms &_uniforms;

    bool _useCubeMapArray {false};
    bool _envPathResolved {false};

    std::shared_ptr<Texture> _brdfLUT;
    std::shared_ptr<Renderbuffer> _brdfDepthBuffer;
    std::shared_ptr<Framebuffer> _brdfFramebuffer;

    std::set<EnvMapDerivedRequest> _envMapDerivedRequests;
    std::shared_ptr<Texture> _irradianceMapArray;
    std::shared_ptr<Renderbuffer> _irradianceDepthBuffer;
    std::shared_ptr<Framebuffer> _irradianceFramebuffer;
    std::shared_ptr<Texture> _prefilteredEnvMapArray;
    std::array<std::shared_ptr<Texture>, kMaxEnvMapDerivedCubemapLayers> _irradianceCubemaps;
    std::array<std::shared_ptr<Texture>, kMaxEnvMapDerivedCubemapLayers> _prefilterCubemaps;
    std::vector<std::shared_ptr<Renderbuffer>> _prefilterDepthBuffers;
    std::shared_ptr<Framebuffer> _prefilterFramebuffer;
    std::map<std::string, int> _envMapToDerivedLayer;

    int _envMapDerivedLayer {0};

    int maxEnvMapDerivedLayers() const {
        return _useCubeMapArray ? kMaxEnvMapDerivedLayers : kMaxEnvMapDerivedCubemapLayers;
    }

    void resolveEnvPath();

    void initBRDFLUT();
    void initIrradianceMapArray();
    void initPrefilteredEnvMapArray();
    void initEnvMapCubemapPool();

    void refreshEnvMapDerived(const EnvMapDerivedRequest &request);
    void refreshIrradianceMap(const EnvMapDerivedRequest &request, int layer);
    void refreshPrefilteredEnvMap(const EnvMapDerivedRequest &request, int layer);
};

} // namespace graphics

} // namespace reone
