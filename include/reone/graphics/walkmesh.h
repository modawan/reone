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

    std::vector<glm::vec3> vertices;
    std::vector<FaceVertices> faces;
    std::vector<glm::vec3> normals;
    std::vector<uint32_t> materials;

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
