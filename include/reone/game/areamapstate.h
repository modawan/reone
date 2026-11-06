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
 * An area carries a map when it has both of these:
 *
 * - The image `lbl_map<area>`, named after the area, not the module, so
 *   modules that share an area share its map. The image is twice as wide as
 *   it is tall, at any resolution. KotOR shows the left 440/512 of its width.
 *   TSL shows all of it. Without the image the area carries no map, whatever
 *   its map settings.
 * - The `Map` struct of the area file:
 *   - `MapResX` is the exploration grid's width in cells. It is capped at
 *     kMaxGridWidth, and 0 means the area carries no map. The grid's height
 *     follows the canvas's proportions. A grid one cell wide is fully
 *     explored as soon as the party arrives.
 *   - `MapPt1X/Y` and `MapPt2X/Y` are two reference points on the map, as
 *     fractions of the canvas measured from its top left. The canvas is the
 *     part of the image the game shows.
 *   - `WorldPt1X/Y` and `WorldPt2X/Y` are the world positions of those two
 *     points. The two points must differ along both axes, both on the map and
 *     in the world. Points that share a row or column leave that axis with no
 *     scale, and the area carries no map.
 *   - `NorthAxis` sets which world axis runs across the map: world x for 0
 *     and 1, world y for 2 and 3. The reference points set each axis's scale
 *     and direction. Within each pair, only the party leader's arrow differs,
 *     by half a turn.
 *   - `MapZoom` is 1 in every area the games ship, and the map does not use
 *     it.
 *
 * Map notes are waypoints with `HasMapNote` set. Each shows its `MapNote`
 * once `MapNoteEnabled` is on and its place is explored.
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

    /** The image that carries the map of the area named \p area. */
    static std::string imageResRef(const std::string &area);

    /**
     * Takes on \p map for the area named \p area, sizing the exploration grid
     * and leaving it unexplored. An area whose map asks for no grid, or whose
     * reference points share a row or column, carries no map at all: it has
     * no position on the map, nothing to explore, and nothing to save.
     */
    void load(
        resource::GameID gameId,
        const std::string &area,
        const resource::generated::ARE_Map &map);

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

    void setFullyExplored();

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
