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
 * A beam from an effector to the object: the beam's visual effect is integer
 * 0, the body part it reaches integer 1, the miss flag integer 2 and the
 * effector object 0.
 */
class BeamEffect : public CopyableEffect<BeamEffect> {
public:
    BeamEffect(
        int beamVisualEffect,
        std::shared_ptr<Object> effector,
        BodyNode bodyPart,
        bool missEffect) :
        CopyableEffect(EffectType::Beam) {
        setSaveFacingInteger(0, beamVisualEffect);
        setSaveFacingInteger(1, static_cast<int>(bodyPart));
        setSaveFacingInteger(2, missEffect ? 1 : 0);
        setSaveFacingObject(0, effector);
    }

    /**
     * Shows the beam as a visual on a creature, a placeable or a door, and in
     * TSL a trigger; the record is kept on any object.
     */
    EffectApplicationResult onApply(Object &object, EffectInstance &) override;
};

} // namespace game

} // namespace reone
