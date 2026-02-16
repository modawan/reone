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

#include "actionslot.h"

namespace reone {

namespace gui {
class Button;
class Label;
} // namespace gui

namespace game {

struct ServicesView;
class Game;

class ActionBar {
public:
    ActionBar(Game &game, ServicesView &services) :
        _game(game), _services(services) {}

    void addDescription(std::shared_ptr<gui::Label> desc,
                        std::shared_ptr<gui::Label> background);

    void addSlot(std::shared_ptr<gui::Button> button,
                 std::shared_ptr<gui::Button> up,
                 std::shared_ptr<gui::Button> down);

    void update();
    void render();

private:
    void handleMouseWheel(ActionSlot &slot, int x, int y);
    void handleMouseButtonDown(ActionSlot &slot);

private:
    Game &_game;
    ServicesView &_services;

    std::shared_ptr<gui::Label> _desc;
    std::shared_ptr<gui::Label> _descBg;

    struct Slot {
        ActionSlot slot;
        std::shared_ptr<gui::Button> button;
        std::shared_ptr<gui::Button> up;
        std::shared_ptr<gui::Button> down;
    };
    std::vector<Slot> _slots;
};

} // namespace game
} // namespace reone
