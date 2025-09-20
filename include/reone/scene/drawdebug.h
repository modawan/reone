/*
 * Copyright (c) 2025 The reone project contributors
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

#include <limits>
#include <string>

namespace reone {

namespace graphics {
class GraphicsServices;
}

namespace scene {
class IRenderPass;
}

namespace resource {
class ResourceServices;
}

void updateDrawDebug(float dt, const char *scene = "main");

void renderDrawDebug(scene::IRenderPass &pass,
                     graphics::GraphicsServices &services,
                     resource::ResourceServices &resources,
                     const char *scene = "main");

void clearDrawDebug(const char *tag, const char *scene = "main");

void drawDebugLine(glm::vec3 start, glm::vec3 end, glm::vec4 color,
                   float thickness, const char *tag = nullptr,
                   float lifeTime = std::numeric_limits<float>::max(),
                   const char *scene = "main");

void drawDebugTriangle(glm::vec3 v0, glm::vec3 v1, glm::vec3 v2, glm::vec4 color,
                       const char *tag = nullptr,
                       float lifeTime = std::numeric_limits<float>::max(),
                       const char *scene = "main");

void drawDebugText(const std::string &str, glm::vec3 position, glm::vec4 color,
                   float scale = 1.0, const char *tag = nullptr,
                   float lifeTime = std::numeric_limits<float>::max(),
                   const char *scene = "main");

void drawDebugPoint(glm::vec3 position, glm::vec4 color,
                    float scale = 1.0, const char *tag = nullptr,
                    float lifeTime = std::numeric_limits<float>::max(),
                    const char *scene = "main");

void drawDebugAABB(glm::vec3 min, glm::vec3 max, glm::vec4 color,
                   const char *tag = nullptr,
                   float lifeTime = std::numeric_limits<float>::max(),
                   const char *scene = "main");

} // namespace reone
