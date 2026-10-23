/*
 * Copyright (c) 2026 The reone project contributors
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
 * Deals a creature a share of its vitality at once, by rank: a quarter, a
 * third or half for ranks 1 to 3. Nothing is kept.
 */
class AssassinateEffect : public CopyableEffect<AssassinateEffect> {
public:
    explicit AssassinateEffect(int rank) :
        CopyableEffect(EffectType::Assassinate) {
        setSaveFacingInteger(0, rank);
    }

    EffectApplicationResult onApply(Object &object, EffectInstance &instance) override;
};

} // namespace game

} // namespace reone
