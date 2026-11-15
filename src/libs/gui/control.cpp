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

#include "reone/gui/control.h"

#include "reone/graphics/context.h"
#include "reone/graphics/font.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/renderbuffer.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/textureutil.h"
#include "reone/graphics/textutil.h"
#include "reone/graphics/uniforms.h"
#include "reone/resource/gff.h"
#include "reone/resource/provider/fonts.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/strings.h"
#include "reone/scene/graphs.h"
#include "reone/scene/render/pipeline.h"

#include "reone/gui/gui.h"

using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;

namespace reone {

namespace gui {

// The flags of a border's or a text's PULSING field: a border's frame pulses
// on the first and its fill on the second; a text pulses on the first.
static constexpr int kPulsingFrameFlag = 1;
static constexpr int kPulsingFillFlag = 2;
static constexpr int kPulsingTextFlag = 1;

ControlType Control::getType(const resource::generated::GUI_BASECONTROL &gui) {
    return static_cast<ControlType>(gui.CONTROLTYPE);
}

std::string Control::getTag(const resource::generated::GUI_BASECONTROL &gui) {
    return gui.TAG;
}

std::string Control::getParent(const resource::generated::GUI_BASECONTROL &gui) {
    return gui.Obj_Parent;
}

Control::Extent::Extent(int left, int top, int width, int height) :
    left(left), top(top), width(width), height(height) {
}

bool Control::Extent::contains(int x, int y) const {
    return x >= left && x <= left + width && y >= top && y <= top + height;
}

void Control::Extent::getCenter(int &x, int &y) const {
    x = left + width / 2;
    y = top + height / 2;
}

void Control::load(const resource::generated::GUI_BASECONTROL &gui, bool protoItem) {
    loadExtent(gui.EXTENT);
    loadBorder(gui.BORDER);
    _isProtoItem = protoItem;
    // A button's text pulses while the button is selected, and so does an
    // image button's where the GUI says so.
    _text.hilightPulsing = _type == ControlType::Button ||
                           _type == ControlType::ToggleButton ||
                           (_type == ControlType::ImageButton && _gui.imageButtonTextPulses());

    if (static_cast<ControlType>(gui.CONTROLTYPE) == ControlType::Panel) {
        _id = -1;
    } else if (static_cast<ControlType>(gui.CONTROLTYPE) == ControlType::ScrollBar) {
        // do nothing
    } else if (protoItem) {
        auto &protoItem = *static_cast<const resource::generated::GUI_CONTROLS_PROTOITEM *>(&gui);
        if (protoItem.TEXT) {
            loadText(*protoItem.TEXT);
        }
        if (protoItem.HILIGHT) {
            loadHilight(*protoItem.HILIGHT);
        }
    } else {
        auto &controlStruct = *static_cast<const resource::generated::GUI_CONTROLS *>(&gui);
        _id = controlStruct.ID;
        _padding = controlStruct.PADDING;
        _moveTo = {controlStruct.MOVETO.UP, controlStruct.MOVETO.LEFT, controlStruct.MOVETO.DOWN, controlStruct.MOVETO.RIGHT};
        _layoutMoveTo = _moveTo;
        if (controlStruct.TEXT) {
            loadText(*controlStruct.TEXT);
        }
        if (controlStruct.HILIGHT) {
            loadHilight(*controlStruct.HILIGHT);
        }
    }

    updateTransform();
}

void Control::loadExtent(const resource::generated::GUI_EXTENT &gui) {
    _authoredExtent.left = gui.LEFT;
    _authoredExtent.top = gui.TOP;
    _authoredExtent.width = gui.WIDTH;
    _authoredExtent.height = gui.HEIGHT;
    _extent = _authoredExtent;
}

std::shared_ptr<Control::Border> Control::createBorder(const resource::generated::GUI_BORDER &gui) const {
    std::string corner(gui.CORNER);
    std::string edge(gui.EDGE);
    std::string fill(gui.FILL);

    auto border = std::make_shared<Border>();

    if (namesArt(corner)) {
        border->corner = _resourceSvc.textures.get(corner, TextureUsage::GUI);
    }
    if (namesArt(edge)) {
        border->edge = _resourceSvc.textures.get(edge, TextureUsage::GUI);
    }
    if (namesArt(fill)) {
        border->fill = _resourceSvc.textures.get(fill, TextureUsage::GUI);
    }

    border->fillStyle = static_cast<Border::FillStyle>(gui.FILLSTYLE);
    border->dimension = gui.DIMENSION;
    border->innerOffset = gui.INNEROFFSET;
    border->innerOffsetY = gui.INNEROFFSETY;
    border->color = gui.COLOR;
    border->pulseFrame = (gui.PULSING & kPulsingFrameFlag) != 0;
    border->pulseFill = (gui.PULSING & kPulsingFillFlag) != 0;
    return border;
}

void Control::loadBorder(const resource::generated::GUI_BORDER &gui) {
    _border = createBorder(gui);
    _authoredBorderDimension = gui.DIMENSION;
    _authoredInnerOffset = gui.INNEROFFSET;
    _authoredInnerOffsetY = gui.INNEROFFSETY;
}

void Control::loadText(const resource::generated::GUI_TEXT &gui) {
    _text.font = _resourceSvc.fonts.get(gui.FONT);

    int strRef = gui.STRREF;
    _text.text = strRef == -1 ? gui.TEXT : _resourceSvc.strings.getText(strRef);

    _text.color = gui.COLOR;
    _text.align = static_cast<TextAlign>(gui.ALIGNMENT);
    _text.pulsing = (gui.PULSING & kPulsingTextFlag) != 0;

    updateTextLines();
}

void Control::updateTextLines() {
    _textLines.clear();
    if (_text.font && !_text.text.empty()) {
        _textLines = breakText(_text.text, *_text.font, innerExtent(textExtent()).width, _scale);
    }
}

void Control::loadHilight(const resource::generated::GUI_BORDER &gui) {
    _hilight = createBorder(gui);
    _authoredHilightDimension = gui.DIMENSION;
    _authoredHilightInnerOffset = gui.INNEROFFSET;
    _authoredHilightInnerOffsetY = gui.INNEROFFSETY;
}

void Control::updateTransform() {
    _transform = glm::translate(glm::mat4(1.0f), glm::vec3(_extent.left, _extent.top, 0.0f));
    _transform = glm::scale(_transform, glm::vec3(_extent.width, _extent.height, 1.0f));
}

bool Control::handleKeyDown(const input::KeyCode key) {
    return false;
}

bool Control::handleKeyUp(const input::KeyCode key) {
    return false;
}

bool Control::handleMouseMotion(int x, int y) {
    return false;
}

bool Control::handleMouseWheel(int x, int y) {
    if (_onMouseWheel) {
        _onMouseWheel(x, y);
    }
    return true;
}

bool Control::handleClick(int x, int y, int clicks) {
    if (_onClick) {
        _onClick();
    }
    return true;
}

void Control::update(float dt) {
    if (!_visible) {
        return;
    }
    for (auto &child : _children) {
        child.get().update(dt);
    }
    if (!_sceneName.empty()) {
        _sceneGraphs.get(_sceneName).update(dt);
    }
}

void Control::render(const glm::ivec2 &screenSize,
                     const glm::ivec2 &offset,
                     IRenderPass &pass) {
    if (!_visible) {
        return;
    }
    const Extent borderArea = borderExtent();
    const glm::ivec2 borderOffset(offset.x + borderArea.left - _extent.left, offset.y + borderArea.top - _extent.top);
    const glm::ivec2 borderSize(borderArea.width, borderArea.height);
    const Extent textArea = textExtent();
    const glm::ivec2 textOffset(offset.x + textArea.left - _extent.left, offset.y + textArea.top - _extent.top);
    const glm::ivec2 textSize(textArea.width, textArea.height);

    // The borders this control is currently showing, in the order they stack:
    // its own border, its HILIGHT border, or - where the hilight is authored to
    // sit over the border rather than replace it - both.
    const auto &border = shownBorder();
    const auto &hilight = shownHilight();
    std::array<const Border *, 2> borderStorage;
    size_t borderCount = 0;
    if (border && (!_selected || _hilightOverBorder || !hilight)) {
        borderStorage[borderCount++] = border.get();
    }
    if (_selected && hilight) {
        borderStorage[borderCount++] = hilight.get();
    }
    renderControlLayers(
        ArrayRef<const Border *>(borderStorage.data(), borderCount),
        !_sceneName.empty() && _gui.sceneRenderEnabled(),
        [this, &borderOffset, &borderSize, &pass](const Border &border, BorderRenderPart part) {
            renderBorder(border, borderOffset, borderSize, pass, part);
        },
        [this, &screenSize, &offset, &pass]() {
            renderScene(screenSize, offset, pass);
        },
        [this, &textOffset, &textSize, &pass]() {
            if (!_textLines.empty()) {
                renderText(_textLines, textOffset, textSize, pass);
            }
        });
}

void Control::renderScene(const glm::ivec2 &screenSize,
                          const glm::ivec2 &offset,
                          IRenderPass &pass) {
    std::optional<std::reference_wrapper<Texture>> output;
    _graphicsSvc.context.withBlendMode(BlendMode::None, [this, &output]() {
        output = _sceneGraphs.get(_sceneName).render({_extent.width, _extent.height});
    });
    _graphicsSvc.uniforms.setGlobals([&screenSize](auto &globals) {
        globals.reset();
        globals.projection = glm::ortho(
            0.0f,
            static_cast<float>(screenSize.x),
            static_cast<float>(screenSize.y),
            0.0f, 0.0f, 100.0f);
        globals.projectionInv = glm::inverse(globals.projection);
    });
    _graphicsSvc.context.withDepthTestMode(DepthTestMode::None, [this, &offset, &pass, &output]() {
        pass.drawImage(
            *output,
            {_extent.left + offset.x, _extent.top + offset.y},
            {_extent.width, _extent.height});
    });
}

glm::mat3x4 Control::upsideDownUV() {
    return glm::mat3x4(
        glm::vec4(1.0f, 0.0f, 0.0f, 0.0f),
        glm::vec4(0.0f, -1.0f, 0.0f, 0.0f),
        glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
}

glm::mat3x4 Control::leftToRightUV() {
    return glm::mat3x4(
        glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f),
        glm::vec4(0.0f, 1.0f, 0.0f, 0.0f),
        glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
}

void Control::renderBorder(const Border &border,
                           const glm::ivec2 &offset,
                           const glm::ivec2 &size,
                           IRenderPass &pass,
                           BorderRenderPart part,
                           bool pulsing) {
    _graphicsSvc.context.useProgram(_graphicsSvc.shaderRegistry.get(ShaderProgramId::mvpTexture));

    glm::vec3 color(getBorderColor());
    const auto frameAlpha = pulsing || border.pulseFrame ? _gui.pulsingAlpha() : border.alpha;
    const auto fillAlpha = pulsing || border.pulseFill ? _gui.pulsingAlpha() : border.alpha;
    glm::mat4 transform(1.0f);
    glm::mat3x4 uv(1.0f);
    // Controls thinner than two authored slices - the TSL title bars - split
    // the constrained axis between the opposing pieces so their translucent
    // pixels meet rather than accumulating through the center. Controls with
    // room for both slices keep the full authored dimension on every side.
    int leftWidth = std::min(border.dimension, std::max(0, (size.x + 1) / 2));
    int rightWidth = std::min(border.dimension, std::max(0, size.x - leftWidth));
    int topHeight = std::min(border.dimension, std::max(0, (size.y + 1) / 2));
    int bottomHeight = std::min(border.dimension, std::max(0, size.y - topHeight));
    glm::ivec2 fillSize {size.x - leftWidth - rightWidth, size.y - topHeight - bottomHeight};
    // A border without a frame has nothing to make room for. Where its
    // dimension would leave the fill no room at all, as on the options pages'
    // left arrows (32 on buttons 24 to 28 across), the fill covers the control.
    if (!border.corner && !border.edge && (fillSize.x <= 0 || fillSize.y <= 0)) {
        leftWidth = rightWidth = topHeight = bottomHeight = 0;
        fillSize = size;
    }

    if (part != BorderRenderPart::Frame && border.fill && fillSize.x > 0 && fillSize.y > 0) {
        if (border.fillTransform == Border::FillTransform::Rotate180) {
            uv = glm::mat3x4(
                glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f),
                glm::vec4(0.0f, -1.0f, 0.0f, 0.0f),
                glm::vec4(1.0f, 1.0f, 0.0f, 0.0f));
        }
        auto blending = border.fill->features().blending == Texture::Blending::Additive
                            ? BlendMode::Additive
                            : BlendMode::Normal;
        glm::ivec2 fillPosition {_extent.left + leftWidth + offset.x, _extent.top + topHeight + offset.y};
        int frame = getCycleFrame(*border.fill, _gui.textureTime());
        auto drawFill = [&](const glm::ivec2 &position, const glm::ivec2 &drawSize) {
            pass.drawImage(
                *border.fill,
                position,
                drawSize,
                glm::vec4(color, fillAlpha),
                uv,
                _sharpenBorderFillAlpha ? ImageAlphaMode::Sharpen : ImageAlphaMode::Default,
                frame);
        };
        // Tiles and a centred image are as big as the image is authored,
        // grown with the layout - and never less than a pixel, so that
        // there is always a whole number of them.
        glm::ivec2 imageSize = glm::max(
            glm::ivec2(1),
            glm::ivec2(glm::vec2(getImageSize(*border.fill)) * _frameLayoutScale));
        _graphicsSvc.context.withBlendMode(blending, [&]() {
            switch (border.fillStyle) {
            case Border::FillStyle::Tile: {
                // As many whole images as fit along each axis, or one where
                // none does, with what is left over shared out a pixel each
                // from the first.
                glm::ivec2 count = glm::max(glm::ivec2(1), fillSize / imageSize);
                glm::ivec2 tileSize = fillSize / count;
                glm::ivec2 widened = fillSize - tileSize * count;
                int x = fillPosition.x;
                for (int column = 0; column < count.x; ++column) {
                    int width = tileSize.x + (column < widened.x ? 1 : 0);
                    int y = fillPosition.y;
                    for (int row = 0; row < count.y; ++row) {
                        int height = tileSize.y + (row < widened.y ? 1 : 0);
                        drawFill({x, y}, {width, height});
                        y += height;
                    }
                    x += width;
                }
                break;
            }
            case Border::FillStyle::Center: {
                glm::ivec2 drawSize = glm::min(imageSize, fillSize);
                drawFill(fillPosition + (fillSize - drawSize) / 2, drawSize);
                break;
            }
            case Border::FillStyle::Stretch:
                drawFill(fillPosition, fillSize);
                break;
            }
        });
    }

    if (part != BorderRenderPart::Fill && border.edge) {
        int width = fillSize.x;
        int height = fillSize.y;

        if (height > 0.0f) {
            int x = _extent.left + offset.x;
            int y = _extent.top + topHeight + offset.y;

            // Left edge
            uv = glm::mat3x4(
                glm::vec4(0.0f, -1.0f, 0.0f, 0.0f),
                glm::vec4(1.0f, 0.0f, 0.0f, 0.0f),
                glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
            pass.drawImage(
                *border.edge,
                {x, y},
                {leftWidth, height},
                glm::vec4(color, frameAlpha),
                uv);

            // Right edge
            uv = glm::mat3x4(
                glm::vec4(0.0f, 1.0f, 0.0f, 0.0f),
                glm::vec4(1.0f, 0.0f, 0.0f, 0.0f),
                glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
            pass.drawImage(
                *border.edge,
                {x + size.x - rightWidth, y},
                {rightWidth, height},
                glm::vec4(color, frameAlpha),
                uv);
        }

        if (width > 0.0f) {
            int x = _extent.left + leftWidth + offset.x;
            int y = _extent.top + offset.y;

            // Top edge
            pass.drawImage(
                *border.edge,
                {x, y},
                {width, topHeight},
                glm::vec4(color, frameAlpha));

            // Bottom edge
            uv = glm::mat3x4(
                glm::vec4(1.0f, 0.0f, 0.0f, 0.0f),
                glm::vec4(0.0f, -1.0f, 0.0f, 0.0f),
                glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
            pass.drawImage(
                *border.edge,
                {x, y + size.y - bottomHeight},
                {width, bottomHeight},
                glm::vec4(color, frameAlpha),
                uv);
        }
    }

    if (part != BorderRenderPart::Fill && border.corner) {
        int x = _extent.left + offset.x;
        int y = _extent.top + offset.y;

        // Top left corner
        pass.drawImage(
            *border.corner,
            {x, y},
            {leftWidth, topHeight},
            glm::vec4(color, frameAlpha));

        // Bottom left corner
        uv = glm::mat3x4(
            glm::vec4(1.0f, 0.0f, 0.0f, 0.0f),
            glm::vec4(0.0f, -1.0f, 0.0f, 0.0f),
            glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
        pass.drawImage(
            *border.corner,
            {x, y + size.y - bottomHeight},
            {leftWidth, bottomHeight},
            glm::vec4(color, frameAlpha),
            uv);

        // Top right corner
        uv = glm::mat3x4(
            glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f),
            glm::vec4(0.0f, 1.0f, 0.0f, 0.0f),
            glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        pass.drawImage(
            *border.corner,
            {x + size.x - rightWidth, y},
            {rightWidth, topHeight},
            glm::vec4(color, frameAlpha),
            uv);

        // Bottom right corner
        uv = glm::mat3x4(
            glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f),
            glm::vec4(0.0f, -1.0f, 0.0f, 0.0f),
            glm::vec4(1.0f, 1.0f, 0.0f, 0.0f));
        pass.drawImage(
            *border.corner,
            {x + size.x - rightWidth, y + size.y - bottomHeight},
            {rightWidth, bottomHeight},
            glm::vec4(color, frameAlpha),
            uv);
    }
}

const glm::vec3 &Control::getBorderColor() const {
    if (_useBorderColorOverride) {
        return _borderColorOverride;
    }
    const auto &hilight = shownHilight();
    return (_selected && hilight) ? hilight->color : shownBorder()->color;
}

void Control::renderText(const std::vector<std::string> &lines,
                         const glm::ivec2 &offset,
                         const glm::ivec2 &size,
                         IRenderPass &pass) {
    glm::ivec2 position;
    TextGravity gravity;
    const Extent area = innerExtent({_extent.left, _extent.top, size.x, size.y});
    getTextPosition(position, static_cast<int>(lines.size()), area, gravity);

    glm::vec3 linePosition(0.0f);
    const auto &hilight = shownHilight();
    auto textColor = _text.color;
    if (_selected && _text.selectedColor) {
        textColor = *_text.selectedColor;
    } else if (_selected && hilight) {
        textColor = hilight->color;
    }
    const bool pulsing = _text.pulsing || (_selected && _text.hilightPulsing);
    glm::vec4 color {textColor, pulsing ? _gui.pulsingAlpha() : _text.opacity};

    // Each line is drawn a line advance below the one before, unrounded.
    const float lineAdvance = _text.font->lineAdvance(_scale);
    linePosition.x = static_cast<float>(position.x + offset.x);
    linePosition.y = static_cast<float>(position.y + offset.y);

    // Text taller than its area, measured as the block screens size it by,
    // shows the lines its alignment keeps: middle aligned text drops its
    // first lines, a line advance each, until the half of the overflow above
    // the area is gone, and bottom aligned text until the rest fits, though
    // never its last line. The first line kept is drawn; no later line
    // starting below the area is.
    const int lineCount = static_cast<int>(lines.size());
    const int overflow = textBlockHeight(lineCount) - area.height;
    const int vertical = static_cast<int>(_text.align) & kTextAlignVertical;
    int first = 0;
    if (overflow > 0 && vertical == kTextAlignMiddle) {
        while (first < lineCount - 1 && static_cast<float>(first) * lineAdvance < 0.5f * static_cast<float>(overflow)) {
            ++first;
        }
    } else if (overflow > 0 && vertical == kTextAlignBottom) {
        while (first < lineCount - 1 && textBlockHeight(lineCount - first) > area.height) {
            ++first;
        }
    }
    // A line's top, from the point it is drawn at.
    const float fontHeight = _text.font->height() * _scale;
    const float lineTop = vertical == kTextAlignMiddle ? -0.5f * fontHeight : (vertical == kTextAlignBottom ? -fontHeight : 0.0f);
    const float areaBottom = static_cast<float>(area.top + area.height + offset.y);
    linePosition.y += static_cast<float>(first) * lineAdvance;
    for (int i = first; i < lineCount; ++i) {
        if (i > first && linePosition.y + lineTop > areaBottom) {
            break;
        }
        _text.font->render(lines[i], linePosition, color, gravity, _scale);
        linePosition.y += lineAdvance;
    }
}

int Control::textBlockHeight(int lineCount) const {
    if (!_text.font) {
        return 0;
    }
    return _text.font->blockHeight(lineCount, _scale);
}

float Control::textLineAdvance() const {
    return _text.font ? _text.font->lineAdvance(_scale) : 0.0f;
}

int Control::textLineUnit() const {
    if (!_text.font) {
        return 0;
    }
    return static_cast<int>(std::lround(_text.font->lineAdvance(_scale)));
}

// Text is centred or put at the right only by those bits, and in the middle
// or at the bottom only by those; any other alignment is left and top.
void Control::getTextPosition(glm::ivec2 &position, int lineCount, const Extent &area, TextGravity &gravity) const {
    const int align = static_cast<int>(_text.align);
    const int horizontal = align & kTextAlignHorizontal;
    const int vertical = align & kTextAlignVertical;
    int column = 0;
    position.x = area.left;
    if (horizontal == kTextAlignCenter) {
        column = 1;
        position.x = area.left + area.width / 2;
    } else if (horizontal == kTextAlignRight) {
        column = 2;
        position.x = area.left + area.width;
    }
    const float lineAdvance = _text.font->lineAdvance(_scale);
    int row = 0;
    position.y = area.top;
    if (vertical == kTextAlignMiddle) {
        row = 1;
        position.y = area.top + area.height / 2 - static_cast<int>(0.5f * (lineCount - 1) * lineAdvance);
    } else if (vertical == kTextAlignBottom) {
        row = 2;
        position.y = area.top + area.height - static_cast<int>(glm::max(0, lineCount - 1) * lineAdvance);
    }
    // The way the text runs from its anchor, by column and row.
    static constexpr TextGravity kGravities[3][3] {
        {TextGravity::RightBottom, TextGravity::RightCenter, TextGravity::RightTop},
        {TextGravity::CenterBottom, TextGravity::CenterCenter, TextGravity::CenterTop},
        {TextGravity::LeftBottom, TextGravity::LeftCenter, TextGravity::LeftTop}};
    gravity = kGravities[column][row];
}

Control::Extent Control::innerExtent(const Extent &area) const {
    return _border ? borderInnerExtent(*_border, area) : area;
}

Control::Extent Control::innerExtentWithHilight(const Extent &area) const {
    const Extent inner = Control::innerExtent(area);
    return _hilight ? intersection(inner, borderInnerExtent(*_hilight, area)) : inner;
}

int Control::borderDimension() const {
    return _border ? _border->dimension : 0;
}

// The area inside a frame is inset by its dimension on each axis, as the
// frame is drawn, or, with less room than half the dimension, collapses onto
// its centre; the inner offsets then widen it, across at most by the
// dimension and up and down by all of the vertical one.
Control::Extent Control::borderInnerExtent(const Border &border, const Extent &area) const {
    const int dimension = border.dimension;
    // A border without frame art whose dimension leaves no room is drawn
    // over the whole control, and so leaves the whole of it.
    if (!border.corner && !border.edge && (area.width <= 2 * dimension || area.height <= 2 * dimension)) {
        return area;
    }
    auto inset = [dimension](int start, int length, int &innerStart, int &innerLength) {
        const int room = length - 2 * dimension;
        if (room <= 0 || dimension / 2 > room) {
            innerStart = start + length / 2;
            innerLength = 0;
        } else {
            innerStart = start + dimension;
            innerLength = room;
        }
    };
    Extent inner;
    inset(area.left, area.width, inner.left, inner.width);
    inset(area.top, area.height, inner.top, inner.height);
    const int offsetX = std::min(border.innerOffset, dimension);
    const int offsetY = border.innerOffsetY != 0 ? border.innerOffsetY : border.innerOffset;
    inner.left -= offsetX;
    inner.width += 2 * offsetX;
    inner.top -= offsetY;
    inner.height += 2 * offsetY;
    return inner;
}

Control::Extent Control::intersection(const Extent &a, const Extent &b) {
    const int left = std::max(a.left, b.left);
    const int top = std::max(a.top, b.top);
    const int right = std::min(a.left + a.width, b.left + b.width);
    const int bottom = std::min(a.top + a.height, b.top + b.height);
    return {left, top, std::max(0, right - left), std::max(0, bottom - top)};
}

glm::ivec2 Control::frameArtSize(const std::shared_ptr<Texture> &art) const {
    if (!art) {
        return glm::ivec2(0);
    }
    return {static_cast<int>(std::lround(art->width() * _frameLayoutScale)), static_cast<int>(std::lround(art->height() * _frameLayoutScale))};
}

void Control::stretch(float x, float y, int mask) {
    if (mask & kStretchLeft) {
        _extent.left = static_cast<int>(_authoredExtent.left * x);
    }
    if (mask & kStretchTop) {
        _extent.top = static_cast<int>(_authoredExtent.top * y);
    }
    if (mask & kStretchWidth) {
        _extent.width = static_cast<int>(_authoredExtent.width * x);
    }
    if (mask & kStretchHeight) {
        _extent.height = static_cast<int>(_authoredExtent.height * y);
    }
    float frameLayoutScale = x == y ? x : 1.0f;
    setPresentationScale(frameLayoutScale, _gui.textLayoutScale(x, y));
    updateTransform();
}

void Control::setPresentationScale(float layoutScale) {
    setPresentationScale(layoutScale, layoutScale);
}

void Control::setPresentationScale(float frameLayoutScale, float textLayoutScale) {
    // Text and frame slices do not follow the extent on their own. Keeping
    // this separate lets compound controls give their contents an independent
    // density while retaining the parent's layout rectangle.
    _scale = textLayoutScale * _gui.textScale();
    _frameLayoutScale = frameLayoutScale * _gui.borderScale();
    // An authored frame size is never scaled down to 0, which would take the
    // art's own size.
    auto frameSize = [this](int authored) {
        const int scaled = static_cast<int>(std::lround(authored * _frameLayoutScale));
        return authored > 0 ? std::max(scaled, 1) : scaled;
    };
    if (_border) {
        _border->dimension = frameSize(_authoredBorderDimension);
        _border->innerOffset = static_cast<int>(std::lround(_authoredInnerOffset * _frameLayoutScale));
        _border->innerOffsetY = static_cast<int>(std::lround(_authoredInnerOffsetY * _frameLayoutScale));
    }
    if (_hilight) {
        _hilight->dimension = frameSize(_authoredHilightDimension);
        _hilight->innerOffset = static_cast<int>(std::lround(_authoredHilightInnerOffset * _frameLayoutScale));
        _hilight->innerOffsetY = static_cast<int>(std::lround(_authoredHilightInnerOffsetY * _frameLayoutScale));
    }
    scaleOwnFrames();
    updateTextLines();
}

void Control::setSelectable(bool selectable) {
    _selectable = selectable;
}

void Control::setHeight(int height) {
    _extent.height = height;
    updateTransform();
    updateTextLines();
}

void Control::setVisible(bool visible) {
    _visible = visible;
}

void Control::setDisabled(bool disabled) {
    _disabled = disabled;
}

void Control::setSelected(bool setSelected) {
    if (_selected == setSelected)
        return;

    _selected = setSelected;

    // Deselecting a control whose text pulses while selected stops the text's
    // pulse. A list's prototype is selected and deselected for each row it
    // draws, so it keeps its text's pulse.
    if (!setSelected && _text.hilightPulsing && !_isProtoItem) {
        _text.pulsing = false;
    }

    if (_onSelectedChanged) {
        _onSelectedChanged(setSelected);
    }
}

void Control::setExtent(Extent extent) {
    _extent = std::move(extent);
    updateTransform();
    updateTextLines();
}

void Control::setExtentHeight(int height) {
    _extent.height = height;
    updateTransform();
}

void Control::setExtentTop(int top) {
    _extent.top = top;
    updateTransform();
}

void Control::setBorder(Border border) {
    _border = std::make_shared<Border>(std::move(border));
    _authoredBorderDimension = _border->dimension;
    _authoredInnerOffset = _border->innerOffset;
    _authoredInnerOffsetY = _border->innerOffsetY;
}

// Gives \p border the fill \p texture, making the border when there is
// none; without a texture, the border loses its fill.
static void giveFill(std::shared_ptr<Control::Border> &border, std::shared_ptr<Texture> texture) {
    if (!texture) {
        if (border) {
            border->fill.reset();
        }
        return;
    }
    if (!border) {
        border = std::make_shared<Control::Border>();
    }
    border->fill = std::move(texture);
}

void Control::setBorderFill(std::string resRef) {
    std::shared_ptr<Texture> texture;
    if (!resRef.empty()) {
        texture = _resourceSvc.textures.get(resRef, TextureUsage::GUI);
    }
    giveFill(_border, std::move(texture));
    _borderFillResRef = std::move(resRef);
}

void Control::setBorderFill(std::shared_ptr<Texture> texture) {
    _borderFillResRef.clear();
    giveFill(_border, std::move(texture));
}

void Control::setBorderFillStyle(Border::FillStyle style) {
    if (!_border) {
        _border = std::make_shared<Border>();
    }
    _border->fillStyle = style;
}

void Control::setBorderFillTransform(Border::FillTransform transform) {
    if (_border) {
        _border->fillTransform = transform;
    }
}

void Control::setBorderColor(glm::vec3 color) {
    if (!_border) {
        _border = std::make_shared<Border>();
    }
    _border->color = std::move(color);
}

void Control::setBorderAlpha(float alpha) {
    if (_border) {
        _border->alpha = alpha;
    }
}

void Control::setBorderColorOverride(glm::vec3 color) {
    _borderColorOverride = std::move(color);
}

void Control::setUseBorderColorOverride(bool use) {
    _useBorderColorOverride = use;
}

void Control::setHilight(Border hilight) {
    _hilight = std::make_shared<Border>(hilight);
    _authoredHilightDimension = _hilight->dimension;
    _authoredHilightInnerOffset = _hilight->innerOffset;
    _authoredHilightInnerOffsetY = _hilight->innerOffsetY;
}

void Control::setHilightColor(glm::vec3 color) {
    if (!_hilight) {
        _hilight = std::make_shared<Border>();
    }
    _hilight->color = color;
    _text.selectedColor = color;
}

void Control::setSelectedTextColor(std::optional<glm::vec3> color) {
    _text.selectedColor = color;
}

void Control::setHilightPulsing(bool frame, bool fill) {
    if (!_hilight) {
        return;
    }
    _hilight->pulseFrame = frame;
    _hilight->pulseFill = fill;
}

void Control::setHilightFill(std::string resRef) {
    std::shared_ptr<Texture> texture;
    if (!resRef.empty()) {
        texture = _resourceSvc.textures.get(resRef, TextureUsage::GUI);
    }
    giveFill(_hilight, std::move(texture));
    _hilightFillResRef = std::move(resRef);
}

void Control::setHilightFill(std::shared_ptr<Texture> texture) {
    _hilightFillResRef.clear();
    giveFill(_hilight, std::move(texture));
}

void Control::setHilightFillTransform(Border::FillTransform transform) {
    if (_hilight) {
        _hilight->fillTransform = transform;
    }
}

void Control::setText(Text text) {
    _text = std::move(text);
    updateTextLines();
}

void Control::setTextAlignment(TextAlign align) {
    _text.align = align;
}

void Control::setTextMessage(std::string text) {
    _text.text = std::move(text);
    updateTextLines();
}

void Control::setTextFont(std::shared_ptr<Font> font) {
    _text.font = std::move(font);
    updateTextLines();
}

void Control::setTextColor(glm::vec3 color) {
    _text.color = std::move(color);
}

void Control::setSceneName(std::string name) {
    _sceneName = std::move(name);
}

void Control::setPadding(int padding) {
    _padding = padding;
}

} // namespace gui

} // namespace reone
