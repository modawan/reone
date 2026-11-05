/*
 * Copyright (c) 2020-2026 The reone project contributors
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
#include "reone/system/types.h"

namespace reone {

namespace resource {
class Gff;
}

namespace game {

// The logical map canvas the area map textures are drawn on, in pixels.
constexpr float kKotorMapWidth = 440.0f;
constexpr float kTSLMapWidth = 512.0f;
constexpr float kMapHeight = 256.0f;

// The label of the struct in an area record that keeps its explored map
// cells. No original save carrying them was available, so this is reone's
// choice.
constexpr char kAreaMapStructLabel[] = "AreaMap";

/**
 * Which cells of an area's map the party has seen.
 *
 * The grid lies over the logical map canvas in normalized map coordinates: the
 * column of a map position is its x times the column count, its row its y
 * times the row count. The area file gives the column count (MapResX); the
 * row count, and everything about how cells are revealed, is reone's choice,
 * since the game data does not say.
 */
class MapExploration {
public:
    MapExploration() = default;
    MapExploration(int columns, int rows);

    /**
     * The grid an area's map gets from its MapResX: that many columns, and as
     * many rows as keeps the cells square on the logical canvas, at least one.
     * The row rule is reone's choice.
     */
    static MapExploration forMapResX(int mapResX, bool tsl);
    static int rowsForMapResX(int mapResX, float logicalMapWidth);

    int columns() const { return _columns; }
    int rows() const { return _rows; }
    bool empty() const { return _cells.empty(); }

    bool isExplored(int column, int row) const;
    int exploredCount() const;

    /** The cell a normalized map position falls in; may lie outside the grid. */
    glm::ivec2 cellAt(const glm::vec2 &mapPos) const;

    /**
     * Reveals every cell whose centre is within radius cells of the centre of
     * the given cell, which may itself lie outside the grid.
     *
     * @return whether a cell was newly revealed
     */
    bool revealAround(const glm::ivec2 &cell, int radius);

    /** Reveals around the cell a normalized map position falls in; a non-finite position reveals nothing. */
    bool revealAround(const glm::vec2 &mapPos, int radius);

    /** @return whether a cell was newly revealed */
    bool revealAll();

    /**
     * Changes whenever the explored cells change, and differs between any two
     * grids that were built or changed separately, so a cached picture of a
     * grid can tell when it is stale.
     */
    uint64_t revision() const { return _revision; }

    /** The explored cells, one bit each, row by row, low bit first. */
    ByteBuffer toBytes() const;

    /**
     * Restores the explored cells written by toBytes for a grid of the given
     * size. A size that is not this grid's leaves nothing explored, and cells
     * past the end of short data stay unexplored.
     */
    void fromBytes(int columns, int rows, const ByteBuffer &bytes);

    /** The saved form: an AreaMap struct, whose labels are reone's choice. */
    std::shared_ptr<resource::Gff> toGff() const;

    /** Restores from an area record's AreaMap struct; without one nothing is explored. */
    void loadGff(const resource::Gff &are);

private:
    int _columns {0};
    int _rows {0};
    std::vector<bool> _cells;
    uint64_t _revision {0};

    void touch();
};

/** Whether an area file's map north axis is one of the four the map knows. */
bool isValidMapNorthAxis(int northAxis);

/**
 * Converts a world position into a normalized map position, using the area
 * file's two reference points and its north axis. An invalid north axis gives
 * the top left corner; the area warns of it once, when it loads.
 */
glm::vec2 worldToMapPosition(const resource::generated::ARE_Map &map, const glm::vec2 &world);

} // namespace game

} // namespace reone
