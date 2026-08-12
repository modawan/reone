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
#include "reone/graphics/mesh.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/renderbuffer.h"
#include "reone/graphics/shaderregistry.h"
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
    _tintBorderFill = _gui.tintBorderFills();

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

void Control::loadBorder(const resource::generated::GUI_BORDER &gui) {
    std::string corner(gui.CORNER);
    std::string edge(gui.EDGE);
    std::string fill(gui.FILL);

    _border = std::make_shared<Border>();

    if (!corner.empty() && corner != "0") {
        _border->corner = _resourceSvc.textures.get(corner, TextureUsage::GUI);
    }
    if (!edge.empty() && edge != "0") {
        _border->edge = _resourceSvc.textures.get(edge, TextureUsage::GUI);
    }
    if (!fill.empty() && fill != "0") {
        _border->fill = _resourceSvc.textures.get(fill, TextureUsage::GUI);
    }

    _border->dimension = gui.DIMENSION;
    _authoredBorderDimension = gui.DIMENSION;
    _border->color = gui.COLOR;
}

void Control::loadText(const resource::generated::GUI_TEXT &gui) {
    _text.font = _resourceSvc.fonts.get(gui.FONT);

    int strRef = gui.STRREF;
    _text.text = strRef == -1 ? gui.TEXT : _resourceSvc.strings.getText(strRef);

    _text.color = gui.COLOR;
    _text.align = static_cast<TextAlign>(gui.ALIGNMENT);

    updateTextLines();
}

void Control::updateTextLines() {
    _textLines.clear();
    if (_text.font && !_text.text.empty()) {
        _textLines = breakText(_text.text, *_text.font, _extent.width, _scale);
    }
}

void Control::loadHilight(const resource::generated::GUI_BORDER &gui) {
    std::string corner(gui.CORNER);
    std::string edge(gui.EDGE);
    std::string fill(gui.FILL);

    _hilight = std::make_shared<Border>();

    if (!corner.empty() && corner != "0") {
        _hilight->corner = _resourceSvc.textures.get(corner, TextureUsage::GUI);
    }
    if (!edge.empty() && edge != "0") {
        _hilight->edge = _resourceSvc.textures.get(edge, TextureUsage::GUI);
    }
    if (!fill.empty() && fill != "0") {
        _hilight->fill = _resourceSvc.textures.get(fill, TextureUsage::GUI);
    }

    _hilight->dimension = gui.DIMENSION;
    _authoredHilightDimension = gui.DIMENSION;
    _hilight->color = gui.COLOR;
}

