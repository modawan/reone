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

/**
 * Force Jump: the creature turns to face the target, and three tenths of a
 * second later leaps at it. The record stays applied.
 */
class ForceJumpEffect : public CopyableEffect<ForceJumpEffect> {
public:
    ForceJumpEffect(std::shared_ptr<Object> target, int advanced) :
        CopyableEffect(EffectType::ForceJump) {
        setSaveFacingInteger(0, advanced);
        setSaveFacingObject(0, target);
    }

    EffectApplicationResult onApply(Object &object, EffectInstance &instance) override;
};

/**
 * The leap of a Force jump: the creature is carried to just short of the
 * target, or onto the target's own spot when the target would not fit there.
 */
class ForceJumpDelayedEffect : public CopyableEffect<ForceJumpDelayedEffect> {
public:
    explicit ForceJumpDelayedEffect(std::shared_ptr<Object> target) :
        CopyableEffect(EffectType::Invalid) {
        setSaveFacingObject(0, target);
    }

    EffectInstance saveFacingInstance() const override;
    EffectApplicationResult onApply(Object &object, EffectInstance &instance) override;
};

} // namespace game

} // namespace reone
