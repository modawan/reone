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

#include "reone/game/animations.h"
#include "reone/game/attack.h"
#include "reone/game/combat.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/projectiles.h"
#include "reone/scene/graphs.h"

namespace reone {

namespace game {

static std::vector<std::string> attack(
    const CombatRound &round,
    Creature &attacker,
    Object &target,
    const IAnimations &anims,
    AttackBuffer &attacks) {

    attacks.addPhysicalAttacks(attacker, target);

    scene::AnimationProperties animProp =
        scene::AnimationProperties::fromFlags(scene::AnimationFlags::blend);

    CreatureWieldType targetWield = CreatureWieldType::None;
    if (auto *targetCreature = dyn_cast<Creature>(&target)) {
        targetWield = targetCreature->getWieldType();
    }

    CreatureWieldType attackerWield = attacker.getWieldType();
    bool melee = !isRangedWieldType(attackerWield);
    size_t animationCount = melee ? attacks.attackCount() : 1;
    std::vector<std::string> attackAnimations;
    attackAnimations.reserve(animationCount);

    for (size_t index = 0; index < animationCount; ++index) {
        std::string attackAnim;
        if (isRangedWieldType(attackerWield)) {
            attackAnim = getRangedAttackAnim(attacker, /*kind=*/1);
        } else {
            attackAnim = selectPhysicalMeleeAttackAnimation(
                attacker,
                target,
                attackerWield);
        }
        attackAnimations.push_back(std::move(attackAnim));
    }

    const std::string &primaryAttackAnimation = attackAnimations.front();
    attacker.playAnimation(primaryAttackAnimation, animProp);

    if (round.duel) {
        auto &opponent = cast<Creature>(target);
        opponent.face(attacker);

        std::string resultAnim = anims.getAttackResult(
            primaryAttackAnimation,
            targetWield,
            attacks.result());
        opponent.playAnimation(resultAnim, animProp);
    }

    return attackAnimations;
}

/**
 * Add projectiles matching the corresponding attack animation.
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
    attacker.setAttemptedAttackTarget(_target->id());

    if (_target->isDead() && !_attacks.hasPendingMelee()) {
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

        std::vector<std::string> attackAnimations = attack(
            round,
            attacker,
            *_target,
            _services.game.animations,
            _attacks);
        _attacks.resolve(attacker, *_target);

        if (!isRangedWieldType(attacker.getWieldType())) {
            _attacks.prepareMeleeSequence(
                _services.game.animations,
                attackAnimations);
            _schedule.startMelee(
                _attacks.latestMeleeImpactMilliseconds());
            _attacks.signalReadyMelee(
                0,
                _game,
                _services,
                attacker,
                *_target);
        }

        addProjectiles(attacker);
        return;
    }
    case AttackSchedule::WaitDamage: {
        if (_schedule.isMelee()) {
            _attacks.signalReadyMelee(
                _schedule.meleeElapsedMilliseconds(),
                _game,
                _services,
                attacker,
                *_target);
        }
        break;
    }
    case AttackSchedule::Damage: {
        if (_schedule.isMelee()) {
            _attacks.signalReadyMelee(
                _schedule.meleeElapsedMilliseconds(),
                _game,
                _services,
                attacker,
                *_target);
        } else {
            _attacks.signal(_game, _services, attacker, *_target);
        }
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
    _attacks.discardPendingMelee();
    finish(attacker);
}

void AttackObjectAction::finish(Creature &attacker) {
    attacker.setMovementRestricted(false);
    _projectiles.reset();
    complete();
}

} // namespace game

} // namespace reone
