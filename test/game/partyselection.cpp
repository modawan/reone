/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <gtest/gtest.h>

#include "reone/game/gui/partyselect.h"

using namespace reone;
using namespace reone::game;

namespace reone::game {

class PartySelectionTestAccess {
public:
    static glm::vec3 frameColor(
        const glm::vec3 &baseColor,
        const glm::vec3 &inPartyColor,
        bool inParty,
        bool chosen) {
        return PartySelection::tslFrameColor(baseColor, inPartyColor, inParty, chosen);
    }

    static std::pair<bool, bool> sharedSlot(bool firstAvailable, bool secondAvailable) {
        return PartySelection::sharedSlotVisibility(firstAvailable, secondAvailable);
    }

    static std::string count(int count, bool tsl, const std::string &label) {
        return PartySelection::availableCountText(count, tsl, label);
    }
};

} // namespace reone::game

namespace {

void expectColor(const glm::vec3 &expected, const glm::vec3 &actual) {
    EXPECT_FLOAT_EQ(expected.r, actual.r);
    EXPECT_FLOAT_EQ(expected.g, actual.g);
    EXPECT_FLOAT_EQ(expected.b, actual.b);
}

} // namespace

TEST(PartySelectionPresentation, tsl_frames_use_membership_color_and_dim_unselected_slots) {
    const glm::vec3 base {0.2f, 0.4f, 0.6f};
    const glm::vec3 inParty {0.9f, 0.8f, 0.7f};

    expectColor(base, PartySelectionTestAccess::frameColor(base, inParty, false, true));
    expectColor(inParty, PartySelectionTestAccess::frameColor(base, inParty, true, true));
    expectColor(base * 0.67f, PartySelectionTestAccess::frameColor(base, inParty, false, false));
    expectColor(inParty * 0.67f, PartySelectionTestAccess::frameColor(base, inParty, true, false));
}

TEST(PartySelectionPresentation, shared_tsl_slots_show_one_empty_frame_or_each_available_companion) {
    EXPECT_EQ((std::pair<bool, bool> {true, false}), PartySelectionTestAccess::sharedSlot(false, false));
    EXPECT_EQ((std::pair<bool, bool> {true, false}), PartySelectionTestAccess::sharedSlot(true, false));
    EXPECT_EQ((std::pair<bool, bool> {false, true}), PartySelectionTestAccess::sharedSlot(false, true));
    EXPECT_EQ((std::pair<bool, bool> {true, true}), PartySelectionTestAccess::sharedSlot(true, true));
}

TEST(PartySelectionPresentation, tsl_count_includes_the_available_slots_label) {
    EXPECT_EQ("2", PartySelectionTestAccess::count(2, false, "Available Slots"));
    EXPECT_EQ("2 Available Slots", PartySelectionTestAccess::count(2, true, "Available Slots"));
}
