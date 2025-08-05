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

#include "reone/game/effect/beam.h"
#include "reone/game/object.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/effect.h"

namespace reone {

namespace game {

using namespace scene;

BeamEffect::~BeamEffect() {
    _source->removeChild(*_node);
    auto &sceneGraph = _services.scene.graphs.get(kSceneMain);    
    sceneGraph.removeRoot(*_node);
}

static SceneNode *selectSourceNode(SceneNode *src) {
    if (src->type() != SceneNodeType::Model) {
        return src;
    }

    ModelSceneNode * model = (ModelSceneNode *) src;
    if (SceneNode *hand = model->getNodeByName("lhand")) {
        return hand;
    }
    return model;
}

void BeamEffect::applyTo(Object &object) {
    auto &sceneGraph = _services.scene.graphs.get(kSceneMain);


    std::shared_ptr<SceneNode> effectorNode = _effector->sceneNode();
    std::shared_ptr<SceneNode> objectNode = _effector->sceneNode();

    _node = std::make_shared<scene::EffectSceneNode>(
            sceneGraph,
            _services.graphics,
            _services.audio,
            _services.resource,
            objectNode);

    _source = selectSourceNode(effectorNode.get());
    _source->addChild(*_node);
    sceneGraph.addRoot(_node);
}

} // namespace game

} // namespace reone
