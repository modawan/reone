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

#include "reone/game/gui.h"

#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/gui/sounds.h"
#include "reone/graphics/di/services.h"
#include "reone/gui/control.h"
#include "reone/gui/guis.h"
#include "reone/resource/di/services.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/provider/textures.h"
#include "reone/scene/di/services.h"

using namespace reone::audio;
using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

GameGUI::GameGUI(Game &game,
                 ServicesView &services) :
    _game(game),
    _services(services) {

    if (game.isTSL()) {
        _baseColor = kTSLGUIColorBase;
        _hilightColor = kTSLGUIColorHilight;
        _disabledColor = kTSLGUIColorDisabled;
    } else {
        _baseColor = kGUIColorBase;
        _hilightColor = kGUIColorHilight;
        _disabledColor = kGUIColorDisabled;
    }
}

void GameGUI::init() {
    if (_resRef.empty()) {
        throw std::logic_error("GUI resRef must not be empty");
    }
    _gui = _services.gui.guis.get(_resRef, std::bind(&GameGUI::preload, this, std::placeholders::_1));
    if (!_gui) {
        throw ResourceNotFoundException(str(boost::format("GUI not found: %s") % _resRef));
    }
    _gui->setEventListener(*this);

    onGUILoaded();
}

void GameGUI::preload(IGUI &gui) {
    if (_game.isTSL()) {
        gui.setResolution(800, 600);
    } else {
        gui.setDefaultHilightColor(kGUIColorHilight);
    }
}

bool GameGUI::handle(const input::Event &event) {
    if (!_gui) {
        return false;
    }
    return _gui->handle(event);
}

void GameGUI::update(float dt) {
    if (_gui) {
        _gui->update(dt);
    }
}

void GameGUI::render() {
    if (_gui) {
        _gui->render();
    }
}

void GameGUI::loadBackground(BackgroundType type) {
    std::string resRef;

    if (_game.isTSL()) {
        switch (type) {
        case BackgroundType::Computer0:
        case BackgroundType::Computer1:
            resRef = "pnl_computer_pc";
            break;
        default:
            break;
        }
    } else {
        auto &options = _game.options().graphics;
        if ((options.width == 1600 && options.height == 1200) ||
            (options.width == 1280 && options.height == 960) ||
            (options.width == 1024 && options.height == 768) ||
            (options.width == 800 && options.height == 600)) {

            resRef = str(boost::format("%dx%d") % options.width % options.height);
        } else {
            resRef = "1600x1200";
        }
        switch (type) {
        case BackgroundType::Menu:
            resRef += "back";
            break;
        case BackgroundType::Load:
            resRef += "load";
            break;
        case BackgroundType::Computer0:
            resRef += "comp0";
            break;
        case BackgroundType::Computer1:
            resRef += "comp1";
            break;
        default:
            return;
        }
    }

    if (_gui) {
        _gui->setBackground(_services.resource.textures.get(resRef, TextureUsage::MainTex));
    }
}

void GameGUI::tintK2InGameFooter() {
    if (!_game.isTSL()) {
        return;
    }

    for (auto &tag : {"LBL_BAR3", "LBL_BAR4", "LBL_BAR5"}) {
        auto control = _gui->findControl(tag);
        if (control) {
            control->setTintBorderFill(true);
        }
    }
}

void GameGUI::tintK2InGameHeader() {
    if (!_game.isTSL()) {
        return;
    }

    for (auto &tag : {"LBL_BAR1", "LBL_BAR2", "LBL_BAR6"}) {
        auto control = _gui->findControl(tag);
        if (control) {
            control->setTintBorderFill(true);
        }
    }
}

void GameGUI::enableK2ButtonBodyFill(Control &control) {
    if (!_game.isTSL()) {
        return;
    }

    auto edge = _services.resource.textures.get("uibit_brdr_16wet", TextureUsage::GUI);
    auto corner = _services.resource.textures.get("uibit_brdr_16wct", TextureUsage::GUI);

    auto border = control.border();
    border.edge = edge;
    border.corner = corner;
    control.setBorder(std::move(border));

    auto hilight = control.hilight();
    hilight.edge = std::move(edge);
    hilight.corner = std::move(corner);
    control.setHilight(std::move(hilight));
}

void GameGUI::enableK2ButtonBodyFill(const std::shared_ptr<Control> &control) {
    if (!control) {
        return;
    }

    enableK2ButtonBodyFill(*control);
}

void GameGUI::fillK2SectionStrip(const std::shared_ptr<Control> &topBar, const std::shared_ptr<Control> &bottomBar) {
    static constexpr int kLineThickness = 2;

    if (!_game.isTSL() || !topBar || !bottomBar) {
        return;
    }

    topBar->setBorderFill("uibit_brdr_16we");
    topBar->setTintBorderFill(true);

    bottomBar->setBorderFill("uibit_brdr_16we");
    bottomBar->setBorderFillTransform(Control::Border::FillTransform::Rotate180);
    bottomBar->setTintBorderFill(true);

    auto &upper = topBar->extent();
    auto &lower = bottomBar->extent();
    int lowerLineTop = lower.top + kLineThickness;
    bottomBar->setExtentTop(lowerLineTop - lower.height);

    int top = upper.top;
    int height = lowerLineTop - top;
    if (height <= 0) {
        return;
    }

    auto fill = std::shared_ptr<Control>(_gui->newControl(ControlType::Label, topBar->tag() + "_SECTION_FILL"));
    fill->setExtent({upper.left, top, upper.width, height});
    fill->setBorderFill("uibit_fill_2wt");
    fill->setBorderColor(topBar->border().color);
    fill->setTintBorderFill(true);
    _gui->addControlToFront(std::move(fill));
}

void GameGUI::useK2ShellTitle(const std::shared_ptr<Control> &control) {
    if (!_game.isTSL() || !control) {
        return;
    }

    _k2InGameTitleControl = control;
    control->setVisible(false);
}

void GameGUI::updateK2FilterButton(const std::shared_ptr<Control> &button, bool selected) {
    if (!button) {
        return;
    }

    button->setSelected(selected);
    button->setTextColor(selected ? glm::vec3 {1.0f} : _baseColor);
}

std::string GameGUI::guiResRef(const std::string &base) const {
    return _game.isTSL() ? base + "_p" : base;
}

void GameGUI::onClick(const std::string &control) {
    auto clip = _services.game.guiSounds.getOnClick();
    _audioSource = _services.audio.mixer.play(std::move(clip), AudioType::Sound);
}

void GameGUI::onSelectionChanged(const std::string &control, bool selected) {
    if (selected) {
        auto clip = _services.game.guiSounds.getOnEnter();
        _audioSource = _services.audio.mixer.play(std::move(clip), AudioType::Sound);
    }
}

} // namespace game

} // namespace reone
