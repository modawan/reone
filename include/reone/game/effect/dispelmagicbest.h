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
 * Removes one magical temporary or permanent effect group from the target:
 * the first group in the effect list whose roll succeeds, d20 plus the
 * caster's level against 11. A group whose creator is a creature of level 0
 * is passed over. The dispel itself is not retained.
 */
class DispelMagicBestEffect : public CopyableEffect<DispelMagicBestEffect> {
public:
    DispelMagicBestEffect() :
        CopyableEffect(EffectType::DispelMagicBest) {
    }

    EffectApplicationResult onApply(Object &object, EffectInstance &instance) override;
};

} // namespace game

} // namespace reone
