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
 * Poisons a creature with a poison.2da row: a Fortitude save resists it;
 * otherwise it deals its damage at once and then once a period until its
 * duration runs out, its ability damage growing each time. A creature holds
 * one poison at a time.
 */
class PoisonEffect : public CopyableEffect<PoisonEffect> {
public:
    explicit PoisonEffect(Poison type) :
        CopyableEffect(EffectType::Poison) {
        setSaveFacingInteger(0, static_cast<int>(type));
    }

    EffectApplicationResult onApply(Object &, EffectInstance &) override;
    EffectRemovalResult onRemove(Object &, const EffectInstance &) override;
    void onUpdate(Object &, const EffectInstance &, float) override;

private:
    /** Ends the poison at the next update; it is kept until then. */
    EffectApplicationResult queueRemoval(Object &, EffectId);
    /** Deals the poison's damage, its ability damage times \p factor. */
    void applyDamage(Creature &, const EffectInstance &, float factor);
};

} // namespace game

} // namespace reone
