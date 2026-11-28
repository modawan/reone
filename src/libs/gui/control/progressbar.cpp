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

#include <algorithm>

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
    setMaxValue(controlStruct.MAXVALUE);
    setCurValue(controlStruct.CURVALUE);
    _startFromLeft = controlStruct.STARTFROMLEFT != 0;
    if (controlStruct.PROGRESS) {
        _progress = *createBorder(*controlStruct.PROGRESS);
        _authoredProgressDimension = controlStruct.PROGRESS->DIMENSION;
        scaleOwnFrames();
    }
}

void ProgressBar::render(const glm::ivec2 &screenSize,
                         const glm::ivec2 &offset,
                         scene::IRenderPass &pass) {
    if (!_visible) {
        return;
    }
    Control::render(screenSize, offset, pass);
    // The progress border, frame and fill, is laid over the inner extent cut
    // to the value's share, its art fitted to the cut by its fill style. The
    // share starts at the bar's top or left edge, or at its bottom or right
    // edge when the bar does not start from the left.
    const Extent inner = innerExtent(_extent);
    const float fraction {fillFraction(_curValue, _maxValue)};
    Extent fill = inner;
    if (_extent.height > _extent.width) {
        fill.height = static_cast<int>(static_cast<float>(inner.height) * fraction);
        if (!_startFromLeft) {
            fill.top = inner.top + inner.height - fill.height;
        }
    } else {
        fill.width = static_cast<int>(static_cast<float>(inner.width) * fraction);
        if (!_startFromLeft) {
            fill.left = inner.left + inner.width - fill.width;
        }
    }
    if (fill.width <= 0 || fill.height <= 0) {
        return;
    }
    renderBorder(
        _progress,
        offset + glm::ivec2(fill.left - _extent.left, fill.top - _extent.top),
        {fill.width, fill.height},
        pass,
        BorderRenderPart::All,
        false,
        _progress.color);
}

float ProgressBar::fillFraction(int curValue, int maxValue) {
    if (maxValue <= 0) {
        return 1.0f;
    }
    return static_cast<float>(std::clamp(curValue, 0, maxValue)) / static_cast<float>(maxValue);
}

void ProgressBar::setMaxValue(int value) {
    if (value >= 0) {
        _maxValue = value;
    }
}

void ProgressBar::setCurValue(int value) {
    _curValue = std::clamp(value, 0, _maxValue);
}

void ProgressBar::scaleOwnFrames() {
    _progress.dimension = scaledFrameSize(_authoredProgressDimension);
}

} // namespace gui

} // namespace reone
