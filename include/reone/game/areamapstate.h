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

#include "reone/resource/parser/gff/are.h"
#include "reone/resource/types.h"

namespace reone {

namespace resource {

class Gff;

}

namespace game {

/**
 * An area's map: the conversion between world space and the area map, and
 * which parts of the map the party has explored.
 *
 * The authored map points are fractions of the map canvas each game lays its
 * map screen out at, so the canvas sets both the pixel space positions are
 * measured in and the aspect the exploration grid takes.
 */
class AreaMapState {
public:
    /** The largest exploration grid an area may ask for. */
    static constexpr int kMaxGridWidth = 88;

    /** The grid cells a party member reveals around itself. */
    static constexpr int kPartyRevealRadius = 1;

    /** A map position, in canvas pixels. */
    struct MapPoint {
        int x {0};
        int y {0};
    };

    /**
     * Takes on \p map, sizing the exploration grid and leaving it unexplored.
     * An area whose map asks for no grid carries no map at all: it has no
     * position on the map, nothing to explore, and nothing to save.
     */
    void load(resource::GameID gameId, const resource::generated::ARE_Map &map);

    bool isLoaded() const { return !_explored.empty(); }

    int canvasWidth() const { return _canvasWidth; }
    int canvasHeight() const { return _canvasHeight; }
    int gridWidth() const { return _gridWidth; }
    int gridHeight() const { return _gridHeight; }

    /** The map position of \p world, or none when it lies off the map. */
    std::optional<MapPoint> toMapPoint(const glm::vec2 &world) const;

    /**
     * The map position of \p world as a fraction of the canvas, or none when
     * it lies off the map.
     */
    std::optional<glm::vec2> toMapPosition(const glm::vec2 &world) const;

    /** The angle, counter-clockwise, the map shows a world facing of \p facing at. */
    float toMapFacing(float facing) const;

    /** Reveals the cell \p world falls in and \p radius rings of cells around it. */
    void setExplored(const glm::vec2 &world, int radius);

    bool isExplored(const glm::vec2 &world) const;
    bool isCellExplored(int gridX, int gridY) const;

    /**
     * Applies the exploration an area's saved instances carry. Data sized for
     * a different grid than this area's map describes is not applied.
     */
    void loadFromGit(const resource::Gff &git);

    /** The exploration struct an area's saved instances carry, packed as stored. */
    std::shared_ptr<resource::Gff> writeToGit() const;

private:
    int _canvasWidth {0};
    int _canvasHeight {0};
    int _gridWidth {0};
    int _gridHeight {0};

    int _northAxis {0};
    /** World units a canvas pixel spans, and the world point the canvas origin sits at. */
    glm::vec2 _worldPerPixel {0.0f};
    glm::vec2 _worldBase {0.0f};

    std::vector<bool> _explored;

    /**
     * Turns world space so that the area's north runs up the map, which lets
     * the map points fit the turned axes one to one.
     */
    glm::vec2 orientToMap(const glm::vec2 &world) const;

    void setMapPointExplored(const MapPoint &point);
    std::optional<MapPoint> toGridCell(const MapPoint &point) const;
    int cellIndex(int gridX, int gridY) const { return (_gridWidth + 1) * gridY + gridX; }
};

} // namespace game

} // namespace reone
