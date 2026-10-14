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

class SpellLevelAbsorptionEffect : public CopyableEffect<SpellLevelAbsorptionEffect> {
public:
    SpellLevelAbsorptionEffect(int maxSpellLevelAbsorbed, int totalSpellLevelsAbsorbed, int spellSchool) :
        CopyableEffect(EffectType::SpellLevelAbsorption),
        _maxSpellLevelAbsorbed(maxSpellLevelAbsorbed),
        _totalSpellLevelsAbsorbed(totalSpellLevelsAbsorbed),
        _spellSchool(spellSchool) {
        setSaveFacingInteger(0, maxSpellLevelAbsorbed);
        setSaveFacingInteger(1, totalSpellLevelsAbsorbed);
        setSaveFacingInteger(2, spellSchool);
    }

    EffectApplicationResult onApply(Object &object, EffectInstance &) override {
        return EffectApplicationResult::Retained;
    }

private:
    int _maxSpellLevelAbsorbed;
    int _totalSpellLevelsAbsorbed;
    int _spellSchool;
};

} // namespace game

} // namespace reone
