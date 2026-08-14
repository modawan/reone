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

#include "reone/gui/control/progressbar.h"

#include "reone/graphics/context.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/renderbuffer.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/texture.h"
#include "reone/graphics/uniforms.h"
#include "reone/gui/gui.h"
#include "reone/resource/gff.h"
#include "reone/resource/provider/textures.h"
#include "reone/scene/render/pass.h"

using namespace reone::graphics;
using namespace reone::resource;

namespace reone {

namespace gui {

void ProgressBar::load(const resource::generated::GUI_BASECONTROL &gui, bool protoItem) {
    Control::load(gui, protoItem);

    auto &controlStruct = *static_cast<const resource::generated::GUI_CONTROLS *>(&gui);
    _startFromLeft = controlStruct.STARTFROMLEFT != 0;
    if (controlStruct.PROGRESS) {
        _progress.fill = _resourceSvc.textures.get(controlStruct.PROGRESS->FILL, TextureUsage::GUI);
        _progress.color = controlStruct.PROGRESS->COLOR;
    }
}

void ProgressBar::render(const glm::ivec2 &screenSize,
                         const glm::ivec2 &offset,
                         scene::IRenderPass &pass) {
    if (!_visible) {
        return;
    }
    // The authored backing - the same capsule art tinted with the border
    // colour - draws at full size behind the cropped fill, so a partially
    // filled bar keeps its full silhouette.
    Control::render(screenSize, offset, pass);
    if (_value == 0 || !_progress.fill) {
        return;
    }
    // The fill fraction follows the bar's long axis, keeping the full
    // authored size on the cross axis, and crops the art to the visible
    // fraction instead of squashing it. Tall bars - the party vitality and
    // Force columns - grow from the bottom; wide bars anchor to their
    // authored start edge. Each pixel edge is rounded once and the size is
    // their difference, keeping the anchored edge aligned with the backing.
    float fraction = _value / 100.0f;
    glm::mat3x4 uv(1.0f);
    if (_extent.height > _extent.width) {
        int bottom = _extent.top + _extent.height + offset.y;
        int top = static_cast<int>(std::lround(bottom - _extent.height * fraction));
        uv[1][1] = fraction;
        uv[2][1] = 0.0f;
        pass.drawImage(
            *_progress.fill,
            {_extent.left + offset.x, top},
            {_extent.width, bottom - top},
            glm::vec4(_progress.color, 1.0f),
            uv);
    } else {
        int left = _extent.left + offset.x;
        int right = left + _extent.width;
        int fillLeft = _startFromLeft
                           ? left
                           : static_cast<int>(std::lround(right - _extent.width * fraction));
        int fillRight = _startFromLeft
                            ? static_cast<int>(std::lround(left + _extent.width * fraction))
                            : right;
        uv[0][0] = fraction;
        uv[2][0] = _startFromLeft ? 0.0f : 1.0f - fraction;
        pass.drawImage(
            *_progress.fill,
            {fillLeft, _extent.top + offset.y},
            {fillRight - fillLeft, _extent.height},
            glm::vec4(_progress.color, 1.0f),
            uv);
    }
}

void ProgressBar::setValue(int value) {
    if (value < 0 || value > 100) {
        throw std::out_of_range("value out of range: " + std::to_string(value));
    }
    _value = value;
}

} // namespace gui

} // namespace reone
