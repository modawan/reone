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

#include "../action.h"

#include "reone/game/attack.h"

namespace reone {

namespace scene {
class ModelSceneNode;
}

namespace game {

/**
 * Perform a basic attack of the target using the current weapon.
 */
class AttackObjectAction : public Action {
public:
    AttackObjectAction(Game &game, ServicesView &services,
                       const std::shared_ptr<Object> &target,
                       bool passive = false) :
        Action(game, services, ActionType::AttackObject),
        _target(target),
        _passive(passive), _services(services) {}

    static bool classof(Action *from) {
        return from->type() == ActionType::AttackObject;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    void cancel(std::shared_ptr<Action> self, Object &actor) override;
    const std::shared_ptr<Object> &target() const { return _target; }

    AttackResultType result() const { return _attacks.result(); }

private:
    void addProjectiles(const Creature &creature);

    void finish(Creature &attacker);

    std::shared_ptr<Object> _target;
    bool _passive;
    ServicesView &_services;

    AttackSchedule _schedule;
    AttackBuffer _attacks;
    bool _reachedTarget {false};

    ProjectileSequence _projectiles;
};

} // namespace game

} // namespace reone
