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

#include "reone/game/action/usetalentonobject.h"
#include "reone/game/action/castspellatobject.h"
#include "reone/game/action/usefeat.h"
#include "reone/game/game.h"

namespace reone {

namespace game {

void UseTalentOnObjectAction::dispatchToAction() {
    assert(!_action && "dispatchToAction is called twice");
    switch (_chosenTalent->type()) {
    case TalentType::Feat: {
        _action = _game.newAction<UseFeatAction>(
            static_cast<FeatType>(_chosenTalent->value()),
            _target);
        break;
    }
    case TalentType::Spell: {
        auto spell = _services.game.spells.get(static_cast<SpellType>(_chosenTalent->value()));
        if (!spell) {
            return;
        }
        _action = _game.newAction<CastSpellAtObjectAction>(
            std::move(spell), _target, /*item=*/std::nullopt, /*cheat=*/false);
        break;
    }
    default:
        break;
    }
}

void UseTalentOnObjectAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    if (!_action) {
        complete();
        return;
    }

    _action->execute(_action, actor, dt);

    if (_action->isCompleted()) {
        complete();
    }
}

} // namespace game

} // namespace reone
