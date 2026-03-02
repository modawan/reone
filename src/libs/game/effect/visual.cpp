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

#include "reone/game/effect/visual.h"
#include "reone/audio/mixer.h"
#include "reone/game/di/services.h"
#include "reone/game/object.h"
#include "reone/game/object/area.h"
#include "reone/game/visualeffects.h"
#include "reone/graphics/animation.h"
#include "reone/graphics/model.h"
#include "reone/scene/graph.h"

namespace reone {

namespace game {

VisualEffect::VisualEffect(int visualEffectId, bool missEffect, ServicesView &services) :
    Effect(EffectType::Visual),
    _visualEffectId(visualEffectId),
    _missEffect(missEffect),
    _desc(services.game.visualEffects.get(visualEffectId).value_or(nullptr)),
    _services(services) {
}

VisualEffect::~VisualEffect() {
    if (_node) {
        scene::ISceneGraph &graph = _node->graph();
        graph.removeRoot(*_node);
    }
}

static float durationAnim(const std::shared_ptr<graphics::Model> &model) {
    if (!model) {
        return 0.0f;
    }

    if (std::shared_ptr<graphics::Animation> anim = model->getAnimation("impact")) {
        return anim->length();
    };

    return 0.0f;
}

static float durationSound(const std::shared_ptr<audio::AudioClip> &sound) {
    if (!sound) {
        return 0.0f;
    }

    return sound->duration();
}

void VisualEffect::applyTo(Object &object) {
    if (!_desc || !_location) {
        return;
    }

    scene::ISceneGraph *graph = nullptr;
    if (Area *area = dyn_cast<Area>(&object)) {
        graph = &area->graph();
    } else if (auto node = object.sceneNode()) {
        graph = &node->graph();
    }

    if (!graph) {
        return;
    }

    if (_desc->impRootMNode) {
        _node = graph->newModel(*_desc->impRootMNode, scene::ModelUsage::Projectile);
        graph->addRoot(_node);
        _node->setLocalTransform(glm::translate(_location.value()));
        _node->playAnimation("impact");
    }

    if (_desc->soundImpact) {
        _services.audio.mixer.play(
            _desc->soundImpact,
            audio::AudioType::Sound,
            /*gain=*/1.0f,
            /*loop=*/false,
            _location);
    }
}

float VisualEffect::duration() const {
    if (!_desc) {
        return 0.0f;
    }

    float anim = durationAnim(_desc->impRootMNode);
    float sound = durationSound(_desc->soundImpact);
    return std::max(anim, sound);
}

} // namespace game

} // namespace reone
