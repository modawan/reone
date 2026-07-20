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

    // Authored for 640x480 in both games. Keep the authored top coordinate,
    // while centering the banner horizontally in the current viewport.
    gui.setResolution(640, 480);
    gui.setScaling(GUI::ScalingMode::CenterHorizontal);
}

void AreaTransition::onGUILoaded() {
    bindControls();
    hide();
}

void AreaTransition::show(const std::string &destination) {
    if (destination.empty()) {
        hide();
        return;
    }
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

} // namespace game

} // namespace reone
