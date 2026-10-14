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
 * Changes how the creature sees, at once; nothing is kept. Scripts have no
 * effect type for it.
 */
class VisionEffect : public CopyableEffect<VisionEffect> {
public:
    /** The vision Ultravision gives. */
    static constexpr int kUltravision = 3;
    /** The vision blindness leaves. */
    static constexpr int kBlind = 4;

    explicit VisionEffect(int vision) :
        CopyableEffect(EffectType::Invalid) {
        setSaveFacingInteger(0, vision);
    }

    EffectInstance saveFacingInstance() const override;
    EffectApplicationResult onApply(Object &, EffectInstance &) override;
};

class UltravisionEffect : public CopyableEffect<UltravisionEffect> {
public:
    UltravisionEffect() :
        CopyableEffect(EffectType::Ultravision) {
    }

    EffectApplicationResult onApply(Object &object, EffectInstance &instance) override;
    EffectRemovalResult onRemove(Object &object, const EffectInstance &instance) override;
};

} // namespace game

} // namespace reone
