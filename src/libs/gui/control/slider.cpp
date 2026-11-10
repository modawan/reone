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
#include "reone/graphics/textureutil.h"
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
    _maxValue = controlStruct.MAXVALUE;
    _value = std::clamp(controlStruct.CURVALUE, 0, _maxValue);
    _thumb = _resourceSvc.textures.get(controlStruct.THUMB->IMAGE, TextureUsage::GUI);
}

// The thumb keeps its image's width, grown with the layout, and the track's
// height. It travels the track's width less its own.
Control::Extent Slider::thumbExtent() const {
    int width = static_cast<int>(getImageSize(*_thumb).x * _frameLayoutScale);
    int left = _extent.left + (_extent.width - width) * _value / _maxValue;
    return Extent(left, _extent.top, width, _extent.height);
}

void Slider::render(const glm::ivec2 &screenSize,
                    const glm::ivec2 &offset,
                    scene::IRenderPass &pass) {
    if (!_visible) {
        return;
    }
    Control::render(screenSize, offset, pass);
    Extent thumb = thumbExtent();
    pass.drawImage(
        *_thumb,
        {thumb.left + offset.x, thumb.top + offset.y},
        {thumb.width, thumb.height});
}

bool Slider::handleKeyDown(input::KeyCode key) {
    if (!_selected) {
        return false;
    }
    switch (key) {
    case input::KeyCode::Left:
        changeValue(_value - 1);
        return true;
    case input::KeyCode::Right:
        changeValue(_value + 1);
        return true;
    default:
        return false;
    }
}

// A click on the track steps the value one towards the click.
bool Slider::handleClick(int x, int y, int clicks) {
    Extent thumb = thumbExtent();
    if (x < thumb.left) {
        changeValue(_value - 1);
    } else if (x >= thumb.left + thumb.width) {
        changeValue(_value + 1);
    }
    return true;
}

// Only a press on the thumb holds it; a press on the track is a click.
bool Slider::handleMouseDown(int x, int y) {
    return thumbExtent().contains(x, y);
}

void Slider::handleMouseDrag(int x, int y) {
    changeValue((x - _extent.left) * _maxValue / _extent.width);
}

void Slider::setValue(int value) {
    _value = std::clamp(value, 0, _maxValue);
}

void Slider::changeValue(int value) {
    int changed = std::clamp(value, 0, _maxValue);
    if (changed == _value) {
        return;
    }
    _value = changed;
    if (_onValueChanged) {
        _onValueChanged(_value);
    }
}

} // namespace gui

} // namespace reone
