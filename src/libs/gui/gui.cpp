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

#include "reone/gui/gui.h"

#include "reone/graphics/context.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/texture.h"
#include "reone/graphics/uniforms.h"
#include "reone/gui/control/button.h"
#include "reone/gui/control/iconchain.h"
#include "reone/gui/control/imagebutton.h"
#include "reone/gui/control/label.h"
#include "reone/gui/control/listbox.h"
#include "reone/gui/control/panel.h"
#include "reone/gui/control/progressbar.h"
#include "reone/gui/control/scrollbar.h"
#include "reone/gui/control/slider.h"
#include "reone/gui/control/togglebutton.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/gff.h"
#include "reone/resource/parser/gff/gui.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/resources.h"
#include "reone/scene/render/pass/pbr.h"
#include "reone/scene/render/pass/retro.h"
#include "reone/system/exception/validation.h"
#include "reone/system/logutil.h"

using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;

namespace reone {

namespace gui {

void GUI::load(const Gff &gui) {
    auto guiParsed = resource::generated::parseGUI(gui);
    auto type = Control::getType(guiParsed);
    auto tag = Control::getTag(guiParsed);
    auto rootControl = newControl(type, tag);
    rootControl->load(guiParsed);

    _tagToControl.insert({tag, *rootControl});
    _rootControl = *rootControl;
    _controls.push_back(std::move(rootControl));

    for (auto &controlStruct : guiParsed.CONTROLS) {
        loadControl(controlStruct);
    }
    for (auto &[tag, children] : _controlTagToChildren) {
        auto maybeParent = _tagToControl.find(tag);
        if (maybeParent == _tagToControl.end()) {
            throw ValidationException("Parent control not found: " + tag);
        }
        auto &parent = maybeParent->second.get();
        for (auto &child : children) {
            parent.addChildToBack(child);
        }
    }

    applyLayout();
}

void GUI::stretchControl(Control &control) {
    float aspectX = _options.width / static_cast<float>(_resolutionX);
    float aspectY = _options.height / static_cast<float>(_resolutionY);
    control.stretch(aspectX, aspectY);
}

float GUI::scaledFactor() const {
    // KVP's retail draw-stream scaler uses the limiting axis: content is as
    // large as possible without cropping or changing its authored aspect.
    //
    // Background artwork is a separate cover layer drawn by renderBackground,
    // so it does not enter this factor: composite GUIs such as the in-game tab
    // strip and its active tab must share one coordinate space regardless of
    // whether either of them carries a backdrop.
    return std::min(
               _options.width / static_cast<float>(_resolutionX),
               _options.height / static_cast<float>(_resolutionY)) *
           _options.guiScale;
}

void GUI::loadControl(const resource::generated::GUI_CONTROLS &gui) {
    auto type = Control::getType(gui);
    auto tag = Control::getTag(gui);
    auto parentTag = Control::getParent(gui);
    debug(str(boost::format("Loading control: type=%s, tag='%s', parent='%s'") % static_cast<int>(type) % tag % parentTag),
          LogChannel::GUI);

    auto control = newControl(type, tag);
    if (!control) {
        return;
    }
    control->load(gui);
    if (_hasDefaultHilightColor) {
        control->setHilightColor(_defaultHilightColor);
    }

    _tagToControl.insert({tag, *control});
    _controlTagToChildren[parentTag].push_back(*control);
    _controls.push_back(std::move(control));
}

void GUI::positionRelativeToCenter(Control &control) {
    // Anchored controls - HUD icons, portraits, the minimap - scale like
    // everything else, uniformly and aspect-preserved, while keeping their
    // authored screen-edge attachment: the inset from the anchored edge
    // scales with the same factor as the control itself. Before this they
    // kept their native 800x600-era pixel sizes on any screen.
    float s = scaledFactor();
    Control::Extent extent(control.authoredExtent());
    bool anchorRight = extent.left >= 0.5f * _resolutionX;
    bool anchorBottom = extent.top >= 0.5f * _resolutionY;
    int left = static_cast<int>(extent.left * s);
    int top = static_cast<int>(extent.top * s);
    if (anchorRight) {
        left = _options.width - static_cast<int>((_resolutionX - extent.left) * s);
    }
    if (anchorBottom) {
        top = _options.height - static_cast<int>((_resolutionY - extent.top) * s);
    }
    extent.left = left;
    extent.top = top;
    extent.width = static_cast<int>(extent.width * s);
    extent.height = static_cast<int>(extent.height * s);
    control.setScale(s * _options.guiTextScale);
    control.setExtent(std::move(extent));
}

void GUI::applyControlLayout(Control &control) {
    switch (controlScaling(control)) {
    case ScalingMode::PositionRelativeToCenter:
        if (&control != &_rootControl->get()) {
            positionRelativeToCenter(control);
        }
        break;
    case ScalingMode::Stretch:
        stretchControl(control);
        break;
    case ScalingMode::Scaled: {
        float factor = scaledFactor();
        control.stretch(factor, factor);
        break;
    }
    default:
        break;
    }
}

void GUI::applyLayout() {
    if (!_rootControl) {
        return;
    }

    _rootOffset = {0, 0};

    if (_scaling == ScalingMode::Center) {
        _rootOffset = {
            screenCenter().x - _resolutionX / 2,
            screenCenter().y - _resolutionY / 2};
    } else if (_scaling == ScalingMode::CenterHorizontal) {
        _rootOffset.x = screenCenter().x - _resolutionX / 2;
    } else if (_scaling == ScalingMode::Scaled) {
        float factor = scaledFactor();
        int scaledWidth = static_cast<int>(_resolutionX * factor);
        int scaledHeight = static_cast<int>(_resolutionY * factor);
        _rootOffset.x = (_options.width - scaledWidth) / 2;
        _rootOffset.y = (_options.height - scaledHeight) / 2;
    }

    for (auto &control : _controls) {
        applyControlLayout(*control);
    }

    const Control::Extent &rootExtent = _rootControl->get().extent();
    _controlOffset = _scaling == ScalingMode::Stretch
                         ? glm::ivec2(0)
                         : _rootOffset + glm::ivec2(rootExtent.left, rootExtent.top);
}

GUI::ScalingMode GUI::controlScaling(const Control &control) const {
    auto maybeScaling = _scalingByControlTag.find(control.tag());
    return maybeScaling != _scalingByControlTag.end() ? maybeScaling->second : _scaling;
}

glm::ivec2 GUI::renderOffset(const Control &control) const {
    switch (controlScaling(control)) {
    case ScalingMode::Stretch:
    case ScalingMode::PositionRelativeToCenter:
        return {0, 0};
    default:
        return &control == &_rootControl->get() ? _rootOffset : _controlOffset;
    }
}

void GUI::setBackground(std::shared_ptr<graphics::Texture> texture) {
    _background = std::move(texture);
}

bool GUI::handle(const input::Event &event) {
    switch (event.type) {
    case input::EventType::KeyDown:
        return handleKeyDown(event.key.code);

    case input::EventType::KeyUp:
        return handleKeyUp(event.key.code);

    case input::EventType::MouseMotion: {
        glm::ivec2 ctrlCoords(event.motion.x - _controlOffset.x, event.motion.y - _controlOffset.y);
        updateSelection(ctrlCoords.x, ctrlCoords.y);
        if (_selection) {
            _selection->get().handleMouseMotion(ctrlCoords.x, ctrlCoords.y);
        }
        break;
    }
    case input::EventType::MouseButtonDown:
        if (event.button.button == input::MouseButton::Left) {
            _leftMouseDown = true;
        }
        break;
    case input::EventType::MouseButtonUp:
        if (_leftMouseDown && event.button.button == input::MouseButton::Left) {
            _leftMouseDown = false;
            glm::ivec2 ctrlCoords(event.button.x - _controlOffset.x, event.button.y - _controlOffset.y);
            auto control = findControlAt(
                ctrlCoords.x, ctrlCoords.y,
                [](const auto &control) { return control.isSelectable(); });
            if (control) {
                debug("Control clicked: " + control->get().tag(), LogChannel::GUI);
                onClick(control->get().tag());
                return control->get().handleClick(ctrlCoords.x, ctrlCoords.y, event.button.clicks);
            }
        }
        break;

    case input::EventType::MouseWheel:
        if (_selection && _selection->get().handleMouseWheel(event.wheel.x, event.wheel.y))
            return true;
        break;
    }

    return false;
}

bool GUI::handleKeyDown(input::KeyCode key) {
    return false;
}

bool GUI::handleKeyUp(input::KeyCode key) {
    return false;
}

void GUI::updateSelection(int x, int y) {
    auto control = findControlAt(
        x, y,
        [](const auto &control) { return control.isSelectable(); });
    if ((!_selection && !control) ||
        (_selection && control && _selection->get().id() == control->get().id())) {
        return;
    }
    if (_selection) {
        _selection->get().setSelected(false);
        onSelectionChanged(_selection->get().tag(), false);
    }
    _selection = control;
    if (control) {
        control->get().setSelected(true);
        onSelectionChanged(control->get().tag(), true);
    }
}

std::optional<std::reference_wrapper<Control>> GUI::findControlAt(int x, int y,
                                                                  const std::function<bool(const Control &)> &test) const {
    if (!_rootControl) {
        return std::nullopt;
    }
    std::stack<std::reference_wrapper<Control>> controls;
    controls.push(*_rootControl);
    while (!controls.empty()) {
        auto &control = controls.top().get();
        controls.pop();
        if (control.isVisible() && !control.isDisabled() &&
            control.extent().contains(x, y) &&
            test(control)) {
            return control;
        }
        for (auto &child : control.children()) {
            controls.push(child);
        }
    }
    return std::nullopt;
}

void GUI::update(float dt) {
    if (!_rootControl) {
        return;
    }
    _rootControl->get().update(dt);
}

void GUI::render() {
    _graphicsSvc.context.withBlendMode(BlendMode::Normal, [this]() {
        auto retroPass = RetroRenderPass(
            _options,
            _graphicsSvc.context,
            _graphicsSvc.shaderRegistry,
            _graphicsSvc.statistic,
            _graphicsSvc.meshRegistry,
            _graphicsSvc.textureRegistry,
            _graphicsSvc.uniforms);
        auto pbrPass = PBRRenderPass(
            _options,
            _graphicsSvc.context,
            _graphicsSvc.shaderRegistry,
            _graphicsSvc.statistic,
            _graphicsSvc.meshRegistry,
            _graphicsSvc.pbrTextures,
            _graphicsSvc.textureRegistry,
            _graphicsSvc.uniforms);
        auto &pass = _options.pbr ? static_cast<IRenderPass &>(pbrPass)
                                  : static_cast<IRenderPass &>(retroPass);
        if (_background) {
            renderBackground(pass);
        }
        if (!_rootControl) {
            return;
        }
        std::queue<std::pair<std::reference_wrapper<Control>, glm::ivec2>> controls;
        controls.push({*_rootControl, renderOffset(_rootControl->get())});
        while (!controls.empty()) {
            auto &[controlWrapper, offset] = controls.front();
            auto &control = controlWrapper.get();
            control.render({_options.width, _options.height}, offset, pass);
            for (auto &child : control.children()) {
                controls.push({child, renderOffset(child)});
            }
            controls.pop();
        }
    });
}

void GUI::renderBackground(IRenderPass &pass) {
    // The outer background is a surround, not part of the control layout. It
    // covers the viewport without changing aspect, while controls use the
    // independent limiting-axis factor above.
    float cover = std::max(_options.width / static_cast<float>(_resolutionX),
                           _options.height / static_cast<float>(_resolutionY));
    glm::ivec2 size {
        static_cast<int>(_resolutionX * cover),
        static_cast<int>(_resolutionY * cover)};
    pass.drawImage(
        *_background,
        {(_options.width - size.x) / 2, (_options.height - size.y) / 2},
        size);
}

void GUI::clearSelection() {
    if (_selection) {
        _selection->get().setSelected(false);
        onSelectionChanged(_selection->get().tag(), false);
        _selection.reset();
    }
}

std::shared_ptr<Control> GUI::findControl(const std::string &tag) const {
    for (auto &control : _controls) {
        if (control->tag() == tag) {
            return control;
        }
    }
    return nullptr;
}

std::unique_ptr<Control> GUI::newControl(
    ControlType type,
    std::string tag) {
    std::unique_ptr<Control> control;
    switch (type) {
    case ControlType::Panel:
        control = std::make_unique<Panel>(*this, _sceneGraphs, _graphicsSvc, _resourceSvc);
        break;
    case ControlType::Label:
        control = std::make_unique<Label>(*this, _sceneGraphs, _graphicsSvc, _resourceSvc);
        break;
    case ControlType::ImageButton:
        control = std::make_unique<ImageButton>(*this, _sceneGraphs, _graphicsSvc, _resourceSvc);
        break;
    case ControlType::Button:
        control = std::make_unique<Button>(*this, _sceneGraphs, _graphicsSvc, _resourceSvc);
        break;
    case ControlType::ToggleButton:
        control = std::make_unique<ToggleButton>(*this, _sceneGraphs, _graphicsSvc, _resourceSvc);
        break;
    case ControlType::Slider:
        control = std::make_unique<Slider>(*this, _sceneGraphs, _graphicsSvc, _resourceSvc);
        break;
    case ControlType::ScrollBar:
        control = std::make_unique<ScrollBar>(*this, _sceneGraphs, _graphicsSvc, _resourceSvc);
        break;
    case ControlType::ProgressBar:
        control = std::make_unique<ProgressBar>(*this, _sceneGraphs, _graphicsSvc, _resourceSvc);
        break;
    case ControlType::ListBox:
        control = std::make_unique<ListBox>(*this, _sceneGraphs, _graphicsSvc, _resourceSvc);
        break;
    case ControlType::IconChain:
        control = std::make_unique<IconChain>(*this, _sceneGraphs, _graphicsSvc, _resourceSvc);
        break;
    default:
        debug("Unsupported control type: " + std::to_string(static_cast<int>(type)), LogChannel::GUI);
        return nullptr;
    }

    control->setTag(tag);

    return control;
}

void GUI::addControlToFront(std::shared_ptr<Control> control, ControlCoordinates coordinates) {
    if (coordinates == ControlCoordinates::Authored) {
        applyControlLayout(*control);
    }
    _rootControl->get().addChildToFront(*control);
    _tagToControl.insert({control->tag(), *control});
    _controls.push_back(std::move(control));
}

void GUI::addControlToBack(std::shared_ptr<Control> control, ControlCoordinates coordinates) {
    if (coordinates == ControlCoordinates::Authored) {
        applyControlLayout(*control);
    }
    _rootControl->get().addChildToBack(*control);
    _tagToControl.insert({control->tag(), *control});
    _controls.push_back(std::move(control));
}

} // namespace gui

} // namespace reone
