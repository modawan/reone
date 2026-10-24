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

#include "reone/game/game.h"
#include "reone/gui/control.h"

namespace reone::game {

GameGUI::GameGUI(Game &game, ServicesView &services) :
    PresentationGUI(game.gameId(), game.options().graphics,
                    {services.gui.guis, services.resource.textures,
                     services.audio.mixer, services.game.guiSounds}),
    _game(game),
    _services(services) {
}

void GameGUI::showOnlyNamedControls(
    const std::unordered_set<std::string> &controls,
    const std::unordered_set<std::string> &titleControls) {
    for (auto &control : _gui->rootControl().children()) {
        const std::string &tag = control.get().tag();
        if (controls.count(tag) == 0 && titleControls.count(tag) == 0) {
            control.get().setVisible(false);
        }
    }
}

} // namespace reone::game
