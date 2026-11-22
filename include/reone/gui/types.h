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

#include <functional>

namespace reone {

namespace gui {

class Control;

enum class ControlType {
    Invalid = -1,
    Panel = 2,
    Label = 4,
    ImageButton = 5,
    Button = 6,
    ToggleButton = 7,
    Slider = 8,
    ScrollBar = 9,
    ProgressBar = 10,
    ListBox = 11,
    IconChain = 12
};

/** How the controls of a GUI draw their borders and fills. */
struct FillRules {
    /**
     * A fill of the Extent style is stretched over its control's whole
     * extent; without, such a fill is not drawn.
     */
    bool extentFills {false};
    /**
     * Frames (dimension, inner offsets, corner and edge art) and fill art
     * keep their own size, at the border scale, against the laid-out area
     * they frame and fill; without, they are scaled by the factor their
     * control is laid out at.
     */
    bool unscaledArt {false};
    /** A control this holds for once it is loaded stretches its fill over its area, whatever its layout names. */
    std::function<bool(const Control &)> stretchesFill;
    /**
     * A frame's bottom-left and top-right corners mirror the corner art,
     * upside down and left to right; without, the bottom-left, bottom-right
     * and top-right corners turn it a quarter, a half and three quarters
     * anticlockwise. The bottom-right corner turns it a half either way.
     */
    bool mirroredCorners {false};
};

} // namespace gui

} // namespace reone
