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

#include "reone/graphics/shader.h"
#include "reone/graphics/shaderprogram.h"
#include "reone/graphics/types.h"
#include "reone/system/types.h"

namespace reone {

namespace graphics {

struct GraphicsOptions;

class ShaderRegistry;

} // namespace graphics

namespace resource {

class IResources;

/**
 * GLSL resource names of the stages linked into one shader program.
 */
struct ShaderProgramSpec {
    std::string id; /**< ShaderProgramId */
    std::string vertex;
    std::string geometry; /**< empty if the program has no geometry stage */
    std::string fragment;
};

/**
 * Every shader program the engine links, in registration order.
 */
const std::vector<ShaderProgramSpec> &shaderProgramSpecs();

/**
 * Splices `#include "name.glsl"` lines of resRef, recursively, into a list
 * of sources ordered so that each include precedes its includer. The
 * #version line and option defines are not added. Includes are not
 * deduplicated: a file reached twice is spliced twice.
 */
std::list<std::string> composeShaderSources(const std::string &resRef,
                                            const std::function<std::string(const std::string &)> &loadSource);

class IShaders {
public:
    virtual ~IShaders() = default;
};

class Shaders : public IShaders, boost::noncopyable {
public:
    Shaders(graphics::GraphicsOptions &graphicsOpt,
            graphics::ShaderRegistry &shaderRegistry,
            IResources &resources) :
        _graphicsOpt(graphicsOpt),
        _shaderRegistry(shaderRegistry),
        _resources(resources) {
    }

    ~Shaders() {
        deinit();
    }

    void init();
    void deinit();

private:
    graphics::GraphicsOptions &_graphicsOpt;
    graphics::ShaderRegistry &_shaderRegistry;
    IResources &_resources;

    bool _inited {false};

    std::map<std::string, ByteBuffer> _sourceResRefToData;

    std::shared_ptr<graphics::Shader> initShader(graphics::ShaderType type, std::string resRef);
    std::shared_ptr<graphics::ShaderProgram> initShaderProgram(std::vector<std::shared_ptr<graphics::Shader>> shaders);
};

} // namespace resource

} // namespace reone
