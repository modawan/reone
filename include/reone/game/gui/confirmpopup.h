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

#pragma once

#include "../gui.h"

#include "reone/gui/control/button.h"
#include "reone/gui/control/listbox.h"

namespace reone {

namespace graphics {

class Texture;

}

namespace game {

/**
 * Centered in-game message popup based on the game's confirmation dialog,
 * dismissed with the OK button. An optional icon is shown left of the
 * message text.
 */
class ConfirmPopup : public GameGUI {
public:
    ConfirmPopup(Game &game, ServicesView &services) :
        GameGUI(game, services) {
        _resRef = guiResRef("confirm");
    }

    void show(const std::string &message, std::shared_ptr<graphics::Texture> icon = nullptr);
    void hide();

    bool isVisible() const { return _visible; }

private:
    struct Controls {
        std::shared_ptr<gui::Button> BTN_CANCEL;
        std::shared_ptr<gui::Button> BTN_OK;
        std::shared_ptr<gui::ListBox> LB_MESSAGE;
    };

    Controls _controls;
    bool _visible {false};

    std::shared_ptr<gui::Control> _icon;
    gui::Control::Extent _messageExtent;

    void preload(gui::IGUI &gui) override;
    void onGUILoaded() override;

    void bindControls() {
        _controls.BTN_CANCEL = findControl<gui::Button>("BTN_CANCEL");
        _controls.BTN_OK = findControl<gui::Button>("BTN_OK");
        _controls.LB_MESSAGE = findControl<gui::ListBox>("LB_MESSAGE");
    }
};

} // namespace game

} // namespace reone
