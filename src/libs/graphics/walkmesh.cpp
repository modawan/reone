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

namespace reone {

namespace graphics {

Raycast Walkmesh::raycast(
    std::set<uint32_t> surfaces,
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
    std::set<uint32_t> surfaces,
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
    std::set<uint32_t> surfaces,
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

bool Walkmesh::contains(const glm::vec2 &point) const {
    if (!_rootAabb) {
        return false;
    }
    return _rootAabb->value.contains(point);
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
