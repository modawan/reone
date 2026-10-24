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

#include "surface.h"

namespace reone {

namespace resource {

class TwoDAs;

}

namespace game {

class ISurfaces {
public:
    virtual ~ISurfaces() = default;

    /**
     * Whether a position over a surface stands on walkable ground: the
     * surface's walk cell is not zero. A surface without a row is not.
     */
    virtual bool isWalkable(int index) const = 0;

    /** A surface's row; a surface the table has no row for reads as a blank row. */
    virtual const Surface &getSurface(int index) const = 0;

    /**
     * The surfaces whose faces grow grass: grass cell not zero. Like the sets
     * below, it is taken over the table's first
     * graphics::Walkmesh::kSurfaceMaterialCount rows only.
     */
    virtual std::set<uint32_t> getGrassSurfaces() const = 0;
    /** The surfaces whose faces are walkable in a walkmesh: walk cell 1. */
    virtual std::set<uint32_t> getWalkableSurfaces() const = 0;
    /** The surfaces whose faces are checked when walking: walkcheck cell 1. */
    virtual std::set<uint32_t> getWalkcheckSurfaces() const = 0;
    /** The surfaces whose faces block line of sight: lineofsight cell 1. */
    virtual std::set<uint32_t> getLineOfSightSurfaces() const = 0;
};

class Surfaces : public ISurfaces, boost::noncopyable {
public:
    Surfaces(resource::TwoDAs &twoDas) :
        _twoDas(twoDas) {
    }

    void init();

    bool isWalkable(int index) const override;

    const Surface &getSurface(int index) const override;

    std::set<uint32_t> getGrassSurfaces() const override;
    std::set<uint32_t> getWalkableSurfaces() const override;
    std::set<uint32_t> getWalkcheckSurfaces() const override;
    std::set<uint32_t> getLineOfSightSurfaces() const override;

private:
    resource::TwoDAs &_twoDas;

    std::vector<Surface> _surfaces;

    inline std::set<uint32_t> getSurfaceIndices(const std::function<bool(const Surface &)> &pred) const;
};

} // namespace game

} // namespace reone
