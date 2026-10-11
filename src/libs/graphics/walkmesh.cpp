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

    if (surfaces.count(face.material) == 0) {
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

// Horizontal distance from a point to a segment.
static float distanceToSegment2D(const glm::vec2 &point, const glm::vec2 &a, const glm::vec2 &b) {
    glm::vec2 ab {b - a};
    float t = glm::clamp(glm::dot(point - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
    return glm::length(point - (a + t * ab));
}

// The signed area of the parallelogram of u-v and u-w: which side of u-v w is on.
static float sideOf(const glm::vec2 &u, const glm::vec2 &v, const glm::vec2 &w) {
    return (v.x - u.x) * (w.y - u.y) - (v.y - u.y) * (w.x - u.x);
}

bool Walkmesh::hasFaceWithin(const std::set<uint32_t> &materials,
                             const glm::vec3 &point,
                             float radius,
                             float minZ,
                             float maxZ) const {
    glm::vec2 p {point};
    for (size_t index = 0; index < faces.size(); ++index) {
        if (materials.count(this->materials[index]) == 0) {
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
        if (std::min({a.x, b.x, c.x}) > p.x + radius || std::max({a.x, b.x, c.x}) < p.x - radius ||
            std::min({a.y, b.y, c.y}) > p.y + radius || std::max({a.y, b.y, c.y}) < p.y - radius) {
            continue;
        }
        // Inside the face, or within radius of one of its edges.
        float d0 = sideOf(a, b, p);
        float d1 = sideOf(b, c, p);
        float d2 = sideOf(c, a, p);
        bool inside = (d0 >= 0.0f && d1 >= 0.0f && d2 >= 0.0f) || (d0 <= 0.0f && d1 <= 0.0f && d2 <= 0.0f);
        if (inside ||
            distanceToSegment2D(p, a, b) <= radius ||
            distanceToSegment2D(p, b, c) <= radius ||
            distanceToSegment2D(p, c, a) <= radius) {
            return true;
        }
    }
    return false;
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
