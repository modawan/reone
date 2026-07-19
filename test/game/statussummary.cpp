/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <gtest/gtest.h>

#include "reone/game/statussummary.h"

using namespace reone::game;

TEST(StatusSummaryAccumulator, empty_accumulator_has_no_displayable_batch) {
    StatusSummaryAccumulator accumulator;

    EXPECT_TRUE(accumulator.pending().empty());
    EXPECT_FALSE(accumulator.beginPresentation());
    EXPECT_FALSE(accumulator.awaitingAcknowledgement());
}

TEST(StatusSummaryAccumulator, journal_submissions_collapse_to_one_category) {
    StatusSummaryAccumulator accumulator;

    accumulator.submit(StatusSummaryCategory::Journal);
    accumulator.submit(StatusSummaryCategory::Journal);

    auto active = accumulator.pending().activeCategories();
    ASSERT_EQ(active.size(), 1u);
    EXPECT_EQ(active.front(), StatusSummaryCategory::Journal);
}

TEST(StatusSummaryAccumulator, fixed_order_is_independent_of_arrival_order) {
    StatusSummaryAccumulator accumulator;

    accumulator.submit(StatusSummaryCategory::ItemsLost);
    accumulator.submit(StatusSummaryCategory::PlotXP, 350);
    accumulator.submit(StatusSummaryCategory::Journal);
    accumulator.submit(StatusSummaryCategory::Credits, 100);

    EXPECT_EQ(
        accumulator.pending().activeCategories(),
        (std::vector<StatusSummaryCategory> {
            StatusSummaryCategory::Journal,
            StatusSummaryCategory::Credits,
            StatusSummaryCategory::PlotXP,
            StatusSummaryCategory::ItemsLost}));
}

TEST(StatusSummaryAccumulator, snapshot_moves_pending_state_to_displayed_batch) {
    StatusSummaryAccumulator accumulator;
    accumulator.submit(StatusSummaryCategory::Journal);

    ASSERT_TRUE(accumulator.beginPresentation());

    EXPECT_TRUE(accumulator.pending().empty());
    ASSERT_TRUE(accumulator.displayed());
    EXPECT_TRUE(accumulator.displayed()->entry(StatusSummaryCategory::Journal).active);
    EXPECT_FALSE(accumulator.beginPresentation());
}

TEST(StatusSummaryAccumulator, acknowledgement_clears_only_displayed_batch) {
    StatusSummaryAccumulator accumulator;
    accumulator.submit(StatusSummaryCategory::Journal);
    ASSERT_TRUE(accumulator.beginPresentation());
    accumulator.submit(StatusSummaryCategory::Credits, 50);

    accumulator.acknowledge();

    EXPECT_FALSE(accumulator.displayed());
    EXPECT_TRUE(accumulator.pending().entry(StatusSummaryCategory::Credits).active);
    ASSERT_TRUE(accumulator.beginPresentation());
    EXPECT_EQ(accumulator.displayed()->entry(StatusSummaryCategory::Credits).amount, 50);
}

TEST(StatusSummaryAccumulator, repeated_numeric_and_item_data_is_accumulated_losslessly) {
    StatusSummaryAccumulator accumulator;

    accumulator.submit(StatusSummaryCategory::PlotXP, 100);
    accumulator.submit(StatusSummaryCategory::PlotXP, 250);
    accumulator.submit(StatusSummaryCategory::ItemsReceived, 0, {"first"});
    accumulator.submit(StatusSummaryCategory::ItemsReceived, 0, {"second"});

    EXPECT_EQ(accumulator.pending().entry(StatusSummaryCategory::PlotXP).amount, 350);
    EXPECT_EQ(
        accumulator.pending().entry(StatusSummaryCategory::ItemsReceived).items,
        (std::vector<std::string> {"first", "second"}));
}

TEST(StatusSummaryAccumulator, reset_clears_pending_and_displayed_batches) {
    StatusSummaryAccumulator accumulator;
    accumulator.submit(StatusSummaryCategory::Journal);
    ASSERT_TRUE(accumulator.beginPresentation());
    accumulator.submit(StatusSummaryCategory::Credits, 1);

    accumulator.reset();

    EXPECT_TRUE(accumulator.pending().empty());
    EXPECT_FALSE(accumulator.displayed());
}

TEST(StatusSummaryPresentation, journal_indicator_uses_vanilla_four_second_interval) {
    EXPECT_FLOAT_EQ(kStatusSummaryIndicatorDuration, 4.0f);
}

TEST(StatusSummaryPresentation, journal_indicator_expires_after_four_seconds) {
    StatusSummaryIndicator indicator;

    indicator.activate();
    indicator.update(3.9f);
    EXPECT_TRUE(indicator.visible());

    indicator.update(0.1f);
    EXPECT_FALSE(indicator.visible());
}

