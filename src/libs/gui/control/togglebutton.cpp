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

#include "reone/gui/control/togglebutton.h"

#include <cmath>

#include "reone/graphics/renderbuffer.h"

namespace reone {

namespace gui {

// A checkbox's box is 25 pixels square, at the button's left and two pixels
// below its middle; the text starts 30 pixels in.
static constexpr int kCheckBoxSize = 25;
static constexpr int kCheckBoxDrop = 2;
static constexpr int kCheckBoxTextInset = 30;

void ToggleButton::load(const resource::generated::GUI_BASECONTROL &gui, bool protoItem) {
    Button::load(gui, protoItem);
    if (protoItem) {
        // A list prototype keeps its on frames for rows that carry a state;
        // it starts off, since every row shares it.
        auto &proto = static_cast<const resource::generated::GUI_CONTROLS_PROTOITEM &>(gui);
        auto authored = [](const resource::generated::GUI_BORDER &border) {
            return !border.FILL.empty() || !border.EDGE.empty() || !border.CORNER.empty();
        };
        if (authored(proto.SELECTED)) {
            _onBorder = createBorder(proto.SELECTED);
        }
        if (authored(proto.HILIGHTSELECTED)) {
            _onHilight = createBorder(proto.HILIGHTSELECTED);
        }
    } else {
        auto &controlStruct = static_cast<const resource::generated::GUI_CONTROLS &>(gui);
        if (controlStruct.SELECTED) {
            _onBorder = createBorder(*controlStruct.SELECTED);
        }
        if (controlStruct.HILIGHTSELECTED) {
            _onHilight = createBorder(*controlStruct.HILIGHTSELECTED);
        }
        _on = controlStruct.ISSELECTED != 0;
    }
    scaleOnFrames();
}

void ToggleButton::setPresentationScale(float frameLayoutScale, float textLayoutScale) {
    // A checkbox lays its text out beside the box, so the box must be sized
    // before the text is broken into lines.
    _layoutScale = frameLayoutScale;
    Button::setPresentationScale(frameLayoutScale, textLayoutScale);
    scaleOnFrames();
}

void ToggleButton::showAsCheckBox(const glm::vec3 &color, const glm::vec3 &hilightColor) {
    _checkBox = true;
    for (auto *border : {&_border, &_onBorder}) {
        if (*border) {
            (*border)->color = color;
        }
    }
    for (auto *border : {&_hilight, &_onHilight}) {
        if (*border) {
            (*border)->color = hilightColor;
        }
    }
    _text.color = color;
    _text.selectedColor = hilightColor;
    updateTextLines();
}

Control::Extent ToggleButton::borderExtent() const {
    if (!_checkBox) {
        return Button::borderExtent();
    }
    const int size = static_cast<int>(std::lround(kCheckBoxSize * _layoutScale));
    const int drop = static_cast<int>(std::lround(kCheckBoxDrop * _layoutScale));
    return Extent(_extent.left, _extent.top + (_extent.height - size) / 2 + drop, size, size);
}

Control::Extent ToggleButton::textExtent() const {
    if (!_checkBox) {
        return Button::textExtent();
    }
    const int inset = static_cast<int>(std::lround(kCheckBoxTextInset * _layoutScale));
    return Extent(_extent.left + inset, _extent.top, _extent.width - inset, _extent.height);
}

void ToggleButton::scaleOnFrames() {
    if (_onBorder && _border) {
        _onBorder->dimension = _border->dimension;
    }
    if (_onHilight && _hilight) {
        _onHilight->dimension = _hilight->dimension;
    }
}

const std::shared_ptr<Control::Border> &ToggleButton::shownBorder() const {
    return _on && _onBorder ? _onBorder : _border;
}

const std::shared_ptr<Control::Border> &ToggleButton::shownHilight() const {
    return _on && _onHilight ? _onHilight : _hilight;
}

const glm::vec3 &ToggleButton::getBorderColor() const {
    return _on && _onColor ? *_onColor : Button::getBorderColor();
}

void ToggleButton::toggle() {
    _on = !_on;
}

void ToggleButton::setOnColor(const glm::vec3 &color) {
    _onColor = color;
}

} // namespace gui

} // namespace reone
