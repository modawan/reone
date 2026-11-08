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
    // the attack.
    if (isAttackTargetDown(*target)) {
        finish(attacker);
        return;
    }
    // A creature outside the party cannot go on attacking one it does not see.
    if (!_instantCastAttack && !attacker.hasDetectedTarget(*target)) {
        finish(attacker);
        return;
    }

    // An instant cast's attack starts its round afresh as it is made, so
    // nothing holds it until it has swung.
    const bool unheld = _instantCastAttack && !_schedule.started();
    if (!unheld && _game.combat().isActionPaused(*self)) {
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

    // No feat starts while the attacker is held without owning a round, or
    // while a paused round of the attacker's other than its own holds it. A
    // feat waits for no round end: the dispatcher takes no round action while
    // its creature's round action is spent.
    if (!unheld && _game.combat().awaitsRoundPause(attacker, *self)) {
        return;
    }
    // An attacker engaged by a partner that masters the exchange waits for the
    // partner's swing to start its side.
    if (!unheld && _game.combat().awaitsMaster(attacker, *target)) {
        return;
    }
    const CombatRound &round = _game.combat().addAction(self, actor);
    if (round.suspends(*self)) {
        return;
    }
    if (_schedule.update(round, *self) != AttackSchedule::Attack) {
        return;
    }

    lock();
    attacker.setMovementType(Creature::MovementType::None);
    attacker.setMovementRestricted(true);
    // Each swing turns the attacker toward its own target.
    attacker.setOrientationLock(target->id());

    auto swing = beginPhysicalAttack(
        attacker,
        *target,
        _services.game.animations,
        _attacks,
        _feat,
        !_instantCastAttack,
        !_approach.withoutSwing);
    if (isCompleted() || isCancelled()) {
        return;
    }
    // The attack is over with its swing; one with no swing or hit to show
    // shows nothing.
    if (!swing || swing->animations.empty()) {
        finish(attacker);
        return;
    }
    _attacks.resolve(attacker, *target);
    if (isCompleted() || isCancelled()) {
        return;
    }
    presentPhysicalAttack(attacker, *target, _services.game.animations, _attacks, *swing);

    // The round lands the hits and discharges over the swing's pause.
    if (!swing->ranged) {
        _attacks.prepareMeleeSequence(
            _services.game.animations,
            swing->animations);
        _schedule.startMelee();
    } else {
        _schedule.startRanged();
    }
    finish(attacker);
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
    // An attack is over with its swing. An earlier save keeps one queued
    // until its round ends.
    if (_schedule.started()) {
        complete();
    }
}

void UseFeatAction::finish(Creature &attacker) {
    attacker.setMovementRestricted(false);
    complete();
}

} // namespace game

} // namespace reone
