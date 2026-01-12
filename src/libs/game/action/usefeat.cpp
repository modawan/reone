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

#include "reone/game/action/usefeat.h"

#include "reone/game/animations.h"
#include "reone/game/attack.h"
#include "reone/game/combat.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object.h"
#include "reone/game/projectiles.h"
#include "reone/scene/graphs.h"
#include "reone/system/randomutil.h"

namespace reone {

namespace game {

static const char *getAnimFormat(FeatType feat) {
    switch (feat) {
    case FeatType::CriticalStrike:
    case FeatType::ImprovedCriticalStrike:
    case FeatType::MasterCriticalStrike:
        return "f%da1";

    case FeatType::Flurry:
    case FeatType::ImprovedFlurry:
    case FeatType::WhirlwindAttack:
        return "f%da2";

    case FeatType::PowerAttack:
    case FeatType::ImprovedPowerAttack:
    case FeatType::MasterPowerAttack:
        return "f%da3";

    case FeatType::RapidShot:
    case FeatType::ImprovedRapidShot:
    case FeatType::MultiShot:
        return "b%da2";

    case FeatType::SniperShot:
    case FeatType::ImprovedSniperShot:
    case FeatType::MasterSniperShot:
        return "b%da3";

    case FeatType::PowerBlast:
    case FeatType::ImprovedPowerBlast:
    case FeatType::MasterPowerBlast:
        return "b%da4";

    default:
        return nullptr;
    }
}

static std::optional<ProjectileAttackType> getProjectileType(FeatType feat) {
    switch (feat) {
    case FeatType::RapidShot:
    case FeatType::ImprovedRapidShot:
    case FeatType::MultiShot:
        return ProjectileAttackType::Rapid;

    case FeatType::SniperShot:
    case FeatType::ImprovedSniperShot:
    case FeatType::MasterSniperShot:
        return ProjectileAttackType::Sniper;

    case FeatType::PowerBlast:
    case FeatType::ImprovedPowerBlast:
    case FeatType::MasterPowerBlast:
        return ProjectileAttackType::Power;

    default:
        return std::nullopt;
    }
}

static std::string getAttackAnim(FeatType feat, CreatureWieldType attackerWield) {
    const char *format = getAnimFormat(feat);
    if (!format) {
        return std::string();
    }
    return str(boost::format(format) % static_cast<int>(attackerWield));
}

static void attack(FeatType feat, const CombatRound &round,
                   Creature &attacker, Object &target,
                   const IAnimations &anims, AttackBuffer &attacks) {

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

    std::string attackAnim = getAttackAnim(feat, attackerWield);
    attacker.playAnimation(attackAnim, animProp);

    if (round.duel) {
        auto &opponent = static_cast<Creature &>(target);
        opponent.face(attacker);

        std::string resultAnim = anims.getAttackResult(attackAnim, targetWield, attacks.result());
        opponent.playAnimation(resultAnim, animProp);
    }
}

void UseFeatAction::addProjectiles(const Creature &creature, FeatType feat) {
    auto projType = getProjectileType(feat);
    if (!projType) {
        return;
    }

    ProjectileSpec *spec = _services.game.projectiles.get(
        projType.value(), creature.getWieldType(), creature.appearance());

    if (!spec) {
        // no projectiles for this attack
        return;
    }

    addProjectilesFromSpec(_projectiles, *spec);
}

void UseFeatAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    Creature &attacker = static_cast<Creature &>(actor);

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

        attack(_feat, round, attacker, *_target, _services.game.animations, _attacks);

        if (auto target = dyn_cast<Creature>(_target)) {
            target->runAttackedScript(attacker.id());
        }

        addProjectiles(attacker, _feat);
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

void UseFeatAction::cancel(std::shared_ptr<Action> self, Object &actor) {
    Creature &attacker = static_cast<Creature &>(actor);
    finish(attacker);
}

void UseFeatAction::finish(Creature &attacker) {
    attacker.setMovementRestricted(false);
    _projectiles.reset();
    complete();
}

} // namespace game

} // namespace reone
