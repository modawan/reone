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
#include "../talent.h"

namespace reone {

namespace game {

class UseTalentOnObjectAction : public Action {
public:
    UseTalentOnObjectAction(Game &game, ServicesView &services, std::shared_ptr<Talent> chosenTalent, std::shared_ptr<Object> target, Object &actor) :
        Action(game, services, ActionType::UseTalentOnObject),
        _chosenTalent(std::move(chosenTalent)),
        _target(std::move(target)) {

        requireRuntimeObject(_target);

        dispatchToAction(actor);
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::UseTalentOnObject;
    }

    /**
     * Makes the action the talent stands for: a feat use, a skill use, or a
     * spell cast or item use. A creature that cannot be commanded makes none.
     */
    void dispatchToAction(Object &actor);
    void onQueued(Object &actor) override {
        if (_action) {
            _action->onQueued(actor);
        }
    }
    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    bool cancel(std::shared_ptr<Action> self, Object &actor) override;
    std::optional<SavedActionRecord> saveFacingState() const override;
    uint32_t serializedActionId() const override {
        return _action ? _action->serializedActionId() : Action::serializedActionId();
    }
    bool suppressesEndRoundScript() const override { return _action && _action->suppressesEndRoundScript(); }

    /** The action the talent stands for; none when it stands for nothing to do. */
    const std::shared_ptr<Action> &subAction() const { return _action; }
    Action &combatAction() override { return _action ? _action->combatAction() : *this; }
    const Action &combatAction() const override {
        if (!_action) {
            return *this;
        }
        const Action &action = *_action;
        return action.combatAction();
    }
    bool holdsCombatRound() const override { return _action && _action->holdsCombatRound(); }
    void retireCombatRound() override {
        if (_action) {
            _action->retireCombatRound();
        }
    }

private:
    std::shared_ptr<Talent> _chosenTalent;
    std::shared_ptr<Object> _target;
    std::shared_ptr<Action> _action;
};

} // namespace game

} // namespace reone
