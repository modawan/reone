/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "reone/system/timer.h"

namespace reone {

namespace game {

/**
 * The fixed status-summary rows, in display order. KotOR has the first nine;
 * TSL adds the influence and maximum Force point rows.
 *
 * The amount a producer submits is the change, except for the influence rows,
 * where it is the party NPC index. The net shift row is not submitted: it
 * follows from the dark and light side totals.
 */
enum class StatusSummaryCategory : size_t {
    Journal,
    Credits,
    PlotXP,
    StealthXP,
    DarkSideShift,
    LightSideShift,
    NetShift,
    ItemsReceived,
    ItemsLost,
    InfluenceGained,
    InfluenceLost,
    MaxForcePointsGained,
    MaxForcePointsLost,
    Count
};

constexpr size_t kStatusSummaryCategoryCount = static_cast<size_t>(StatusSummaryCategory::Count);
constexpr float kStatusSummaryIndicatorDuration = 4.0f;

/** Party NPC indices the influence rows name. */
constexpr int kStatusSummaryInfluenceMemberCount = 12;

/** One row of a status summary. */
struct StatusSummaryEntry {
    /** The row is shown. */
    bool active {false};
    /** The summed change; for the net shift, the light side total less the dark side total. */
    int amount {0};
    /** Credits: gains and losses were both reported. */
    bool mixed {false};
    /** Items rows: the names of the items. */
    std::vector<std::string> items;
    /** Influence rows: the party NPC indices, in index order. */
    std::set<int> npcs;
};

/** The rows of one status summary. */
struct StatusSummaryBatch {
    std::array<StatusSummaryEntry, kStatusSummaryCategoryCount> entries;
    /** Something was reported, even when it shows no row. */
    bool requested {false};
    /** The level-up chime plays when the batch is presented. */
    bool levelUpSound {false};
    /** The new quest notice plays when the batch is presented. */
    bool questNewSound {false};
    /** The completed quest notice plays when the batch is presented. */
    bool questCompleteSound {false};

    /** Whether nothing was reported. */
    bool empty() const;
    const StatusSummaryEntry &entry(StatusSummaryCategory category) const;
    std::vector<StatusSummaryCategory> activeCategories() const;
    /**
     * Whether presenting a shown row flashes its indicator on the main
     * interface. The net shift never does; in TSL a side's shift does only
     * when it outweighs the other side's.
     */
    bool flashes(StatusSummaryCategory category, bool tsl) const;
};

/**
 * Lossless fixed-category accumulator. Pending events are snapshotted for one
 * modal presentation; events submitted while that snapshot is visible stay in
 * the next pending batch.
 */
class StatusSummaryAccumulator {
public:
    /** Takes a report; false when a suppression swallowed it. */
    bool submit(StatusSummaryCategory category, int amount = 0, std::vector<std::string> items = {});
    /**
     * Queues the level-up chime for the next presentation.
     *
     * @return false when a suppression swallowed it
     */
    bool submitLevelUpSound();
    /**
     * Queues the new quest notice, or the completed quest notice for an entry
     * that ends its quest, for the next presentation.
     *
     * @return false when a suppression swallowed it
     */
    bool submitQuestSound(bool complete);
    /** The next \p count reports are swallowed; a negative count changes nothing. */
    void suppress(int count);
    /**
     * Takes a pending batch that has only sounds to play, while no batch is
     * displayed. Such a batch plays its sounds without being shown.
     *
     * @return the batch whose sounds should play; none when there is no such batch
     */
    std::optional<StatusSummaryBatch> takeSoundBatch();

    bool beginPresentation();
    void acknowledge();
    /** Drops what is pending without presenting it. */
    void discardPending();
    void reset();

    const StatusSummaryBatch &pending() const { return _pending; }
    const std::optional<StatusSummaryBatch> &displayed() const { return _displayed; }
    bool awaitingAcknowledgement() const { return _displayed.has_value(); }

private:
    StatusSummaryBatch _pending;
    std::optional<StatusSummaryBatch> _displayed;
    int _suppressed {0};

    /**
     * Uses up one suppression, if any is left.
     *
     * @return whether a suppression swallowed the report
     */
    bool swallowedBySuppression();
};

/** Visibility state for one fixed HUD category indicator. */
class StatusSummaryIndicator {
public:
    void activate();
    /**
     * Counts the flash down by \p dt.
     *
     * @return whether this is the first update with time passing since the flash started
     */
    bool update(float dt);
    void reset();

    bool visible() const { return _visible; }

private:
    Timer _timer;
    bool _visible {false};
    /** The flash has started and no time has passed on it yet. */
    bool _starting {false};
};

} // namespace game

} // namespace reone
