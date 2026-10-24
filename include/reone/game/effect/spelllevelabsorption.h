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
 * Absorbs spells: the highest level is integer 0, the total levels integer 1,
 * the school integer 2 and the unlimited mark integer 3.
 */
class SpellLevelAbsorptionEffect : public CopyableEffect<SpellLevelAbsorptionEffect> {
public:
    /**
     * Absorbs spells up to \p maxSpellLevelAbsorbed, \p totalSpellLevelsAbsorbed
     * levels in all, of \p spellSchool; a total of zero is marked unlimited.
     */
    SpellLevelAbsorptionEffect(int maxSpellLevelAbsorbed, int totalSpellLevelsAbsorbed, int spellSchool) :
        CopyableEffect(EffectType::SpellLevelAbsorption) {
        setSaveFacingInteger(0, maxSpellLevelAbsorbed);
        setSaveFacingInteger(1, totalSpellLevelsAbsorbed);
        setSaveFacingInteger(2, spellSchool);
        // A total of zero absorbs without limit.
        setSaveFacingInteger(3, totalSpellLevelsAbsorbed == 0 ? 1 : 0);
    }

    EffectApplicationResult onApply(Object &object, EffectInstance &) override {
        return EffectApplicationResult::Retained;
    }
};

} // namespace game

} // namespace reone
