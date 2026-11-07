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

#include "reone/graphics/shaderprogram.h"

#include "reone/graphics/types.h"
#include "reone/system/threadutil.h"

namespace reone {

namespace graphics {

void ShaderProgram::init() {
    if (_inited) {
        return;
    }
    checkMainThread();

    _nameGL = glCreateProgram();
    for (auto &shader : _shaders) {
        glAttachShader(_nameGL, shader->nameGL());
    }
    glLinkProgram(_nameGL);

    GLint success;
    glGetProgramiv(_nameGL, GL_LINK_STATUS, &success);
    if (!success) {
        char log[4096];
        GLsizei logSize;
        glGetProgramInfoLog(_nameGL, sizeof(log), &logSize, log);
        throw std::runtime_error("Failed linking shader program: " + std::string(log, logSize));
    }

    _inited = true;
}

void ShaderProgram::deinit() {
    if (!_inited) {
        return;
    }
    checkMainThread();
    glDeleteProgram(_nameGL);
    _shaders.clear();
    _inited = false;
}

void ShaderProgram::use() {
    glUseProgram(_nameGL);
}

void ShaderProgram::bindUniformBlock(const std::string &name, int bindingPoint) {
    GLuint blockIdx = glGetUniformBlockIndex(_nameGL, name.c_str());
    if (blockIdx == GL_INVALID_INDEX) {
        return;
    }
    glUniformBlockBinding(_nameGL, blockIdx, bindingPoint);
}

void ShaderProgram::setSamplerUnit(const std::string &name, int unit) {
    GLint location = glGetUniformLocation(_nameGL, name.c_str());
    if (location != -1) {
        glUniform1i(location, unit);
    }
}

} // namespace graphics

} // namespace reone
