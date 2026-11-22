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

#include <algorithm>
#include <cmath>

#include "../control.h"

namespace reone {

namespace gui {

/** A run of pixels along one axis of a track: where it starts and how long it is. */
struct TrackSpan {
    int start {0};
    int length {0};

    int end() const { return start + length; }
    /** Whether \p value lies inside the span, its ends excluded. */
    bool containsInside(int value) const { return start < value && value < end(); }
};

/** \p extent along a track, which runs down when \p vertical and right otherwise. */
inline TrackSpan spanAlong(const Control::Extent &extent, bool vertical) {
    return vertical ? TrackSpan {extent.top, extent.height} : TrackSpan {extent.left, extent.width};
}

/** \p extent across a track. */
inline TrackSpan spanAcross(const Control::Extent &extent, bool vertical) {
    return vertical ? TrackSpan {extent.left, extent.width} : TrackSpan {extent.top, extent.height};
}

/** The point's coordinate along a track. */
inline int pointAlong(int x, int y, bool vertical) {
    return vertical ? y : x;
}

/** The point's coordinate across a track. */
inline int pointAcross(int x, int y, bool vertical) {
    return vertical ? x : y;
}

/**
 * The value a thumb dragged along a track stands at: the value it was
 * pressed at, moved by \p maxValue times the share of its travel the
 * pointer has gone since the press, rounded and kept from 0 to \p maxValue.
 * A thumb with no travel keeps the value it was pressed at.
 *
 * @param pressValue the value when the thumb was pressed
 * @param pressAlong where along the track the thumb was pressed
 * @param pointerAlong where along the track the pointer is now
 * @param travel how far the thumb moves from the least value to the most
 * @param maxValue the most value
 */
inline int draggedValue(int pressValue, int pressAlong, int pointerAlong, int travel, int maxValue) {
    if (travel <= 0 || maxValue <= 0) {
        return std::clamp(pressValue, 0, std::max(maxValue, 0));
    }
    const float moved = static_cast<float>(pointerAlong - pressAlong) / static_cast<float>(travel);
    const auto value = static_cast<int>(std::lround(static_cast<float>(pressValue) + moved * static_cast<float>(maxValue)));
    return std::clamp(value, 0, maxValue);
}

} // namespace gui

} // namespace reone
