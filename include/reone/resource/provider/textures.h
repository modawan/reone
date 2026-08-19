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

#include <utility>

#include "reone/system/cache.h"

#include "reone/graphics/types.h"

namespace reone {

namespace graphics {

class GraphicsOptions;
class Texture;

} // namespace graphics

namespace resource {

class IResources;

class ITextures {
public:
    virtual ~ITextures() {
    }

    virtual void clear() = 0;

    virtual std::shared_ptr<graphics::Texture> get(const std::string &resRef, graphics::TextureUsage usage = graphics::TextureUsage::Default) = 0;
};

class Textures : public ITextures, boost::noncopyable {
public:
    Textures(graphics::GraphicsOptions &options, IResources &resources) :
        _options(options),
        _resources(resources) {
    }

    void init();

    void clear() override;

    std::shared_ptr<graphics::Texture> get(const std::string &resRef, graphics::TextureUsage usage = graphics::TextureUsage::Default) override;

private:
    int _activeUnit {0};

    graphics::GraphicsOptions &_options;
    IResources &_resources;

    /**
     * Keyed by BOTH the lowercased resref and the usage it was decoded for.
     *
     * doGet is usage-dependent - the readers take it, and a grayscale bump map
     * is promoted to a 2D array under BumpMap and left flat under anything else
     * - so a key that drops the usage hands back a texture built for a
     * different slot. Measured on danm14ab: `loadscreen3` is fetched as GUI and
     * then as MainTex, and the admission's `is2DArray` assertion is one asset
     * away from aborting a frame the same way.
     *
     * Lowercased on the way in rather than only on the way out: the resrefs
     * arrive in mixed case from the authored data, and looking one up under its
     * original spelling used to miss an entry stored in lower case and decode
     * the whole TPC again. Measured at over two hundred redundant decodes of
     * `CM_Baremetal` in a single 900-frame run.
     */
    Cache<std::pair<std::string, graphics::TextureUsage>, graphics::Texture> _cache;

    std::shared_ptr<graphics::Texture> doGet(const std::string &resRef, graphics::TextureUsage usage);
};

} // namespace resource

} // namespace reone
