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
#include "reone/scene/graph.h"
#include "reone/scene/render/pass.h"
#include "reone/system/randomutil.h"

namespace reone {

namespace scene {

EffectSceneNode::EffectSceneNode(ISceneGraph &sceneGraph) :
    SceneNode(SceneNodeType::Effect,
              sceneGraph,
              sceneGraph.graphicsServices(),
              sceneGraph.audioServices(),
              sceneGraph.resourceServices()) {}

DrainLifeBeamNode::DrainLifeBeamNode(
    SceneNode &source,
    SceneNode &target,
    float duration,
    ISceneGraph &sceneGraph) :
    EffectSceneNode(sceneGraph),
    _source(source), _target(target), _duration(duration) {

    _beginDirP1 = glm::vec3(randomFloat(-1.0f, 1.0f),
                            randomFloat(-1.0f, 1.0f),
                            randomFloat(0, 1.0f));

    _endDirP1 = glm::vec3(randomFloat(-1.0f, 1.0f),
                          randomFloat(-1.0f, 1.0f),
                          randomFloat(0, 1.0f));

    std::shared_ptr<graphics::Texture> tex =
        _resourceSvc.textures.get("fx_drain", graphics::TextureUsage::MainTex);

    graphics::Texture::Features features;
    features.blending = graphics::Texture::Blending::Additive;
    features.decal = true;
    tex->setFeatures(features);

    _material.type = graphics::MaterialType::TransparentModel;

    _material.uv = glm::mat3x4(
        glm::vec4(1.0f, 0.0f, 0.0f, 0.0f),
        glm::vec4(0.0f, 1.0f, 0.0f, 0.0f),
        glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));

    _material.faceCulling = graphics::FaceCullMode::None;
    _material.textures.insert({graphics::TextureUnits::mainTex, *tex});

    float width = 0.3;
    _scale = glm::mat4(
        glm::vec4(width, 0.0f, 0.0f, 0.0f),
        glm::vec4(0.0f, 1.0f, 0.0f, 0.0f),
        glm::vec4(0.0f, 0.0f, 1.0f, 0.0f),
        glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
}

void DrainLifeBeamNode::update(float dt) {
    glm::vec3 p0 = _source.origin();
    glm::vec3 p2 = _target.origin();

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

void DrainLifeBeamNode::render(IRenderPass &pass) {
    pass.drawBezier(_graphicsSvc.meshRegistry.get(graphics::MeshName::billboard),
                    _material, _scale, _bezierPoints,
                    /*numSegments=*/20, /*numOrientations=*/3);
}

static float fpsInterval(graphics::Texture &tex) {
    return 1.0f / tex.features().fps;
}

static int numFrames(graphics::Texture &tex) {
    const graphics::Texture::Features &f = tex.features();
    return f.numX * f.numY;
}

FieldNode::FieldNode(
    ModelSceneNode &target,
    const std::shared_ptr<graphics::Texture> &tex,
    ISceneGraph &sceneGraph) :
    EffectSceneNode(sceneGraph),
    _target(target), _tex(tex), _frameTimer(fpsInterval(*tex)) {

    _material.type = graphics::MaterialType::Field;

    _material.uv = glm::mat3x4(
        glm::vec4(1.0f, 0.0f, 0.0f, 0.0f),
        glm::vec4(0.0f, 1.0f, 0.0f, 0.0f),
        glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));

    _material.faceCulling = graphics::FaceCullMode::None;
    _material.textures.insert({graphics::TextureUnits::mainArrayTex, *tex});
    _material.shiftAlongNormal = 0.01f;
}

void FieldNode::update(float dt) {
    _frameTimer.update(dt);
    if (_frameTimer.elapsed()) {
        _material.mainArrayFrame = (_material.mainArrayFrame + 1) % numFrames(*_tex);
        _frameTimer.reset(fpsInterval(*_tex));
    }
}

void FieldNode::render(IRenderPass &pass) {
    std::vector<SceneNode *> worklist;
    worklist.push_back(&_target);
    while (!worklist.empty()) {
        SceneNode &node = *worklist.back();
        worklist.pop_back();

        SceneNodeType type = node.type();
        if (type == SceneNodeType::Model) {
            auto &model = static_cast<ModelSceneNode &>(node);
            if (model.isCulled()) {
                continue;
            }
        } else if (type == SceneNodeType::Mesh) {
            auto &meshNode = static_cast<MeshSceneNode &>(node);
            if (meshNode.shouldRender() && !meshNode.isTransparent()) {
                meshNode.renderWithMaterial(_material, pass);
            }
        }

        for (SceneNode *child : node.children()) {
            worklist.push_back(child);
        }
    }
}

} // namespace reone
} // namespace scene
