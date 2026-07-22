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

#include <gtest/gtest.h>

#include "reone/game/transitioncandidate.h"

using namespace reone;
using namespace reone::game;

namespace {

TransitionPortal makePortal(uint32_t id, std::string destination, glm::vec2 center) {
    TransitionPortal portal;
    portal.objectId = id;
    portal.destination = std::move(destination);
    portal.points = {
        glm::vec3(center.x - 1.0f, center.y - 1.0f, 0.0f),
        glm::vec3(center.x - 1.0f, center.y + 1.0f, 0.0f),
        glm::vec3(center.x + 1.0f, center.y + 1.0f, 0.0f),
        glm::vec3(center.x + 1.0f, center.y - 1.0f, 0.0f)};
    return portal;
}

glm::mat4 viewProjectionLookingAt(glm::vec3 eye, glm::vec3 target) {
    static const glm::vec3 up(0.0f, 0.0f, 1.0f);
    auto projection = glm::perspective(glm::radians(55.0f), 4.0f / 3.0f, 0.1f, 500.0f);
    auto view = glm::lookAt(eye, target, up);
    return projection * view;
}

} // namespace

TEST(TransitionCandidate, should_measure_distance_to_portal_polygon) {
    auto portal = makePortal(1, "somewhere", glm::vec2(0.0f));

    EXPECT_EQ(0.0f, distanceToTransitionPortal(glm::vec2(0.0f), portal.points));
    EXPECT_EQ(0.0f, distanceToTransitionPortal(glm::vec2(0.9f, -0.9f), portal.points));
    EXPECT_NEAR(2.0f, distanceToTransitionPortal(glm::vec2(3.0f, 0.0f), portal.points), 1e-4f);
    EXPECT_NEAR(4.0f, distanceToTransitionPortal(glm::vec2(0.0f, -5.0f), portal.points), 1e-4f);
}

TEST(TransitionCandidate, should_qualify_nearby_portal_in_camera_view) {
    glm::vec3 leader(0.0f);
    auto portals = std::vector<TransitionPortal> {makePortal(7, "Ahead", glm::vec2(4.0f, 0.0f))};

    TransitionView view;
    view.leaderPosition = leader;
    view.cameraViewProjection = viewProjectionLookingAt(glm::vec3(-2.0f, 0.0f, 1.5f), glm::vec3(4.0f, 0.0f, 0.5f));

    auto candidate = pickTransitionPortal(portals, view);

    ASSERT_TRUE(candidate.has_value());
    EXPECT_EQ(candidate->objectId, 7u);
    EXPECT_EQ(candidate->destination, "Ahead");
}

TEST(TransitionCandidate, should_reject_portal_inside_viewport_but_well_off_axis) {
    glm::vec3 leader(0.0f);
    auto portals = std::vector<TransitionPortal> {makePortal(7, "Off axis", glm::vec2(3.0f, 3.0f))};

    TransitionView view;
    view.leaderPosition = leader;
    view.cameraViewProjection = viewProjectionLookingAt(
        glm::vec3(-2.0f, 0.0f, 1.5f),
        glm::vec3(4.0f, 0.0f, 1.5f));

    ASSERT_TRUE(isTransitionPortalInView(
        portals.front().points,
        leader,
        view.cameraViewProjection,
        1.0f)) << "the aim point must remain inside the ordinary viewport";
    EXPECT_FALSE(pickTransitionPortal(portals, view).has_value());
}

TEST(TransitionCandidate, should_not_qualify_large_portal_from_favourable_distant_vertex) {
    TransitionPortal portal;
    portal.objectId = 7;
    portal.destination = "Large exit";
    portal.points = {
        glm::vec3(3.0f, 3.0f, 0.0f),
        glm::vec3(4.0f, 4.0f, 0.0f),
        glm::vec3(30.0f, 0.0f, 0.0f)};

    TransitionView view;
    view.leaderPosition = glm::vec3(0.0f);
    view.cameraViewProjection = viewProjectionLookingAt(
        glm::vec3(-2.0f, 0.0f, 1.5f),
        glm::vec3(4.0f, 0.0f, 1.5f));

    EXPECT_FALSE(pickTransitionPortal({portal}, view).has_value());
}

TEST(TransitionCandidate, should_not_qualify_portal_behind_camera) {
    auto portals = std::vector<TransitionPortal> {makePortal(7, "Behind", glm::vec2(4.0f, 0.0f))};

    TransitionView view;
    view.leaderPosition = glm::vec3(0.0f);
    // Same leader position, camera looking the opposite way.
    view.cameraViewProjection = viewProjectionLookingAt(glm::vec3(-2.0f, 0.0f, 1.5f), glm::vec3(-10.0f, 0.0f, 0.5f));

    EXPECT_FALSE(pickTransitionPortal(portals, view).has_value());
}

