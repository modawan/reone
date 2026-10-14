/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "reone/game/statussummary.h"

#include <algorithm>
#include <iterator>

namespace reone {

namespace game {

static size_t categoryIndex(StatusSummaryCategory category) {
    return static_cast<size_t>(category);
}

bool StatusSummaryBatch::empty() const {
    return !requested;
}

const StatusSummaryEntry &StatusSummaryBatch::entry(StatusSummaryCategory category) const {
    return entries.at(categoryIndex(category));
}

std::vector<StatusSummaryCategory> StatusSummaryBatch::activeCategories() const {
    std::vector<StatusSummaryCategory> result;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].active) {
            result.push_back(static_cast<StatusSummaryCategory>(i));
        }
    }
    return result;
}

bool StatusSummaryBatch::flashes(StatusSummaryCategory category, bool tsl) const {
    const int dark = entry(StatusSummaryCategory::DarkSideShift).amount;
    const int light = entry(StatusSummaryCategory::LightSideShift).amount;
    switch (category) {
    case StatusSummaryCategory::NetShift:
        return false;
    case StatusSummaryCategory::DarkSideShift:
        return !tsl || light < dark;
    case StatusSummaryCategory::LightSideShift:
        return !tsl || dark < light;
    default:
        return true;
    }
}

bool StatusSummaryAccumulator::submit(
    StatusSummaryCategory category,
    int amount,
    std::vector<std::string> items) {

    // A suppressed report is swallowed whole.
    if (_suppressed > 0) {
        --_suppressed;
        return false;
    }
    auto &entry = _pending.entries.at(categoryIndex(category));
    switch (category) {
    case StatusSummaryCategory::Credits:
        // A change against the running total's sign marks gains and losses as
        // mixed.
        if (amount == 0) {
            return true;
        }
        if ((entry.amount > 0 && amount < 0) || (entry.amount < 0 && amount > 0)) {
            entry.mixed = true;
        }
        entry.amount += amount;
        entry.active = true;
        break;
    case StatusSummaryCategory::PlotXP:
    case StatusSummaryCategory::StealthXP:
        if (amount == 0) {
            return true;
        }
        entry.amount += amount;
        entry.active = entry.amount != 0;
        break;
    case StatusSummaryCategory::DarkSideShift:
    case StatusSummaryCategory::LightSideShift: {
        if (amount == 0) {
            return true;
        }
        entry.amount += amount;
        entry.active = entry.amount != 0;
        // Both sides moved by different totals: the net shift shows too.
        const int dark = _pending.entry(StatusSummaryCategory::DarkSideShift).amount;
        const int light = _pending.entry(StatusSummaryCategory::LightSideShift).amount;
        auto &net = _pending.entries.at(categoryIndex(StatusSummaryCategory::NetShift));
        net.amount = light - dark;
        net.active = dark != 0 && light != 0 && dark != light;
        break;
    }
    case StatusSummaryCategory::InfluenceGained:
    case StatusSummaryCategory::InfluenceLost:
        // Only the named party NPCs show; any other index still asks for the
        // summary.
        if (amount >= 0 && amount < kStatusSummaryInfluenceMemberCount) {
            entry.npcs.insert(amount);
            entry.active = true;
        }
        break;
    case StatusSummaryCategory::MaxForcePointsGained:
    case StatusSummaryCategory::MaxForcePointsLost:
        entry.amount += amount;
        entry.active = entry.amount != 0;
        break;
    default:
        entry.active = true;
        entry.amount += amount;
        entry.items.insert(entry.items.end(),
                           std::make_move_iterator(items.begin()),
                           std::make_move_iterator(items.end()));
        break;
    }
    _pending.requested = true;
    return true;
}

void StatusSummaryAccumulator::suppress(int count) {
    if (count >= 0) {
        _suppressed = count;
    }
}

bool StatusSummaryAccumulator::beginPresentation() {
    if (_displayed || _pending.empty()) {
        return false;
    }
    _displayed = std::move(_pending);
    _pending = StatusSummaryBatch();
    return true;
}

void StatusSummaryAccumulator::acknowledge() {
    _displayed.reset();
}

void StatusSummaryAccumulator::discardPending() {
    _pending = StatusSummaryBatch();
}

void StatusSummaryAccumulator::reset() {
    _pending = StatusSummaryBatch();
    _displayed.reset();
    _suppressed = 0;
}

void StatusSummaryIndicator::activate() {
    _timer.reset(kStatusSummaryIndicatorDuration);
    _visible = true;
}

void StatusSummaryIndicator::update(float dt) {
    if (!_visible) {
        return;
    }
    _timer.update(dt);
    if (_timer.elapsed()) {
        _visible = false;
    }
}

void StatusSummaryIndicator::reset() {
    _timer.reset(0.0f);
    _visible = false;
}

} // namespace game

} // namespace reone
