/*
 * Copyright (c) 2026 The reone project contributors
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

#include "reone/game/castspell.h"
#include "reone/game/game.h"

namespace reone {
namespace game {

SpellSchedule::State SpellSchedule::update(
    const CombatRound &round, Action &action, float dt) {

    _time += dt;

    switch (_state) {
    case SpellSchedule::WaitConjure: {
        if (round.canExecute(action)) {
            _state = SpellSchedule::Conjure;
        }
        break;
    }
    case SpellSchedule::Conjure: {
        _state = SpellSchedule::WaitCast;
        break;
    }

    case SpellSchedule::WaitCast: {
        if (_time >= _conjTime) {
            _state = SpellSchedule::Cast;
        }
        break;
    }
    case SpellSchedule::Cast: {
        _state = SpellSchedule::WaitEffect;
        break;
    }
    case SpellSchedule::WaitEffect: {
        if (_time >= (_castTime + _conjTime)) {
            _state = SpellSchedule::Effect;
        }
        break;
    }
    case SpellSchedule::Effect: {
        _state = SpellSchedule::WaitFinish;
        break;
    }
    case SpellSchedule::WaitFinish: {
        if (round.state == CombatRound::Finished) {
            _state = SpellSchedule::Finish;
        }
        break;
    }
    case SpellSchedule::Finish: {
        break;
    }
    }

    return _state;
}

static scene::SceneNode &determineGrenadeAttachment(scene::ModelSceneNode &model) {
    if (scene::ModelNodeSceneNode *handHook = model.getNodeByName("rhand")) {
        return *handHook;
    }

    return model;
}

void Grenade::fire(Creature &caster, graphics::Model &projModel, float swingTime, float throwTime) {
    auto casterNode = std::static_pointer_cast<scene::ModelSceneNode>(caster.sceneNode());
    scene::SceneNode &attachmentNode = determineGrenadeAttachment(*casterNode);

    // Create a grenade node and attach it to caster's hand for the duration of
    // the swing.
    scene::ISceneGraph &graph = casterNode->graph();
    _projNode = graph.newModel(projModel, scene::ModelUsage::Projectile);
    attachmentNode.addChild(*_projNode);

    // Keep the grenade attached for swingTime seconds.
    _swingTime = swingTime;
    _throwTime = throwTime;
}

void Grenade::computeTrajectory(glm::vec3 origin, glm::vec3 target, float time) {
    // Position at any time point t, with initial velocity V, origin X', and
    // acceleration g: X = X' + Vt + gt^2/2

    // TODO: instead of solving this for time, we can complicate things more
    // with variable initial velocity and angle.

    _throwAccel = glm::vec3(0.0f, 0.0f, -9.81f);
    _throwVelocity = ((target - origin) / time) - (_throwAccel * time * 0.5f);
    _throwOrigin = origin;
    _throwTarget = target;
    _throwTime = time;
}

void Grenade::update(SpellSchedule::State spellState, Object &target, float dt) {
    _time += dt;

    switch (_state) {
    case Swing: {
        if (_time < _swingTime) {
            // Keep the grenade attached.
            return;
        }

        glm::vec3 origin = _projNode->origin();

        computeTrajectory(origin, target.position(), _throwTime);

        // Detach the grenade and transition to Throw state.
        _projNode->parent()->removeChild(*_projNode);
        _projNode->setLocalTransform(glm::translate(origin));
        _projNode->graph().addRoot(_projNode);
        _state = Throw;
        return;
    }
    case Throw: {
        if (_time < (_swingTime + _throwTime)) {
            float t = _time - _swingTime;
            glm::vec3 pos = _throwOrigin + (_throwVelocity * t) + (_throwAccel * t * t * 0.5f);
            _projNode->setLocalTransform(glm::translate(pos));
            return;
        }
        _projNode->setLocalTransform(glm::translate(_throwTarget));
        _state = Wait;
        return;
    }
    case Wait: {
        if ((int)spellState < (int)SpellSchedule::Effect) {
            return;
        }
        _state = Explode;
        return;
    }
    case Explode: {
        _projNode->graph().removeRoot(*_projNode);
        _projNode.reset();
        _state = Finish;
    }
    default:
        return;
    }
}

Grenade::~Grenade() {
    if (_projNode) {
        _projNode->graph().removeRoot(*_projNode);
    }
}

} // namespace game
} // namespace reone