TEST(TransitionCandidate, should_use_portal_boundary_when_leader_is_inside_and_facing_away) {
    TransitionPortal portal;
    portal.objectId = 7;
    portal.destination = "Behind";
    portal.points = {
        glm::vec3(-3.0f, -4.0f, 0.0f),
        glm::vec3(-3.0f, 4.0f, 0.0f),
        glm::vec3(5.0f, 4.0f, 0.0f),
        glm::vec3(5.0f, -4.0f, 0.0f)};

    TransitionView facingAway;
    facingAway.leaderPosition = glm::vec3(0.0f);
    facingAway.cameraViewProjection = viewProjectionLookingAt(
        glm::vec3(-2.0f, 0.0f, 1.5f),
        glm::vec3(5.0f, 0.0f, 1.5f));
    EXPECT_FALSE(pickTransitionPortal({portal}, facingAway).has_value());

    TransitionView facingPortal;
    facingPortal.leaderPosition = facingAway.leaderPosition;
    facingPortal.cameraViewProjection = viewProjectionLookingAt(
        glm::vec3(2.0f, 0.0f, 1.5f),
        glm::vec3(-3.0f, 0.0f, 1.5f));
    EXPECT_TRUE(pickTransitionPortal({portal}, facingPortal).has_value());
}

TEST(TransitionCandidate, should_clear_when_camera_pans_away_without_moving) {
    auto portals = std::vector<TransitionPortal> {makePortal(7, "Exit", glm::vec2(4.0f, 0.0f))};

    TransitionView facing;
    facing.leaderPosition = glm::vec3(0.0f);
    facing.cameraViewProjection = viewProjectionLookingAt(glm::vec3(-2.0f, 0.0f, 1.5f), glm::vec3(4.0f, 0.0f, 0.5f));
    ASSERT_TRUE(pickTransitionPortal(portals, facing).has_value());

    TransitionView pannedAway;
    pannedAway.leaderPosition = facing.leaderPosition;
    pannedAway.cameraViewProjection = viewProjectionLookingAt(glm::vec3(-2.0f, 0.0f, 1.5f), glm::vec3(-2.0f, 30.0f, 0.5f));
    EXPECT_FALSE(pickTransitionPortal(portals, pannedAway).has_value());

    EXPECT_TRUE(pickTransitionPortal(portals, facing).has_value()) << "panning back must requalify";
}

TEST(TransitionCandidate, should_not_qualify_portal_out_of_presentation_range) {
    auto portals = std::vector<TransitionPortal> {makePortal(7, "Far", glm::vec2(30.0f, 0.0f))};

    TransitionView view;
    view.leaderPosition = glm::vec3(0.0f);
    view.cameraViewProjection = viewProjectionLookingAt(glm::vec3(-2.0f, 0.0f, 1.5f), glm::vec3(30.0f, 0.0f, 0.5f));

    EXPECT_FALSE(pickTransitionPortal(portals, view).has_value());
}

TEST(TransitionCandidate, should_pick_deterministic_best_candidate) {
    auto near = makePortal(9, "Near", glm::vec2(3.0f, 0.0f));
    auto far = makePortal(2, "Farther", glm::vec2(5.0f, 0.0f));

    TransitionView view;
    view.leaderPosition = glm::vec3(0.0f);
    view.cameraViewProjection = viewProjectionLookingAt(glm::vec3(-2.0f, 0.0f, 1.5f), glm::vec3(5.0f, 0.0f, 0.5f));

    auto candidate = pickTransitionPortal({far, near}, view);
    ASSERT_TRUE(candidate.has_value());
    EXPECT_EQ(candidate->destination, "Near");

    // Same geometry at equal distance: lowest object id wins.
    auto twinA = makePortal(4, "Twin A", glm::vec2(3.0f, 0.0f));
    auto twinB = makePortal(3, "Twin B", glm::vec2(3.0f, 0.0f));
    auto twin = pickTransitionPortal({twinA, twinB}, view);
    ASSERT_TRUE(twin.has_value());
    EXPECT_EQ(twin->objectId, 3u);
}

TEST(TransitionCandidate, should_handle_degenerate_portals_safely) {
    TransitionView view;
    view.leaderPosition = glm::vec3(0.0f);
    view.cameraViewProjection = viewProjectionLookingAt(glm::vec3(-2.0f, 0.0f, 1.5f), glm::vec3(4.0f, 0.0f, 0.5f));

    TransitionPortal empty;
    empty.objectId = 1;
    EXPECT_FALSE(pickTransitionPortal({empty}, view).has_value());
    EXPECT_FALSE(pickTransitionPortal({}, view).has_value());
}

TEST(TransitionDestinationDisplayName, should_remove_authored_k1_location_prefix) {
    EXPECT_EQ(transitionDestinationDisplayName("Dantooine - Courtyard"), "Courtyard");
    EXPECT_EQ(transitionDestinationDisplayName("Taris - Lower City"), "Lower City");
}

TEST(TransitionDestinationDisplayName, should_preserve_bare_destination) {
    EXPECT_EQ(transitionDestinationDisplayName("Lower City"), "Lower City");
    EXPECT_TRUE(transitionDestinationDisplayName("").empty());
}

TEST(TransitionDestinationDisplayName, should_preserve_ordinary_internal_hyphens) {
    EXPECT_EQ(transitionDestinationDisplayName("Landing-Platform"), "Landing-Platform");
    EXPECT_EQ(transitionDestinationDisplayName("Taris - Lower-City Annex"), "Lower-City Annex");
    EXPECT_EQ(transitionDestinationDisplayName("World - District - Annex"), "Annex");
}
