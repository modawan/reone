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

#pragma once

namespace reone {

namespace scene {
class ModelSceneNode;
class ISceneGraph;
} // namespace scene

namespace graphics {
class Model;
}

namespace game {

class Action;
class CombatRound;
class Creature;
class Object;

class SpellSchedule {
public:
    explicit SpellSchedule(float conjTime, float castTime) :
        _conjTime(conjTime), _castTime(castTime) {}

    enum State {
        WaitConjure,
        Conjure,
        WaitCast,
        Cast,
        WaitEffect,
        Effect,
        WaitFinish,
        Finish,
    };

    State update(const CombatRound &round, Action &action, float dt);

private:
    State _state {WaitConjure};
    float _time {0.0f};
    float _conjTime {0.0f};
    float _castTime {0.0f};
};

class Grenade {
public:
    ~Grenade();

    enum State {
        Swing,
        Throw,
        Wait,
        Explode,
        Finish,
    };

    void fire(Creature &caster, graphics::Model &projModel, float swingTime, float throwTime);
    void update(SpellSchedule::State spellState, Object &target, float dt);

private:
    // Compute parameters for a parabolic trajectory from origin to target.
    void computeTrajectory(glm::vec3 origin, glm::vec3 target, float throwTime);

private:
    State _state {Swing};
    std::shared_ptr<scene::ModelSceneNode> _projNode;
    float _time {0.0f};
    float _swingTime {0.0f};
    float _throwTime {0.0f};
    glm::vec3 _throwOrigin;
    glm::vec3 _throwTarget;
    glm::vec3 _throwVelocity;
    glm::vec3 _throwAccel;
};

} // namespace game
} // namespace reone
