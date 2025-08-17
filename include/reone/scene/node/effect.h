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

#include "reone/graphics/material.h"
#include "reone/scene/node.h"
#include "reone/scene/node/model.h"

namespace reone {

namespace scene {

class EffectSceneNode : public SceneNode {
public:
    EffectSceneNode(ISceneGraph &sceneGraph);
    virtual void render(IRenderPass &pass) = 0;
};

class DrainLifeBeamNode : public EffectSceneNode {
public:
    DrainLifeBeamNode(
        SceneNode &source,
        SceneNode &target,
        float duration,
        ISceneGraph &sceneGraph);

    void update(float dt) override;
    void render(IRenderPass &pass) override;

private:
    SceneNode &_source;
    SceneNode &_target;

    // Animation
    float _duration;
    float _time = 0.0f;
    glm::vec3 _beginDirP1;
    glm::vec3 _endDirP1;

    // Render
    glm::mat4 _scale;
    graphics::Material _material;

    std::array<glm::vec3, 3> _bezierPoints;
};

class FieldNode : public EffectSceneNode {
public:
    FieldNode(
        ModelSceneNode &target,
        const std::shared_ptr<graphics::Texture> &tex,
        ISceneGraph &sceneGraph);

    void update(float dt) override;
    void render(IRenderPass &pass) override;

private:
    SceneNode &_target;
    graphics::Material _material;
    std::shared_ptr<graphics::Texture> _tex;
};

} // namespace scene

} // namespace reone
