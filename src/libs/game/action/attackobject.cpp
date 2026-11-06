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

#include <algorithm>

#include "reone/game/action/usefeat.h"
#include "reone/game/animations.h"
#include "reone/game/attack.h"
#include "reone/game/combat.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/party.h"
#include "reone/system/randomutil.h"

#include "attackanimations.h"

namespace reone {

namespace game {

namespace {

constexpr int kNonCinematicVariantCount = 2;

int nonCinematicVariant(int variant) {
    return 1 + std::max(0, variant - 1) % kNonCinematicVariantCount;
}

std::string attackAnimation(
    char prefix,
    CreatureWieldType wield,
    int variant) {

    return str(boost::format("%c%da%d") %
               prefix %
               static_cast<int>(wield) %
               variant);
}

} // namespace

std::string getMeleeAttackAnim(
    CreatureWieldType attackerWield,
    CreatureWieldType targetWield,
    int variant,
    bool duel) {

    if (duel && isMeleeWieldType(targetWield)) {
        return attackAnimation('c', attackerWield, variant);
    }

    variant = nonCinematicVariant(variant);
    return attackAnimation(
        targetWield != CreatureWieldType::None ? 'm' : 'g',
        attackerWield,
        variant);
}

std::string getUnarmedAttackAnim(
    CreatureWieldType attackerWield,
    CreatureWieldType targetWield,
    int variant,
    bool duel) {

    if (attackerWield == CreatureWieldType::HandToHandComplex &&
        duel &&
        targetWield == attackerWield) {
        return attackAnimation('c', attackerWield, variant);
    }

    return attackAnimation(
        'g',
        CreatureWieldType::HandToHand,
        nonCinematicVariant(variant));
}

std::string getStunBatonAttackAnim(int variant) {
    return attackAnimation(
        'g',
        CreatureWieldType::StunBaton,
        nonCinematicVariant(variant));
}

void AttackObjectAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    if (isCompleted() || isCancelled()) {
        return;
    }
    Creature &attacker = cast<Creature>(actor);
    // The attack is taken up as it first runs, and not by a creature that
    // cannot be commanded. An attack restored from a save is not refused
    // again: a save need not record whether it was taken up.
    if (!_continuationDispatched && !originalSavedAction() && Combat::refusesAttack(attacker)) {
        finish(attacker);
        return;
    }
    _continuationDispatched = true;
    // A creature carrying out an attack makes no new combat decision.
    attacker.refreshCombatDecisionTimer();
    _game.party().setCombatMessage(attacker, _game.party().idleCombatMessage());
    if (!runtimeDependenciesLive()) {
        cancel(self, actor);
        markCancelled();
        return;
    }
    auto target = _target.resolve();
    if (!target || target->id() == attacker.id()) {
        attacker.cancelAllCombatModes();
        finish(attacker);
        return;
    }
    // An attack not taken from the round is refused on a friend as it would
    // start, as a round entry is when taken.
    if (!isScheduledCommand() && !_schedule.started() && !_approach.reached &&
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
    if (!attacker.hasDetectedTarget(*target)) {
        finish(attacker);
        return;
    }

    if (_game.combat().isActionPaused(*self)) {
        return;
    }

    // An attacker may leap at the target instead of closing on it; the attack
    // is then its Force Jump.
    switch (approachAttackTarget(attacker, *target, dt, _approach, _game, *self, true)) {
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

    // A round held while the attacker owns none keeps the attack from starting,
    // as does a round the attacker has acted in that has not ended.
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
            _approach.forceJump,
            true,
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
void AttackObjectAction::onQueued(Object &actor) {
    if (!originalSavedAction()) {
        if (auto target = _target.resolve()) {
            cast<Creature>(actor).recordQueuedAttack(*target);
        }
    }
}

void AttackObjectAction::retarget(const std::shared_ptr<Object> &target) {
    // The old target no longer holds the attack.
    const uint64_t previous = _target.incarnation();
    _runtimeDependencies.erase(std::remove_if(_runtimeDependencies.begin(), _runtimeDependencies.end(),
                                              [&](const auto &ref) { return ref.incarnation() == previous; }),
                               _runtimeDependencies.end());
    _target = target;
    requireRuntimeObject(target);
}

bool AttackObjectAction::cancel(std::shared_ptr<Action> self, Object &actor) {
    Creature &attacker = cast<Creature>(actor);
    _game.combat().transferEquipment(static_cast<Creature &>(actor));
    _attacks.discardPending();
    _attacks.clearHistory();
    // Clearing an attack forgets both its current and its attempted target.
    attacker.clearCurrentAttackTarget();
    attacker.setAttemptedAttackTarget(script::kObjectInvalid);
    finish(attacker);
    return true;
}

std::optional<SavedActionRecord> AttackObjectAction::saveFacingState() const {
    auto target = _target.resolve();
    if (!target) {
        return std::nullopt;
    }

    // The command's parameters order the attack; its progress (pending
    // resolution and scheduling) goes beside them.
    SavedActionRecord result;
    SavedAttackCommand {SavedObjectReference::fromRuntimeId(target->id()), FeatType::Invalid, _attacks.cutscene()}
        .toAction(result);
    SavedPhysicalAction progress;
    _attacks.saveContinuation(progress, _game);
    _schedule.saveContinuation(*progress.state);
    progress.state->fields().push_back(resource::Gff::Field::newByte("Reached", _approach.reached));
    progress.state->fields().push_back(resource::Gff::Field::newWord("ForceJump", static_cast<uint32_t>(_approach.forceJump)));
    progress.state->fields().push_back(resource::Gff::Field::newByte("NoSwing", _approach.withoutSwing));
    progress.state->fields().push_back(resource::Gff::Field::newByte("RoundContinues", _roundContinuation));
    progress.state->fields().push_back(resource::Gff::Field::newByte("ContinueTaken", _continuationDispatched));
    result.physical = std::move(progress);
    return result;
}

std::shared_ptr<Action> AttackObjectAction::restore(const SavedActionRecord &record, Game &game) {
    const auto command = SavedAttackCommand::fromAction(record);
    auto target = command->target.boundObject();
    if (!target) {
        return nullptr;
    }
    if (command->feat != FeatType::Invalid) {
        auto action = game.newAction<UseFeatAction>(command->feat, std::move(target));
        if (record.physical) {
            action->restorePhysicalState(*record.physical);
        }
        return action;
    }
    auto action = game.newAction<AttackObjectAction>(std::move(target));
    if (command->cutscene) {
        action->forceCutsceneAttack(*command->cutscene);
    }
    if (record.physical) {
        action->restorePhysicalState(*record.physical);
    }
    return action;
}

void AttackObjectAction::restorePhysicalState(const SavedPhysicalAction &state) {
    _attacks.restoreContinuation(state);
    _schedule.restoreContinuation(*state.state);
    _approach.reached = state.state->getBool("Reached");
    _approach.forceJump = static_cast<FeatType>(state.state->getUint("ForceJump"));
    _approach.withoutSwing = state.state->getBool("NoSwing");
    _roundContinuation = state.state->getBool("RoundContinues");
    _continuationDispatched = state.state->getBool("ContinueTaken");
}

void AttackObjectAction::finish(Creature &attacker) {
    attacker.setMovementRestricted(false);
    complete();
}

} // namespace game

} // namespace reone
