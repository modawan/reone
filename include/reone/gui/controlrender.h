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

#include "reone/system/arrayref.h"

namespace reone {

namespace gui {

/** Which parts of a Border a single render call should draw. */
enum class BorderRenderPart {
    /** The whole border: the fill behind it and the frame around it. */
    All,
    /** Only the fill a border paints inside its frame. */
    Fill,
    /** Only the edges and corners a border draws around that fill. */
    Frame
};

/**
 * Draw a control's layers in composition order.
 *
 * A control hosting a 3D scene has to put that scene inside its border rather
 * than beside it: over the fill the border paints as a backdrop, but under the
 * frame it draws around the outside. Treating a border as one indivisible
 * layer forces a choice between the two, and either way something authored is
 * lost - an opaque fill buries the scene, or the scene covers the frame.
 *
 * Both passes walk the same borders, so a control showing its HILIGHT state
 * cannot end up filled by one state and framed by the other.
 *
 * A control with no scene keeps drawing its borders whole and then its text,
 * exactly as before.
 */
template <typename Border, typename RenderBorder, typename RenderScene, typename RenderText>
void renderControlLayers(
    ArrayRef<const Border *> borders,
    bool hasScene,
    RenderBorder renderBorder,
    RenderScene renderScene,
    RenderText renderText) {

    if (!hasScene) {
        for (const auto *border : borders) {
            renderBorder(*border, BorderRenderPart::All);
        }
        renderText();
        return;
    }

    for (const auto *border : borders) {
        renderBorder(*border, BorderRenderPart::Fill);
    }
    renderScene();
    for (const auto *border : borders) {
        renderBorder(*border, BorderRenderPart::Frame);
    }
    renderText();
}

} // namespace gui

} // namespace reone
