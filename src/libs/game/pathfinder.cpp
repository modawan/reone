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

/**
   == Summary

   Pathfinding is implemented as a collection of algorithms:

   1. Global pathfinding algorithm builds a coarse-grained path from A to B as a
   sequence of walkmesh faces. It ensures that a path exists, but makes no
   effort to make it "natural" from the player perspective.

   2. Funnel algorithm takes a sequence of faces from the global pathfinding,
   and attempts to make a straight-line path through it.

   3. Steering behaviour takes a direction from the funnel algorithm, and turns
      it into a "driving" force. It then calculates and combines other forces:

      - "keepout" force to steer away from borders and corners
      - "stuck" force to recover from pathfinding failures.

     The combined force is then integrated to smooth changes of direction and make
     the path more natural.

   == Data structures

   The primary input for these algorithms is a Uniwalk. It is a combined mesh of
   all room walkmeshes of an area. Walkmeshes are loaded into an area Uniwalk,
   and then combined using uniwalkFinalize. Uniwalk still tracks what faces
   belong to what room, and computes room AABBs to make spatial queries faster.

   Pathfinder struct keeps the Uniwalk, global pathfinding context, and a list
   of active paths. Global pathfinding is expensive, so the implementation puts
   an arbitrary limit on the maximum number of active paths. AStarPath objects
   are re-used to avoid frequent re-allocation.

   == Omissions

   1. Dynamic objects (creatures) are not handled. Each creature has a "personal
   space" radius that must be taken into account for pathfinding.

   2. Static objects that do not have a carve-out for them in the room walkmesh
      are not reflected in the Uniwalk yet. Each object has it is own
      "non-walkable" mesh, which must be subtracted from the room mesh.
 */

#include "reone/game/pathfinder.h"
#include "reone/game/debug.h"
#include "reone/scene/drawdebug.h"

