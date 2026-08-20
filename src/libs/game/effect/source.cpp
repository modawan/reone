/*
 * Copyright (c) 2020-2026 The reone project contributors
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

#include "reone/game/effect/source.h"

#include <algorithm>

#include "reone/game/effect.h"
#include "reone/script/types.h"

namespace reone {

namespace game {

namespace {

EffectSourceKey getSpellEffectSourceKey(int spellId) {
    return {
        EffectSourceKind::Spell,
        static_cast<uint32_t>(spellId),
    };
}

} // namespace

EffectSourceKey getEffectSourceKey(const Effect &effect) {
    if (effect.sourceItemId() != script::kObjectInvalid) {
        return getItemEffectSourceKey(effect.sourceItemId());
    }
    if (effect.spellId() != -1) {
        return getSpellEffectSourceKey(effect.spellId());
    }
    // A native source value of -1 means this effect is not grouped with any
    // other effect. The runtime effect ID only distinguishes independent
    // entries inside the reducer.
    return {EffectSourceKind::Independent, effect.id()};
}

EffectSourceKey getItemEffectSourceKey(uint32_t itemId) {
    return {EffectSourceKind::Item, itemId};
}

void EffectModifierReducer::addIncrease(
    EffectSourceKey source,
    int subtype,
    int amount) {

    add(_increases, source, subtype, amount);
}

void EffectModifierReducer::addDecrease(
    EffectSourceKey source,
    int subtype,
    int amount) {

    add(_decreases, source, subtype, amount);
}

int EffectModifierReducer::totalIncrease(int cap) const {
    return cappedTotal(_increases, cap);
}

int EffectModifierReducer::totalDecrease(int cap) const {
    return cappedTotal(_decreases, cap);
}

std::map<int, int> EffectModifierReducer::increasesBySubtype(
    int cap) const {

    return cappedBySubtype(_increases, cap);
}

std::map<int, int> EffectModifierReducer::decreasesBySubtype(
    int cap) const {

    return cappedBySubtype(_decreases, cap);
}

void EffectModifierReducer::add(
    Values &values,
    EffectSourceKey source,
    int subtype,
    int amount) {

    if (amount <= 0) {
        return;
    }

    Key key {source, subtype};
    auto it = values.find(key);
    if (it == values.end() || amount > it->second) {
        values.insert_or_assign(key, amount);
    }
}

int EffectModifierReducer::cappedTotal(
    const Values &values,
    int cap) {

    int result = 0;
    for (const auto &[key, amount] : values) {
        result = std::min(cap, result + amount);
        if (result == cap) {
            break;
        }
    }
    return result;
}

std::map<int, int> EffectModifierReducer::cappedBySubtype(
    const Values &values,
    int cap) {

    std::map<int, int> totals;
    for (const auto &[key, amount] : values) {
        totals[key.second] += amount;
    }

    std::map<int, int> result;
    int remaining = cap;
    for (const auto &[subtype, amount] : totals) {
        if (remaining <= 0) {
            break;
        }

        int retained = std::min(remaining, amount);
        if (retained > 0) {
            result[subtype] = retained;
            remaining -= retained;
        }
    }
    return result;
}

} // namespace game

} // namespace reone
