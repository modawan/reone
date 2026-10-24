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
 * Pure Good or Pure Evil powers, held innately at the ends of the good/evil
 * axis. The controlled creature gains a bonus per Jedi class; every holder
 * shows the effect icon. The bonuses are children of the package, rebuilt
 * whenever the powers are applied or restored and removed with them.
 */
class PureAlignmentPowersEffect : public CopyableEffect<PureAlignmentPowersEffect> {
public:
    explicit PureAlignmentPowersEffect(bool good) :
        CopyableEffect(good ? EffectType::PureGoodPowers : EffectType::PureEvilPowers) {
    }

    EffectApplicationResult onApply(Object &object, EffectInstance &instance) override;
};

} // namespace game

} // namespace reone
