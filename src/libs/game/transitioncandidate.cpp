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

#include "reone/game/transitioncandidate.h"

namespace reone {

namespace game {

static float segmentParameter2D(const glm::vec2 &point, const glm::vec2 &a, const glm::vec2 &b) {
    glm::vec2 ab(b - a);
    float lengthSq = glm::dot(ab, ab);
    if (lengthSq <= 0.0f) {
        return 0.0f;
    }
    return glm::clamp(glm::dot(point - a, ab) / lengthSq, 0.0f, 1.0f);
}

static float distanceToSegment2D(const glm::vec2 &point, const glm::vec2 &a, const glm::vec2 &b) {
    float t = segmentParameter2D(point, a, b);
    return glm::distance(point, a + t * (b - a));
}

static bool isInPolygon2D(const glm::vec2 &point, const std::vector<glm::vec3> &points) {
    bool inside = false;
    size_t count = points.size();
    for (size_t i = 0, j = count - 1; i < count; j = i++) {
        glm::vec2 a(points[i]);
        glm::vec2 b(points[j]);
        if ((a.y > point.y) != (b.y > point.y) &&
            point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x) {
            inside = !inside;
        }
    }
    return inside;
}

float distanceToTransitionPortal(const glm::vec2 &point, const std::vector<glm::vec3> &portalPoints) {
    if (portalPoints.empty()) {
        return std::numeric_limits<float>::max();
    }
    if (portalPoints.size() >= 3 && isInPolygon2D(point, portalPoints)) {
        return 0.0f;
    }
    float minDistance = std::numeric_limits<float>::max();
    size_t count = portalPoints.size();
    for (size_t i = 0; i < count; ++i) {
        glm::vec2 a(portalPoints[i]);
        glm::vec2 b(portalPoints[(i + 1) % count]);
        minDistance = glm::min(minDistance, distanceToSegment2D(point, a, b));
    }
    return minDistance;
}

static glm::vec3 nearestPointOnTransitionPortal(const glm::vec3 &leaderPosition,
                                                const std::vector<glm::vec3> &portalPoints) {
    glm::vec3 nearest(portalPoints.front());
    float minDistance = std::numeric_limits<float>::max();
    size_t count = portalPoints.size();
    for (size_t i = 0; i < count; ++i) {
        const glm::vec3 &a = portalPoints[i];
        const glm::vec3 &b = portalPoints[(i + 1) % count];
        float t = segmentParameter2D(glm::vec2(leaderPosition), glm::vec2(a), glm::vec2(b));
        glm::vec3 point(a + t * (b - a));
        float distance = glm::distance(glm::vec2(leaderPosition), glm::vec2(point));
        if (distance < minDistance) {
            minDistance = distance;
            nearest = point;
        }
    }
    return nearest;
}

static bool isPointInView(const glm::vec3 &point,
                          const glm::mat4 &viewProjection,
                          float horizontalNdcLimit) {
    glm::vec4 clip(viewProjection * glm::vec4(point, 1.0f));
    if (clip.w <= 0.0f) {
        return false;
    }
    float x = clip.x / clip.w;
    float y = clip.y / clip.w;
    float z = clip.z / clip.w;
    return x >= -horizontalNdcLimit && x <= horizontalNdcLimit &&
           y >= -1.0f && y <= 1.0f &&
           z >= -1.0f && z <= 1.0f;
}

bool isTransitionPortalInView(const std::vector<glm::vec3> &portalPoints,
                              const glm::vec3 &leaderPosition,
                              const glm::mat4 &cameraViewProjection,
                              float horizontalNdcLimit) {
    if (portalPoints.empty()) {
        return false;
    }
    glm::vec3 aimPoint(nearestPointOnTransitionPortal(leaderPosition, portalPoints));
    aimPoint.z += kTransitionPortalAimHeight;
    return isPointInView(aimPoint, cameraViewProjection, horizontalNdcLimit);
}

std::string transitionDestinationDisplayName(const std::string &destination) {
    static const std::string separator(" - ");
    size_t separatorPosition = destination.rfind(separator);
    if (separatorPosition == std::string::npos ||
        separatorPosition == 0 ||
        separatorPosition + separator.size() == destination.size()) {
        return destination;
    }
    return destination.substr(separatorPosition + separator.size());
}

std::optional<TransitionPortal> pickTransitionPortal(
    std::vector<TransitionPortal> portals,
    const TransitionView &view,
    float maxDistance) {

    std::optional<TransitionPortal> best;
    float bestDistance = std::numeric_limits<float>::max();

    for (auto &portal : portals) {
        float distance = distanceToTransitionPortal(glm::vec2(view.leaderPosition), portal.points);
        if (distance > maxDistance) {
            continue;
        }
        if (!isTransitionPortalInView(portal.points, view.leaderPosition, view.cameraViewProjection)) {
            continue;
        }
        bool closer = distance < bestDistance;
        bool tieBreak = distance == bestDistance && best && portal.objectId < best->objectId;
        if (closer || tieBreak) {
            bestDistance = distance;
            best = std::move(portal);
        }
    }
    return best;
}

} // namespace game

} // namespace reone
