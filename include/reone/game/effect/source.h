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

#include <cstdint>
#include <map>
#include <tuple>
#include <utility>

#include "../effect.h"

namespace reone {

namespace game {

/** What kind of source an effect is grouped by. */
enum class EffectSourceKind {
    Item,
    Spell,
    Independent,
};

/** The item, spell or single effect an effect comes from. */
struct EffectSourceKey {
    EffectSourceKind kind {EffectSourceKind::Independent};
    uint64_t value {0};

    bool operator<(const EffectSourceKey &other) const {
        return std::tie(kind, value) < std::tie(other.kind, other.value);
    }
};

/** The source of \p effect: its creating item, its spell, or itself. */
EffectSourceKey getEffectSourceKey(const EffectInstance &effect);

/**
 * Totals the ability increases and decreases on a creature by source. A
 * source's increases count once, at the largest; its decreases add up. Effects
 * of no item or spell each count on their own. Each direction is capped.
 */
class AbilityEffectReducer {
public:
    explicit AbilityEffectReducer(bool tsl) :
        _tsl(tsl) {
    }

    void addIncrease(EffectSourceKey source, int amount);
    void addDecrease(EffectSourceKey source, int amount);

    /** The capped increases less the capped decreases. */
    int total() const;

private:
    struct Direction {
        std::map<EffectSourceKey, int> bySource;
        int independent {0};

        int total() const;
    };

    bool _tsl;
    Direction _increases;
    Direction _decreases;
};

/**
 * Totals effect modifiers by source and subtype: for each pair only the
 * largest counts, and the total is capped.
 */
class EffectModifierReducer {
public:
    void addIncrease(EffectSourceKey source, int subtype, int amount);
    void addDecrease(EffectSourceKey source, int subtype, int amount);

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
