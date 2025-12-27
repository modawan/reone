/*
 * Copyright (c) 2025 The reone project contributors
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

namespace game {

class CutsceneAttackAction : public Action {
public:
    CutsceneAttackAction(Game &game, ServicesView &services,
                         const std::shared_ptr<Object> &target,
                         int animation, AttackResultType result, int damage) :
        Action(game, services, ActionType::CutsceneAttack),
        _target(target), _animation(animation), _result(result), _damage(damage) {}

    static bool classof(Action *from) {
        return from->type() == ActionType::CutsceneAttack;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    void cancel(std::shared_ptr<Action> self, Object &actor) override;
    const std::shared_ptr<Object> &target() const { return _target; }

private:
    void addProjectiles(const Creature &creature, std::string attackAnim);
    void finish(Creature &attacker);

    std::shared_ptr<Object> _target;
    int _animation;
    AttackResultType _result;
    int _damage;

    AttackSchedule _schedule;
    ProjectileSequence _projectiles;
};

} // namespace game

} // namespace reone
