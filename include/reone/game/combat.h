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

#include "reone/system/timer.h"

#include "action.h"
#include "effect/damage.h"
#include "object/creature.h"
#include "reone/system/smallvector.h"
#include "types.h"

namespace reone {

namespace game {

class Game;
struct ServicesView;

/**
 * Combat round consists of either one or two actions. Second action is only
 * present when both combatants are creatures.
 */
struct CombatRound {
    explicit CombatRound(const std::shared_ptr<Action> &action,
                         uint32_t attacker, uint32_t target) {
        actions.emplace_back(action, attacker, target);
    }

    enum State {
        Pending,
        FirstAction,
        SecondAction,
        Finished
    };

    struct RoundAction {
        RoundAction(const std::shared_ptr<Action> &action,
                    uint32_t attacker, uint32_t target) :
            action(action),
            attacker(attacker), target(target) {}

        std::shared_ptr<Action> action;
        uint32_t attacker {0};
        uint32_t target {0};
    };

    SmallVector<RoundAction, 2> actions;

    State state {Pending};
    bool duel {false};
    float time {0.0f};

    bool canExecute(Action &action) const;
};

/**
 * Combat is used schedule attacks and form rounds. When a creature starts an
 * attack, Combat creates a new round for it. If the target attacks back, and
 * the original attack is not finished yet, the corresponding round becomes a
 * duel (a CombatRound with more than 1 attack).
 *
 * Actions may handle duel rounds differently. For example, melee duels enable
 * "cinematic" animations.
 */
class Combat {
public:
    Combat(
        Game &game,
        ServicesView &services) :
        _game(game),
        _services(services) {
    }

    /**
     * Adds an action to an existing combat round, or starts a new round, based
     * on attacker and target.
     */
    const CombatRound &addAction(const std::shared_ptr<Action> &action, Object &actor);

    void update(float dt);

public:
    using RoundQueue = std::deque<std::unique_ptr<CombatRound>>;

    /**
     * Returns a list of past (completed) rounds as well as current rounds
     * ordered from oldest to newest.
     */
    const RoundQueue &rounds() const { return _rounds; }

private:
    Game &_game;
    ServicesView &_services;

    RoundQueue _rounds;

    void updateRound(CombatRound &round, float dt);
    void finishRound(CombatRound &round);

    CombatRound *findRoundForAction(const std::shared_ptr<Action> &action, uint32_t attacker);
    CombatRound *tryAppendAction(const std::shared_ptr<Action> &action, uint32_t attacker, uint32_t target);
};

} // namespace game

} // namespace reone
