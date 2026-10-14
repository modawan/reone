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

/**
 * Pushes the creature away from the centre and lays it in the force-pushed
 * state for the push effect's duration.
 */
bool applyForcePushMovement(Object &, const glm::vec3 &centre, bool ignoreDirectLine,
                            EffectInstance &owner);
/** The creator a push comes from; only an object in the world pushes. */
std::shared_ptr<Object> getForcePushCreator(const EffectInstance &owner);
/** A push ending frees the creature's facing and holds its AI a moment. */
void endForcePushEffect(Object &);

class ForcePushedEffect : public CopyableEffect<ForcePushedEffect> {
public:
    ForcePushedEffect() :
        CopyableEffect(EffectType::ForcePushed) {
    }

    EffectApplicationResult onApply(Object &object, EffectInstance &) override;
    EffectRemovalResult onRemove(Object &object, const EffectInstance &) override;
};

} // namespace game

} // namespace reone
