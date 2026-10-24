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

namespace scene {
class ModelSceneNode;
}

namespace game {

/**
 * Perform a basic attack of the target using the current weapon.
 */
class AttackObjectAction : public Action {
public:
    AttackObjectAction(Game &game, ServicesView &services,
                       const std::shared_ptr<Object> &target) :
        Action(game, services, ActionType::AttackObject),
        _target(target),
        _services(services) {
        requireRuntimeObject(target);
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::AttackObject;
    }

    /**
     * An attack that has ended no longer holds its round, which runs out; the
     * round's one attack is then spent. While the attack runs, it is the round.
     */
    bool holdsCombatRound() const override { return _schedule.holdsCombatRound() && !isCompleted(); }
    bool tookRoundAction() const override { return _schedule.started() && isCompleted(); }
    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    bool cancel(std::shared_ptr<Action> self, Object &actor) override;
    void onQueued(Object &actor) override;
    void retireCombatRound() override { _attacks.clearHistory(); }
    void clearSpecialAttacks() override { _attacks.clearSpecialAttacks(); }
    int currentCombatAttackType() const override { return _attacks.currentCombatAttackType(); }
    AttackResultType currentCombatAttackResult() const override { return _attacks.currentRangedResult(); }
    std::optional<std::reference_wrapper<AttackBuffer>> combatAttacks() override { return std::ref(_attacks); }
    std::optional<SavedActionRecord> saveFacingState() const override;
    /** A saved AttackObject command: an attack, or a use of the combat feat it names; none once the target is gone. */
    static std::shared_ptr<Action> restore(const SavedActionRecord &record, Game &game);
    /** Takes up the progress of an attack loaded from a save. */
    void restorePhysicalState(const SavedPhysicalAction &state);
    std::shared_ptr<Object> target() const { return _target.resolve(); }
    /** The attack is aimed at a new target; it stays the attack it was otherwise. */
    void retarget(const std::shared_ptr<Object> &target);

    AttackResultType result() const { return _attacks.result(); }
    /** Make this a cutscene attack: forced swing, result and damage, no end-of-round script. */
    void forceCutsceneAttack(const CutsceneAttack &cutscene) { _attacks.forceCutscene(cutscene); }
    bool isCutsceneAttack() const override { return _attacks.cutscene().has_value(); }
    /** The attack the leader continues with when its round ends. */
    void markRoundContinuation() { _roundContinuation = true; }
    /** A continuation not yet dispatched: it has not executed since it was queued. */
    bool isPendingRoundContinuation() const {
        return _roundContinuation && !_continuationDispatched && !isCompleted() && !isCancelled();
    }

private:
    void finish(Creature &attacker);

    RuntimeObjectRef<Object> _target;
    bool _roundContinuation {false};
    bool _continuationDispatched {false};
    ServicesView &_services;

    AttackSchedule _schedule;
    AttackBuffer _attacks;
    AttackApproach _approach;
};

} // namespace game

} // namespace reone
