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
#include <optional>

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
    /**
     * How many surface materials a set of them can name. A face whose
     * material is this or greater counts as its material modulo this.
     */
    static constexpr uint32_t kSurfaceMaterialCount = 32;

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

    /** An edge of a walkable face that no other walkable face of the walkmesh shares. */
    struct PerimeterEdge {
        /** Three times the face plus the edge's place in it, the place of its first vertex. */
        uint32_t index {0};
        /** The room of the area the edge leads into, by its place in the layout; -1 for none. */
        int transition {-1};
    };

    /** Where a level line crosses the perimeter. */
    struct PerimeterCrossing {
        /** How far along the line, from 0 at its start to 1 at its end. */
        float fraction {0.0f};
        /** The room of the area the crossed edge leads into, by its place in the layout; -1 for none. */
        int transition {-1};
        /** The level normal of the crossed edge, facing back along the line. */
        glm::vec2 normal {0.0f};
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
     * level segment between two points while spanning part of the height
     * band [minZ, maxZ]. A segment of no length is a point. Coordinates are
     * the walkmesh's own.
     */
    bool hasFaceWithin(
        const std::set<uint32_t> &materials,
        const glm::vec3 &from,
        const glm::vec3 &to,
        float radius,
        float minZ,
        float maxZ) const;

    /**
     * The first crossing of the perimeter by the level line between two
     * points beyond a fraction of its length, if it crosses before its end.
     * Coordinates are the walkmesh's own.
     */
    std::optional<PerimeterCrossing> findPerimeterCrossing(const glm::vec2 &from, const glm::vec2 &to, float after) const;

    /**
     * Whether a face's material is one of a set of surface materials, the
     * material taken modulo kSurfaceMaterialCount.
     */
    static bool hasSurfaceMaterial(const std::set<uint32_t> &materials, uint32_t material) {
        return materials.count(material % kSurfaceMaterialCount) > 0;
    }

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
    /** The perimeter of the walkable faces of an area walkmesh, as edge loops. */
    std::vector<PerimeterEdge> perimeter;

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
