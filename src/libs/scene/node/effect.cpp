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

#include "reone/graphics/material.h"
#include "reone/graphics/types.h"
#include "reone/scene/render/pass.h"
#include "reone/system/randomutil.h"

namespace reone {

namespace scene {

EffectSceneNode::EffectSceneNode(
    ISceneGraph &sceneGraph,
    graphics::GraphicsServices &graphicsSvc,
    audio::AudioServices &audioSvc,
    resource::ResourceServices &resourceSvc,
    SceneNode *target,
    float duration) :
    SceneNode(
        SceneNodeType::GrassCluster,
        sceneGraph,
        graphicsSvc,
        audioSvc,
        resourceSvc),
    _target(target),
    _duration(duration) {

    _beginDirP1 = glm::vec3(randomFloat(-1.0f, 1.0f),
                            randomFloat(-1.0f, 1.0f),
                            randomFloat(0, 1.0f));

    _endDirP1 = glm::vec3(randomFloat(-1.0f, 1.0f),
                          randomFloat(-1.0f, 1.0f),
                          randomFloat(0, 1.0f));

    _bezierPoints.resize(3);
}

void EffectSceneNode::update(float dt) {
    glm::vec3 p0 = origin();
    glm::vec3 p2 = _target->origin();

    _time += dt;
    glm::vec3 dirP1 = glm::mix(_beginDirP1, _endDirP1, glm::vec3(_time / _duration));

    glm::vec3 dir = p2 - p0;
    glm::vec3 center = p0 + dir * 0.5f;
    float r = glm::length(dir) / 2;
    glm::vec3 p1 = center + dirP1 * r;

    _bezierPoints[0] = p0;
    _bezierPoints[1] = p1;
    _bezierPoints[2] = p2;
}

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

    pass.drawBezier(_graphicsSvc.meshRegistry.get(graphics::MeshName::billboard),
                    material, transform, glm::inverse(transform),
                    _bezierPoints, /*numSegments=*/20, /*numOrientations=*/3);
}

} // namespace reone
} // namespace scene
