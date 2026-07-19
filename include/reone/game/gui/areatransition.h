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

#include "reone/gui/control/label.h"

namespace reone {

namespace game {

/**
 * Top-centre module transition banner: destination text over the banner
 * background, with the running-person icon beneath. Purely presentational -
 * accepts a destination string, never a live object, so it cannot hold stale
 * state across module unloads.
 */
class AreaTransition : public GameGUI {
public:
    AreaTransition(Game &game, ServicesView &services);

    /** Show the banner and icon for a transition with the given destination. */
    void show(const std::string &destination);
    void hide();

    bool isVisible() const { return _visible; }

protected:
    void preload(gui::IGUI &gui) override;

private:
    struct Controls {
        std::shared_ptr<gui::Label> LBL_DESCRIPTION;
        std::shared_ptr<gui::Label> LBL_ICON;
        std::shared_ptr<gui::Label> LBL_TEXTBG;
    };

    Controls _controls;
    bool _visible {false};
    std::string _destination;

    void onGUILoaded() override;

    void bindControls() {
        _controls.LBL_DESCRIPTION = findControl<gui::Label>("LBL_DESCRIPTION");
        _controls.LBL_ICON = findControl<gui::Label>("LBL_ICON");
        _controls.LBL_TEXTBG = findControl<gui::Label>("LBL_TEXTBG");
    }
};

} // namespace game

} // namespace reone
