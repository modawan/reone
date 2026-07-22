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
    return std::none_of(entries.begin(), entries.end(), [](const auto &entry) {
        return entry.active;
    });
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

void StatusSummaryAccumulator::submit(
    StatusSummaryCategory category,
    int amount,
    std::vector<std::string> items) {

    auto &entry = _pending.entries.at(categoryIndex(category));
    entry.active = true;
    entry.amount += amount;
    entry.items.insert(entry.items.end(),
                       std::make_move_iterator(items.begin()),
                       std::make_move_iterator(items.end()));
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

void StatusSummaryAccumulator::reset() {
    _pending = StatusSummaryBatch();
    _displayed.reset();
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
