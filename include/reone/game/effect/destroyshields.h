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
#include "../object.h"

namespace reone {

namespace game {

/**
 * Strips every Force shield from the object: each damage resistance a Force
 * shield generated (marked in its fifth integer) takes its whole shield with
 * it. The walk is not rewound after a removal, so a record that slides into
 * the removed one's place is passed over. The effect itself is not kept.
 */
class DestroyShieldsEffect : public CopyableEffect<DestroyShieldsEffect> {
public:
    DestroyShieldsEffect() :
        CopyableEffect(EffectType::DestroyShields) {
    }

    EffectApplicationResult onApply(Object &object, EffectInstance &) override {
        const auto &effects = object.effects();
        size_t index = 0;
        while (index < effects.size() && effects[index].serializedType < SavedEffectTypes::damageResistance) {
            ++index;
        }
        for (; index < effects.size(); ++index) {
            if (effects[index].serializedType > SavedEffectTypes::damageResistance) {
                break;
            }
            if (effects[index].integerParameter(4) == 1) {
                const auto id = effects[index].id;
                object.removeEffectsById(id);
            }
        }
        return EffectApplicationResult::Applied;
    }
};

} // namespace game

} // namespace reone