void Control::updateTransform() {
    _transform = glm::translate(glm::mat4(1.0f), glm::vec3(_extent.left, _extent.top, 0.0f));
    _transform = glm::scale(_transform, glm::vec3(_extent.width, _extent.height, 1.0f));
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
    glm::ivec2 size(_extent.width, _extent.height);

    // The borders this control is currently showing, in the order they stack:
    // its own border, its HILIGHT border, or - where the hilight is authored to
    // sit over the border rather than replace it - both.
    std::array<const Border *, 2> borderStorage;
    size_t borderCount = 0;
    if (_border && (!_selected || _hilightOverBorder || !_hilight)) {
        borderStorage[borderCount++] = _border.get();
    }
    if (_selected && _hilight) {
        borderStorage[borderCount++] = _hilight.get();
    }

    renderControlLayers(
        ArrayRef<const Border *>(borderStorage.data(), borderCount),
        !_sceneName.empty(),
        [this, &offset, &size, &pass](const Border &border, BorderRenderPart part) {
            renderBorder(border, offset, size, pass, part);
        },
        [this, &screenSize, &offset, &pass]() {
            renderScene(screenSize, offset, pass);
        },
        [this, &offset, &size, &pass]() {
            if (!_textLines.empty()) {
                renderText(_textLines, offset, size, pass);
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

void Control::renderBorder(const Border &border,
                           const glm::ivec2 &offset,
                           const glm::ivec2 &size,
                           IRenderPass &pass,
                           BorderRenderPart part) {
    _graphicsSvc.context.useProgram(_graphicsSvc.shaderRegistry.get(ShaderProgramId::mvpTexture));

    glm::vec3 color(getBorderColor());
    glm::mat4 transform(1.0f);
    glm::mat3x4 uv(1.0f);

    if (part != BorderRenderPart::Frame && border.fill) {
        if (border.fillTransform == Border::FillTransform::Rotate180) {
            uv = glm::mat3x4(
                glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f),
                glm::vec4(0.0f, -1.0f, 0.0f, 0.0f),
                glm::vec4(1.0f, 1.0f, 0.0f, 0.0f));
        }
        auto blending = border.fill->features().blending == Texture::Blending::Additive
                            ? BlendMode::Additive
                            : BlendMode::Normal;
        _graphicsSvc.context.withBlendMode(blending, [&]() {
            pass.drawImage(
                *border.fill,
                {_extent.left + border.dimension + offset.x, _extent.top + border.dimension + offset.y},
                {size.x - 2 * border.dimension, size.y - 2 * border.dimension},
                _tintBorderFill ? glm::vec4(color, 1.0f) : glm::vec4(1.0f),
                uv,
                _sharpenBorderFillAlpha ? ImageAlphaMode::Sharpen : ImageAlphaMode::Default);
        });
    }

    if (part != BorderRenderPart::Fill && border.edge) {
        int width = size.x - 2 * border.dimension;
        int height = size.y - 2 * border.dimension;

        if (height > 0.0f) {
            int x = _extent.left + offset.x;
            int y = _extent.top + border.dimension + offset.y;

            // Left edge
            uv = glm::mat3x4(
                glm::vec4(0.0f, -1.0f, 0.0f, 0.0f),
                glm::vec4(1.0f, 0.0f, 0.0f, 0.0f),
                glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
            pass.drawImage(
                *border.edge,
                {x, y},
                {border.dimension, height},
                glm::vec4(color, 1.0f),
                uv);

            // Right edge
            uv = glm::mat3x4(
                glm::vec4(0.0f, 1.0f, 0.0f, 0.0f),
                glm::vec4(1.0f, 0.0f, 0.0f, 0.0f),
                glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
            pass.drawImage(
                *border.edge,
                {x + size.x - border.dimension, y},
                {border.dimension, height},
                glm::vec4(color, 1.0f),
                uv);
        }

        if (width > 0.0f) {
            int x = _extent.left + border.dimension + offset.x;
            int y = _extent.top + offset.y;

            // Top edge
            pass.drawImage(
                *border.edge,
                {x, y},
                {width, border.dimension},
                glm::vec4(color, 1.0f));

            // Bottom edge
            uv = glm::mat3x4(
                glm::vec4(1.0f, 0.0f, 0.0f, 0.0f),
                glm::vec4(0.0f, -1.0f, 0.0f, 0.0f),
                glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
            pass.drawImage(
                *border.edge,
                {x, y + size.y - border.dimension},
                {width, border.dimension},
                glm::vec4(color, 1.0f),
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
            {border.dimension, border.dimension},
            glm::vec4(color, 1.0f));

        // Bottom left corner
        uv = glm::mat3x4(
            glm::vec4(1.0f, 0.0f, 0.0f, 0.0f),
            glm::vec4(0.0f, -1.0f, 0.0f, 0.0f),
            glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
        pass.drawImage(
            *border.corner,
            {x, y + size.y - border.dimension},
            {border.dimension, border.dimension},
            glm::vec4(color, 1.0f),
            uv);

        // Top right corner
        uv = glm::mat3x4(
            glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f),
            glm::vec4(0.0f, 1.0f, 0.0f, 0.0f),
            glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        pass.drawImage(
            *border.corner,
            {x + size.x - border.dimension, y},
            {border.dimension, border.dimension},
            glm::vec4(color, 1.0f),
            uv);

        // Bottom right corner
        uv = glm::mat3x4(
            glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f),
            glm::vec4(0.0f, -1.0f, 0.0f, 0.0f),
            glm::vec4(1.0f, 1.0f, 0.0f, 0.0f));
        pass.drawImage(
            *border.corner,
            {x + size.x - border.dimension, y + size.y - border.dimension},
            {border.dimension, border.dimension},
            glm::vec4(color, 1.0f),
            uv);
    }
}

const glm::vec3 &Control::getBorderColor() const {
    if (_useBorderColorOverride) {
        return _borderColorOverride;
    }
    return (_selected && _hilight) ? _hilight->color : _border->color;
}

void Control::renderText(const std::vector<std::string> &lines,
                         const glm::ivec2 &offset,
                         const glm::ivec2 &size,
                         IRenderPass &pass) {
    glm::ivec2 position;
    TextGravity gravity;
    getTextPosition(position, static_cast<int>(lines.size()), size, gravity);

    glm::vec3 linePosition(0.0f);
    glm::vec3 color((_selected && _hilight) ? _hilight->color : _text.color);

    for (auto &line : lines) {
        linePosition.x = static_cast<float>(position.x + offset.x);
        linePosition.y = static_cast<float>(position.y + offset.y);
        _text.font->render(line, linePosition, color, gravity, _scale);
        position.y += static_cast<int>(_text.font->height() * _scale);
    }
}

void Control::getTextPosition(glm::ivec2 &position, int lineCount, const glm::ivec2 &size, TextGravity &gravity) const {
    // Gravity
    switch (_text.align) {
    case TextAlign::LeftTop:
        gravity = TextGravity::RightBottom;
        break;
    case TextAlign::CenterTop:
        gravity = TextGravity::CenterBottom;
        break;
    case TextAlign::RightCenter:
    case TextAlign::RightCenter2:
        gravity = TextGravity::LeftCenter;
        break;
    case TextAlign::LeftCenter:
        gravity = TextGravity::RightCenter;
        break;
    case TextAlign::CenterBottom:
        gravity = TextGravity::CenterTop;
        break;
    case TextAlign::CenterCenter:
    default:
        gravity = TextGravity::CenterCenter;
        break;
    }
    // Vertical alignment
    switch (_text.align) {
    case TextAlign::LeftTop:
    case TextAlign::CenterTop:
        position.y = _extent.top;
        break;
    case TextAlign::CenterBottom:
        position.y = _extent.top + size.y - static_cast<int>(glm::max(0, lineCount - 1) * _text.font->height() * _scale);
        break;
    case TextAlign::RightCenter:
    case TextAlign::LeftCenter:
    case TextAlign::CenterCenter:
    case TextAlign::RightCenter2:
    default:
        position.y = _extent.top + size.y / 2 - static_cast<int>(0.5f * (lineCount - 1) * _text.font->height() * _scale);
        break;
    }
    // Horizontal alignment
    switch (_text.align) {
    case TextAlign::LeftTop:
    case TextAlign::LeftCenter:
        position.x = _extent.left;
        break;
    case TextAlign::RightCenter:
    case TextAlign::RightCenter2:
        position.x = _extent.left + _extent.width;
        break;
    case TextAlign::CenterTop:
    case TextAlign::CenterCenter:
    case TextAlign::CenterBottom:
    default:
        position.x = _extent.left + size.x / 2;
        break;
    }
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
    if (_border) {
        _border->dimension = static_cast<int>(_authoredBorderDimension * frameLayoutScale * _gui.borderScale());
    }
    if (_hilight) {
        _hilight->dimension = static_cast<int>(_authoredHilightDimension * frameLayoutScale * _gui.borderScale());
    }
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
}

void Control::setBorderFill(std::string resRef) {
    std::shared_ptr<Texture> texture;
    if (!resRef.empty()) {
        texture = _resourceSvc.textures.get(resRef, TextureUsage::GUI);
    }
    setBorderFill(std::move(texture));
    _borderFillResRef = std::move(resRef);
}

void Control::setBorderFill(std::shared_ptr<Texture> texture) {
    _borderFillResRef.clear();
    if (!texture && _border) {
        _border->fill.reset();
        return;
    }
    if (texture) {
        if (!_border) {
            _border = std::make_shared<Border>();
        }
        _border->fill = std::move(texture);
    }
}

void Control::setBorderFillTransform(Border::FillTransform transform) {
    if (_border) {
        _border->fillTransform = transform;
    }
}

void Control::setBorderColor(glm::vec3 color) {
    _border->color = std::move(color);
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
}

void Control::setHilightColor(glm::vec3 color) {
    if (!_hilight) {
        _hilight = std::make_shared<Border>();
    }
    _hilight->color = std::move(color);
}

void Control::setHilightFill(std::string resRef) {
    std::shared_ptr<Texture> texture;
    if (!resRef.empty()) {
        texture = _resourceSvc.textures.get(resRef, TextureUsage::GUI);
    }
    setHilightFill(texture);
    _hilightFillResRef = std::move(resRef);
}

void Control::setHilightFill(std::shared_ptr<Texture> texture) {
    _hilightFillResRef.clear();
    if (!texture && _hilight) {
        _hilight->fill.reset();
        return;
    }
    if (texture) {
        if (!_hilight) {
            _hilight = std::make_shared<Border>();
        }
        _hilight->fill = std::move(texture);
    }
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
