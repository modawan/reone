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
#include <functional>

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
    /** Region of each face: faces joined through shared edges share one. */
    std::vector<uint32_t> regions;
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
    /** The face the funnel has reached. */
    uint32_t next;
    /**
     * The points of the walk, walked straight one after another, and the index
     * of the one being walked to. While the path follows its faces they are
     * the point last left behind and the one the funnel gives next. Once a way
     * round a creature has been spliced into the path, they run on to the
     * destination and are never planned again.
     */
    std::vector<glm::vec3> points;
    uint32_t pointNext {1};
    /** The path takes each next point from its faces, rather than walking its points. */
    bool followsFaces {true};

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
/** Whether the walkmesh joins two points, without holding a path. */
bool pathExists(const Pathfinder &pf, const glm::vec3 &from, const glm::vec3 &to);
bool updatePath(Pathfinder &pf, Path p, const glm::vec3 &current);
void releasePath(Pathfinder &pf, Path path);

glm::vec3 getNextPathPoint(Pathfinder &pf, Path path);
glm::vec3 getLastPathPoint(Pathfinder &pf, Path path);

/**
 * The points of a path, from the one last left behind to the destination, and
 * in \p next the index of the one being walked to.
 */
std::vector<glm::vec3> pathPoints(Pathfinder &pf, Path path, uint32_t &next);
/** Whether the path walks explicit points rather than following its faces. */
bool followsPathPoints(Pathfinder &pf, Path path);
/** Walk explicit points from now on, heading for the one at \p next. */
void setPathPoints(Pathfinder &pf, Path path, std::vector<glm::vec3> points, uint32_t next);

// Planning a way round a creature that blocks a walk. The creature is ringed
// by a hexagon; the walk leaves its path where the path enters the hexagon,
// follows the hexagon's edges and rejoins the path where it leaves.

/// Space kept beyond both creatures' personal spaces.
constexpr float kAvoidanceSpace = 0.2f;

/// Corners of the hexagon, clockwise seen from above. Edge e joins corner e to
/// corner e + 1.
struct AvoidanceHex {
    std::array<glm::vec3, 6> v;
};

/// Where a path crosses the hexagon: the edge, the index of the path point
/// ending the crossing segment, and the crossing point. A way in taken from a
/// corner has the corner for its edge and the point last left for its index.
struct AvoidanceCrossing {
    int edge {-1};
    int index {-1};
    glm::vec3 point {0.0f};
};

/**
 * The hexagon round \p center whose edges lie \p inradius from it. Its first
 * corner lies back along \p moverDir, at the height of the centre; the others
 * stand on the ground as \p height gives it.
 */
AvoidanceHex computeAvoidanceHex(const glm::vec3 &center, const glm::vec3 &moverDir, float inradius,
                                 const std::function<float(const glm::vec2 &)> &height);
/** Whether a point lies inside the hexagon or on an edge, seen from above. */
bool isPointInAvoidanceHex(const AvoidanceHex &hex, const glm::vec3 &point);
/** The corner nearest a point; a tie goes to the lower index. */
int closestAvoidanceHexCorner(const AvoidanceHex &hex, const glm::vec3 &point);
/**
 * Where the path, walked to its point \p next, enters and leaves the hexagon:
 * the last two crossings over the whole path, each segment crossing at most
 * once. A single crossing is left through; the way in is then the corner
 * nearest the point last left.
 */
bool findAvoidanceEntryAndExit(const AvoidanceHex &hex, const std::vector<glm::vec3> &points, uint32_t next,
                               AvoidanceCrossing &entry, AvoidanceCrossing &exit);
/**
 * The way along the hexagon from the entry to the exit: clockwise, keeping the
 * creature on the right, or counter-clockwise when \p left is set.
 */
std::vector<glm::vec3> findAvoidanceWay(const AvoidanceHex &hex, const AvoidanceCrossing &entry,
                                        const AvoidanceCrossing &exit, bool left);
/**
 * Put the way in place of the path points from the entry's up to the exit's,
 * that one kept unless both are the same, and bring the point being walked to
 * back to the way's start when it lay beyond.
 */
void insertAvoidanceWay(std::vector<glm::vec3> &points, uint32_t &next, int entryIndex, int exitIndex,
                        const std::vector<glm::vec3> &way);

glm::vec3 computeKeepoutForce(const Uniwalk &uni, const glm::vec3 &position);

} // namespace game

} // namespace reone
