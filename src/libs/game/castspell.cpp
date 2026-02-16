/*
 * Copyright (c) 2026 The reone project contributors
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

#include "reone/game/castspell.h"
#include "reone/game/game.h"

namespace reone {
namespace game {

SpellSchedule::State SpellSchedule::update(
    const CombatRound &round, Action &action, float dt) {

    _time += dt;

    switch (_state) {
    case SpellSchedule::WaitConjure: {
        if (round.canExecute(action)) {
            _state = SpellSchedule::Conjure;
        }
        break;
    }
    case SpellSchedule::Conjure: {
        _state = SpellSchedule::WaitCast;
        break;
    }

    case SpellSchedule::WaitCast: {
        if (_time >= _conjTime) {
            _state = SpellSchedule::Cast;
        }
        break;
    }
    case SpellSchedule::Cast: {
        _state = SpellSchedule::WaitEffect;
        break;
    }
    case SpellSchedule::WaitEffect: {
        if (_time >= (_castTime + _conjTime)) {
            _state = SpellSchedule::Effect;
        }
        break;
    }
    case SpellSchedule::Effect: {
        _state = SpellSchedule::WaitFinish;
        break;
    }
    case SpellSchedule::WaitFinish: {
        if (round.state == CombatRound::Finished) {
            _state = SpellSchedule::Finish;
        }
        break;
    }
    case SpellSchedule::Finish: {
        break;
    }
    }

    return _state;
}

} // namespace game
} // namespace reone
