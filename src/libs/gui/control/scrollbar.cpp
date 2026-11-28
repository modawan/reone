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

#include "reone/gui/control/scrollbar.h"

#include <algorithm>

#include "reone/graphics/context.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/renderbuffer.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/texture.h"
#include "reone/graphics/uniforms.h"
#include "reone/gui/control/track.h"
#include "reone/resource/gff.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/resources.h"
#include "reone/scene/render/pass.h"

#include "reone/gui/gui.h"

using namespace reone::graphics;
using namespace reone::resource;

namespace reone {

namespace gui {

void ScrollBar::load(const resource::generated::GUI_BASECONTROL &gui, bool protoItem) {
    Control::load(gui, protoItem);

    auto &scrollbarStruct = *static_cast<const resource::generated::GUI_CONTROLS_SCROLLBAR *>(&gui);
    if (scrollbarStruct.DIR) {
        auto &dirImage = scrollbarStruct.DIR->IMAGE;
        _dir.image = _resourceSvc.textures.get(dirImage, TextureUsage::GUI);
    }
    if (scrollbarStruct.THUMB) {
        auto &thumbImage = scrollbarStruct.THUMB->IMAGE;
        _thumb.image = _resourceSvc.textures.get(thumbImage, TextureUsage::GUI);
    }
    _arrowsOnly = (scrollbarStruct.DRAWMODE & 1) != 0;
}

// The bar is a track between two arrows. Each arrow keeps its art's size,
// at most half the bar's length and its breadth, centred across the bar; the
// track takes the rest. The thumb lies in the track's inner extent and fills
// it with fewer than two positions; otherwise the thumb's length is the inner
// length's share for the visible value among the visible and maximum values,
// at least 4 but no longer than the inner length, and it moves from the inner
// start to the inner end as the value goes from 0 to the maximum less one.
ScrollBar::Layout ScrollBar::layout() const {
    Layout layout;
    layout.vertical = _extent.height > _extent.width;
    const bool vertical = layout.vertical;
    const int length = vertical ? _extent.height : _extent.width;
    const int breadth = vertical ? _extent.width : _extent.height;
    int arrowLength = 0;
    int arrowBreadth = breadth;
    if (_dir.image) {
        const glm::ivec2 art {frameArtSize(_dir.image)};
        arrowLength = vertical ? art.y : art.x;
        arrowBreadth = vertical ? art.x : art.y;
    }
    if (2 * arrowLength > length) {
        arrowLength = length / 2;
    }
    int arrowStart = 0;
    if (breadth > arrowBreadth) {
        arrowStart = (breadth - arrowBreadth) / 2;
    } else {
        arrowBreadth = breadth;
    }
    layout.arrowLength = arrowLength;
    layout.firstArrow = vertical
                            ? Extent {_extent.left + arrowStart, _extent.top, arrowBreadth, arrowLength}
                            : Extent {_extent.left, _extent.top + arrowStart, arrowLength, arrowBreadth};
    layout.lastArrow = layout.firstArrow;
    if (vertical) {
        layout.lastArrow.top = _extent.top + _extent.height - arrowLength;
    } else {
        layout.lastArrow.left = _extent.left + _extent.width - arrowLength;
    }
    layout.track = vertical
                       ? Extent {_extent.left, _extent.top + arrowLength, _extent.width, _extent.height - 2 * arrowLength}
                       : Extent {_extent.left + arrowLength, _extent.top, _extent.width - 2 * arrowLength, _extent.height};
    layout.inner = _border ? borderInnerExtent(*_border, layout.track) : layout.track;
    layout.thumb = layout.inner;
    const int max = _state.maxValue;
    if (max >= 2) {
        int &start = vertical ? layout.thumb.top : layout.thumb.left;
        int &size = vertical ? layout.thumb.height : layout.thumb.width;
        const int inner = size;
        const float share = static_cast<float>(_state.visibleValue) / static_cast<float>(_state.visibleValue + max);
        const int thumbLength = static_cast<int>(static_cast<float>(inner) * share);
        size = std::min(std::max(thumbLength, 4), std::max(inner, 0));
        start += (inner - size) * std::clamp(_state.curValue, 0, max - 1) / (max - 1);
    }
    return layout;
}

// Unless the bar shows its arrows only, the track's frame, the thumb and both
// arrows are drawn. Otherwise, while the visible value is below the maximum,
// the first arrow shows once the list is scrolled and the last while the
// visible and current values together fall short of the maximum; a list that
// shows one row counts one more while it is not scrolled to the maximum itself,
// unless its range already counts the last position.
void ScrollBar::render(const glm::ivec2 &screenSize,
                       const glm::ivec2 &offset,
                       scene::IRenderPass &pass) {
    if (!_visible) {
        return;
    }
    const Layout layout = this->layout();
    bool firstArrow = true;
    bool lastArrow = true;
    if (_arrowsOnly) {
        const int max = _state.maxValue;
        const int visible = _state.visibleValue;
        int limit = max;
        if (visible <= 1 && !_state.extendedRange) {
            limit += _state.curValue != max ? 1 : 0;
        }
        firstArrow = visible < max && _state.curValue > 0;
        lastArrow = visible < max && visible + _state.curValue < limit;
    } else {
        renderTrack(offset, layout, pass);
    }
    if (!_dir.image) {
        return;
    }
    const glm::vec4 arrowColor {1.0f, 1.0f, 1.0f, _arrowsPulsing ? _gui.arrowPulsingAlpha() : 1.0f};
    const Extent &first = layout.firstArrow;
    if (firstArrow) {
        pass.drawImage(*_dir.image, {first.left + offset.x, first.top + offset.y}, {first.width, first.height}, arrowColor);
    }
    if (lastArrow) {
        // The last arrow is the first one's art, flipped along the bar.
        const Extent &last = layout.lastArrow;
        const glm::mat3x4 uv {layout.vertical ? upsideDownUV() : leftToRightUV()};
        pass.drawImage(*_dir.image, {last.left + offset.x, last.top + offset.y}, {last.width, last.height}, arrowColor, uv);
    }
}

// The track's frame fills the bar between the arrows; the thumb lies in its
// inner extent.
void ScrollBar::renderTrack(const glm::ivec2 &offset, const Layout &layout, scene::IRenderPass &pass) {
    const Extent &track = layout.track;
    if (_border) {
        renderBorder(*_border, offset + glm::ivec2(track.left - _extent.left, track.top - _extent.top), {track.width, track.height}, pass);
    }
    if (!_thumb.image) {
        return;
    }
    const Extent &thumb = layout.thumb;
    pass.drawImage(*_thumb.image, {thumb.left + offset.x, thumb.top + offset.y}, {thumb.width, thumb.height});
}

void ScrollBar::setScrollState(ScrollState state) {
    if (state.visibleValue < 0 || (state.visibleValue == 0 && state.maxValue != 0)) {
        state.visibleValue = 1;
    }
    _state = std::move(state);
}

ScrollBar::Part ScrollBar::partAt(int x, int y) const {
    const Layout layout = this->layout();
    // The point and every extent are taken along the bar and across it.
    const bool vertical = layout.vertical;
    const int along = pointAlong(x, y, vertical);
    const int across = pointAcross(x, y, vertical);
    const TrackSpan bar = spanAlong(_extent, vertical);
    const TrackSpan sides = spanAcross(_extent, vertical);
    const TrackSpan innerSides = spanAcross(layout.inner, vertical);
    const TrackSpan thumb = spanAlong(layout.thumb, vertical);
    const TrackSpan thumbSides = spanAcross(layout.thumb, vertical);
    const int arrow = layout.arrowLength;

    if (thumb.start <= along && along <= thumb.end() && thumbSides.start <= across && across <= thumbSides.end()) {
        return Part::Thumb;
    }
    if (bar.start < along && along <= bar.start + arrow && sides.containsInside(across)) {
        return Part::FirstArrow;
    }
    if (bar.end() - arrow <= along && along < bar.end() && sides.containsInside(across)) {
        return Part::LastArrow;
    }
    if (bar.start + arrow < along && along < thumb.start && innerSides.containsInside(across)) {
        return Part::TrackBeforeThumb;
    }
    if (thumb.end() < along && along < bar.end() - arrow && innerSides.containsInside(across)) {
        return Part::TrackAfterThumb;
    }
    return Part::None;
}

void ScrollBar::press(int x, int y, IScrollable &list) {
    setScrollState(list.getScrollState());
    _held = true;
    _pointer = {x, y};
    _pressedPart = partAt(x, y);
    _repeatTimer = kPressRepeatDelay;
    _holdClock = _gui.textureTime();
    if (_pressedPart == Part::Thumb) {
        _thumbPressAlong = pointAlong(x, y, layout().vertical);
        _thumbPressPosition = list.getScrollPosition();
    }
    act(_pressedPart, list);
}

void ScrollBar::drag(int x, int y, IScrollable &list) {
    _pointer = {x, y};
    if (_pressedPart != Part::Thumb) {
        return;
    }
    const Layout layout = this->layout();
    const bool vertical = layout.vertical;
    const int travel = spanAlong(layout.inner, vertical).length - spanAlong(layout.thumb, vertical).length;
    const int last = std::max(list.getScrollPositions() - 1, 0);
    list.scrollToPosition(draggedValue(_thumbPressPosition, _thumbPressAlong, pointAlong(x, y, vertical), travel, last));
}

void ScrollBar::hold(IScrollable &list) {
    if (!_held) {
        return;
    }
    const double now {_gui.textureTime()};
    const float dt {static_cast<float>(now - _holdClock)};
    _holdClock = now;
    setScrollState(list.getScrollState());
    if (_pressedPart == Part::None || _pressedPart == Part::Thumb || partAt(_pointer.x, _pointer.y) != _pressedPart) {
        return;
    }
    // The repeats keep time at any frame rate; a stall longer than
    // kLongestHoldStep counts as that long.
    _repeatTimer -= std::min(dt, kLongestHoldStep);
    while (_repeatTimer < 0.0f) {
        _repeatTimer += kPressRepeatInterval;
        act(_pressedPart, list);
    }
}

// An arrow moves the list by a position only while there is one to move to.
void ScrollBar::act(Part part, IScrollable &list) {
    switch (part) {
    case Part::FirstArrow:
        list.scrollBy(-1);
        break;
    case Part::LastArrow:
        list.scrollBy(1);
        break;
    case Part::TrackBeforeThumb:
        list.scrollPage(false);
        break;
    case Part::TrackAfterThumb:
        list.scrollPage(true);
        break;
    case Part::Thumb:
    case Part::None:
        break;
    }
}

bool ScrollBar::release() {
    const bool held = _held;
    _held = false;
    _pressedPart = Part::None;
    return held;
}

ScrollBar *ScrollBar::of(const std::shared_ptr<Control> &control) {
    return control && control->type() == ControlType::ScrollBar ? static_cast<ScrollBar *>(control.get()) : nullptr;
}

bool ScrollBar::pressIfOn(int x, int y, IScrollable &list) {
    if (!_visible || !_extent.contains(x, y)) {
        return false;
    }
    press(x, y, list);
    return true;
}

bool ScrollBar::dragIfHeld(int x, int y, IScrollable &list) {
    if (!_held) {
        return false;
    }
    drag(x, y, list);
    return true;
}

} // namespace gui

} // namespace reone
