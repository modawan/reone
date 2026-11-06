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

#include <cassert>

#include "reone/game/animations.h"
#include "reone/game/attack.h"
#include "reone/game/combat.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object.h"
#include "reone/system/randomutil.h"

namespace reone {

namespace game {

void UseFeatAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    if (isCompleted() || isCancelled()) {
        return;
    }
    Creature &attacker = static_cast<Creature &>(actor);
    // A feat attack is taken up as it first runs, and not by a creature that
    // cannot be commanded. An attack restored from a save is not refused
    // again: a save need not record whether it was taken up.
    if (!_taken && !_instantCastAttack && !originalSavedAction() && isPhysicalAttackFeat(_feat) &&
        Combat::refusesAttack(attacker)) {
        finish(attacker);
        return;
    }
    _taken = true;
    // A creature carrying out an attack makes no new combat decision.
    attacker.refreshCombatDecisionTimer();
    _game.party().setCombatMessage(attacker, _game.party().idleCombatMessage());
    if (!runtimeDependenciesLive()) {
        cancel(self, actor);
        markCancelled();
        return;
    }
    auto target = _target.resolve();
    if (!target || target.get() == &actor) {
        if (isPhysicalAttackFeat(_feat)) {
            attacker.cancelAllCombatModes();
        }
        finish(attacker);
        return;
    }
    // A physical feat not taken from the round is refused on a friend as it
    // would start, as a round entry is when taken.
    if (isPhysicalAttackFeat(_feat) && !isScheduledCommand() && !_schedule.started() && !_approach.reached &&
        Combat::refusesAttackOnFriend(attacker, *target)) {
        finish(attacker);
        return;
    }
    // A target that is dead, or a party member down at zero vitality, ends
    // the attack; released discharges keep draining.
    if (isAttackTargetDown(*target) && !_attacks.hasPendingMelee() && !_attacks.hasPendingDischarges()) {
        finish(attacker);
        return;
    }
    // A creature outside the party cannot go on attacking one it does not see.
    if (!_instantCastAttack && !attacker.hasDetectedTarget(*target)) {
        finish(attacker);
        return;
    }

    if (_game.combat().isActionPaused(*self)) {
        return;
    }

    switch (approachAttackTarget(attacker, *target, dt, _approach, _game, *self, false)) {
    case AttackApproachStep::Ended:
        finish(attacker);
        return;
    case AttackApproachStep::Approaching:
        return;
    case AttackApproachStep::Reached:
        break;
    }
    // While the world is held, an attack in reach waits.
    if (_game.holdsWorld()) {
        return;
    }

    attacker.setDesiredFacingToward(target->position());

    // Once its round has been ended, a started attack has nothing left to do,
    // also when its creature has been given a new round meanwhile: the attack
    // does not carry over into it.
    if ((_schedule.isMelee() || _schedule.isRanged()) && !_game.combat().runsRound(*self)) {
        finish(attacker);
        return;
    }

    // No feat starts while the attacker is held without owning a round, or
    // while a round it has acted in has not ended.
    if (_game.combat().awaitsRoundEnd(attacker, *self)) {
        return;
    }
    const CombatRound &round = _game.combat().addAction(self, actor);
    if (round.suspends(*self)) {
        return;
    }
    AttackSchedule::State state = _schedule.update(round, *self, dt);

    // Gameplay updates
    switch (state) {
    case AttackSchedule::Attack: {
        lock();
        attacker.setMovementType(Creature::MovementType::None);
        attacker.setMovementRestricted(true);

        auto swing = beginPhysicalAttack(
            attacker,
            *target,
            _services.game.animations,
            _attacks,
            _feat,
            !_instantCastAttack,
            !_approach.withoutSwing);
        if (!swing || isCompleted() || isCancelled()) {
            return;
        }
        if (swing->animations.empty()) {
            // The round still retires normally, without a swing or hit to emit.
            _schedule.skipAttacks();
            return;
        }
        _attacks.resolve(attacker, *target);
        if (isCompleted() || isCancelled()) {
            return;
        }
        presentPhysicalAttack(attacker, *target, _services.game.animations, _attacks, *swing);

        // Hits and discharges are released from the next update on.
        if (!swing->ranged) {
            _attacks.prepareMeleeSequence(
                _services.game.animations,
                swing->animations);
            _schedule.startMelee();
        } else {
            _schedule.startRanged();
        }
        return;
    }
    case AttackSchedule::WaitDamage:
    case AttackSchedule::Damage: {
        if (_schedule.isMelee()) {
            _attacks.signalReadyMelee(
                _schedule.elapsedMilliseconds(),
                _game,
                _services,
                attacker,
                *target);
        } else if (_schedule.isRanged()) {
            _attacks.signalReadyRanged(
                _schedule.elapsedMilliseconds(),
                _game,
                _services,
                attacker,
                *target);
        }
        // The pause end flushes the last hits; a round that ends with them
        // clears their record before their events are delivered.
        if (state == AttackSchedule::Damage && !isCompleted() && !isCancelled()) {
            _game.combat().requestSettle(self);
            // An instant cast's attack is over with its pause; its round runs out.
            if (_instantCastAttack) {
                finish(attacker);
                return;
            }
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
}

// An attack restored from a save was recorded as queued when it was given;
// restoring the queue does not record it again.
void UseFeatAction::onQueued(Object &actor) {
    if (!originalSavedAction() && isPhysicalAttackFeat(_feat)) {
        if (auto target = _target.resolve()) {
            cast<Creature>(actor).recordQueuedAttack(*target);
        }
    }
}

bool UseFeatAction::cancel(std::shared_ptr<Action> self, Object &actor) {
    Creature &attacker = static_cast<Creature &>(actor);
    _game.combat().transferEquipment(static_cast<Creature &>(actor));
    _attacks.discardPending();
    _attacks.clearHistory();
    attacker.clearCurrentAttackTarget();
    // Clearing a physical attack forgets its attempted target as well.
    if (isPhysicalAttackFeat(_feat)) {
        attacker.setAttemptedAttackTarget(script::kObjectInvalid);
    }
    finish(attacker);
    return true;
}

std::optional<SavedActionRecord> UseFeatAction::saveFacingState() const {
    auto target = _target.resolve();
    if (!target || !isPhysicalAttackFeat(_feat)) {
        return std::nullopt;
    }

    // A physical feat is saved as an attack with the feat; its progress (the
    // pending round, results, schedule and presentation) goes beside the
    // command's parameters.
    SavedActionRecord result;
    SavedAttackCommand {SavedObjectReference::fromRuntimeId(target->id()), _feat, std::nullopt}.toAction(result);
    SavedPhysicalAction progress;
    _attacks.saveContinuation(progress, _game);
    _schedule.saveContinuation(*progress.state);
    progress.state->fields().push_back(resource::Gff::Field::newByte("Reached", _approach.reached));
    progress.state->fields().push_back(resource::Gff::Field::newByte("NoSwing", _approach.withoutSwing));
    progress.state->fields().push_back(resource::Gff::Field::newByte("InstantCast", _instantCastAttack));
    result.physical = std::move(progress);
    return result;
}

void UseFeatAction::restorePhysicalState(const SavedPhysicalAction &state) {
    _attacks.restoreContinuation(state);
    _schedule.restoreContinuation(*state.state);
    _approach.reached = state.state->getBool("Reached");
    _approach.withoutSwing = state.state->getBool("NoSwing");
    _instantCastAttack = state.state->getBool("InstantCast");
}

void UseFeatAction::finish(Creature &attacker) {
    attacker.setMovementRestricted(false);
    complete();
}

} // namespace game

} // namespace reone
