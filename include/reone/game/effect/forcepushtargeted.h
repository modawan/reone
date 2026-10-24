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
#include "../location.h"

namespace reone {

namespace game {

class ForcePushTargetedEffect : public CopyableEffect<ForcePushTargetedEffect> {
public:
    ForcePushTargetedEffect(std::shared_ptr<Location> centre, bool ignoreTestDirectLine) :
        CopyableEffect(EffectType::ForcePushTargeted) {
        setSaveFacingInteger(0, 1);
        setSaveFacingInteger(1, ignoreTestDirectLine);
        setSaveFacingFloat(0, centre->position().x);
        setSaveFacingFloat(1, centre->position().y);
        setSaveFacingFloat(2, centre->position().z);
    }

    EffectApplicationResult onApply(Object &object, EffectInstance &) override;
    void onRemove(Object &object, const EffectInstance &) override;
};

} // namespace game

} // namespace reone
