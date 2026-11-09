/*
 * Copyright (c) 2020-2023 The reone project contributors
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

#include "reone/graphics/walkmesh.h"

#include <algorithm>
#include <array>
#include <limits>
#include <stack>

#include <boost/container/small_vector.hpp>

namespace reone {

namespace graphics {

Raycast Walkmesh::raycast(
    const std::set<uint32_t> &surfaces,
    const glm::vec3 &origin,
    const glm::vec3 &dir,
    float maxDistance,
    bool ignoreBackface) const {

    // For area walkmeshes, find intersection via AABB tree
    if (_rootAabb) {
        return raycastAABB(surfaces, origin, dir, maxDistance, ignoreBackface);
    }

    Raycast minResult = {0};
    minResult.distance = FLT_MAX;

    if (faces.empty()) {
        minResult.fail = RAYCAST_NO_INTERSECTION;
        return minResult;
    }

    // For placeable and door walkmeshes, test all faces for intersection
    for (uint32_t i = 0; i < faces.size(); ++i) {
        Raycast result = raycastFace(surfaces, getFace(i), origin, dir, maxDistance, ignoreBackface);
        if (result.fail) {
            if (!minResult.fail && minResult.distance == FLT_MAX) {
                minResult.fail = result.fail;
            }
            continue;
        }
        if (result.distance < minResult.distance) {
            minResult = result;
        }
    }

    return minResult;
}

Raycast Walkmesh::raycastAABB(
    const std::set<uint32_t> &surfaces,
    const glm::vec3 &origin,
    const glm::vec3 &dir,
    float maxDistance,
    bool ignoreBackface) const {

    std::stack<AABB *> aabbs;
    aabbs.push(_rootAabb.get());

    glm::vec3 invDir = 1.0f / dir;
    Raycast minResult = {0};
    minResult.distance = FLT_MAX;

    bool foundFace = false;
    while (!aabbs.empty()) {
        auto aabb = aabbs.top();
        aabbs.pop();

        // Test ray/face intersection for tree leafs
        if (aabb->faceIdx != -1) {
            foundFace = true;
            Raycast result = raycastFace(surfaces, getFace(aabb->faceIdx), origin, dir, maxDistance, ignoreBackface);
            if (result.fail) {
                if (!minResult.fail && minResult.distance == FLT_MAX) {
                    minResult.fail = result.fail;
                }
                continue;
            }
            if (result.distance < minResult.distance) {
                minResult = result;
            }
        }

        // Test ray/AABB intersection
        float distance = 0.0f;
        if (!aabb->value.raycast(origin, invDir, maxDistance, distance)) {
            continue;
        }

        // Find intersection with child AABB nodes
        if (aabb->left) {
            aabbs.push(aabb->left.get());
        }
        if (aabb->right) {
            aabbs.push(aabb->right.get());
        }
    }

    if (!foundFace) {
        minResult.fail = RAYCAST_NO_INTERSECTION;
    }

    return minResult;
}

Raycast Walkmesh::raycastFace(
    const std::set<uint32_t> &surfaces,
    const Face &face,
    const glm::vec3 &origin,
    const glm::vec3 &dir,
    float maxDistance,
    bool ignoreBackface) const {

    Raycast result = {0};

    if (!hasSurfaceMaterial(surfaces, face.material)) {
        result.fail = RAYCAST_NO_MATERIAL;
        return result;
    }

    const glm::vec3 &p0 = face.vertices[0];
    const glm::vec3 &p1 = face.vertices[1];
    const glm::vec3 &p2 = face.vertices[2];

    glm::vec2 baryPosition(0.0f);
    float distance = 0.0f;

    if (glm::intersectRayTriangle(origin, dir, p0, p1, p2, baryPosition, distance) && distance > 0.0f && distance < maxDistance) {
        result.face = face.index;
        result.distance = distance;
        if (ignoreBackface && glm::dot(face.normal, dir) > 0) {
            result.fail = RAYCAST_FLIPPED_NORMAL;
        }
    } else {
        result.fail = RAYCAST_NO_INTERSECTION;
    }
    return result;
}

// Horizontal distance from a point to a segment; a segment of no length is
// its end point.
static float distanceToSegment2D(const glm::vec2 &point, const glm::vec2 &a, const glm::vec2 &b) {
    glm::vec2 ab {b - a};
    const float length2 = glm::dot(ab, ab);
    float t = length2 > 0.0f ? glm::clamp(glm::dot(point - a, ab) / length2, 0.0f, 1.0f) : 0.0f;
    return glm::length(point - (a + t * ab));
}

// The signed area of the parallelogram of u-v and u-w: which side of u-v w is on.
static float sideOf(const glm::vec2 &u, const glm::vec2 &v, const glm::vec2 &w) {
    return (v.x - u.x) * (w.y - u.y) - (v.y - u.y) * (w.x - u.x);
}

// Whether two segments cross, horizontally, each passing between the ends of
// the other.
static bool segmentsCross2D(const glm::vec2 &a, const glm::vec2 &b, const glm::vec2 &c, const glm::vec2 &d) {
    const float c1 = sideOf(a, b, c);
    const float c2 = sideOf(a, b, d);
    const float a1 = sideOf(c, d, a);
    const float a2 = sideOf(c, d, b);
    return ((c1 > 0.0f && c2 < 0.0f) || (c1 < 0.0f && c2 > 0.0f)) &&
           ((a1 > 0.0f && a2 < 0.0f) || (a1 < 0.0f && a2 > 0.0f));
}

// Horizontal distance between two segments.
static float distanceBetweenSegments2D(const glm::vec2 &a, const glm::vec2 &b, const glm::vec2 &c, const glm::vec2 &d) {
    if (segmentsCross2D(a, b, c, d)) {
        return 0.0f;
    }
    return std::min({distanceToSegment2D(a, c, d),
                     distanceToSegment2D(b, c, d),
                     distanceToSegment2D(c, a, b),
                     distanceToSegment2D(d, a, b)});
}

bool Walkmesh::hasFaceWithin(const std::set<uint32_t> &materials,
                             const glm::vec3 &from,
                             const glm::vec3 &to,
                             float radius,
                             float minZ,
                             float maxZ) const {
    glm::vec2 p {from};
    glm::vec2 q {to};
    for (size_t index = 0; index < faces.size(); ++index) {
        if (!hasSurfaceMaterial(materials, this->materials[index])) {
            continue;
        }
        Face face = getFace(static_cast<uint32_t>(index));
        float faceMinZ = std::min({face.vertices[0].z, face.vertices[1].z, face.vertices[2].z});
        float faceMaxZ = std::max({face.vertices[0].z, face.vertices[1].z, face.vertices[2].z});
        if (faceMaxZ < minZ || faceMinZ > maxZ) {
            continue;
        }
        glm::vec2 a {face.vertices[0]};
        glm::vec2 b {face.vertices[1]};
        glm::vec2 c {face.vertices[2]};
        if (std::min({a.x, b.x, c.x}) > std::max(p.x, q.x) + radius || std::max({a.x, b.x, c.x}) < std::min(p.x, q.x) - radius ||
            std::min({a.y, b.y, c.y}) > std::max(p.y, q.y) + radius || std::max({a.y, b.y, c.y}) < std::min(p.y, q.y) - radius) {
            continue;
        }
        // The segment starts inside the face, or passes within radius of one
        // of its edges.
        float d0 = sideOf(a, b, p);
        float d1 = sideOf(b, c, p);
        float d2 = sideOf(c, a, p);
        bool inside = (d0 >= 0.0f && d1 >= 0.0f && d2 >= 0.0f) || (d0 <= 0.0f && d1 <= 0.0f && d2 <= 0.0f);
        if (inside ||
            distanceBetweenSegments2D(p, q, a, b) <= radius ||
            distanceBetweenSegments2D(p, q, b, c) <= radius ||
            distanceBetweenSegments2D(p, q, c, a) <= radius) {
            return true;
        }
    }
    return false;
}

std::optional<Walkmesh::PerimeterCrossing> Walkmesh::findPerimeterCrossing(const glm::vec2 &from, const glm::vec2 &to, float after) const {
    const glm::vec2 line {to - from};
    std::optional<PerimeterCrossing> first;
    for (const PerimeterEdge &edge : perimeter) {
        const FaceVertices &face = faces[edge.index / 3];
        const uint32_t place = edge.index % 3;
        const glm::vec2 a {vertices[face.indices[place]]};
        const glm::vec2 b {vertices[face.indices[(place + 1) % 3]]};
        const glm::vec2 side {b - a};
        const float denominator = line.x * side.y - line.y * side.x;
        if (denominator == 0.0f) {
            continue;
        }
        const glm::vec2 toEdge {a - from};
        const float fraction = (toEdge.x * side.y - toEdge.y * side.x) / denominator;
        const float along = (toEdge.x * line.y - toEdge.y * line.x) / denominator;
        if (along < 0.0f || along > 1.0f || fraction <= after || fraction > 1.0f) {
            continue;
        }
        if (!first || fraction < first->fraction) {
            glm::vec2 normal {glm::normalize(glm::vec2(side.y, -side.x))};
            if (glm::dot(normal, line) > 0.0f) {
                normal = -normal;
            }
            first = PerimeterCrossing {fraction, edge.transition, normal};
        }
    }
    return first;
}

// Where two level segments cross, ends included, with the height taken along
// the second; empty when they do not cross or run parallel.
static std::optional<glm::vec3> crossingOf(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec3 &d) {
    const glm::vec2 u {b - a};
    const glm::vec2 v {d - c};
    const float denominator = u.x * v.y - u.y * v.x;
    if (denominator == 0.0f) {
        return std::nullopt;
    }
    const glm::vec2 w {c - a};
    const float s = (w.x * v.y - w.y * v.x) / denominator;
    const float t = (w.x * u.y - w.y * u.x) / denominator;
    if (s < 0.0f || s > 1.0f || t < 0.0f || t > 1.0f) {
        return std::nullopt;
    }
    return glm::vec3 {glm::vec2(a) + u * s, c.z + (d.z - c.z) * t};
}

// The points of a level segment at a distance from a centre.
static std::vector<glm::vec2> circleCrossings(const glm::vec2 &centre, float radius, const glm::vec2 &a, const glm::vec2 &b) {
    const glm::vec2 ab {b - a};
    const glm::vec2 fromCentre {a - centre};
    const float qa = glm::dot(ab, ab);
    const float qb = 2.0f * glm::dot(ab, fromCentre);
    const float qc = glm::dot(fromCentre, fromCentre) - radius * radius;
    const float discriminant = qb * qb - 4.0f * qa * qc;
    std::vector<glm::vec2> points;
    if (qa == 0.0f || discriminant < 0.0f) {
        return points;
    }
    const float root = std::sqrt(discriminant);
    for (float t : {(-qb + root) / (2.0f * qa), (-qb - root) / (2.0f * qa)}) {
        if (t >= 0.0f && t <= 1.0f) {
            points.push_back(a + ab * t);
        }
        if (discriminant == 0.0f) {
            break;
        }
    }
    return points;
}

// The level unit vector of a vector, or none for an upright one.
static glm::vec2 levelUnit(const glm::vec2 &vector) {
    const float length = glm::length(vector);
    return length > 0.0f ? vector / length : glm::vec2(0.0f);
}

std::optional<Walkmesh::Contact> Walkmesh::sweep(const std::set<uint32_t> &materials,
                                                 const glm::vec3 &from,
                                                 const glm::vec3 &to,
                                                 float radius,
                                                 float minZ,
                                                 float maxZ) const {
    // The faces looked at are those within this much of the swept region.
    static constexpr float kBoundsPad = 0.01f;
    // The disc round the end meets edges this much wider than the walker,
    // and a walker meeting a corner stops this much nearer than its radius.
    static constexpr float kCornerSlack = 0.001f;
    // A walker meeting an edge stops this much further back.
    static constexpr float kEdgeClearance = 0.03f;

    const glm::vec2 start {from};
    const glm::vec2 end {to};
    const float length = glm::length(end - start);
    if (length == 0.0f || minZ > maxZ) {
        return std::nullopt;
    }
    const glm::vec2 dir {(end - start) / length};
    const glm::vec2 left {-dir.y, dir.x};
    // The strip's sides and its start: left of the start to left of the end,
    // right of the end to right of the start, right of the start to left of
    // it. The disc round the end stands for its far side.
    const std::array<std::pair<glm::vec3, glm::vec3>, 3> borders {{
        {glm::vec3(start + left * radius, 0.0f), glm::vec3(end + left * radius, 0.0f)},
        {glm::vec3(end - left * radius, 0.0f), glm::vec3(start - left * radius, 0.0f)},
        {glm::vec3(start - left * radius, 0.0f), glm::vec3(start + left * radius, 0.0f)},
    }};
    const glm::vec2 farEnd {end + dir * radius};
    glm::vec2 boundsMin {std::numeric_limits<float>::max()};
    glm::vec2 boundsMax {-std::numeric_limits<float>::max()};
    for (const glm::vec2 &corner : {start + left * radius, start - left * radius, farEnd + left * radius, farEnd - left * radius}) {
        boundsMin = glm::min(boundsMin, corner - kBoundsPad);
        boundsMax = glm::max(boundsMax, corner + kBoundsPad);
    }
    const auto along = [&](const glm::vec2 &point) { return glm::dot(point - start, dir); };

    float nearest = std::numeric_limits<float>::max();
    std::optional<uint32_t> nearestFace;
    uint32_t nearestPlace = 0;
    bool nearestIsVertex = false;
    const Contact starting {ContactKind::StartInside, 0.0f, glm::vec3(-dir, 0.0f)};
    // Looks at one face, keeping where the walker first meets it; whether the
    // walker starts over it.
    const auto startsOver = [&](uint32_t index) {
        if (!hasSurfaceMaterial(materials, this->materials[index])) {
            return false;
        }
        const Face face = getFace(index);
        const glm::vec3 *v = face.vertices;
        if (std::max({v[0].z, v[1].z, v[2].z}) < minZ || std::min({v[0].z, v[1].z, v[2].z}) > maxZ ||
            std::max({v[0].x, v[1].x, v[2].x}) < boundsMin.x || std::min({v[0].x, v[1].x, v[2].x}) > boundsMax.x ||
            std::max({v[0].y, v[1].y, v[2].y}) < boundsMin.y || std::min({v[0].y, v[1].y, v[2].y}) > boundsMax.y) {
            return false;
        }
        // A corner in the strip or the disc, within the band.
        bool covered[3];
        for (uint32_t place = 0; place < 3; ++place) {
            const glm::vec2 point {v[place]};
            const float distance = along(point);
            covered[place] = distance >= 0.0f && distance <= length
                                 ? std::abs(glm::dot(point - start, left)) <= radius
                                 : glm::dot(point - end, point - end) <= radius * radius;
            if (covered[place] && v[place].z >= minZ && v[place].z <= maxZ && distance < nearest) {
                nearest = distance;
                nearestFace = index;
                nearestPlace = place;
                nearestIsVertex = true;
            }
        }
        // An edge, not wholly covered, crossing the strip's sides or start,
        // or meeting the disc ahead of the start.
        for (uint32_t place = 0; place < 3; ++place) {
            const uint32_t next = (place + 1) % 3;
            if (covered[place] && covered[next]) {
                continue;
            }
            const auto record = [&](float distance) {
                if (distance < nearest) {
                    nearest = distance;
                    nearestFace = index;
                    nearestPlace = place;
                    nearestIsVertex = false;
                }
            };
            for (const auto &[a, b] : borders) {
                if (auto crossing = crossingOf(v[place], v[next], a, b)) {
                    record(along(glm::vec2(*crossing)));
                }
            }
            for (const glm::vec2 &point : circleCrossings(end, radius + kCornerSlack, glm::vec2(v[place]), glm::vec2(v[next]))) {
                const float distance = along(point);
                if (distance >= 0.0f) {
                    record(distance);
                }
            }
        }
        // A start over a face whose front faces up.
        const glm::vec2 a {v[0]};
        const glm::vec2 b {v[1]};
        const glm::vec2 c {v[2]};
        return sideOf(a, b, start) >= 0.0f && sideOf(b, c, start) >= 0.0f && sideOf(c, a, start) >= 0.0f;
    };
    // An area walkmesh's box tree, which holds every face, gives the faces
    // whose boxes the swept region meets, looked at in order; any other
    // walkmesh is looked at whole.
    if (_rootAabb) {
        boost::container::small_vector<uint32_t, 64> candidates;
        boost::container::small_vector<const AABB *, 64> nodes;
        nodes.push_back(_rootAabb.get());
        while (!nodes.empty()) {
            const AABB *node = nodes.back();
            nodes.pop_back();
            const glm::vec3 &low = node->value.min();
            const glm::vec3 &high = node->value.max();
            if (high.x < boundsMin.x || low.x > boundsMax.x || high.y < boundsMin.y || low.y > boundsMax.y ||
                high.z < minZ || low.z > maxZ) {
                continue;
            }
            if (node->faceIdx != -1) {
                candidates.push_back(static_cast<uint32_t>(node->faceIdx));
            }
            for (const auto &child : {node->left, node->right}) {
                if (child) {
                    nodes.push_back(child.get());
                }
            }
        }
        std::sort(candidates.begin(), candidates.end());
        for (const uint32_t index : candidates) {
            if (startsOver(index)) {
                return starting;
            }
        }
    } else {
        for (uint32_t index = 0; index < faces.size(); ++index) {
            if (startsOver(index)) {
                return starting;
            }
        }
    }

    if (!nearestFace) {
        return std::nullopt;
    }

    const Face face = getFace(*nearestFace);
    const glm::vec3 &v0 = face.vertices[nearestPlace];
    const glm::vec3 &v1 = face.vertices[(nearestPlace + 1) % 3];
    const glm::vec3 &v2 = face.vertices[(nearestPlace + 2) % 3];
    const bool upright = v0.x == v1.x && v0.y == v1.y;
    const auto clamped = [&](float distance) { return glm::clamp(distance, 0.0f, length); };
    if (nearestIsVertex || (upright && v0.z != v1.z)) {
        // The corner's normal halves the normals of the two edges that meet
        // there; the walker stops its radius short of the corner.
        const glm::vec2 out1 {levelUnit(glm::vec2(-(v0 - v1).y, (v0 - v1).x))};
        const glm::vec2 out2 {levelUnit(glm::vec2(-(v2 - v0).y, (v2 - v0).x))};
        glm::vec3 normal {levelUnit(out1 + out2), 0.0f};
        if (face.normal.z <= 0.0f && glm::dot(normal, face.normal) < 0.0f) {
            normal = -normal;
        }
        return Contact {ContactKind::Corner, clamped(nearest + kCornerSlack - radius), normal};
    }
    const glm::vec2 edge {glm::vec2(v1) - glm::vec2(v0)};
    const glm::vec2 segment {end - start};
    const float across = edge.y * segment.x - edge.x * segment.y;
    if (across == 0.0f) {
        // An edge the walk runs along stops the walker where it starts,
        // facing back along the walk.
        return Contact {ContactKind::Corner, 0.0f, glm::vec3(-dir, 0.0f)};
    }
    // The walker stops where its disc, short of the line through the edge,
    // still clears it.
    const glm::vec2 unit {levelUnit(glm::vec2(v0) - glm::vec2(v1))};
    const float cotangent = glm::dot(segment, unit) / (unit.x * segment.y - unit.y * segment.x);
    const float back = std::sqrt(radius * radius + cotangent * radius * cotangent * radius) + kEdgeClearance;
    const glm::vec2 toEdge {glm::vec2(v0) - start};
    const float meeting = (toEdge.x * edge.y - toEdge.y * edge.x) / (dir.x * edge.y - dir.y * edge.x);
    return Contact {ContactKind::Edge, clamped(meeting - back), glm::vec3(-unit.y, unit.x, 0.0f)};
}

// The height of a face's plane at a point over it, edges included.
static std::optional<float> heightOver(const Walkmesh::Face &face, const glm::vec2 &point) {
    const glm::vec3 *v = face.vertices;
    const float d0 = sideOf(glm::vec2(v[0]), glm::vec2(v[1]), point);
    const float d1 = sideOf(glm::vec2(v[1]), glm::vec2(v[2]), point);
    const float d2 = sideOf(glm::vec2(v[2]), glm::vec2(v[0]), point);
    const bool over = (d0 >= 0.0f && d1 >= 0.0f && d2 >= 0.0f) || (d0 <= 0.0f && d1 <= 0.0f && d2 <= 0.0f);
    if (!over || face.normal.z == 0.0f) {
        return std::nullopt;
    }
    const float distance = -glm::dot(face.normal, v[0]);
    return -(face.normal.x * point.x + face.normal.y * point.y + distance) / face.normal.z;
}

std::optional<Walkmesh::HeightBand> Walkmesh::walkBand(const std::set<uint32_t> &surfaces, const glm::vec3 &from, const glm::vec3 &to) const {
    // The start face is looked for from this high down to as far below.
    static constexpr float kColumnReach = 1000.0f;
    static constexpr float kRetryShift = 0.001f;
    // A walk crossing more faces than this is not followed.
    static constexpr int kMaxFaces = 512;

    const auto faceUnder = [&](const glm::vec2 &point) -> std::optional<uint32_t> {
        const auto found = raycast(surfaces, glm::vec3(point, kColumnReach), glm::vec3(0.0f, 0.0f, -1.0f), 2.0f * kColumnReach, /*ignoreBackface=*/true);
        return found.fail == RAYCAST_OK ? std::make_optional(found.face) : std::nullopt;
    };
    auto face = faceUnder(glm::vec2(from));
    if (!face && to != from) {
        face = faceUnder(glm::vec2(from) + glm::vec2(glm::normalize(to - from)) * kRetryShift);
    }
    if (!face) {
        return std::nullopt;
    }
    const auto neighbour = [&](uint32_t index, uint32_t place) {
        const size_t slot = 3 * static_cast<size_t>(index) + place;
        return slot < adjacency.size() && adjacency[slot] >= 0 ? adjacency[slot] / 3 : -1;
    };

    glm::vec3 point {from};
    if (auto height = heightOver(getFace(*face), glm::vec2(from))) {
        point.z = *height;
    }
    HeightBand band {point.z, point.z};
    const auto widen = [&band](float height) {
        band.low = std::min(band.low, height);
        band.high = std::max(band.high, height);
    };
    int current = static_cast<int>(*face);
    for (int crossed = 0; crossed < kMaxFaces; ++crossed) {
        widen(point.z);
        const Face here = getFace(static_cast<uint32_t>(current));
        const glm::vec3 *v = here.vertices;
        bool inside[3];
        for (uint32_t place = 0; place < 3; ++place) {
            inside[place] = sideOf(glm::vec2(v[place]), glm::vec2(v[(place + 1) % 3]), glm::vec2(to)) >= 0.0f;
        }
        if (inside[0] && inside[1] && inside[2]) {
            widen(heightOver(here, glm::vec2(to)).value_or(to.z));
            return band;
        }
        // The walk leaves the face across the first edge, last to first, that
        // the end lies beyond and the walk crosses.
        bool left = false;
        for (int place = 2; place >= 0 && !left; --place) {
            if (inside[place]) {
                continue;
            }
            if (auto crossing = crossingOf(point, to, v[place], v[(place + 1) % 3])) {
                current = neighbour(static_cast<uint32_t>(current), static_cast<uint32_t>(place));
                point = *crossing;
                left = true;
            }
        }
        if (!left) {
            return std::nullopt;
        }
        if (current < 0) {
            widen(point.z);
            return band;
        }
    }
    return std::nullopt;
}

void Walkmesh::verify() const {
    assert(faces.size() == normals.size());
    assert(faces.size() == materials.size());
    for (const FaceVertices &face : faces) {
        for (uint32_t index : face.indices) {
            assert(vertices.size() > index);
        }
    }
}

} // namespace graphics

} // namespace reone
