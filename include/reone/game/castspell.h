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

#pragma once

namespace reone {

namespace game {

class Action;
class CombatRound;

class SpellSchedule {
public:
    explicit SpellSchedule(float conjTime, float castTime) :
        _conjTime(conjTime), _castTime(castTime) {}

    enum State {
        WaitConjure,
        Conjure,
        WaitCast,
        Cast,
        WaitEffect,
        Effect,
        WaitFinish,
        Finish,
    };

    State update(const CombatRound &round, Action &action, float dt);

private:
    State _state {WaitConjure};
    float _time {0.0f};
    float _conjTime {0.0f};
    float _castTime {0.0f};
};

} // namespace game
} // namespace reone
