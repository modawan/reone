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

#include "reone/game/gui/confirmpopup.h"

#include "reone/game/game.h"

using namespace reone::gui;

namespace reone {

namespace game {

void ConfirmPopup::preload(IGUI &gui) {
    gui.setScaling(GUI::ScalingMode::PositionRelativeToCenter);
}

void ConfirmPopup::onGUILoaded() {
    bindControls();

    _controls.BTN_CANCEL->setVisible(false);
    _controls.BTN_OK->setOnClick([this]() {
        hide();
    });
    _controls.LB_MESSAGE->setItemsInteractive(false);
}

void ConfirmPopup::show(const std::string &message) {
    _controls.LB_MESSAGE->clearItems();
    _controls.LB_MESSAGE->addTextLinesAsItems(message);
    _visible = true;
}

void ConfirmPopup::hide() {
    _visible = false;
}

} // namespace game

} // namespace reone
