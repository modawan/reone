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

#include "reone/game/gui/ingame/options.h"

#include "reone/game/game.h"
#include "reone/gui/control/button.h"
#include "reone/gui/control/listbox.h"

using namespace reone::audio;
using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

void OptionsMenu::onGUILoaded() {
    loadBackground(BackgroundType::Menu);
    bindControls();
    tintK2InGameFooter();
    tintK2InGameHeader();

    if (_game.isTSL()) {
        fillK2SectionStrip(_controls.LBL_BAR1, _controls.LBL_BAR2);
        _controls.LB_DESC->setTintBorderFill(true);
        useK2ShellTitle(_controls.LBL_TITLE);
        for (auto &button : {
                 _controls.BTN_SAVEGAME,
                 _controls.BTN_LOADGAME,
                 _controls.BTN_GAMEPLAY,
                 _controls.BTN_FEEDBACK,
                 _controls.BTN_AUTOPAUSE,
                 _controls.BTN_GRAPHICS,
                 _controls.BTN_SOUND,
                 _controls.BTN_QUIT,
                 _controls.BTN_EXIT}) {
            enableK2ButtonBodyFill(button);
        }
    }
    _controls.BTN_LOADGAME->setOnClick([this]() {
        _game.openSaveLoad(SaveLoadMode::LoadFromInGame);
    });
    _controls.BTN_SAVEGAME->setOnClick([this]() {
        _game.openSaveLoad(SaveLoadMode::Save);
    });
    _controls.BTN_EXIT->setOnClick([this]() {
        _game.openInGame();
    });
}

} // namespace game

} // namespace reone
