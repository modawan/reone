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

#pragma once

#include <array>

#include "aabb.h"
#include "types.h"

namespace reone {

namespace graphics {

enum RaycastFail {
    RAYCAST_OK = 0,
    RAYCAST_NO_INTERSECTION,
    RAYCAST_NO_MATERIAL,
    RAYCAST_FLIPPED_NORMAL,
};

struct Raycast {
    uint32_t face;
    float distance;
    RaycastFail fail;
};

class Walkmesh : boost::noncopyable {
public:
    struct Face {
        uint32_t index {0};
        uint32_t material {0};
        glm::vec3 vertices[3];
        glm::vec3 normal {0.0f};
    };

    struct FaceVertices {
        uint32_t indices[3];
    };

    struct AABB {
        graphics::AABB value;
        int faceIdx {-1};
        std::shared_ptr<AABB> left;
        std::shared_ptr<AABB> right;
    };

    /**
     * @return index of the face that the ray intersects, distance from the
     * origin point to the intersection, and an error code if there is no
     * intersection.
     */
    Raycast raycast(
        std::set<uint32_t> walkcheckSurfaces,
        const glm::vec3 &origin,
        const glm::vec3 &dir,
        float maxDistance,
        bool ignoreBackface) const;

    bool contains(const glm::vec2 &point) const;

    /**
     * Whether a face of one of the given materials lies within radius of the
     * point horizontally while spanning part of the height band [minZ, maxZ].
     * Coordinates are the walkmesh's own.
     */
    bool hasFaceWithin(
        const std::set<uint32_t> &materials,
        const glm::vec3 &point,
        float radius,
        float minZ,
        float maxZ) const;

    bool isAreaWalkmesh() const { return _area; }

    Face getFace(uint32_t index) const {
        FaceVertices face = faces[index];
        return Face {
            index,
            materials[index],
            {
                vertices[face.indices[0]],
                vertices[face.indices[1]],
                vertices[face.indices[2]],
            },
            normals[index],
        };
    }

    void setRootAABB(std::shared_ptr<AABB> aabb) {
        _rootAabb = std::move(aabb);
    }

    void verify() const;

    /** Vertices in the owner's coordinates, a door or placeable walkmesh's with its position offset applied. */
    std::vector<glm::vec3> vertices;
    std::vector<FaceVertices> faces;
    std::vector<glm::vec3> normals;
    std::vector<uint32_t> materials;

    /**
     * The two points a door or placeable is used from, each given twice: as
     * placed relative to the object and as placed absolutely. Both are in the
     * walkmesh's own coordinates, before the position offset.
     */
    std::array<glm::vec3, 2> relativeUsePositions {glm::vec3(0.0f), glm::vec3(0.0f)};
    std::array<glm::vec3, 2> absoluteUsePositions {glm::vec3(0.0f), glm::vec3(0.0f)};

    /**
     * Offset of a door or placeable walkmesh's own coordinates from the object
     * it belongs to. The vertices carry it; the use positions do not.
     */
    glm::vec3 position {0.0f};

private:
    std::shared_ptr<AABB> _rootAabb;
    bool _area {false};

    Raycast raycastAABB(
        std::set<uint32_t> surfaces,
        const glm::vec3 &origin,
        const glm::vec3 &dir,
        float maxDistance,
        bool ignoreBackface) const;

    Raycast raycastFace(
        std::set<uint32_t> surfaces,
        const Walkmesh::Face &face,
        const glm::vec3 &origin,
        const glm::vec3 &dir,
        float maxDistance,
        bool ignoreBackface) const;

    friend class BwmReader;
};

} // namespace graphics

} // namespace reone
