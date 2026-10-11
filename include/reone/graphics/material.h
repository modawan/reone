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

#include "types.h"

namespace reone {

namespace graphics {

class Texture;

enum class MaterialType {
    OpaqueModel,
    TransparentModel,
    DirLightShadow,
    PointLightShadow,
    Walkmesh
};

struct Material : boost::noncopyable {
public:
    using TextureUnit = int;
    using TextureUnitToTexture = std::unordered_map<TextureUnit, std::reference_wrapper<Texture>>;

    MaterialType type;
    TextureUnitToTexture textures;
    glm::mat3x4 uv {1.0f};
    glm::vec4 color {1.0f};
    int bumpMapFrame {0};
    float shellOffset {0.0f};

    glm::vec3 ambientColor {1.0f};
    glm::vec3 diffuseColor {1.0f};
    glm::vec3 selfIllumColor {0.0f};

    /** Under Force Sight, whether the surface glows. */
    bool forceGlow {false};
    /**
     * Under Force Sight, the glow colour of a glowing surface, or the alpha
     * the grey of any other surface is drawn with.
     */
    glm::vec4 forceSightColor {0.0f, 0.0f, 0.0f, 1.0f};

    bool staticObject {false};
    bool affectedByShadows {false};
    bool affectedByFog {false};

    std::optional<BlendMode> blending;
    std::optional<FaceCullMode> faceCulling;
    std::optional<PolygonMode> polygonMode;
};

} // namespace graphics

} // namespace reone
