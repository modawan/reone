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

    // std::shared_ptr<graphics::Texture> tex =
    //     _resourceSvc.textures.get("po_pbastila", graphics::TextureUsage::MainTex);

    glm::vec3 target = _absTransformInv * glm::vec4(_target->origin(), 1.0f);
    glm::vec3 dir = glm::normalize(target);

    glm::vec3 newY, newX, newZ;
    if (dir.x == 0.0f && dir.y == 0.0f) {
        if (dir.y < 0.0f) {
            newY = -dir;
            newX = glm::vec3(-1.0f, 0.0f, 0.0f);
            newZ = glm::vec3(0.0f, 0.0f, 1.0f);
        } else {
            newY = dir;
            newX = glm::vec3(1.0f, 0.0f, 0.0f);
            newZ = glm::vec3(0.0f, 0.0f, 1.0f);
        }
    } else {
        newY = dir;
        newZ = glm::cross(newY, glm::vec3(0.0f, 1.0f, 0.0f));
        newX = glm::normalize(glm::cross(newY, newZ));
    }

    glm::mat4x4 rotate(glm::vec4(newX, 0.0f),
                       glm::vec4(newY, 0.0f),
                       glm::vec4(newZ, 0.0f),
                       glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

    float len = glm::length(target);
    float width = 0.1;
    glm::vec3 scale(width, len, 1.0f);
    // glm::vec3 pos(0.0f, len / 2, 0.0f);
    glm::vec3 pos(0.0f, 0.0f, 0.0f);

    glm::mat4 transform = _absTransform
        * rotate
        // * glm::translate(glm::mat4(1.0f), pos)
        * glm::scale(glm::mat4(1.0f), scale);

    _graphicsSvc.context.useProgram(
            _graphicsSvc.shaderRegistry.get(graphics::ShaderProgramId::mvpTexture));
    _graphicsSvc.context.bindTexture(*tex, graphics::TextureUnits::mainTex);

    graphics::Material material;
    material.type = graphics::MaterialType::TransparentModel;
    material.selfIllumColor = glm::vec3(1.0f, 0.0f, 0.0f);

    pass.draw(_graphicsSvc.meshRegistry.get(graphics::MeshName::quad),
              material, transform, glm::inverse(transform));
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
