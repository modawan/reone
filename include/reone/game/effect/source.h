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

/** The source of \p effect by its spell alone: its spell, or itself. */
EffectSourceKey getEffectSpellKey(const EffectInstance &effect);

/**
 * The sources one direction of one kind of effect counts: an item or spell
 * counts once, at its largest amount or, with addsUp, at the sum of its
 * amounts; an effect of neither counts on its own. A new source counts only
 * while fewer than the capacity are counted, in the order they are added.
 */
class EffectSourceSlots {
public:
    explicit EffectSourceSlots(int capacity) :
        _capacity(capacity) {
    }

    /**
     * Adds \p amount from \p source: an item or spell keeps its largest
     * amount, or, with \p addsUp, the sum of its amounts.
     */
    void add(EffectSourceKey source, int amount, bool addsUp = false);

    /** The amounts counted: each source's, plus the independent ones. */
    int total() const;

private:
    /** The amount each item or spell counts for. */
    std::map<EffectSourceKey, int> _bySource;
    /** The sum of the effects of no item or spell. */
    int _independent {0};
    /** The sources counted: each item or spell, and each effect of neither. */
    int _sources {0};
    /** The most sources counted. */
    int _capacity;
};

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

private:
    using Key = std::pair<EffectSourceKey, int>;
    using Values = std::map<Key, int>;

    static void add(
        Values &values,
        EffectSourceKey source,
        int subtype,
        int amount);
    static int cappedTotal(const Values &values, int cap);

    Values _increases;
    Values _decreases;
};

} // namespace game

} // namespace reone
