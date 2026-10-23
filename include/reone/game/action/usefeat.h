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

/**
 * Use of a feat on a target. A physical attack feat is a special attack, made
 * on its creature's combat round as an ordinary attack is.
 */
class UseFeatAction : public Action {
public:
    UseFeatAction(Game &game,
                  ServicesView &services,
                  FeatType feat,
                  std::shared_ptr<Object> target) :
        Action(game, services, ActionType::UseFeat),
        _feat(feat),
        _target(target) {
        requireRuntimeObject(target);
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::UseFeat;
    }

    /**
     * An attack that has ended no longer holds its round, which runs out; the
     * round's one attack is then spent. While the attack runs, it is the round.
     */
    bool holdsCombatRound() const override { return _schedule.holdsCombatRound() && !isCompleted(); }
    bool tookRoundAction() const override { return _schedule.started() && isCompleted() && !_instantCastAttack; }
    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;

    bool cancel(std::shared_ptr<Action> self, Object &actor) override;
    void onQueued(Object &actor) override;
    void retireCombatRound() override { _attacks.clearHistory(); }
    void clearSpecialAttacks() override { _attacks.clearSpecialAttacks(); }
    int currentCombatAttackType() const override { return _attacks.currentCombatAttackType(); }
    AttackResultType currentCombatAttackResult() const override { return _attacks.currentRangedResult(); }
    std::optional<std::reference_wrapper<AttackBuffer>> combatAttacks() override { return std::ref(_attacks); }
    std::optional<SavedActionRecord> saveFacingState() const override;
    /** Takes up the progress of an attack loaded from a save. */
    void restorePhysicalState(const SavedPhysicalAction &state);

    std::shared_ptr<Object> target() const { return _target.resolve(); }

    AttackResultType result() const { return _attacks.result(); }

    FeatType feat() const { return _feat; }

    /**
     * Make this the attack an instant cast makes: it needs no approach,
     * engages no one, takes no action of its round and ends with its pause.
     */
    void markInstantCastAttack() {
        _instantCastAttack = true;
        _approach.reached = true;
    }
    bool isInstantCastAttack() const { return _instantCastAttack; }

private:
    void finish(Creature &attacker);

    FeatType _feat;
    RuntimeObjectRef<Object> _target;

    AttackSchedule _schedule;
    AttackBuffer _attacks;
    AttackApproach _approach;
    bool _instantCastAttack {false};
    /** The attack has run once, and so has been taken up. */
    bool _taken {false};
};

} // namespace game

} // namespace reone