TEST(StatusSummaryPresentation, repeated_journal_submission_resets_indicator_timer) {
    StatusSummaryIndicator indicator;

    indicator.activate();
    indicator.update(3.0f);
    indicator.activate();
    indicator.update(1.1f);
    EXPECT_TRUE(indicator.visible());

    indicator.update(2.9f);
    EXPECT_FALSE(indicator.visible());
}

TEST(StatusSummaryPresentation, acknowledgement_does_not_change_indicator_timer) {
    StatusSummaryAccumulator accumulator;
    StatusSummaryIndicator indicator;
    accumulator.submit(StatusSummaryCategory::Journal);
    ASSERT_TRUE(accumulator.beginPresentation());
    indicator.activate();

    accumulator.acknowledge();

    EXPECT_TRUE(indicator.visible());
    indicator.update(4.0f);
    EXPECT_FALSE(indicator.visible());
}

TEST(CombinedStatusSummary, xp_only_snapshot_contains_one_plot_xp_category) {
    StatusSummaryAccumulator accumulator;
    accumulator.submit(StatusSummaryCategory::PlotXP, 350);

    ASSERT_TRUE(accumulator.beginPresentation());

    ASSERT_EQ(accumulator.displayed()->activeCategories().size(), 1u);
    EXPECT_EQ(
        accumulator.displayed()->activeCategories().front(),
        StatusSummaryCategory::PlotXP);
    EXPECT_EQ(accumulator.displayed()->entry(StatusSummaryCategory::PlotXP).amount, 350);
}

TEST(CombinedStatusSummary, journal_then_xp_uses_fixed_category_order) {
    StatusSummaryAccumulator accumulator;
    accumulator.submit(StatusSummaryCategory::Journal);
    accumulator.submit(StatusSummaryCategory::PlotXP, 350);

    EXPECT_EQ(
        accumulator.pending().activeCategories(),
        (std::vector<StatusSummaryCategory> {
            StatusSummaryCategory::Journal,
            StatusSummaryCategory::PlotXP}));
}

TEST(CombinedStatusSummary, xp_then_journal_uses_fixed_category_order) {
    StatusSummaryAccumulator accumulator;
    accumulator.submit(StatusSummaryCategory::PlotXP, 350);
    accumulator.submit(StatusSummaryCategory::Journal);

    EXPECT_EQ(
        accumulator.pending().activeCategories(),
        (std::vector<StatusSummaryCategory> {
            StatusSummaryCategory::Journal,
            StatusSummaryCategory::PlotXP}));
}

TEST(CombinedStatusSummary, xp_arriving_while_visible_remains_pending) {
    StatusSummaryAccumulator accumulator;
    accumulator.submit(StatusSummaryCategory::Journal);
    ASSERT_TRUE(accumulator.beginPresentation());

    accumulator.submit(StatusSummaryCategory::PlotXP, 350);

    EXPECT_FALSE(accumulator.displayed()->entry(StatusSummaryCategory::PlotXP).active);
    EXPECT_EQ(accumulator.pending().entry(StatusSummaryCategory::PlotXP).amount, 350);
    accumulator.acknowledge();
    ASSERT_TRUE(accumulator.beginPresentation());
    EXPECT_EQ(accumulator.displayed()->entry(StatusSummaryCategory::PlotXP).amount, 350);
}

TEST(PlotXPIndicator, plot_xp_and_journal_timers_are_independent) {
    StatusSummaryIndicator journal;
    StatusSummaryIndicator plotXP;
    journal.activate();
    journal.update(3.0f);
    plotXP.activate();

    journal.update(1.0f);
    plotXP.update(1.0f);

    EXPECT_FALSE(journal.visible());
    EXPECT_TRUE(plotXP.visible());
    plotXP.update(3.0f);
    EXPECT_FALSE(plotXP.visible());
}

TEST(PlotXPIndicator, repeated_plot_xp_award_resets_one_timer) {
    StatusSummaryIndicator plotXP;
    plotXP.activate();
    plotXP.update(3.0f);

    plotXP.activate();
    plotXP.update(3.0f);

    EXPECT_TRUE(plotXP.visible());
    plotXP.update(1.0f);
    EXPECT_FALSE(plotXP.visible());
}

TEST(PlotXPIndicator, acknowledgement_does_not_clear_plot_xp_or_journal) {
    StatusSummaryAccumulator accumulator;
    StatusSummaryIndicator journal;
    StatusSummaryIndicator plotXP;
    accumulator.submit(StatusSummaryCategory::Journal);
    accumulator.submit(StatusSummaryCategory::PlotXP, 350);
    ASSERT_TRUE(accumulator.beginPresentation());
    journal.activate();
    plotXP.activate();

    accumulator.acknowledge();

    EXPECT_TRUE(journal.visible());
    EXPECT_TRUE(plotXP.visible());
}

TEST(PlotXPIndicator, reset_clears_both_indicators) {
    StatusSummaryIndicator journal;
    StatusSummaryIndicator plotXP;
    journal.activate();
    plotXP.activate();

    journal.reset();
    plotXP.reset();

    EXPECT_FALSE(journal.visible());
    EXPECT_FALSE(plotXP.visible());
}
