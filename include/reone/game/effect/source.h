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
 * Totals the bonus that effects of one kind give a creature. Increases and
 * decreases are grouped by source: a source's increases count once, at the
 * largest, and effects of no item or spell each count on their own. Each
 * direction counts at most getEffectSourceCapacity sources, in the order
 * they are added, and is capped before the decreases are taken from the
 * increases.
 */
class EffectBonusReducer {
public:
    /** Ability effects: a source's decreases add up. */
    static EffectBonusReducer forAbility(bool tsl);
    /** Skill effects: a source's decreases also count once, at the largest. */
    static EffectBonusReducer forSkill(bool tsl);

    /** Counts an increase of \p amount from \p source. */
    void addIncrease(EffectSourceKey source, int amount);
    /** Counts a decrease of \p amount from \p source. */
    void addDecrease(EffectSourceKey source, int amount);

    /** The capped increases less the capped decreases. */
    int total() const;

private:
    /** The most the increases count for. */
    int _increaseCap {0};
    /** The most the decreases count for. */
    int _decreaseCap {0};
    /** Whether a source's decreases add up rather than count at the largest. */
    bool _decreasesAddUp {false};
    /** The increases counted so far. */
    EffectSourceSlots _increases;
    /** The decreases counted so far. */
    EffectSourceSlots _decreases;

    EffectBonusReducer(int increaseCap, int decreaseCap, bool decreasesAddUp, int sourceCapacity) :
        _increaseCap(increaseCap),
        _decreaseCap(decreaseCap),
        _decreasesAddUp(decreasesAddUp),
        _increases(sourceCapacity),
        _decreases(sourceCapacity) {
    }
};

} // namespace game

} // namespace reone
