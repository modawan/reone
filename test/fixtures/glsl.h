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

#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace reone {

namespace glsl {

/**
 * Reads glsl/<name> from the source tree. Returns an empty string if the
 * file does not exist.
 */
std::string readShaderSource(const std::string &name);

/**
 * Lists the .glsl file names in glsl/, sorted.
 */
std::vector<std::string> listShaderSources();

struct Variable {
    std::string type;
    std::string name;
    int arraySize {0}; /**< 0 if not an array */
};

struct UniformBlock {
    std::string name;
    bool std140 {false};
    std::vector<Variable> members;
};

/**
 * Uniform declarations of a GLSL source, after comments are stripped.
 * Both branches of every #if are read, which over-counts at worst.
 */
struct Uniforms {
    std::map<std::string, int> constants; /**< #define and const int */
    std::map<std::string, std::vector<Variable>> structs;
    std::vector<UniformBlock> blocks;
    std::vector<Variable> samplers;
    std::vector<Variable> loose; /**< uniforms that are neither samplers nor in a block */

    int samplerCount() const;
};

/**
 * Parses the uniform declarations of a GLSL source. Throws if a sampler or
 * block member array size is not a constant integer expression.
 */
Uniforms parseUniforms(const std::string &source);

struct Std140Member {
    std::string name;
    size_t offset {0};
    size_t size {0};
};

struct Std140Layout {
    std::vector<Std140Member> members;
    size_t size {0}; /**< end of the last member */
};

/**
 * Lays out members by the std140 rules. Struct types are looked up in
 * structs. Throws on a type it does not know.
 */
Std140Layout std140Layout(const std::vector<Variable> &members,
                          const std::map<std::string, std::vector<Variable>> &structs);

} // namespace glsl

} // namespace reone
