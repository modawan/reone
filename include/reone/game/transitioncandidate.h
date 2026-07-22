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

namespace reone {

namespace game {

/**
 * Maximum distance from the party leader to a transition portal at which the
 * destination banner is shown. Mirrors the presentation band that KotOR.js
 * uses for door transition lines; not a verified original-engine constant.
 */
constexpr float kMaxTransitionPresentationDistance = 5.0f;

/**
 * Maximum horizontal displacement from the screen centre, in NDC, at which a
 * portal is considered camera-facing. A manual parity-tuning value rather
 * than a verified original-engine constant.
 */
constexpr float kTransitionViewHorizontalNdcLimit = 0.6f;

/// Height above portal ground used for the camera-facing aim point.
constexpr float kTransitionPortalAimHeight = 1.5f;

/// World-space presentation geometry of one module transition.
struct TransitionPortal {
    uint32_t objectId {0};
    std::string destination;
    std::vector<glm::vec3> points; /**< polygon vertices, world space */
};

/// Party leader and gameplay camera state a candidate is evaluated against.
struct TransitionView {
    glm::vec3 leaderPosition {0.0f};
    glm::mat4 cameraViewProjection {1.0f};
};

/**
 * Distance from a point to the portal polygon in the XY plane: zero when the
 * point is inside the polygon, distance to the nearest edge otherwise.
 */
float distanceToTransitionPortal(const glm::vec2 &point, const std::vector<glm::vec3> &portalPoints);

/**
 * Whether the portal boundary's nearest point to the leader, raised to body
 * height, falls inside the viewport and central horizontal presentation band.
 * The leader itself is never used as the aim point when already inside the
 * polygon, because that would remain centred while looking away from the exit.
 */
bool isTransitionPortalInView(const std::vector<glm::vec3> &portalPoints,
                              const glm::vec3 &leaderPosition,
                              const glm::mat4 &cameraViewProjection,
                              float horizontalNdcLimit = kTransitionViewHorizontalNdcLimit);

/**
 * Convert an authored transition destination into the concise banner label.
 * K1 commonly prefixes a place with its planet using " - "; K2 generally
 * stores the concise label already. Unprefixed labels are returned unchanged.
 */
std::string transitionDestinationDisplayName(const std::string &destination);

/**
 * Pick the presentation candidate: the nearest portal within presentation
 * distance of the leader that is also in the camera view. Ties are broken by
 * object id for determinism. Purely observational: no game state is touched.
 */
std::optional<TransitionPortal> pickTransitionPortal(
    std::vector<TransitionPortal> portals,
    const TransitionView &view,
    float maxDistance = kMaxTransitionPresentationDistance);

} // namespace game

} // namespace reone
