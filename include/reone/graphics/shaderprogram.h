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

#include "shader.h"

namespace reone {

namespace graphics {

class ShaderProgram : boost::noncopyable {
public:
    ShaderProgram(std::vector<std::shared_ptr<Shader>> shaders) :
        _shaders(std::move(shaders)) {
    }

    ~ShaderProgram() { deinit(); }

    void init();
    void deinit();

    void use();

    void bindUniformBlock(const std::string &name, int bindingPoint);

    /**
     * Points the named sampler at a texture unit. Every other uniform lives
     * in a uniform block, see uniforms.h.
     */
    void setSamplerUnit(const std::string &name, int unit);

private:
    std::vector<std::shared_ptr<Shader>> _shaders;

    bool _inited {false};

    // OpenGL

    uint32_t _nameGL {0};

    // END OpenGL
};

} // namespace graphics

} // namespace reone
