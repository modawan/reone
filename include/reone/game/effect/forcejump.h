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
#include "../object.h"

namespace reone {

namespace game {

class ForceJumpEffect : public Effect {
public:
    ForceJumpEffect(std::shared_ptr<Object> target, int advanced) :
        Effect(EffectType::ForceJump),
        _target(target),
        _advanced(advanced) {
        setSaveFacingInteger(0, advanced);
        setSaveFacingObject(0, target);
    }

    void applyTo(Object &object) override {
    }
    void retireAreaRuntime(
        const std::set<const Object *> &retainedObjects) override {
        auto target = _target.resolve();
        if (!target || retainedObjects.count(target.get()) == 0) {
            _target.reset();
        }
    }

private:
    RuntimeObjectRef<Object> _target;
    int _advanced;
};

} // namespace game

} // namespace reone
