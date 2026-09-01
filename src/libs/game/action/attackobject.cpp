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
#include "reone/system/randomutil.h"

#include "attackanimations.h"

namespace reone {

namespace game {

// Attack animation variants are numbered from 1 - a0 is not an authored
// animation. Cinematic attacks consume the whole 1-5 roll, while non-cinematic
// attacks keep to the a1/a2 subset this selector has always intended to use.
// Some K2 families author further variants; selecting those is a separate
// change.
static constexpr int kNonCinematicVariants = 2;

static int nonCinematicVariant(int variant) {
    return 1 + std::max(0, variant - 1) % kNonCinematicVariants;
}

std::string getMeleeAttackAnim(CreatureWieldType attackerWield,
                               CreatureWieldType targetWield,
                               int variant, bool duel) {
    // Cinematic attack variants.
    if (duel && isMeleeWieldType(targetWield)) {
        return str(boost::format("c%da%d") % static_cast<int>(attackerWield) % variant);
    }

    variant = nonCinematicVariant(variant);

    if (targetWield != CreatureWieldType::None) {
        return str(boost::format("m%da%d") % static_cast<int>(attackerWield) % variant);
    }

    return str(boost::format("g%da%d") % static_cast<int>(attackerWield) % variant);
}

std::string getUnarmedAttackAnim(CreatureWieldType attackerWield, CreatureWieldType targetWield, int variant, bool duel) {
    if (attackerWield == CreatureWieldType::HandToHandComplex) {
        if (duel && targetWield == attackerWield) {
            return str(boost::format("c%da%d") % static_cast<int>(attackerWield) % variant);
        }
    }

    // Fallback to a basic unarmed animation.
    variant = nonCinematicVariant(variant);
    return str(boost::format("g8a%d") % variant);
}

std::string getStunBatonAttackAnim(int variant) {
    variant = nonCinematicVariant(variant);
    return str(boost::format("g1a%d") % variant);
}

static void attack(const CombatRound &round, Creature &attacker, Object &target,
                   const IAnimations &anims, AttackBuffer &attacks) {
    attacks.addPhysicalAttacks(attacker, target);

    scene::AnimationProperties animProp =
        scene::AnimationProperties::fromFlags(scene::AnimationFlags::blend);

    CreatureWieldType targetWield = CreatureWieldType::None;
    if (auto *targetCreature = dyn_cast<Creature>(&target)) {
        targetWield = targetCreature->getWieldType();
    }

    int variant = randomInt(1, 5);

    CreatureWieldType attackerWield = attacker.getWieldType();

    std::string attackAnim;
    switch (attackerWield) {
    case CreatureWieldType::None: {
        assert(0 && "Monster attacks are not supported");
        break;
    }
    case CreatureWieldType::SingleSword:
    case CreatureWieldType::DoubleBladedSword:
    case CreatureWieldType::DualSwords: {
        attackAnim = getMeleeAttackAnim(attackerWield, targetWield, variant, round.duel);
        break;
    }
    case CreatureWieldType::BlasterPistol:
    case CreatureWieldType::DualPistols:
    case CreatureWieldType::BlasterRifle:
    case CreatureWieldType::HeavyWeapon: {
        attackAnim = getRangedAttackAnim(attacker, /*kind=*/1);
        break;
    }
    case CreatureWieldType::HandToHand:
    case CreatureWieldType::HandToHandComplex: {
        attackAnim = getUnarmedAttackAnim(attackerWield, targetWield, variant, round.duel);
        break;
    }
    case CreatureWieldType::StunBaton:
        attackAnim = getStunBatonAttackAnim(variant);
        break;
    }

    attacker.playAnimation(attackAnim, animProp);

    if (round.duel) {
        auto &opponent = cast<Creature>(target);
        opponent.face(attacker);

        std::string resultAnim = anims.getAttackResult(attackAnim, targetWield, attacks.result());
        opponent.playAnimation(resultAnim, animProp);
    }
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
    if (!runtimeDependenciesLive()) {
        cancel(self, actor);
        markCancelled();
        return;
    }
    auto target = _target.resolve();
    if (!target || target->id() == attacker.id()) {
        finish(attacker);
        return;
    }
    attacker.setAttemptedAttackTarget(target->id());

    if (target->isDead()) {
        finish(attacker);
        return;
    }

    if (!navigateToAttackTarget(attacker, *target, dt, _reachedTarget)) {
        return;
    }

    attacker.face(*target);

    const CombatRound &round = _game.combat().addAction(self, actor);
    AttackSchedule::State state = _schedule.update(round, *self, dt);

    // Gameplay updates
    switch (state) {
    case AttackSchedule::Attack: {
        lock();
        attacker.setMovementType(Creature::MovementType::None);
        attacker.setMovementRestricted(true);

        attack(round, attacker, *target, _services.game.animations, _attacks);
        _attacks.resolve(attacker, *target);

        addProjectiles(attacker);
        return;
    }
    case AttackSchedule::Damage: {
        _attacks.signal(_game, _services, attacker, *target);
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
        _projectiles.update(dt, attacker, *target, sceneGraph);
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

std::optional<SavedActionRecord> AttackObjectAction::saveFacingState() const {
    auto target = _target.resolve();
    if (!target) {
        return std::nullopt;
    }

    // K1 and K2 both save the high-level physical-attack command as retail
    // ActionId 12. Combat-round resolution, navigation, animations and
    // projectiles live outside this queue record and restart after load.
    SavedActionRecord result = originalSavedAction().value_or(SavedActionRecord {});
    result.actionId = 12;
    result.declaredParameterCount = 10;
    result.parameters = {
        {1, int32_t {0}},
        {3, SavedObjectReference::fromRuntimeId(target->id())},
        {1, int32_t {1}},
        {1, int32_t {10009}},
        {1, int32_t {1500}},
        {1, int32_t {1}},
        {1, int32_t {0}},
        {1, int32_t {0}},
        {1, int32_t {4}},
        {1, int32_t {0}},
    };
    return result;
}

void AttackObjectAction::finish(Creature &attacker) {
    attacker.setMovementRestricted(false);
    _projectiles.reset();
    complete();
}

} // namespace game

} // namespace reone
