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

#include "graphics.h"

namespace reone {

namespace graphics {

MockContext::MockContext() = default;
MockContext::~MockContext() = default;

MockMeshRegistry::MockMeshRegistry() = default;
MockMeshRegistry::~MockMeshRegistry() = default;

MockPBRTextures::MockPBRTextures() = default;
MockPBRTextures::~MockPBRTextures() = default;

MockShaderRegistry::MockShaderRegistry() = default;
MockShaderRegistry::~MockShaderRegistry() = default;

MockStatistic::MockStatistic() = default;
MockStatistic::~MockStatistic() = default;

MockTextureRegistry::MockTextureRegistry() = default;
MockTextureRegistry::~MockTextureRegistry() = default;

MockUniforms::MockUniforms() = default;
MockUniforms::~MockUniforms() = default;

} // namespace graphics

} // namespace reone
