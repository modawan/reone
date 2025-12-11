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

#include "reone/game/action/cutsceneattack.h"

#include "reone/game/attack.h"
#include "reone/game/combat.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/resource/provider/2das.h"
#include "reone/scene/graphs.h"
#include "reone/system/randomutil.h"

namespace reone {

namespace game {

std::string getAnimationName(int index, resource::ITwoDAs &twoDas) {
    std::shared_ptr<resource::TwoDA> animations(twoDas.get("animations"));
    if (index < 0 || index >= animations->getRowCount()) {
        warn("CutsceneAttack: animation index out of bounds: " + std::to_string(index));
        return "";
    }

    return boost::to_lower_copy(animations->getString(index, "name"));
}

static void attack(Creature &attacker, std::string animation) {
    scene::AnimationProperties animProp =
        scene::AnimationProperties::fromFlags(scene::AnimationFlags::blend);

    attacker.playAnimation(animation, animProp);
}

void CutsceneAttackAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    Creature &attacker = cast<Creature>(actor);

    attacker.face(*_target);

    const CombatRound &round = _game.combat().addAction(self, actor);
    AttackSchedule::State state = _schedule.update(round, *self, dt);

    // Gameplay updates
    switch (state) {
    case AttackSchedule::Attack: {
        lock();
        resource::ITwoDAs &twoDas = _services.resource.twoDas;
        attack(attacker, getAnimationName(_animation, twoDas));

        if (auto target = dyn_cast<Creature>(_target)) {
            target->runAttackedScript(attacker.id());
        }
        return;
    }
    case AttackSchedule::Damage: {
        auto effect = _game.newEffect<DamageEffect>(
            _damage, DamageType::Universal, DamagePower::Normal, attacker.id());

        _target->applyEffect(std::move(effect), DurationType::Instant);
        break;
    }
    case AttackSchedule::Finish: {
        attacker.setMovementRestricted(false);
        complete();
        return;
    }
    default:
        break;
    }
}

} // namespace game

} // namespace reone
