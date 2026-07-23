/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include "reone/system/timer.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace reone {

namespace game {

/** The eight fixed K1 status-summary categories, in authored display order. */
enum class StatusSummaryCategory : size_t {
    Journal,
    Credits,
    PlotXP,
    StealthXP,
    DarkSideShift,
    LightSideShift,
    ItemsReceived,
    ItemsLost,
    Count
};

constexpr size_t kStatusSummaryCategoryCount = static_cast<size_t>(StatusSummaryCategory::Count);
constexpr float kStatusSummaryIndicatorDuration = 4.0f;

struct StatusSummaryEntry {
    bool active {false};
    int amount {0};
    std::vector<std::string> items;
};

struct StatusSummaryBatch {
    std::array<StatusSummaryEntry, kStatusSummaryCategoryCount> entries;

    bool empty() const;
    const StatusSummaryEntry &entry(StatusSummaryCategory category) const;
    std::vector<StatusSummaryCategory> activeCategories() const;
};

/**
 * Lossless fixed-category accumulator. Pending events are snapshotted for one
 * modal presentation; events submitted while that snapshot is visible stay in
 * the next pending batch.
 */
class StatusSummaryAccumulator {
public:
    void submit(StatusSummaryCategory category, int amount = 0, std::vector<std::string> items = {});

    bool beginPresentation();
    void acknowledge();
    void reset();

    const StatusSummaryBatch &pending() const { return _pending; }
    const std::optional<StatusSummaryBatch> &displayed() const { return _displayed; }
    bool awaitingAcknowledgement() const { return _displayed.has_value(); }

private:
    StatusSummaryBatch _pending;
    std::optional<StatusSummaryBatch> _displayed;
};

/** Visibility state for one fixed HUD category indicator. */
class StatusSummaryIndicator {
public:
    void activate();
    void update(float dt);
    void reset();

    bool visible() const { return _visible; }

private:
    Timer _timer;
    bool _visible {false};
};

} // namespace game

} // namespace reone
