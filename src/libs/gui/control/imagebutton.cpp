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

#include "reone/gui/control/imagebutton.h"

#include <algorithm>
#include <cmath>

#include "reone/graphics/context.h"
#include "reone/graphics/di/services.h"
#include "reone/graphics/font.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/renderbuffer.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/texture.h"
#include "reone/graphics/textureutil.h"
#include "reone/graphics/uniforms.h"
#include "reone/gui/gui.h"
#include "reone/resource/provider/fonts.h"
#include "reone/resource/provider/textures.h"
#include "reone/scene/render/pass.h"

using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;

namespace reone {

namespace gui {

void ImageButton::setIconFont(std::string baseFont) {
    _iconBaseFont = std::move(baseFont);
    resetFont();
}

void ImageButton::resetFont() {
    Control::resetFont();
    _iconFont = _resourceSvc.fonts.get(fontName(_iconBaseFont, _gui.smallFonts()));
    if (!_iconFont) {
        _iconFont = _text.font;
    }
}

void ImageButton::render(
    const glm::ivec2 &offset,
    const std::vector<std::string> &text,
    const std::string &iconText,
    const std::shared_ptr<Texture> &iconTexture,
    const std::shared_ptr<Texture> &iconFrame,
    IRenderPass &pass,
    const std::optional<glm::vec3> &iconTextColor) {

    if (!_visible)
        return;

    glm::ivec2 borderOffset(offset);
    borderOffset.x += _extent.height;

    glm::ivec2 size(_extent.width - _extent.height, _extent.height);

    if (_selected && _hilight) {
        renderBorder(*_hilight, borderOffset, size, pass);
    } else if (_border) {
        renderBorder(*_border, borderOffset, size, pass);
    }

    renderIcon(offset, iconText, iconTexture, iconFrame, pass, iconTextColor);

    if (!text.empty()) {
        renderText(text, borderOffset, size, pass);
    }
}

void ImageButton::renderIcon(
    const glm::ivec2 &offset,
    const std::string &iconText,
    const std::shared_ptr<Texture> &iconTexture,
    const std::shared_ptr<Texture> &iconFrame,
    IRenderPass &pass,
    const std::optional<glm::vec3> &iconTextColor) const {

    // The count takes the colour the frames show unless given its own.
    IconSlotLook look;
    look.frameColor = iconFrameColor(*this, _iconFrameHilightColor, _iconFrameHilightPulsing ? std::optional<float>(_gui.pulsingAlpha()) : std::nullopt);
    look.textColor = iconTextColor.value_or(shownFrameColor());
    look.centred = _iconCentred;
    look.artScale = _frameLayoutScale;
    look.textFont = _iconFont.get();
    look.textBox = _iconTextBox ? &*_iconTextBox : nullptr;
    look.textScale = _scale;
    renderIconSlot(pass, IconSlot {iconFrame.get(), iconTexture.get(), iconText}, look, {offset.x + _extent.left, offset.y + _extent.top}, _extent.height);
}

glm::vec4 ImageButton::iconFrameColor(const Control &row, const std::optional<glm::vec3> &hilightColor, std::optional<float> pulse) {
    if (row.isSelected() && row.hasHilight()) {
        return {hilightColor.value_or(row.shownFrameColor()), pulse.value_or(1.0f)};
    }
    return {row.shownFrameColor(), row.frameAlpha()};
}

void ImageButton::renderIconSlot(IRenderPass &pass, const IconSlot &slot, const IconSlotLook &look,
                                 const glm::ivec2 &position, int side) {
    if (!slot.frame && !slot.icon) {
        return;
    }
    if (slot.frame) {
        pass.drawImage(*slot.frame, position, {side, side}, look.frameColor);
    }
    if (slot.icon) {
        const auto [iconPosition, iconSize] = iconRect(*slot.icon, look.centred, look.artScale, position, side);
        pass.drawIcon(*slot.icon, iconPosition, iconSize);
    }
    if (slot.text.empty() || !look.textFont) {
        return;
    }
    if (look.textBox) {
        renderIconText(*look.textFont, slot.text, look.textColor, look.textScale, position, side, *look.textBox);
        return;
    }
    // Without a box the count sits in the icon's lower right corner,
    // right-aligned, its middle half a line above the icon's bottom.
    const glm::vec3 textPosition {static_cast<float>(position.x + side),
                                  static_cast<float>(position.y + side) - 0.5f * look.textFont->height() * look.textScale,
                                  0.0f};
    look.textFont->render(slot.text, textPosition, glm::vec4(look.textColor, 1.0f), TextGravity::LeftCenter, look.textScale);
}

std::pair<glm::ivec2, glm::ivec2> ImageButton::iconRect(const Texture &icon, bool centred, float artScale,
                                                        const glm::ivec2 &position, int side) {
    if (!centred) {
        return {position, {side, side}};
    }
    // The art is measured whole and rounded, as frame art is.
    const glm::ivec2 image {getImageSize(icon)};
    const glm::ivec2 size {
        std::min(static_cast<int>(std::lround(image.x * artScale)), side),
        std::min(static_cast<int>(std::lround(image.y * artScale)), side)};
    return {position + (glm::ivec2(side) - size) / 2, size};
}

void ImageButton::renderIconText(Font &font, std::string_view text, const glm::vec3 &color, float scale,
                                 const glm::ivec2 &iconPosition, int iconSize, const IconTextBox &box) {
    // A box of no size places nothing.
    if (box.iconSize <= 0.0f) {
        return;
    }
    static constexpr size_t kWideTextLength = 3;
    const bool wide {text.size() >= kWideTextLength};
    const float width {wide ? box.wideWidth : box.narrowWidth};
    const float unit {iconSize / box.iconSize};
    const float left {(box.iconSize - width) * unit};
    const float right {left + (width + box.growth) * unit};
    const float bottom {(box.iconSize + box.growth) * unit};
    const bool rightAligned {wide && box.wideRightAligned};
    glm::vec3 position {0.0f};
    position.x = static_cast<float>(iconPosition.x + static_cast<int>(rightAligned ? right : 0.5f * (left + right)));
    position.y = static_cast<float>(iconPosition.y + static_cast<int>(bottom));
    font.render(text, position, glm::vec4(color, 1.0f), rightAligned ? TextGravity::LeftTop : TextGravity::CenterTop, scale);
}

} // namespace gui

} // namespace reone
