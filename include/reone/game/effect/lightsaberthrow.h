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

#include "../effect.h"

namespace reone {

namespace game {

class LightsaberThrowEffect : public Effect {
public:
    LightsaberThrowEffect(
        std::shared_ptr<Object> target1,
        std::shared_ptr<Object> target2,
        std::shared_ptr<Object> target3,
        int advancedDamage) :
        Effect(EffectType::LightsaberThrow),
        _target1(std::move(target1)),
        _target2(std::move(target2)),
        _target3(std::move(target3)),
        _advancedDamage(advancedDamage) {
        // Retail accepts the fourth argument but does not persist it in the
        // CGameEffect. The three object slots are the complete wire payload.
        setSaveFacingObject(0, _target1);
        setSaveFacingObject(1, _target2);
        setSaveFacingObject(2, _target3);
    }

    void applyTo(Object &object) override;
    void retireAreaRuntime(
        const std::set<const Object *> &retainedObjects) override {
        for (auto *target : {&_target1, &_target2, &_target3}) {
            if (*target && retainedObjects.count(target->get()) == 0) {
                target->reset();
            }
        }
    }

private:
    std::shared_ptr<Object> _target1;
    std::shared_ptr<Object> _target2;
    std::shared_ptr<Object> _target3;
    int _advancedDamage;
};

} // namespace game

} // namespace reone
