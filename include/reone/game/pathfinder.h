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

#include "reone/graphics/aabb.h"
#include "reone/graphics/walkmesh.h"

namespace reone {

namespace game {

/// Walkable face that is used for pathfinding.
struct Uniface {
    /// Indices into Uniwalk::vertices array.
    uint32_t vertices[3];

    /// Indices of adjecent faces. Edges [0, 1], [1, 2], [2, 0] are
    // adjecent when they are common with any other face.
    uint32_t adjecent[3];

    /// Center of a face. Used to estimate distance between two faces.
    glm::vec3 centroid;
};

/// Subdivision of a walkmesh. Uniroom associates a range of faces [begin, end)
/// to AABB.
struct Uniroom {
    uint32_t begin;
    uint32_t end;
    glm::vec3 min;
    glm::vec3 max;
};

/// Unified walkmesh, assembled from walkmeshes of all rooms in the area.
struct Uniwalk {
    std::vector<glm::vec3> vertices;
    std::vector<Uniface> faces;
    std::vector<Uniroom> rooms;
};

/// State of a face for A* algorithm.
struct AStarFace {
    enum Flag {
        Open = 1,
        Closed = 2,
    };

    Flag flag;
    uint32_t parent;
};

/// Element of a list of faces to consider next for A* algorithm.
struct AStarOpenFace {
    uint32_t index;
    float cost;
};

struct AStarContext {
    std::vector<AStarFace> state;
    std::vector<AStarOpenFace> open;
};

/// Fully calculated path.
struct AStarPath {
    std::vector<uint32_t> faces;
    glm::vec3 from;
    glm::vec3 to;
    uint32_t next;
    glm::vec3 nextPoint;

    int32_t index;
    bool active;
};

/// Handle to a calculated path.
struct Path {
    int32_t index;
};

struct Pathfinder : public boost::noncopyable {
    Uniwalk uni;
    AStarContext astar;
    std::vector<AStarPath> paths;
};

void uniwalkLoadRoom(struct Uniwalk &wm, graphics::Walkmesh &data, std::set<uint32_t> &walkableMaterial);
void uniwalkFinalize(struct Uniwalk &uni);

std::optional<Path> createPath(Pathfinder &pf, const glm::vec3 &from, const glm::vec3 &to);
bool updatePath(Pathfinder &pf, Path p, const glm::vec3 &current);
void releasePath(Pathfinder &pf, Path path);

glm::vec3 getNextPathPoint(Pathfinder &pf, Path path);
glm::vec3 getLastPathPoint(Pathfinder &pf, Path path);

glm::vec3 computeKeepoutForce(const Uniwalk &uni, const glm::vec3 &position);

} // namespace game

} // namespace reone
