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

namespace game {

class UseFeatAction : public Action {
public:
    UseFeatAction(Game &game,
                  ServicesView &services,
                  FeatType feat,
                  std::shared_ptr<Object> target) :
        Action(game, services, ActionType::UseFeat),
        _feat(feat),
        _target(std::move(target)) {
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::UseFeat;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;

    void cancel(std::shared_ptr<Action> self, Object &actor) override;

    const std::shared_ptr<Object> &target() const { return _target; }

    AttackResultType result() const { return _attacks.result(); }

    FeatType feat() const { return _feat; }

private:
    void addProjectiles(const Creature &creature, FeatType feat);
    void finish(Creature &attacker);

    FeatType _feat;
    std::shared_ptr<Object> _target;

    AttackSchedule _schedule;
    AttackBuffer _attacks;
    bool _reachedTarget {false};

    ProjectileSequence _projectiles;
};

} // namespace game

} // namespace reone
