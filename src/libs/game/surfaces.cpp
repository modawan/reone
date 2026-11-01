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

#include "reone/game/surfaces.h"

#include "reone/graphics/walkmesh.h"
#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"

using namespace reone::resource;

namespace reone {

namespace game {

void Surfaces::init() {
    std::shared_ptr<TwoDA> surfacemat(_twoDas.get("surfacemat"));
    if (!surfacemat) {
        return;
    }
    for (int row = 0; row < surfacemat->getRowCount(); ++row) {
        Surface surface;
        surface.label = surfacemat->getString(row, "label");
        // Only the value 1 sets a surface's walk check and line of sight
        // flags.
        surface.walk = surfacemat->getInt(row, "walk");
        surface.walkcheck = surfacemat->getInt(row, "walkcheck") == 1;
        surface.lineOfSight = surfacemat->getInt(row, "lineofsight") == 1;
        surface.grass = surfacemat->getBool(row, "grass");
        surface.name = surfacemat->getString(row, "name");
        _surfaces.push_back(std::move(surface));
    }
}

bool Surfaces::isWalkable(int index) const {
    // A material counts modulo the materials a walkmesh can name; a surface
    // the table has no row for has a blank walk cell.
    const uint32_t row = static_cast<uint32_t>(index) % graphics::Walkmesh::kSurfaceMaterialCount;
    return row < _surfaces.size() && _surfaces[row].walk != 0;
}

const Surface &Surfaces::getSurface(int index) const {
    // A surface the table has no row for reads as a blank row.
    static const Surface kBlankSurface;
    if (index < 0 || index >= static_cast<int>(_surfaces.size())) {
        return kBlankSurface;
    }
    return _surfaces[index];
}

std::set<uint32_t> Surfaces::getSurfaceIndices(const std::function<bool(const Surface &)> &pred) const {
    // Only the first rows, one per material a set can name, take part.
    const size_t count = std::min<size_t>(_surfaces.size(), graphics::Walkmesh::kSurfaceMaterialCount);
    std::set<uint32_t> result;
    for (size_t i = 0; i < count; ++i) {
        if (pred(_surfaces[i])) {
            result.insert(static_cast<uint32_t>(i));
        }
    }
    return result;
}

std::set<uint32_t> Surfaces::getGrassSurfaces() const {
    return getSurfaceIndices([](auto &surface) { return surface.grass; });
}

std::set<uint32_t> Surfaces::getWalkableSurfaces() const {
    return getSurfaceIndices([](auto &surface) { return surface.walk == 1; });
}

std::set<uint32_t> Surfaces::getWalkcheckSurfaces() const {
    return getSurfaceIndices([](auto &surface) { return surface.walkcheck; });
}

std::set<uint32_t> Surfaces::getLineOfSightSurfaces() const {
    return getSurfaceIndices([](auto &surface) { return surface.lineOfSight; });
}

} // namespace game

} // namespace reone
