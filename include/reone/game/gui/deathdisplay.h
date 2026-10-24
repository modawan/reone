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

#include "reone/gui/control/button.h"

#include "../gui.h"

namespace reone {

namespace game {

/**
 * The second title's authored gameover panel. The first title instead uses the
 * existing confirmation message box; neither title borrows the other's layout.
 *
 * Each time the panel is put up it ignores its buttons for half a second,
 * showing them unready until then. Every button ends the game-over state; Load
 * Game and Last Save act only when there are saves, and a Last Save with no
 * save of the current character says so and leaves the panel up.
 */
class DeathDisplay : public GameGUI {
public:
    DeathDisplay(Game &game, ServicesView &services) :
        GameGUI(game, services) {
        _resRef = guiResRef("gameover");
    }

    /** Put the panel up; it is ready again only once it has been taken down. */
    void present();
    /** Take the panel down. */
    void dismiss() { _presented = false; }
    bool isPresented() const { return _presented; }

    void update(float dt) override;

private:
    struct Controls {
        std::shared_ptr<gui::Button> BTN_LASTSAVE;
        std::shared_ptr<gui::Button> BTN_LOADGAME;
        std::shared_ptr<gui::Button> BTN_QUIT;
    };

    Controls _controls;
    bool _presented {false};
    /** The seconds until the buttons take presses. */
    float _readiness {0.0f};

    void onGUILoaded() override;
    void showButtonsReady(bool ready);
};

} // namespace game

} // namespace reone
