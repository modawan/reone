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

#include <cmath>

namespace reone {

namespace graphics {

Raycast Walkmesh::raycast(
    std::set<uint32_t> surfaces,
    const glm::vec3 &origin,
    const glm::vec3 &dir,
    float maxDistance,
    bool ignoreBackface) const {

    // A degenerate or non-finite ray hits nothing, and a NaN direction would
    // defeat the AABB slab test so the tree culls nothing. Negated `>` so a
    // NaN takes this branch.
    if (!(glm::dot(dir, dir) > 0.0f) || !std::isfinite(origin.x + origin.y + origin.z)) {
        Raycast result = {0};
        result.distance = FLT_MAX;
        result.fail = RAYCAST_NO_INTERSECTION;
        return result;
    }

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
