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

#include "reone/scene/node/effect.h"
#include "reone/resource/di/services.h"
#include "reone/graphics/di/services.h"
#include "reone/graphics/types.h"
#include "reone/resource/provider/textures.h"
#include "reone/graphics/context.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/uniforms.h"
#include "reone/graphics/texture.h"

#include "reone/scene/render/pass.h"
#include "reone/graphics/types.h"
#include "reone/graphics/material.h"

namespace reone {

namespace scene {

void EffectSceneNode::render(IRenderPass &pass) {
    std::shared_ptr<graphics::Texture> tex =
        _resourceSvc.textures.get("fx_drain", graphics::TextureUsage::MainTex);

    graphics::Texture::Features features;
    features.blending = graphics::Texture::Blending::Additive;
    features.decal = true;
    tex->setFeatures(features);

    float width = 0.3;
    glm::mat4 transform =
        glm::mat4(glm::vec4(width, 0.0f, 0.0f, 0.0f),
                  glm::vec4(0.0f,  1.0f, 0.0f, 0.0f),
                  glm::vec4(0.0f,  0.0f, 1.0f, 0.0f),
                  glm::vec4(0.0f,  0.0f, 0.0f, 1.0f));

    graphics::Material material;
    material.type = graphics::MaterialType::TransparentModel;

    material.uv = glm::mat3x4(
            glm::vec4(1.0f, 0.0f, 0.0f, 0.0f),
            glm::vec4(0.0f, 1.0f, 0.0f, 0.0f),
            glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));

    material.faceCulling = graphics::FaceCullMode::None;
    material.textures.insert({graphics::TextureUnits::mainTex, *tex});

    glm::vec3 p0 = origin();
    glm::vec3 p2 = _target->origin();

    glm::vec3 p1 = glm::mix(p0, p2, glm::vec3(0.5f, 0.5f, 0.5f));
    float dist = glm::distance(p2, p1);
    p1 += glm::vec3(0.0f, 0.0f, dist * 0.5f);

    pass.drawBezier(_graphicsSvc.meshRegistry.get(graphics::MeshName::billboard),
                    material, transform, glm::inverse(transform),
                    {p0, p1, p2}, /*numSegments=*/20, /*numOrientations=*/3);
}

void EffectSceneNode::update(float dt) {
    // std::shared_ptr<graphics::Texture> tex =
    //     _resourceSvc.textures.get("fx_drain", graphics::TextureUsage::Default);
    // _graphicsSvc.context.bindTexture(*tex);

    // glm::vec2 mapPos(100.0f, 100.0f);
    // glm::vec4 bounds(512.0f, 512.0f, 8.0f, 8.0f);

    // glm::vec3 topLeft(0.0f);
    // topLeft.x = 512.0f;
    // topLeft.y = 512.0f;

    // glm::mat4 transform(1.0f);
    // transform = glm::translate(transform, topLeft);
    // transform = glm::scale(transform, glm::vec3(tex->width(), tex->height(), 1.0f));

    // _graphicsSvc.uniforms.setLocals([transform](auto &locals) {
    //     locals.reset();
    //     locals.model = std::move(transform);
    // });
    // _graphicsSvc.context.useProgram(_graphicsSvc.shaderRegistry.get(graphics::ShaderProgramId::mvpTexture));

    // int height = 1024;
    // glm::ivec4 scissorBounds(bounds[0], height - (bounds[1] + bounds[3]), bounds[2], bounds[3]);
    // _graphicsSvc.context.withScissorTest(scissorBounds, [&]() {
    //     _graphicsSvc.meshRegistry.get(graphics::MeshName::quad)
    //         .draw(_graphicsSvc.statistic);
    // });
}

} // namespace reone
} // namespace scene
