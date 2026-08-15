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

#include "types.h"

namespace reone {

namespace graphics {

struct GraphicsOptions {
    int width {1024};
    int height {768};
    int winScale {100};
    bool fullscreen {false};
    /** Hide the presentation window while retaining a renderable surface. */
    bool headless {false};
    /** Draw 3D scene content: the world and scene-backed GUI panels. */
    bool sceneRender {true};
    bool vsync {true};
    bool grass {true};
    /**
     * Draw the shadow of the selected shadow light, or none at all.
     *
     * Off means the scene has no shadow light for the frame: no shadow pass
     * runs and no shadow term reaches the uniforms, rather than a pass that
     * renders and resolves to nothing. It exists so a comparison against
     * another build can exclude a subsystem whose two implementations are
     * known to differ, and it must therefore mean the same thing in both
     * builds - a switch that disables slightly different work on each side
     * measures itself.
     */
    bool shadows {true};
    /** Admit emitter particles, or leave them out of the frame entirely. */
    bool particles {true};
    bool pbr {false};
    bool ssao {false};
    bool ssr {false};
    bool fxaa {true};
    bool sharpen {true};
    TextureQuality textureQuality {TextureQuality::High};
    int shadowResolution {2048};
    int anisotropicFiltering {2};
    float drawDistance {kDefaultObjectDrawDistance};
    float guiScale {1.0f};
    float guiTextScale {0.5f};
    float guiDialogTextScale {0.6f};
    float guiBorderScale {1.0f};
    float guiListScale {0.5f};
};

} // namespace graphics

} // namespace reone