namespace reone {

namespace game {

static const uint32_t kTriangleEdges[3][2] = {{0, 1}, {1, 2}, {2, 0}};

// Import a walkmesh of a single room into Uniwalk structure. Once all rooms are
// imported, call uniwalkFinalize to establish connections between rooms.
void uniwalkLoadRoom(struct Uniwalk &uni, graphics::Walkmesh &data, std::set<uint32_t> &walkableMaterial) {
    // Base of the vertex array for this room.
    uint32_t beginVertex = uni.vertices.size();

    // Copy all vertices without de-duplicating them with the previously loaded
    // meshes. Calculate AABB.
    glm::vec3 min = {FLT_MAX, FLT_MAX, FLT_MAX};
    glm::vec3 max = {FLT_MIN, FLT_MIN, FLT_MIN};
    uni.vertices.reserve(uni.vertices.size() + data.vertices.size());
    for (glm::vec3 v : data.vertices) {
        uni.vertices.push_back(v);
        min = glm::min(min, v);
        max = glm::max(max, v);
    }

    // Expand AABB a bit to avoid rounding errors.
    glm::vec3 expand = {0.1f, 0.1f, 0.1f};
    min -= expand;
    max += expand;

    // Base of the face array for this room.
    uint32_t beginFace = uni.faces.size();

    // Copy all walkable faces.
    uni.faces.reserve(uni.faces.size() + data.faces.size());
    for (uint32_t i = 0; i < data.faces.size(); ++i) {
        uint32_t material = data.materials[i];
        if (!walkableMaterial.count(material)) {
            continue;
        }

        const graphics::Walkmesh::FaceVertices inputFace = data.faces[i];
        Uniface face = {0};
        face.centroid = {0.0f, 0.0f, 0.0f};
        for (uint32_t i = 0; i < 3; ++i) {
            uint32_t index = inputFace.indices[i] + beginVertex;
            face.vertices[i] = index;
            face.adjecent[i] = UINT32_MAX;
            face.centroid += uni.vertices[index];
        }
        face.centroid *= 0.333333f;
        uni.faces.push_back(face);
    }

    uint32_t endFace = uni.faces.size();
    Uniroom room = {
        beginFace,
        endFace,
        min, max};

    uni.rooms.push_back(room);
}

static void drawDebugFace(const Uniwalk &uni, uint32_t index) {
    uint32_t colorAdj = 0x0000FFFF;
    uint32_t colorBorder = 0x9400D3FF;
    float thickness = 0.03f;

    const Uniface &face = uni.faces[index];
    bool isBorderFace = false;
    for (uint32_t i = 0; i < 3; ++i) {
        uint32_t v0 = face.vertices[kTriangleEdges[i][0]];
        uint32_t v1 = face.vertices[kTriangleEdges[i][1]];
        uint32_t color = colorAdj;
        if (face.adjecent[i] == UINT32_MAX) {
            isBorderFace = true;
            color = colorBorder;
        }
        drawdebug::line(uni.vertices[v0], uni.vertices[v1], color, thickness);
    }

    glm::vec3 textOffset(0.0f, 0.0f, 0.5f);
    uint32_t color = isBorderFace ? colorBorder : colorAdj;
    drawdebug::text(std::to_string(index), face.centroid + textOffset, color);
}

static void drawDebugUniwalk(const Uniwalk &uni) {
    drawdebug::pushId("uniwalk");
    drawdebug::clear();

    if (!isShowPathEnabled()) {
        drawdebug::popId();
        return;
    }

    for (uint32_t i = 0; i < uni.faces.size(); ++i) {
        // Draw contour and index of each face.
        drawDebugFace(uni, i);

        // Draw connections to adjecent faces.
        for (uint32_t j = 0; j < 3; ++j) {

            uint32_t adj = uni.faces[i].adjecent[j];
            if (adj == UINT32_MAX) {
                continue;
            }
            glm::vec3 centroidAdj = uni.faces[adj].centroid;
            drawdebug::line(uni.faces[i].centroid, centroidAdj, 0x00ff00ff, 0.02f);
        }
    }

    drawdebug::popId();
}

/// Mark edges that are shared between two faces.
static void markAdjecent(Uniwalk &uni, uint32_t faceA, uint32_t faceB) {
    Uniface &a = uni.faces[faceA];
    Uniface &b = uni.faces[faceB];

    for (uint32_t i = 0; i < 3; ++i) {
        for (uint32_t j = 0; j < 3; ++j) {

            uint32_t i0 = a.vertices[kTriangleEdges[i][0]];
            uint32_t i1 = a.vertices[kTriangleEdges[i][1]];

            uint32_t j0 = b.vertices[kTriangleEdges[j][0]];
            uint32_t j1 = b.vertices[kTriangleEdges[j][1]];

            if (i0 != j1 || i1 != j0) {
                continue;
            }

            // i-th edge of face A is the same as j-th edge of face B.
            a.adjecent[i] = faceB;
            b.adjecent[j] = faceA;
        }
    }
}

void uniwalkFinalize(struct Uniwalk &uni) {
    // Deduplicate vertices, so that the same vertex gets the same index in all
    // faces. This way we can compare indices of two vertices instead of
    // calculating distance between them every time.
    const float eps2 = 0.1f * 0.1f;
    std::vector<uint32_t> dedup(uni.vertices.size(), UINT32_MAX);
    for (uint32_t i = 0; i < uni.vertices.size(); ++i) {
        if (dedup[i] != UINT32_MAX) {
            // This vertex, and all following vertices that are close to it are
            // already deduplicated.
            continue;
        }
        for (uint32_t j = i + 1; j < uni.vertices.size(); ++j) {
            if (glm::distance2(uni.vertices[i], uni.vertices[j]) < eps2) {
                dedup[j] = i;
            }
        }
    }
    for (Uniface &face : uni.faces) {
        for (uint32_t &v : face.vertices) {
            if (dedup[v] != UINT32_MAX) {
                v = dedup[v];
            }
        }
    }

    // For each faces, find faces that are adjecent to it.
    for (uint32_t i = 0; i < uni.faces.size(); ++i) {
        for (uint32_t j = i + 1; j < uni.faces.size(); ++j) {
            markAdjecent(uni, i, j);
        }
    }

    drawDebugUniwalk(uni);
}

struct CastResult {
    glm::vec3 intersection;
    bool intersects;
};

static CastResult castFace(const Uniwalk &uni, uint32_t face, glm::vec3 position) {
    // Offset position slightly to prevent rounding errors.
    glm::vec3 castPosition = position + glm::vec3(0.0f, 0.0f, 0.1f);
    float maxDistance = 100.0f;
    float distance = 0.0f;
    glm::vec2 baryPosition(0.0f);
    glm::vec3 down(0.0f, 0.0f, -1.0f);

    const uint32_t *indices = uni.faces[face].vertices;
    glm::vec3 v0 = uni.vertices[indices[0]];
    glm::vec3 v1 = uni.vertices[indices[1]];
    glm::vec3 v2 = uni.vertices[indices[2]];

    bool intersects = glm::intersectRayTriangle(castPosition, down, v0, v1, v2, baryPosition, distance);
    glm::vec3 intersection = castPosition + down * distance;
    return {intersection, intersects};
}

static uint32_t findFaceAt(const Uniwalk &uni, glm::vec3 position) {
    for (const Uniroom &room : uni.rooms) {
        // Skip the room if the if the position is not in its AABB.
        if (glm::any(glm::greaterThan(position, room.max)) || glm::any(glm::lessThan(position, room.min))) {
            continue;
        }

        // Raycast to each face in the room.
        for (uint32_t i = room.begin; i < room.end; ++i) {
            CastResult res = castFace(uni, i, position);
            if (res.intersects) {
                return i;
            }
        }
    }

    return UINT32_MAX;
}

/// Find a face with the minimum cost and remove it from the open list.
static AStarOpenFace popOpenFace(std::vector<AStarOpenFace> &open) {
    assert(open.size() >= 1);
    if (open.size() == 1) {
        AStarOpenFace face = open[0];
        open.resize(0);
        return face;
    }

    float minCost = FLT_MAX;
    uint32_t minIndex = UINT32_MAX;
    for (uint32_t i = 0; i < open.size(); ++i) {
        if (open[i].cost < minCost) {
            minCost = open[i].cost;
            minIndex = i;
        }
    }

    AStarOpenFace face = open[minIndex];

    // Remove the face from the list.
    uint32_t lastIndex = open.size() - 1;
    if (lastIndex != minIndex) {
        std::swap(open[minIndex], open[lastIndex]);
    }
    open.resize(open.size() - 1);

    return face;
}

static bool findPathAStar(AStarPath &path, AStarContext &ctx, const Uniwalk &uni,
                          uint32_t fromFace, uint32_t toFace) {
    // Reset the context.
    ctx.state.resize(uni.faces.size());
    memset(&ctx.state[0], 0xFF, sizeof(ctx.state[0]) * ctx.state.size());
    ctx.open.resize(0);

    // Initialize the algorithm. Find a reverse path - a path from toFace to
    // fromFace. When the algorithm reaches the fromFace, it backtracks and
    // reverses the path, so it transforms to natural order [fromFace, toFace].
    ctx.state[toFace].flag = AStarFace::Open;
    ctx.open.push_back({toFace, 0.0f});

    while (!ctx.open.empty()) {
        // Extract face with least total cost from open list and close it.
        AStarOpenFace current = popOpenFace(ctx.open);
        ctx.state[current.index].flag = AStarFace::Closed;

        if (current.index == fromFace) {
            // Reached the destination face. Now reconstruct the path by
            // following the parents.
            uint32_t index = fromFace;
            path.faces.resize(0);
            while (index != UINT32_MAX) {
                path.faces.push_back(index);
                index = ctx.state[index].parent;
            }
            return true;
        }

        for (uint32_t adj : uni.faces[current.index].adjecent) {
            if (adj == UINT32_MAX) {
                continue;
            }

            // Skip adjacent vertex if it is closed.
            if (ctx.state[adj].flag == AStarFace::Closed) {
                continue;
            }

            glm::vec3 currentCentroid = uni.faces[current.index].centroid;
            glm::vec3 adjCentroid = uni.faces[adj].centroid;
            float distance = glm::distance2(currentCentroid, adjCentroid);
            float heuristic = glm::distance2(adjCentroid, uni.faces[toFace].centroid);
            float cost = current.cost + distance + heuristic;

            if (ctx.state[adj].flag == AStarFace::Open) {
                bool foundOpen = false;
                for (AStarOpenFace &open : ctx.open) {
                    if (open.index != adj) {
                        continue;
                    }
                    foundOpen = true;

                    if (cost < open.cost) {
                        // Adjecent face is "best" reachable from the current face.
                        open.cost = cost;
                        ctx.state[adj].parent = current.index;
                    }
                    break;
                }
                assert(foundOpen);
            } else {
                // Open the adjecent face.
                ctx.open.push_back({adj, cost});
                ctx.state[adj] = {AStarFace::Open, current.index};
            }
        }
    }

    // Path not found.
    return false;
}

/// Portal is an edge between two consecutive faces on a path.
struct Portal {
    uint32_t left;
    uint32_t right;
};

/// Find an edge between \p face and \p nextFace.
static Portal findPortal(const Uniwalk &uni, uint32_t face, uint32_t nextFace) {
    uint32_t found[2];
    uint32_t numFound = 0;

    for (uint32_t vi : uni.faces[face].vertices) {
        for (uint32_t vj : uni.faces[nextFace].vertices) {
            if (vi != vj) {
                continue;
            }
            found[numFound] = vi;
            ++numFound;
            if (numFound == 2) {
                return Portal {found[0], found[1]};
            }
        }
    }
    assert(0 && "faces are not adjecent");
    return Portal {0, 0};
}

/// Funnel is a segment of a path where all portals are reachable in a straight
/// line from the base.
struct Funnel {
    // Apex of the funnel
    glm::vec3 base;

