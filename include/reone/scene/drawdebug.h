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

void updateDrawDebug(float dt);

void renderDrawDebug(scene::IRenderPass &pass,
                     graphics::GraphicsServices &services,
                     resource::ResourceServices &resources,
                     std::string_view sceneName);

namespace drawdebug {
/**
 * DrawDebug ID opens a new scope for elements to go to.
 *
 * All operations (such as clear(), line(), box(), etc.) operate implicitly on
 * the list of elements of the current scope.
 */
void pushId(uint64_t id);

/**
 * Convenience wrapper to use any pointer as an ID.
 */
void pushId(const void *id);

/**
 * Close the current scope.
 */
void popId();

/**
 * Set lifetime (in seconds) for all subsequent elements. Elements are removed
 * by updateDrawDebug once their lifetime is over, or when clear() is called
 * explicitly.
 */
void pushLifetime(float lifetime);

/**
 * Set the previous lifetime value.
 */
void popLifetime();

/**
 * Set scene graph name for all subsequent elements.
 */
void pushScene(std::string sceneName);

/**
 * Set the previous scene graph.
 */
void popScene();

/**
 * Remove all elements in the current scope (which is defined by pushId()).
 */
void clear();

void line(glm::vec3 start, glm::vec3 end, uint32_t colorRgba, float thickness);
void triangle(glm::vec3 v0, glm::vec3 v1, glm::vec3 v2, uint32_t colorRgba);
void text(const std::string &str, glm::vec3 position, uint32_t colorRgba, float scale = 1.0);
void point(glm::vec3 position, uint32_t colorRgba, float scale = 1.0);
void box(glm::vec3 min, glm::vec3 max, uint32_t colorRgba);

} // namespace drawdebug

} // namespace reone
