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

#include "../node.h"

namespace reone {

namespace scene {

class EffectSceneNode : public SceneNode {
public:
    EffectSceneNode(
        ISceneGraph &sceneGraph,
        graphics::GraphicsServices &graphicsSvc,
        audio::AudioServices &audioSvc,
        resource::ResourceServices &resourceSvc,
        SceneNode *target,
        float duration);

    void update(float dt) override;
    void render(IRenderPass &pass);

private:
    SceneNode *_target;
    float _duration;
    float _time = 0.0f;
    glm::vec3 _beginDirP1;
    glm::vec3 _endDirP1;
    std::vector<glm::vec3> _bezierPoints;
};

} // namespace scene

} // namespace reone
