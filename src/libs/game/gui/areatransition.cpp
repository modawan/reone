/*
 * Copyright (c) 2026 The reone project contributors
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

#include "reone/game/gui/areatransition.h"

#include "reone/game/game.h"
#include "reone/game/transitioncandidate.h"

using namespace reone::gui;

namespace reone {

namespace game {

AreaTransition::AreaTransition(Game &game, ServicesView &services) :
    GameGUI(game, services) {
    // The dedicated transition banner GUI is named differently per game.
    _resRef = game.isTSL() ? guiResRef("areatrans") : "areatransition";
}

void AreaTransition::preload(IGUI &gui) {
    GameGUI::preload(gui);

    // Authored for 640x480 in both games. Scale every axis by the same
    // limiting-axis factor while retaining the authored top anchor.
    gui.setResolution(640, 480);
    gui.setScaling(GUI::ScalingMode::ScaledTopCenter);
}

void AreaTransition::onGUILoaded() {
    bindControls();
    // The banner's colour tints its images as well as their frames. A dimmed
    // banner draws both in the refused colour.
    _controls.LBL_TEXTBG->setTintBorderFill(true);
    _controls.LBL_ICON->setTintBorderFill(true);
    _controls.LBL_TEXTBG->setBorderColorOverride(kTravelRefusedBorderColor);
    _controls.LBL_ICON->setBorderColorOverride(kTravelRefusedBorderColor);
    if (_game.isTSL()) {
        _travelRefusedTextColor = kTSLTravelRefusedTextColor;
    } else {
        // KotOR's open banner is white with blue text.
        _controls.LBL_TEXTBG->setBorderColor(kTravelOpenBorderColor);
        _controls.LBL_ICON->setBorderColor(kTravelOpenBorderColor);
        _controls.LBL_DESCRIPTION->setTextColor(kTravelOpenTextColor);
        _travelRefusedTextColor = kTravelRefusedTextColor;
    }
    _travelOpenTextColor = _controls.LBL_DESCRIPTION->text().color;
    _travelOpen = true;
    hide();
}

void AreaTransition::show(const std::string &destination, bool travelOpen) {
    if (destination.empty()) {
        hide();
        return;
    }
    setTravelOpen(travelOpen);
    std::string displayName(transitionDestinationDisplayName(destination));
    if (_visible && displayName == _destination) {
        return;
    }
    _destination = std::move(displayName);
    if (_controls.LBL_DESCRIPTION) {
        _controls.LBL_DESCRIPTION->setTextMessage(_destination);
        _controls.LBL_DESCRIPTION->setVisible(true);
    }
    if (_controls.LBL_TEXTBG) {
        _controls.LBL_TEXTBG->setVisible(true);
    }
    if (_controls.LBL_ICON) {
        _controls.LBL_ICON->setVisible(true);
    }
    _visible = true;
}

void AreaTransition::hide() {
    _destination.clear();
    if (_controls.LBL_DESCRIPTION) {
        _controls.LBL_DESCRIPTION->setTextMessage("");
        _controls.LBL_DESCRIPTION->setVisible(false);
    }
    if (_controls.LBL_TEXTBG) {
        _controls.LBL_TEXTBG->setVisible(false);
    }
    if (_controls.LBL_ICON) {
        _controls.LBL_ICON->setVisible(false);
    }
    _visible = false;
}

void AreaTransition::setTravelOpen(bool open) {
    if (open == _travelOpen) {
        return;
    }
    _travelOpen = open;
    if (_controls.LBL_DESCRIPTION) {
        _controls.LBL_DESCRIPTION->setTextColor(open ? _travelOpenTextColor : _travelRefusedTextColor);
    }
    if (_controls.LBL_TEXTBG) {
        _controls.LBL_TEXTBG->setUseBorderColorOverride(!open);
    }
    if (_controls.LBL_ICON) {
        _controls.LBL_ICON->setUseBorderColorOverride(!open);
    }
}

} // namespace game

} // namespace reone
