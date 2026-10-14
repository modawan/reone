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
 * Blinds a creature, with a miss chance and the blind visual, for as long as
 * it lasts. Integer 0 says which blindness it is: that of darkness, which
 * Ultravision and True Seeing see through, or blindness itself, which a
 * creature may be immune to.
 */
class BlindEffect : public CopyableEffect<BlindEffect> {
public:
    BlindEffect();

    EffectApplicationResult onApply(Object &object, EffectInstance &instance) override;
    EffectRemovalResult onRemove(Object &object, const EffectInstance &instance) override;
};

} // namespace game

} // namespace reone
