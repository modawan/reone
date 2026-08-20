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

#pragma once

#include <cstdint>
#include <map>
#include <tuple>
#include <utility>

namespace reone {

namespace game {

class Effect;

enum class EffectSourceKind : uint8_t {
    Item,
    Spell,
    Independent,
};

struct EffectSourceKey {
    EffectSourceKind kind {EffectSourceKind::Independent};
    uint64_t value {0};

    bool operator<(const EffectSourceKey &other) const {
        return std::tie(kind, value) < std::tie(other.kind, other.value);
    }
};

EffectSourceKey getEffectSourceKey(const Effect &effect);
EffectSourceKey getItemEffectSourceKey(uint32_t itemId);

class EffectModifierReducer {
public:
    void addIncrease(
        EffectSourceKey source,
        int subtype,
        int amount);
    void addDecrease(
        EffectSourceKey source,
        int subtype,
        int amount);

    int totalIncrease(int cap) const;
    int totalDecrease(int cap) const;

    std::map<int, int> increasesBySubtype(int cap) const;
    std::map<int, int> decreasesBySubtype(int cap) const;

private:
    using Key = std::pair<EffectSourceKey, int>;
    using Values = std::map<Key, int>;

    static void add(
        Values &values,
        EffectSourceKey source,
        int subtype,
        int amount);
    static int cappedTotal(const Values &values, int cap);
    static std::map<int, int> cappedBySubtype(
        const Values &values,
        int cap);

    Values _increases;
    Values _decreases;
};

} // namespace game

} // namespace reone
