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

#include "reone/game/action/attackobject.h"

#include "reone/game/attack.h"
#include "reone/game/combat.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/projectiles.h"
#include "reone/scene/graphs.h"
#include "reone/system/randomutil.h"

namespace reone {

namespace game {

static std::string getMeleeAttackAnim(CreatureWieldType attackerWield,
                                      CreatureWieldType targetWield,
                                      int variant, bool duel) {
    // Cinematic attack variants.
    if (duel && isMeleeWieldType(targetWield)) {
        return str(boost::format("c%da%d") % static_cast<int>(attackerWield) % variant);
    }

    // Only 2 non-cinematic variants.
    variant = variant % 3;

    if (targetWield != CreatureWieldType::None) {
        return str(boost::format("m%da%d") % static_cast<int>(attackerWield) % variant);
    }

    return str(boost::format("g%da%d") % static_cast<int>(attackerWield) % variant);
}

static std::string getMeleeDamageAnim(CreatureWieldType targetWield, int variant) {
    if (isMeleeWieldType(targetWield)) {
        return str(boost::format("c%dd%d") % static_cast<int>(targetWield) % variant);
    }

    // No damage animation for melee attacker -> ranged target, so use combat
    // stance animation.
    return str(boost::format("g%dr1") % static_cast<int>(targetWield));
}

static std::string getMeleeParryAnim(CreatureWieldType targetWield, int variant) {
    if (isMeleeWieldType(targetWield)) {
        return str(boost::format("c%dp%d") % static_cast<int>(targetWield) % variant);
    }

    // No parry animation for melee attacker -> ranged target, so use combat
    // stance animation.
    return str(boost::format("g%dr1") % static_cast<int>(targetWield));
}

static std::string getRangedDamageAnim(CreatureWieldType targetWield) {
    return str(boost::format("g%dd1") % static_cast<int>(targetWield));
}

static std::string getRangedDodgeAnim(CreatureWieldType targetWield) {
    return str(boost::format("g%dg1") % static_cast<int>(targetWield));
}

static void attack(const CombatRound &round, Creature &attacker, Object &target,
                   AttackBuffer &attacks) {

    if (auto main = attacker.getEquippedItem(InventorySlots::rightWeapon)) {
        attacks.addWeaponAttack(attacker, target, *main);

        if (auto offhand = attacker.getEquippedItem(InventorySlots::leftWeapon)) {
            attacks.addWeaponAttack(attacker, target, *offhand);
        }
    } else {
        // TODO: handle Unarmed
    }

    scene::AnimationProperties animProp =
        scene::AnimationProperties::fromFlags(scene::AnimationFlags::blend);

    CreatureWieldType targetWield = CreatureWieldType::None;
    if (auto *targetCreature = dyn_cast<Creature>(&target)) {
        targetWield = targetCreature->getWieldType();
    }

    int variant = randomInt(1, 5);

    CreatureWieldType attackerWield = attacker.getWieldType();
    bool isMelee = isMeleeWieldType(attacker.getWieldType());

    std::string attackAnim = isMelee
                                 ? getMeleeAttackAnim(attackerWield, targetWield, variant, round.duel)
                                 : getRangedAttackAnim(attacker, /*kind=*/1);

    attacker.playAnimation(attackAnim, animProp);

    if (round.duel) {
        auto &opponent = cast<Creature>(target);
        opponent.face(attacker);

        std::string hitAnim = isMelee
                                  ? getMeleeDamageAnim(targetWield, variant)
                                  : getRangedDamageAnim(targetWield);

        std::string missAnim = isMelee
                                   ? getMeleeParryAnim(targetWield, variant)
                                   : getRangedDodgeAnim(targetWield);

        std::string anim = isAttackSuccessful(attacks.result()) ? hitAnim : missAnim;
        opponent.playAnimation(anim, animProp);
    }
}

/**
 * Add projectiles matching the corresponding attack animation.
 *
 * TODO: refactor this to be tied to the animation, not wield type. We need to
 * use it for CutsceneAttack as well.
 */
void AttackObjectAction::addProjectiles(const Creature &creature) {
    ProjectileSpec *spec = _services.game.projectiles.get(
        ProjectileAttackType::Basic, creature.getWieldType(), creature.appearance());

    if (!spec) {
        // no projectiles for this attack
        return;
    }

    addProjectilesFromSpec(_projectiles, *spec);
}

void AttackObjectAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    Creature &attacker = cast<Creature>(actor);

    if (_target->isDead()) {
        finish(attacker);
        return;
    }

    if (!navigateToAttackTarget(attacker, *_target, dt, _reachedTarget)) {
        return;
    }

    attacker.face(*_target);

    const CombatRound &round = _game.combat().addAction(self, actor);
    AttackSchedule::State state = _schedule.update(round, *self, dt);

    // Gameplay updates
    switch (state) {
    case AttackSchedule::Attack: {
        lock();
        attacker.setMovementType(Creature::MovementType::None);
        attacker.setMovementRestricted(true);

        attack(round, attacker, *_target, _attacks);

        if (auto target = dyn_cast<Creature>(_target)) {
            target->runAttackedScript(attacker.id());
        }

        addProjectiles(attacker);
        return;
    }
    case AttackSchedule::Damage: {
        _attacks.applyEffects(attacker, *_target, _game);
        break;
    }
    case AttackSchedule::Finish: {
        finish(attacker);
        return;
    }
    default:
        break;
    }

    // Projectiles
    switch (state) {
    case AttackSchedule::Damage:
    case AttackSchedule::WaitDamage:
    case AttackSchedule::WaitFinish: {
        auto &sceneGraph = _services.scene.graphs.get(kSceneMain);
        _projectiles.update(dt, attacker, *_target, sceneGraph);
        break;
    }
    default:
        break;
    }
}

void AttackObjectAction::cancel(std::shared_ptr<Action> self, Object &actor) {
    Creature &attacker = cast<Creature>(actor);
    finish(attacker);
}

void AttackObjectAction::finish(Creature &attacker) {
    attacker.setMovementRestricted(false);
    _projectiles.reset();
    complete();
}

} // namespace game

} // namespace reone
