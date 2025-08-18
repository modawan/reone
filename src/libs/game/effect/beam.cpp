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
#include "reone/resource/provider/models.h"
#include "reone/resource/provider/textures.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/effect.h"

#include <limits>

namespace reone {

namespace game {

using namespace scene;

BeamDrainLife::BeamDrainLife(
    std::shared_ptr<Object> effector,
    BodyNode bodyPart,
    bool missEffect,
    ServicesView &services) :
    Effect(EffectType::Beam),
    _effector(effector),
    _bodyPart(bodyPart),
    _missEffect(missEffect),
    _services(services) {}

BeamDrainLife::~BeamDrainLife() {
    auto &sceneGraph = _services.scene.graphs.get(kSceneMain);
    if (_beamNode) {
        sceneGraph.removeRoot(*_beamNode);
    }
    if (_conjNode) {
        _conjNode->parent()->removeChild(*_conjNode);
    }
}

static SceneNode &selectSourceNode(SceneNode &src, BodyNode bodyPart) {
    if (src.type() != SceneNodeType::Model) {
        return src;
    }
    ModelSceneNode &model = (ModelSceneNode &)src;

    const char *name = nullptr;
    switch (bodyPart) {
    case BodyNode::Hand:
        name = "handconjure";
        break;
    case BodyNode::Chest:
        name = "torso";
        break;
    case BodyNode::Head:
        name = "headconjure";
        break;
    case BodyNode::HandLeft:
        name = "lhand";
        break;
    case BodyNode::HandRight:
        name = "lhand";
        break;
    }

    if (!name) {
        return model;
    }

    if (ModelNodeSceneNode *part = model.getNodeByName(name)) {
        return *part;
    }

    return model;
}

static SceneNode &selectTargetNode(SceneNode &target) {
    if (target.type() != SceneNodeType::Model) {
        return target;
    }
    ModelSceneNode &model = (ModelSceneNode &)target;

    if (SceneNode *torso = model.getNodeByName("impact")) {
        return *torso;
    }
    return model;
}

void BeamDrainLife::apply(Object &object, DurationType durationType, float duration) {
    SceneNode &source = selectSourceNode(*_effector->sceneNode(), _bodyPart);
    SceneNode &target = selectTargetNode(*object.sceneNode());

    auto &sceneGraph = _services.scene.graphs.get(kSceneMain);

    // Add a beam effect from the source to the target
    _beamNode = std::make_shared<scene::DrainLifeBeamNode>(
        source, target, duration, sceneGraph);
    sceneGraph.addRoot(std::static_pointer_cast<scene::EffectSceneNode>(_beamNode));

    // Add a conjuration effect to the source
    _conjModel = _services.resource.models.get("v_drain_dur");
    _conjNode = sceneGraph.newModel(*_conjModel, scene::ModelUsage::Projectile);
    source.addChild(*_conjNode);
}

} // namespace game

} // namespace reone
