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

#include "reone/game/mapexploration.h"

#include <algorithm>
#include <cmath>

#include "reone/resource/gff.h"
#include "reone/system/logutil.h"

using namespace reone::resource;

namespace reone {

namespace game {

// The fields of the saved explored cells. No original save carrying them was
// available to copy, so the labels and the struct type are reone's choice.
static constexpr char kAreaMapResXField[] = "AreaMapResX";
static constexpr char kAreaMapResYField[] = "AreaMapResY";
static constexpr char kAreaMapDataField[] = "AreaMapData";
static constexpr uint32_t kAreaMapStructType = 0;

static constexpr double kMaxCellOffset = 1 << 30;

// Every grid takes a revision no other grid has had, so a cached picture of
// one grid is never mistaken for another's.
static uint64_t nextRevision() {
    static uint64_t revision = 0;
    return ++revision;
}

MapExploration::MapExploration(int columns, int rows) {
    if (columns > 0 && rows > 0) {
        _columns = columns;
        _rows = rows;
        _cells.assign(static_cast<size_t>(columns) * static_cast<size_t>(rows), false);
    }
    touch();
}

int MapExploration::rowsForMapResX(int mapResX, float logicalMapWidth) {
    if (mapResX <= 0 || logicalMapWidth <= 0.0f) {
        return 0;
    }
    // reone's choice: square cells on the logical canvas.
    int rows = static_cast<int>(std::lround(mapResX * kMapHeight / logicalMapWidth));
    return std::max(1, rows);
}

MapExploration MapExploration::forMapResX(int mapResX, bool tsl) {
    int rows = rowsForMapResX(mapResX, tsl ? kTSLMapWidth : kKotorMapWidth);
    return MapExploration(mapResX, rows);
}

bool MapExploration::isExplored(int column, int row) const {
    if (column < 0 || column >= _columns || row < 0 || row >= _rows) {
        return false;
    }
    return _cells[static_cast<size_t>(row) * _columns + column];
}

int MapExploration::exploredCount() const {
    return static_cast<int>(std::count(_cells.begin(), _cells.end(), true));
}

glm::ivec2 MapExploration::cellAt(const glm::vec2 &mapPos) const {
    // Clamped so a position far off the map cannot overflow the index; no
    // real position comes near the limit, and the grid is out of reach of
    // any centre beyond it but for an absurd radius.
    auto index = [](float position, int count) {
        double cell = std::floor(static_cast<double>(position) * count);
        if (std::isnan(cell)) {
            return static_cast<int>(-kMaxCellOffset);
        }
        return static_cast<int>(std::clamp(cell, -kMaxCellOffset, kMaxCellOffset));
    };
    return glm::ivec2(index(mapPos.x, _columns), index(mapPos.y, _rows));
}

bool MapExploration::revealAround(const glm::ivec2 &cell, int radius) {
    if (empty() || radius < 0) {
        return false;
    }
    // Only the cells inside the grid are visited, so a far-off centre or a
    // huge radius costs no more than the grid.
    int64_t minColumn = std::max<int64_t>(0, static_cast<int64_t>(cell.x) - radius);
    int64_t maxColumn = std::min<int64_t>(_columns - 1, static_cast<int64_t>(cell.x) + radius);
    int64_t minRow = std::max<int64_t>(0, static_cast<int64_t>(cell.y) - radius);
    int64_t maxRow = std::min<int64_t>(_rows - 1, static_cast<int64_t>(cell.y) + radius);
    int64_t radius2 = static_cast<int64_t>(radius) * radius;
    bool changed = false;
    for (int64_t row = minRow; row <= maxRow; ++row) {
        for (int64_t column = minColumn; column <= maxColumn; ++column) {
            int64_t dx = column - cell.x;
            int64_t dy = row - cell.y;
            if (dx * dx + dy * dy > radius2) {
                continue;
            }
            std::vector<bool>::reference cellBit = _cells[static_cast<size_t>(row * _columns + column)];
            if (!cellBit) {
                cellBit = true;
                changed = true;
            }
        }
    }
    if (changed) {
        touch();
    }
    return changed;
}

bool MapExploration::revealAround(const glm::vec2 &mapPos, int radius) {
    if (!std::isfinite(mapPos.x) || !std::isfinite(mapPos.y)) {
        return false;
    }
    return revealAround(cellAt(mapPos), radius);
}

bool MapExploration::revealAll() {
    if (std::find(_cells.begin(), _cells.end(), false) == _cells.end()) {
        return false;
    }
    std::fill(_cells.begin(), _cells.end(), true);
    touch();
    return true;
}

ByteBuffer MapExploration::toBytes() const {
    ByteBuffer bytes((_cells.size() + 7) / 8, 0);
    for (size_t i = 0; i < _cells.size(); ++i) {
        if (_cells[i]) {
            bytes[i / 8] = static_cast<char>(static_cast<uint8_t>(bytes[i / 8]) | (1u << (i % 8)));
        }
    }
    return bytes;
}

void MapExploration::fromBytes(int columns, int rows, const ByteBuffer &bytes) {
    std::vector<bool> cells(_cells.size(), false);
    if (columns == _columns && rows == _rows) {
        size_t count = std::min(cells.size(), bytes.size() * 8);
        for (size_t i = 0; i < count; ++i) {
            cells[i] = (static_cast<uint8_t>(bytes[i / 8]) >> (i % 8)) & 1u;
        }
    } else if (!empty()) {
        warn("MapExploration: saved grid " + std::to_string(columns) + "x" + std::to_string(rows) +
             " does not match the area's " + std::to_string(_columns) + "x" + std::to_string(_rows));
    }
    if (cells != _cells) {
        _cells = std::move(cells);
        touch();
    }
}

std::shared_ptr<Gff> MapExploration::toGff() const {
    return Gff::Builder()
        .type(kAreaMapStructType)
        .field(Gff::Field::newInt(kAreaMapResXField, _columns))
        .field(Gff::Field::newInt(kAreaMapResYField, _rows))
        .field(Gff::Field::newVoid(kAreaMapDataField, toBytes()))
        .build();
}

void MapExploration::loadGff(const Gff &are) {
    auto areaMap = are.findStruct(kAreaMapStructLabel);
    if (!areaMap) {
        fromBytes(_columns, _rows, ByteBuffer());
        return;
    }
    fromBytes(
        areaMap->getInt(kAreaMapResXField),
        areaMap->getInt(kAreaMapResYField),
        areaMap->getData(kAreaMapDataField));
}

void MapExploration::touch() {
    _revision = nextRevision();
}

bool isValidMapNorthAxis(int northAxis) {
    return northAxis >= 0 && northAxis <= 3;
}

glm::vec2 worldToMapPosition(const generated::ARE_Map &map, const glm::vec2 &world) {
    glm::vec2 worldPoint1(map.WorldPt1X, map.WorldPt1Y);
    glm::vec2 worldPoint2(map.WorldPt2X, map.WorldPt2Y);
    glm::vec2 mapPoint1(map.MapPt1X, map.MapPt1Y);
    glm::vec2 mapPoint2(map.MapPt2X, map.MapPt2Y);
    float scaleX, scaleY;
    glm::vec2 result(0.0f);

    switch (map.NorthAxis) {
    case 0:
    case 1:
        scaleX = (mapPoint1.x - mapPoint2.x) / (worldPoint1.x - worldPoint2.x);
        scaleY = (mapPoint1.y - mapPoint2.y) / (worldPoint1.y - worldPoint2.y);
        result.x = (world.x - worldPoint1.x) * scaleX + mapPoint1.x;
        result.y = (world.y - worldPoint1.y) * scaleY + mapPoint1.y;
        break;
    case 2:
    case 3:
        scaleX = (mapPoint1.y - mapPoint2.y) / (worldPoint1.x - worldPoint2.x);
        scaleY = (mapPoint1.x - mapPoint2.x) / (worldPoint1.y - worldPoint2.y);
        result.x = (world.y - worldPoint1.y) * scaleY + mapPoint1.x;
        result.y = (world.x - worldPoint1.x) * scaleX + mapPoint1.y;
        break;
    default:
        break;
    }

    return result;
}

} // namespace game

} // namespace reone
