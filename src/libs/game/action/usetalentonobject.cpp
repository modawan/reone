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
#include "reone/game/action/usefeat.h"
#include "reone/game/action/castspellatobject.h"
#include "reone/game/game.h"
#include "reone/game/object.h"
#include "reone/game/object/creature.h"

namespace reone {

namespace game {

UseTalentOnObjectAction::UseTalentOnObjectAction(
        Game &game,
        ServicesView &services,
        std::shared_ptr<Talent> chosenTalent,
        std::shared_ptr<Object> target) :
        Action(game, services, ActionType::UseTalentOnObject),
        _chosenTalent(std::move(chosenTalent)),
        _target(std::move(target)) {

    if (_chosenTalent->type() == TalentType::Feat) {
        _action = std::make_unique<UseFeatAction>(
            _game, _services, (FeatType)_chosenTalent->value(), _target);
    } else if (_chosenTalent->type() == TalentType::Spell) {
        _action = std::make_unique<CastSpellAtObjectAction>(
                _game, _services, (SpellType)_chosenTalent->value(), _target,
                /*metaMagic=*/ 0, /*cheat=*/ 0, /*domainLevel=*/ 0,
                ProjectilePathType::Default, /*instantSpell=*/ false);
    }
}


void UseTalentOnObjectAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    if (!_action) {
        complete();
        return;
    }

    _action->execute(self, actor, dt);
    if (_action->isCompleted()) {
        complete();
    }
}

} // namespace game

} // namespace reone
