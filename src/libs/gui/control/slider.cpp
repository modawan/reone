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

#include "reone/gui/control/slider.h"

#include <algorithm>

#include "reone/graphics/texture.h"
#include "reone/gui/control/track.h"
#include "reone/input/event.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/resources.h"
#include "reone/scene/render/pass.h"

using namespace reone::graphics;
using namespace reone::resource;

namespace reone {

namespace gui {

void Slider::load(const resource::generated::GUI_BASECONTROL &gui, bool protoItem) {
    Control::load(gui, protoItem);

    auto &controlStruct = *static_cast<const resource::generated::GUI_CONTROLS *>(&gui);
    if (controlStruct.MAXVALUE >= 0) {
        _maxValue = controlStruct.MAXVALUE;
    }
    setValue(controlStruct.CURVALUE);
    if (controlStruct.THUMB && !controlStruct.THUMB->IMAGE.empty()) {
        _thumb = _resourceSvc.textures.get(controlStruct.THUMB->IMAGE, TextureUsage::GUI);
    }
}

void Slider::setValue(int value) {
    _value = std::clamp(value, 0, std::max(0, _maxValue));
}

void Slider::step(int steps) {
    if (moveThumb(steps) && _onValueChanged) {
        _onValueChanged(_value);
    }
}

bool Slider::moveThumb(int steps) {
    const int previous = _value;
    setValue(_value + steps);
    if (_value == previous) {
        return false;
    }
    if (_onStep) {
        _onStep();
    }
    return true;
}

// The thumb spans the track's breadth; along the track it keeps its image's
// length, grown with the layout, and sits in proportion to the value.
Control::Extent Slider::thumbExtent() const {
    Extent extent {_extent.left, _extent.top, _extent.width, _extent.height};
    if (!_thumb) {
        return extent;
    }
    const bool vertical = isVertical();
    const glm::ivec2 image {frameArtSize(_thumb)};
    int &start = vertical ? extent.top : extent.left;
    int &length = vertical ? extent.height : extent.width;
    const int trackLength = length;
    length = std::min(trackLength, vertical ? image.y : image.x);
    if (_maxValue > 0) {
        start += (trackLength - length) * _value / _maxValue;
    }
    return extent;
}

// The thumb takes a press anywhere on it; the track either side of it takes
// one strictly inside the frame's inner extent.
Slider::Press Slider::pressAt(int x, int y) const {
    const Extent thumb = thumbExtent();
    if (thumb.contains(x, y)) {
        return Press::Thumb;
    }
    const bool vertical = isVertical();
    const Extent track = innerExtent(_extent);
    if (!spanAcross(track, vertical).containsInside(pointAcross(x, y, vertical))) {
        return Press::None;
    }
    const int along = pointAlong(x, y, vertical);
    const TrackSpan trackAlong = spanAlong(track, vertical);
    const TrackSpan thumbAlong = spanAlong(thumb, vertical);
    if (trackAlong.start < along && along < thumbAlong.start) {
        return Press::TrackBefore;
    }
    if (thumbAlong.end() < along && along < trackAlong.end()) {
        return Press::TrackAfter;
    }
    return Press::None;
}

bool Slider::handleMouseDown(int x, int y) {
    _pressValue = _value;
    const Press press = pressAt(x, y);
    _dragging = press == Press::Thumb;
    switch (press) {
    case Press::Thumb:
        _thumbPressAlong = pointAlong(x, y, isVertical());
        break;
    case Press::TrackBefore:
        moveThumb(-1);
        break;
    case Press::TrackAfter:
        moveThumb(1);
        break;
    default:
        break;
    }
    return true;
}

// The thumb keeps the point it was pressed at under the pointer.
bool Slider::handleMouseDrag(int x, int y) {
    if (_dragging) {
        const bool vertical = isVertical();
        const int travel = spanAlong(_extent, vertical).length - spanAlong(thumbExtent(), vertical).length;
        setValue(draggedValue(_pressValue, _thumbPressAlong, pointAlong(x, y, vertical), travel, _maxValue));
    }
    return true;
}

// The release reports the value the press and drag left, wherever it is, and
// reports it over the slider even when it did not move.
bool Slider::handleMouseUp(int x, int y, bool over, int clicks) {
    _dragging = false;
    if (_onValueChanged && (over || _value != _pressValue)) {
        _onValueChanged(_value);
    }
    return false;
}

// The GUI gives the arrow keys only to the control holding the keyboard.
bool Slider::handleKeyDown(input::KeyCode key) {
    const input::KeyCode decrease = isVertical() ? input::KeyCode::Up : input::KeyCode::Left;
    const input::KeyCode increase = isVertical() ? input::KeyCode::Down : input::KeyCode::Right;
    if (key == decrease) {
        step(-1);
        return true;
    }
    if (key == increase) {
        step(1);
        return true;
    }
    return false;
}

bool Slider::handleMouseWheel(int x, int y) {
    if (y != 0) {
        // The wheel turned up moves the thumb up or right; a vertical
        // slider's value grows downwards.
        step(isVertical() ? -y : y);
    }
    return true;
}

void Slider::render(const glm::ivec2 &screenSize, const glm::ivec2 &offset, scene::IRenderPass &pass) {
    if (!_visible) {
        return;
    }
    Control::render(screenSize, offset, pass);
    if (!_thumb) {
        return;
    }
    const Extent thumb = thumbExtent();
    pass.drawImage(*_thumb, {thumb.left + offset.x, thumb.top + offset.y}, {thumb.width, thumb.height});
}

} // namespace gui

} // namespace reone
