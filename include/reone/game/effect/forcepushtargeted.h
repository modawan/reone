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
        CopyableEffect(EffectType::ForcePushTargeted),
        _centre(std::move(centre)),
        _ignoreTestDirectLine(ignoreTestDirectLine) {
        setSaveFacingInteger(0, 1);
        setSaveFacingInteger(1, ignoreTestDirectLine);
        setSaveFacingFloat(0, _centre->position().x);
        setSaveFacingFloat(1, _centre->position().y);
        setSaveFacingFloat(2, _centre->position().z);
    }

    ForcePushTargetedEffect(const ForcePushTargetedEffect &other) :
        CopyableEffect(other),
        _centre(std::make_shared<Location>(other._centre->position(),
                                           other._centre->saveOrientation())),
        _ignoreTestDirectLine(other._ignoreTestDirectLine) {
    }

    EffectApplicationResult onApply(Object &object, EffectInstance &) override;
    EffectRemovalResult onRemove(Object &object, const EffectInstance &) override;

private:
    std::shared_ptr<Location> _centre;
    bool _ignoreTestDirectLine;
};

} // namespace game

} // namespace reone
