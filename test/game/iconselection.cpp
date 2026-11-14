/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <gtest/gtest.h>

#include "reone/game/gui/chargen/iconselection.h"

using namespace reone;
using namespace reone::game;

TEST(IconSelection, feat_chains_start_on_separate_rows_and_wrap_after_three_icons) {
    const std::vector<FeatDisplayEntry> entries {
        {FeatType::PowerAttack, FeatType::PowerAttack, 0, 0},
        {FeatType::ImprovedPowerAttack, FeatType::PowerAttack, 1, 1},
        {FeatType::MasterPowerAttack, FeatType::PowerAttack, 2, 3},
        {FeatType::Flurry, FeatType::Flurry, 0, 0},
        {FeatType::MasterFlurry, FeatType::Flurry, 1, 1},
    };

    const auto positions = getFeatIconPositions(entries);

    EXPECT_EQ(glm::ivec2(0, 0), positions.at(FeatType::PowerAttack));
    EXPECT_EQ(glm::ivec2(1, 0), positions.at(FeatType::ImprovedPowerAttack));
    EXPECT_EQ(glm::ivec2(0, 1), positions.at(FeatType::MasterPowerAttack));
    EXPECT_EQ(glm::ivec2(0, 2), positions.at(FeatType::Flurry));
    EXPECT_EQ(glm::ivec2(1, 2), positions.at(FeatType::MasterFlurry));
}

TEST(IconSelection, power_chains_ignore_hidden_powers_and_pack_visible_icons) {
    const auto entry = [](SpellType type, SpellType root, bool visible) {
        SpellDisplayEntry result;
        result.type = type;
        result.chainRoot = root;
        result.visible = visible;
        return result;
    };
    const std::vector<SpellDisplayEntry> entries {
        entry(SpellType::ForcePush, SpellType::ForcePush, true),
        entry(SpellType::ForceWhirlwind, SpellType::ForcePush, false),
        entry(SpellType::ForceWave, SpellType::ForcePush, true),
        entry(SpellType::AffectMind, SpellType::ForcePush, true),
        entry(SpellType::DominateMind, SpellType::ForcePush, true),
        entry(SpellType::Cure, SpellType::Cure, true),
    };

    const auto positions = getPowerIconPositions(entries);

    EXPECT_EQ(5u, positions.size());
    EXPECT_EQ(0u, positions.count(SpellType::ForceWhirlwind));
    EXPECT_EQ(glm::ivec2(0, 0), positions.at(SpellType::ForcePush));
    EXPECT_EQ(glm::ivec2(1, 0), positions.at(SpellType::ForceWave));
    EXPECT_EQ(glm::ivec2(2, 0), positions.at(SpellType::AffectMind));
    EXPECT_EQ(glm::ivec2(0, 1), positions.at(SpellType::DominateMind));
    EXPECT_EQ(glm::ivec2(0, 2), positions.at(SpellType::Cure));
}
