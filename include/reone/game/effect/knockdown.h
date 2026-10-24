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
 * Knocks a creature down: its round stands still and it takes no commands
 * until the effect ends, its AI stays restricted a second and a half longer,
 * and it shows the fall. In TSL it also lies in the knocked-down state.
 */
class KnockdownEffect : public CopyableEffect<KnockdownEffect> {
public:
    KnockdownEffect() :
        CopyableEffect(EffectType::Knockdown) {
    }

    EffectApplicationResult onApply(Object &object, EffectInstance &instance) override;
    void onRemove(Object &object, const EffectInstance &instance) override;
};

} // namespace game

} // namespace reone
