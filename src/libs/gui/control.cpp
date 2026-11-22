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

// A fill style the layout names that is none of the four draws no fill.
static Control::Border::FillStyle fillStyleOf(int value) {
    using FillStyle = Control::Border::FillStyle;
    switch (value) {
    case static_cast<int>(FillStyle::Tile):
        return FillStyle::Tile;
    case static_cast<int>(FillStyle::Center):
        return FillStyle::Center;
    case static_cast<int>(FillStyle::Stretch):
        return FillStyle::Stretch;
    case static_cast<int>(FillStyle::Extent):
        return FillStyle::Extent;
    default:
        return FillStyle::None;
    }
}

std::shared_ptr<Control::Border> Control::createBorder(const resource::generated::GUI_BORDER &gui) const {
    std::string corner(gui.CORNER);
    std::string edge(gui.EDGE);
    std::string fill(gui.FILL);

    auto border = std::make_shared<Border>();

    // A name counts as a piece, whether or not its art is held (namesArt).
    if (namesArt(corner)) {
        border->corner = _resourceSvc.textures.get(corner, TextureUsage::GUI);
        border->cornerMissing = !border->corner;
    }
    if (namesArt(edge)) {
        border->edge = _resourceSvc.textures.get(edge, TextureUsage::GUI);
        border->edgeMissing = !border->edge;
    }
    if (namesArt(fill)) {
        border->fill = _resourceSvc.textures.get(fill, TextureUsage::GUI);
        border->fillMissing = !border->fill;
    }
    border->fillStyle = fillStyleOf(gui.FILLSTYLE);

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

bool Control::handleGamepadButtonDown(const input::GamepadButtonEvent &event) {
    return false;
}

bool Control::handleGamepadButtonUp(const input::GamepadButtonEvent &event) {
    return false;
}

bool Control::handleGamepadAxisMotion(const input::GamepadAxisMotionEvent &event) {
    return false;
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

glm::mat3x4 Control::quarterTurnUV(int quarters) {
    switch (quarters) {
    case 1:
        return glm::mat3x4(
            glm::vec4(0.0f, -1.0f, 0.0f, 0.0f),
            glm::vec4(1.0f, 0.0f, 0.0f, 0.0f),
            glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
    case 2:
        return glm::mat3x4(
            glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f),
            glm::vec4(0.0f, -1.0f, 0.0f, 0.0f),
            glm::vec4(1.0f, 1.0f, 0.0f, 0.0f));
    case 3:
        return glm::mat3x4(
            glm::vec4(0.0f, 1.0f, 0.0f, 0.0f),
            glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f),
            glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    default:
        return glm::mat3x4(1.0f);
    }
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

/**
 * Visits the cells, as start and length, that tiled art covers along an
 * axis: the copies that fit whole, each widened by an even share of the rest
 * and the first ones by a pixel more; one cell over the whole length where
 * no copy fits.
 *
 * @param length the length tiled
 * @param artLength the art's length, above 0
 */
template <typename Visit>
static void forEachTileCell(int length, int artLength, Visit &&visit) {
    const int count {length / artLength};
    if (count == 0) {
        visit(0, length);
        return;
    }
    const int rest {length - count * artLength};
    const int share {rest / count};
    int extra {rest - count * share};
    int start {0};
    for (int i = 0; i < count; ++i) {
        int span {artLength + share};
        if (extra > 0) {
            --extra;
            ++span;
        }
        visit(start, span);
        start += span;
    }
}

template <typename Visit>
void Control::forEachFillRect(const Border &border, const glm::ivec2 &size, Visit &&visit) const {
    if (!border.hasFill() || size.x <= 0 || size.y <= 0) {
        return;
    }
    const Extent area {frameLayout(border, size).area};
    const glm::ivec2 art {frameArtSize(border.fill)};
    auto add = [&visit](const Extent &rect) {
        if (rect.width > 0 && rect.height > 0) {
            visit(rect);
        }
    };
    switch (border.fillStyle) {
    case Border::FillStyle::Tile:
        if (art.x <= 0 || art.y <= 0) {
            break;
        }
        forEachTileCell(area.width, art.x, [&](int left, int width) {
            forEachTileCell(area.height, art.y, [&](int top, int height) {
                add(Extent(area.left + left, area.top + top, width, height));
            });
        });
        break;
    case Border::FillStyle::Center: {
        const int width {std::min(art.x, area.width)};
        const int height {std::min(art.y, area.height)};
        add(Extent(area.left + (area.width - width) / 2, area.top + (area.height - height) / 2, width, height));
        break;
    }
    case Border::FillStyle::Stretch:
        add(area);
        break;
    case Border::FillStyle::Extent:
        if (_gui.fillRules().extentFills) {
            add(Extent(0, 0, size.x, size.y));
        }
        break;
    case Border::FillStyle::None:
        break;
    }
}

void Control::renderBorder(const Border &border,
                           const glm::ivec2 &offset,
                           const glm::ivec2 &size,
                           IRenderPass &pass,
                           BorderRenderPart part,
                           bool pulsing,
                           const std::optional<glm::vec3> &color) {
    const glm::vec3 borderColor {color.value_or(getBorderColor())};
    const auto frameAlpha = pulsing || border.pulseFrame ? _gui.pulsingAlpha() : border.alpha;
    const auto fillAlpha = pulsing || border.pulseFill ? _gui.pulsingAlpha() : border.alpha;
    glm::mat3x4 uv(1.0f);
    // The frame's pieces are as wide and as high as the area inside it is
    // inset on their side.
    const FrameLayout layout {frameLayout(border, size)};
    const Extent &area {layout.area};
    const int leftWidth {area.left};
    const int rightWidth {size.x - area.left - area.width};
    const int topHeight {area.top};
    const int bottomHeight {size.y - area.top - area.height};

    // The frame is drawn first and the fill over it. A border that names no
    // corner art has no frame. A piece whose art is missing is a plain quad
    // in the frame's colour.
    if (part != BorderRenderPart::Fill && border.hasCorner()) {
        const glm::vec4 frameColor {borderColor, frameAlpha};
        auto drawPiece = [&](const std::shared_ptr<Texture> &art, const glm::ivec2 &position, const glm::ivec2 &pieceSize, const glm::mat3x4 &pieceUV) {
            if (art) {
                pass.drawImage(*art, position, pieceSize, frameColor, pieceUV);
            } else {
                pass.drawQuad(position, pieceSize, frameColor);
            }
        };

        const int left {_extent.left + offset.x};
        const int top {_extent.top + offset.y};
        const int right {left + size.x - rightWidth};
        const int bottom {top + size.y - bottomHeight};

        // The corner art is drawn as the top left corner, and turned, or
        // mirrored, to the others.
        const bool mirrored {_gui.fillRules().mirroredCorners};
        drawPiece(border.corner, {left, top}, {leftWidth, topHeight}, quarterTurnUV(0));
        drawPiece(border.corner, {left, bottom}, {leftWidth, bottomHeight}, mirrored ? upsideDownUV() : quarterTurnUV(1));
        drawPiece(border.corner, {right, bottom}, {rightWidth, bottomHeight}, quarterTurnUV(2));
        drawPiece(border.corner, {right, top}, {rightWidth, topHeight}, mirrored ? leftToRightUV() : quarterTurnUV(3));

        if (border.hasEdge()) {
            // Each edge is a run of segments, each the whole edge art turned to its
            // side, laid out along the room as tiles of the axis's step are.
            auto segments = [](int count, int room, int step, auto &&draw) {
                if (count == 0) {
                    return;
                }
                if (step <= 0) {
                    draw(0, room);
                    return;
                }
                forEachTileCell(room, step, draw);
            };
            segments(layout.segments.x, area.width, layout.step, [&](int start, int length) {
                drawPiece(border.edge, {left + leftWidth + start, top}, {length, topHeight}, quarterTurnUV(0));
                drawPiece(border.edge, {left + leftWidth + start, bottom}, {length, bottomHeight}, quarterTurnUV(2));
            });
            segments(layout.segments.y, area.height, layout.step, [&](int start, int length) {
                drawPiece(border.edge, {left, top + topHeight + start}, {leftWidth, length}, quarterTurnUV(1));
                drawPiece(border.edge, {right, top + topHeight + start}, {rightWidth, length}, quarterTurnUV(3));
            });
        }
    }

    if (part != BorderRenderPart::Frame) {
        const glm::vec4 fillColor {borderColor, fillAlpha};
        // The fill turns as authored; quarterTurnUV counts its turns
        // anticlockwise.
        if (border.fill) {
            switch (border.fillTransform) {
            case Border::FillTransform::Rotate90:
                uv = quarterTurnUV(3);
                break;
            case Border::FillTransform::Rotate180:
                uv = quarterTurnUV(2);
                break;
            case Border::FillTransform::Rotate270:
                uv = quarterTurnUV(1);
                break;
            default:
                break;
            }
        }
        // A fill that cycles through frames shows the one the GUIs' clock is at.
        const int frame {border.fill ? getCycleFrame(*border.fill, _gui.textureTime()) : 0};
        // The whole fill is drawn in one blending.
        const bool additive {border.fill && border.fill->features().blending == Texture::Blending::Additive};
        _graphicsSvc.context.withBlendMode(additive ? BlendMode::Additive : BlendMode::Normal, [&]() {
            forEachFillRect(border, size, [&](const Extent &rect) {
                const glm::ivec2 position {_extent.left + rect.left + offset.x, _extent.top + rect.top + offset.y};
                if (!border.fill) {
                    pass.drawQuad(position, {rect.width, rect.height}, fillColor);
                    return;
                }
                pass.drawImage(
                    *border.fill,
                    position,
                    {rect.width, rect.height},
                    fillColor,
                    uv,
                    _sharpenBorderFillAlpha ? ImageAlphaMode::Sharpen : ImageAlphaMode::Default,
                    frame);
            });
        });
    }
}

Control::FrameLayout Control::frameLayout(const Border &border, const glm::ivec2 &size) const {
    FrameLayout layout;
    if (!border.hasCorner()) {
        layout.area = Extent(0, 0, size.x, size.y);
        return layout;
    }
    const FrameMeasure measure = frameMeasure(border);
    const glm::ivec2 &corner {measure.corner};
    layout.step = measure.step;
    // Along an axis the room between the corners holds a segment for each
    // step across that fits whole, or one where it holds half of one or
    // there is no step. With a segment the frame takes the corner's length
    // on both sides; otherwise it takes the whole axis, the far side the
    // larger half.
    auto place = [](int length, int cornerLength, int step, int &count, int &start, int &innerLength) {
        const int room {length - 2 * cornerLength};
        if (room <= 0) {
            count = 0;
        } else if (step == 0 || room / step == 0) {
            count = room < step / 2 ? 0 : 1;
        } else {
            count = room / step;
        }
        if (count != 0) {
            start = cornerLength;
            innerLength = room;
        } else {
            start = length / 2;
            innerLength = 0;
        }
    };
    place(size.x, corner.x, layout.step, layout.segments.x, layout.area.left, layout.area.width);
    place(size.y, corner.y, layout.step, layout.segments.y, layout.area.top, layout.area.height);
    return layout;
}

glm::ivec2 Control::frameArtSize(const std::shared_ptr<Texture> &art) const {
    if (!art) {
        return glm::ivec2(0);
    }
    const glm::ivec2 image {getImageSize(*art)};
    return {static_cast<int>(std::lround(image.x * _frameLayoutScale)), static_cast<int>(std::lround(image.y * _frameLayoutScale))};
}

int Control::scaledFrameSize(int authored) const {
    const int scaled = static_cast<int>(std::lround(authored * _frameLayoutScale));
    return authored > 0 ? std::max(scaled, 1) : scaled;
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
    return _border ? innerMeasure(*_border).corner.y : 0;
}

Control::FrameMeasure Control::frameMeasure(const Border &border) const {
    FrameMeasure measure;
    if (border.dimension != 0 || !border.hasCorner()) {
        measure.corner = glm::ivec2(border.dimension);
        measure.step = border.dimension;
        return measure;
    }
    const glm::ivec2 cornerArt {frameArtSize(border.corner)};
    if (border.hasEdge() && cornerArt.y != 0) {
        measure.corner = glm::ivec2(cornerArt.y);
        measure.step = cornerArt.y;
    } else {
        measure.corner = cornerArt;
        measure.step = border.hasEdge() ? frameArtSize(border.edge).x : cornerArt.x;
    }
    return measure;
}

Control::FrameMeasure Control::innerMeasure(const Border &border) const {
    // Without a dimension, corner art measures the area inside the frame
    // only alongside edge art.
    if (border.dimension == 0 && !border.hasEdge()) {
        return FrameMeasure {};
    }
    return frameMeasure(border);
}

// The area inside a frame is inset by the frame's corners on each axis, as
// the frame is drawn, or, with less room than half the step, collapses onto
// its centre; the inner offsets then widen it, across at most by the
// corner's length and up and down by all of the vertical one.
Control::Extent Control::borderInnerExtent(const Border &border, const Extent &area) const {
    const FrameMeasure measure = innerMeasure(border);
    // A border without frame art whose measure leaves no room is drawn over
    // the whole control, and so leaves the whole of it.
    if (!border.hasCorner() && (area.width <= 2 * measure.corner.x || area.height <= 2 * measure.corner.y)) {
        return area;
    }
    auto inset = [](int start, int length, int by, int step, int &innerStart, int &innerLength) {
        const int room = length - 2 * by;
        if (room <= 0 || step / 2 > room) {
            innerStart = start + length / 2;
            innerLength = 0;
        } else {
            innerStart = start + by;
            innerLength = room;
        }
    };
    Extent inner;
    inset(area.left, area.width, measure.corner.x, measure.step, inner.left, inner.width);
    inset(area.top, area.height, measure.corner.y, measure.step, inner.top, inner.height);
    const int offsetX = std::min(border.innerOffset, measure.corner.x);
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
    _frameLayoutScale = (_gui.fillRules().unscaledArt ? 1.0f : frameLayoutScale) * _gui.borderScale();
    if (_border) {
        _border->dimension = scaledFrameSize(_authoredBorderDimension);
        _border->innerOffset = static_cast<int>(std::lround(_authoredInnerOffset * _frameLayoutScale));
        _border->innerOffsetY = static_cast<int>(std::lround(_authoredInnerOffsetY * _frameLayoutScale));
    }
    if (_hilight) {
        _hilight->dimension = scaledFrameSize(_authoredHilightDimension);
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

// Gives \p border the fill \p texture, or a fill whose art is missing when
// \p missing, making the border when there is none; with neither, the border
// loses its fill.
static void giveFill(std::shared_ptr<Control::Border> &border, std::shared_ptr<Texture> texture, bool missing) {
    if (!texture && !missing) {
        if (border) {
            border->clearFill();
        }
        return;
    }
    if (!border) {
        border = std::make_shared<Control::Border>();
    }
    border->fill = std::move(texture);
    border->fillMissing = !border->fill;
}

void Control::setBorderFill(std::string resRef) {
    // The fill already shown is kept, so a screen may set it every frame.
    if (!resRef.empty() && resRef == _borderFillResRef && _border && _border->fill) {
        return;
    }
    std::shared_ptr<Texture> texture;
    if (!resRef.empty()) {
        texture = _resourceSvc.textures.get(resRef, TextureUsage::GUI);
    }
    // A name whose art is missing still makes a fill.
    giveFill(_border, std::move(texture), !resRef.empty());
    _borderFillResRef = std::move(resRef);
}

void Control::setBorderFill(std::shared_ptr<Texture> texture) {
    _borderFillResRef.clear();
    giveFill(_border, std::move(texture), false);
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

void Control::setBorderPulsing(bool frame, bool fill) {
    if (!_border) {
        return;
    }
    _border->pulseFrame = frame;
    _border->pulseFill = fill;
}

void Control::setHilightFill(std::string resRef) {
    std::shared_ptr<Texture> texture;
    if (!resRef.empty()) {
        texture = _resourceSvc.textures.get(resRef, TextureUsage::GUI);
    }
    // A name whose art is missing still makes a fill.
    giveFill(_hilight, std::move(texture), !resRef.empty());
    _hilightFillResRef = std::move(resRef);
}

void Control::setHilightFill(std::shared_ptr<Texture> texture) {
    _hilightFillResRef.clear();
    giveFill(_hilight, std::move(texture), false);
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