    // Left and right edges of the funnel.
    uint32_t left;
    uint32_t right;

    // Last processed portal
    Portal portal;
};

/// FunnelUpdate describes how a funnel changes with the next portal.

/// A funnel can only narrow, so attempts to expand a funnel should be
/// ignored. A funnel collapses when it has to go around the corner (i.e. when
/// the right edge goes over the left edge and vise versa).
struct FunnelUpdate {
    enum Change {
        Expand,
        Narrow,
        Corner,
    };

    // Direction of Expand or Corner.
    enum Direction {
        Left,
        Right,
    };

    Change change;
    Direction direction;
};

static FunnelUpdate checkFunnelUpdate(Funnel funnel, Portal portal, const Uniwalk &uni) {
    glm::vec3 left = uni.vertices[funnel.left] - funnel.base;
    glm::vec3 right = uni.vertices[funnel.right] - funnel.base;

    glm::vec3 portLeft = uni.vertices[portal.left] - funnel.base;
    glm::vec3 portRight = uni.vertices[portal.right] - funnel.base;

    if (funnel.portal.left != portal.left) {
        float leftToPort = glm::cross(left, portLeft).z;
        float leftToRight = glm::cross(left, right).z;
        if ((leftToPort * leftToRight) >= 0.0f) {
            float rightToPort = glm::cross(right, portLeft).z;
            float portToLeft = glm::cross(portLeft, left).z;
            if ((rightToPort * portToLeft) >= 0) {
                // Portal left narrows the funnel.
                return {FunnelUpdate::Narrow, FunnelUpdate::Left};
            }
            // Portal left went over the right edge - mark the right point as
            // the corner.
            return {FunnelUpdate::Corner, FunnelUpdate::Right};
        }
        // Funnel tries to expand to the left.
        return {FunnelUpdate::Expand, FunnelUpdate::Left};
    }

    float rightToPort = glm::cross(right, portRight).z;
    float rightToLeft = glm::cross(right, left).z;
    if ((rightToPort * rightToLeft) >= 0.0f) {
        float leftToPort = glm::cross(left, portRight).z;
        float portToRight = glm::cross(portRight, right).z;
        if ((leftToPort * portToRight) >= 0) {
            // Portal right narrows the funnel.
            return {FunnelUpdate::Narrow, FunnelUpdate::Right};
        }
        // Portal right went over the right edge - mark left point as the
        // corner and restart the algorithm.
        return {FunnelUpdate::Corner, FunnelUpdate::Left};
    }
    // Funnel tries to expand to the right.
    return {FunnelUpdate::Expand, FunnelUpdate::Right};
}

static void drawDebugFunnel(const Funnel &funnel, const Uniwalk &uni, int32_t id) {
    drawdebug::pushId(id);
    drawdebug::pushId("funnel");
    drawdebug::clear();
    if (isShowPathEnabled()) {
        drawdebug::line(funnel.base, uni.vertices[funnel.left], 0xC0FF3EFF, 0.1f);
        drawdebug::line(funnel.base, uni.vertices[funnel.right], 0xFF7F24FF, 0.1f);
    }
    drawdebug::popId();
    drawdebug::popId();
}

/// Find a straight line path from the current point through a complete path.
/// If there is a corner that makes a straight path imposible, return a midpoint
/// of the farthest edge on the path that we can reach with a straight line.
static glm::vec3 funnelPath(AStarPath &path, const Uniwalk &uni, const glm::vec3 &current) {
    Funnel funnel;
    funnel.base = current;

    bool initialized = false;
    for (; path.next + 1 < path.faces.size(); ++path.next) {
        Portal portal = findPortal(uni, path.faces[path.next], path.faces[path.next + 1]);
        if (!initialized) {
            funnel.left = portal.left;
            funnel.right = portal.right;
            funnel.portal = portal;
            initialized = true;
            continue;
        }

        // Keep left/right consistent.
        if ((funnel.portal.left == portal.right) || funnel.portal.right == portal.left) {
            std::swap(portal.left, portal.right);
        }

        FunnelUpdate upd = checkFunnelUpdate(funnel, portal, uni);
        funnel.portal = portal;
        switch (upd.change) {
        case FunnelUpdate::Expand: {
            // Never expand the funnel.
            break;
        }
        case FunnelUpdate::Narrow: {
            switch (upd.direction) {
            case FunnelUpdate::Left: {
                funnel.left = portal.left;
                break;
            }
            case FunnelUpdate::Right: {
                funnel.right = portal.right;
                break;
            }
            }
            break;
        }
        case FunnelUpdate::Corner: {
            // Path goes around a corner. Pick a midpoint between funnel edges
            // to avoid getting stuck at a corner.
            glm::vec3 left = uni.vertices[funnel.left];
            glm::vec3 right = uni.vertices[funnel.right];
            drawDebugFunnel(funnel, uni, path.index);
            return (left + right) * 0.5f;
        }
        }
    }

    if (!initialized) {
        // Trivial path between two points.
        return path.to;
    }

    // Check that the destination is within the funnel. Otherwise move to the
    // funnel edge, same as when we hit a corner.
    glm::vec3 left = uni.vertices[funnel.left] - funnel.base;
    glm::vec3 right = uni.vertices[funnel.right] - funnel.base;
    glm::vec3 last = path.to - funnel.base;

    float leftToLast = glm::cross(left, last).z;
    float leftToRight = glm::cross(left, right).z;
    float rightToLast = glm::cross(right, last).z;
    float rightToLeft = glm::cross(right, left).z;

    drawDebugFunnel(funnel, uni, path.index);

    if ((leftToLast * leftToRight) < 0.0f || (rightToLast * rightToLeft) < 0.0f) {
        // Hit a corner.
        return (uni.vertices[funnel.left] + uni.vertices[funnel.right]) * 0.5f;
    }

    return path.to;
}

static bool findPath(AStarPath &path, AStarContext &astar, const Uniwalk &uni,
                     const glm::vec3 &from, const glm::vec3 &to) {
    uint32_t fromFace = findFaceAt(uni, from);
    uint32_t toFace = findFaceAt(uni, to);

    if (fromFace == UINT32_MAX || toFace == UINT32_MAX) {
        return false;
    }

    bool found = findPathAStar(path, astar, uni, fromFace, toFace);
    if (!found) {
        return false;
    }

    drawdebug::pushId(path.index);
    drawdebug::pushId("findPath");
    drawdebug::clear();
    if (isShowPathEnabled()) {
        for (uint32_t i = 1; i < path.faces.size(); ++i) {
            glm::vec3 from = uni.faces[path.faces[i - 1]].centroid;
            glm::vec3 to = uni.faces[path.faces[i]].centroid;
            drawdebug::line(from, to, 0xCD2626FF, 0.1);
        }
    }
    drawdebug::popId();
    drawdebug::popId();

    path.from = from;
    path.to = to;
    path.next = 0;
    path.active = true;
    path.nextPoint = funnelPath(path, uni, from);
    return true;
}

std::optional<Path> createPath(Pathfinder &pf, const glm::vec3 &from, const glm::vec3 &to) {
    for (int32_t i = 0; i < pf.paths.size(); ++i) {
        AStarPath &path = pf.paths[i];
        if (path.active) {
            continue;
        }

        bool found = findPath(path, pf.astar, pf.uni, from, to);
        if (!found) {
            return std::nullopt;
        }
        path.index = i;
        return Path {i};
    }

    return std::nullopt;
}

void releasePath(Pathfinder &pf, Path path) {
    pf.paths[path.index].active = false;

    if (isShowPathEnabled()) {
        drawdebug::pushId(path.index);

        drawdebug::pushId("findPath");
        drawdebug::clear();
        drawdebug::popId();

        drawdebug::pushId("funnel");
        drawdebug::clear();
        drawdebug::popId();

        drawdebug::pushId("updatePath");
        drawdebug::clear();
        drawdebug::popId();

        drawdebug::popId();
    }
}

bool updatePath(Pathfinder &pf, Path p, const glm::vec3 &current) {
    AStarPath &path = pf.paths[p.index];

    uint32_t checkFrom = (path.next == 0) ? path.next : path.next - 1;
    uint32_t checkTo = path.next + 1;

    bool isOnPath = false;
    uint32_t currentFace = UINT32_MAX;
    for (uint32_t i = checkFrom; i < checkTo; ++i) {
        CastResult res = castFace(pf.uni, path.faces[i], current);
        if (res.intersects) {
            isOnPath = true;
            break;
        }
    }
    if (!isOnPath) {
        // Wandered off the path, recalculate.
        bool foundNewPath = findPath(path, pf.astar, pf.uni, current, path.to);
        if (!foundNewPath) {
            return false;
        }
    }

    float eps2 = 0.01f;
    if (path.next < path.faces.size()) {
        if (glm::distance2(path.nextPoint, current) < eps2) {
            // Reached an intermediate point, move to the next.
            path.nextPoint = funnelPath(path, pf.uni, current);
        }
    }

    drawdebug::pushId(p.index);
    drawdebug::pushId("updatePath");
    drawdebug::clear();

    if (isShowPathEnabled()) {
        drawdebug::line(current, path.nextPoint, 0xF0E68CFF, 0.05);
    }

    drawdebug::popId();
    drawdebug::popId();

    return true;
}

glm::vec3 getNextPathPoint(Pathfinder &pf, Path p) {
    AStarPath &path = pf.paths[p.index];
    return path.nextPoint;
}

glm::vec3 getLastPathPoint(Pathfinder &pf, Path path) {
    return pf.paths[path.index].to;
}

// Keepout vector points away from a border edge or a corner vertex.
glm::vec3 computeKeepoutForce(const Uniwalk &uni, const glm::vec3 &position) {
    float keepoutDistance2 = 4.0f;
    float keepoutCornerDistance2 = 2.0f;
    glm::vec3 keepout = {0.0f, 0.0f, 0.0f};

    for (const Uniroom &room : uni.rooms) {
        if (glm::any(glm::greaterThan(position, room.max)) || glm::any(glm::lessThan(position, room.min))) {
            // Not in this room.
            continue;
        }
        for (uint32_t i = room.begin; i < room.end; ++i) {
            const Uniface &face = uni.faces[i];
            for (uint32_t j = 0; j < 3; ++j) {
                if (face.adjecent[j] != UINT32_MAX) {
                    // Edges to other walkable faces do not contribute to
                    // keepout.
                    continue;
                }
                glm::vec3 v0 = uni.vertices[face.vertices[kTriangleEdges[j][0]]];
                glm::vec3 v1 = uni.vertices[face.vertices[kTriangleEdges[j][1]]];
                glm::vec3 edge = v0 - v1;
                float edgeLen2 = glm::length2(edge);
                float projRatio = glm::dot(edge, position - v1) / edgeLen2;
                if (projRatio > 0.0f && projRatio < 1.0f) {
                    // Current position projects to the edge.
                    glm::vec3 proj = v1 + edge * projRatio;
                    glm::vec3 fromEdge = position - proj;
                    float fromEdgeLength2 = glm::length2(fromEdge);
                    if (fromEdgeLength2 < keepoutDistance2) {
                        keepout += fromEdge / fromEdgeLength2;
                    }
                }

                // Corner points push outside.
                glm::vec3 fromCorner0 = position - v0;
                float fromCorner0Len2 = glm::length2(fromCorner0);
                if (fromCorner0Len2 < keepoutCornerDistance2) {
                    keepout += fromCorner0 / fromCorner0Len2;
                }

                glm::vec3 fromCorner1 = position - v1;
                float fromCorner1Len2 = glm::length2(fromCorner1);
                if (fromCorner1Len2 < keepoutCornerDistance2) {
                    keepout += fromCorner1 / fromCorner1Len2;
                }
            }
        }
    }

    return keepout;
}

} // namespace game

} // namespace reone
