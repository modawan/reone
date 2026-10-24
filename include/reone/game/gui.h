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

#pragma once

#include <string>
#include <unordered_set>

#include "reone/game/di/services.h"
#include "presentationgui.h"

namespace reone::game {

class Game;

/** Strong gameplay facade for existing screen constructors and subclasses. */
class GameGUI : public PresentationGUI {
protected:
    Game &_game;
    ServicesView &_services;

    GameGUI(Game &game, ServicesView &services);

    /**
     * Shows only the controls the panel names: every control of the layout
     * whose tag is in neither \p controls nor \p titleControls is hidden.
     *
     * @param controls the controls the panel names in both titles
     * @param titleControls the controls it names in the current title only
     */
    void showOnlyNamedControls(
        const std::unordered_set<std::string> &controls,
        const std::unordered_set<std::string> &titleControls);
};

} // namespace reone::game
