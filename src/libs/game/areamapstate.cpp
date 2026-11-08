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

#include "reone/game/areamapstate.h"

#include "reone/resource/gff.h"

using namespace reone::resource;

namespace reone {

namespace game {

// The canvas each game lays its map screen out at.
static constexpr int kKotorCanvasWidth = 440;
static constexpr int kKotorCanvasHeight = 256;
static constexpr int kTSLCanvasWidth = 588;
static constexpr int kTSLCanvasHeight = 294;

// The exploration bits are stored as 32-bit words, least significant bit
// first, and the trailing partial word is kept whole.
static constexpr int kBitsPerWord = 32;
static constexpr int kBytesPerWord = 4;

/** The struct type an area's saved exploration carries. */
static constexpr uint32_t kAreaMapStructType = 101;

static int wordCountFor(int gridWidth, int gridHeight) {
    return (gridWidth + 1) * (gridHeight + 1) / kBitsPerWord + 1;
}

std::string AreaMapState::imageResRef(const std::string &area) {
    return "lbl_map" + area;
}

void AreaMapState::load(GameID gameId, const resource::generated::ARE_Map &map) {
    *this = AreaMapState();

    if (map.MapResX == 0) {
        // The area carries no map.
        return;
    }

    const bool tsl = gameId == GameID::TSL;
    _canvasWidth = tsl ? kTSLCanvasWidth : kKotorCanvasWidth;
    _canvasHeight = tsl ? kTSLCanvasHeight : kKotorCanvasHeight;

    _gridWidth = map.MapResX > kMaxGridWidth ? kMaxGridWidth : map.MapResX;
    _gridHeight = std::max(1, _gridWidth * _canvasHeight / _canvasWidth);
    _explored.assign(static_cast<size_t>(_gridWidth + 1) * (_gridHeight + 1), false);

    _northAxis = map.NorthAxis;

    // The authored points are fractions of the canvas; measure them in its
    // pixels so that a world point converts straight into a map position.
    const glm::vec2 mapPoint1(
        std::floor(map.MapPt1X * _canvasWidth + 0.5f),
        std::floor(map.MapPt1Y * _canvasHeight + 0.5f));
    const glm::vec2 mapPoint2(
        std::floor(map.MapPt2X * _canvasWidth + 0.5f),
        std::floor(map.MapPt2Y * _canvasHeight + 0.5f));

    // Both reference points are turned the same way the map turns world
    // space, which leaves a straight fit along each axis.
    const glm::vec2 worldPoint1(
        orientToMap(glm::vec2(map.WorldPt1X, map.WorldPt1Y)));
    const glm::vec2 worldPoint2(
        orientToMap(glm::vec2(map.WorldPt2X, map.WorldPt2Y)));

    _worldPerPixel = (worldPoint2 - worldPoint1) / (mapPoint2 - mapPoint1);
    _worldBase = worldPoint1 - mapPoint1 * _worldPerPixel;
}

glm::vec2 AreaMapState::orientToMap(const glm::vec2 &world) const {
    switch (_northAxis) {
    case 1:
        return glm::vec2(-world.x, -world.y);
    case 2:
        return glm::vec2(world.y, -world.x);
    case 3:
        return glm::vec2(-world.y, world.x);
    default:
        return world;
    }
}

glm::vec2 AreaMapState::toCanvasPixel(const glm::vec2 &world) const {
    return (orientToMap(world) - _worldBase) / _worldPerPixel;
}

std::optional<AreaMapState::MapPoint> AreaMapState::toMapPoint(const glm::vec2 &world) const {
    if (!isLoaded()) {
        return std::nullopt;
    }
    const glm::vec2 point(toCanvasPixel(world));

    const int x = static_cast<int>(point.x + 0.5f);
    const int y = static_cast<int>(point.y + 0.5f);
    if (x < 0 || x > _canvasWidth || y < 0 || y > _canvasHeight) {
        return std::nullopt;
    }
    return MapPoint {x, y};
}

std::optional<glm::vec2> AreaMapState::toMapPosition(const glm::vec2 &world) const {
    auto point = toMapPoint(world);
    if (!point) {
        return std::nullopt;
    }
    return glm::vec2(
        static_cast<float>(point->x) / _canvasWidth,
        static_cast<float>(point->y) / _canvasHeight);
}

glm::vec2 AreaMapState::toCanvasPosition(const glm::vec2 &world) const {
    return glm::floor(toCanvasPixel(world) + 0.5f) /
           glm::vec2(_canvasWidth, _canvasHeight);
}

float AreaMapState::toMapFacing(float facing) const {
    switch (_northAxis) {
    case 1:
        return facing + glm::pi<float>();
    case 2:
        return facing + glm::half_pi<float>();
    case 3:
        return facing + glm::three_over_two_pi<float>();
    default:
        return facing;
    }
}

std::optional<AreaMapState::MapPoint> AreaMapState::toGridCell(const MapPoint &point) const {
    if (point.x < 0 || point.x > _canvasWidth || point.y < 0 || point.y > _canvasHeight) {
        return std::nullopt;
    }
    const float cellWidth = static_cast<float>(_canvasWidth) / _gridWidth;
    const float cellHeight = static_cast<float>(_canvasHeight) / _gridHeight;
    return MapPoint {
        static_cast<int>(point.x / cellWidth + 0.5f),
        static_cast<int>(point.y / cellHeight + 0.5f)};
}

void AreaMapState::setMapPointExplored(const MapPoint &point) {
    auto cell = toGridCell(point);
    if (!cell) {
        return;
    }
    _explored[cellIndex(cell->x, cell->y)] = true;
}

void AreaMapState::setExplored(const glm::vec2 &world, int radius) {
    auto point = toMapPoint(world);
    if (!point) {
        return;
    }
    // A whole cell at a time, out to a diamond of the given radius.
    const int cellWidth = _canvasWidth / _gridWidth;
    const int cellHeight = _canvasHeight / _gridHeight;

    for (int offsetX = -radius; offsetX <= radius; ++offsetX) {
        const int spanY = radius - std::abs(offsetX);
        for (int offsetY = -spanY; offsetY <= spanY; ++offsetY) {
            setMapPointExplored(MapPoint {
                std::clamp(point->x + cellWidth * offsetX, 0, _canvasWidth),
                std::clamp(point->y + cellHeight * offsetY, 0, _canvasHeight)});
        }
    }
}

void AreaMapState::setFullyExplored() {
    _explored.assign(_explored.size(), true);
}

bool AreaMapState::isExplored(const glm::vec2 &world) const {
    auto point = toMapPoint(world);
    if (!point) {
        return false;
    }
    auto cell = toGridCell(*point);
    return cell && _explored[cellIndex(cell->x, cell->y)];
}

bool AreaMapState::isCellExplored(int gridX, int gridY) const {
    return _explored[cellIndex(gridX, gridY)];
}

void AreaMapState::loadFromGit(const resource::Gff &git) {
    auto areaMap = git.findStruct("AreaMap");
    if (!areaMap) {
        return;
    }
    int32_t gridWidth = 0;
    int32_t gridHeight = 0;
    uint32_t dataSize = 0;
    areaMap->readInt(gridWidth, "AreaMapResX");
    areaMap->readInt(gridHeight, "AreaMapResY");
    areaMap->readDword(dataSize, "AreaMapDataSize");
    auto data = areaMap->getData("AreaMapData");

    // Exploration recorded against a different grid than this area's map
    // describes says nothing about this grid's cells.
    const size_t wordCount = wordCountFor(_gridWidth, _gridHeight);
    if (gridWidth != _gridWidth || gridHeight != _gridHeight ||
        dataSize != wordCount * kBytesPerWord ||
        data.size() != dataSize) {
        return;
    }

    for (size_t cell = 0; cell < _explored.size(); ++cell) {
        const size_t byte = cell / 8;
        const int bit = static_cast<int>(cell % 8);
        _explored[cell] = (static_cast<unsigned char>(data[byte]) & (1u << bit)) != 0;
    }
}

std::shared_ptr<resource::Gff> AreaMapState::writeToGit() const {
    if (!isLoaded()) {
        return nullptr;
    }
    const size_t wordCount = wordCountFor(_gridWidth, _gridHeight);
    ByteBuffer data(wordCount * kBytesPerWord, 0);
    for (size_t cell = 0; cell < _explored.size(); ++cell) {
        if (!_explored[cell]) {
            continue;
        }
        const size_t byte = cell / 8;
        const int bit = static_cast<int>(cell % 8);
        data[byte] = static_cast<char>(
            static_cast<unsigned char>(data[byte]) | (1u << bit));
    }

    return Gff::Builder()
        .type(kAreaMapStructType)
        .field(Gff::Field::newInt("AreaMapResX", _gridWidth))
        .field(Gff::Field::newInt("AreaMapResY", _gridHeight))
        .field(Gff::Field::newDword(
            "AreaMapDataSize", static_cast<uint32_t>(data.size())))
        .field(Gff::Field::newVoid("AreaMapData", std::move(data)))
        .build();
}

} // namespace game

} // namespace reone
