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

#include "reone/script/types.h"

#include "actionslot.h"

namespace reone {

namespace gui {
class Button;
class Label;
} // namespace gui

namespace graphics {
class Texture;
} // namespace graphics

namespace game {

struct ServicesView;
class Creature;
class Game;
class Item;

class ActionBar {
public:
    ActionBar(Game &game, ServicesView &services) :
        _game(game), _services(services) {}

    void addDescription(std::shared_ptr<gui::Label> desc,
                        std::shared_ptr<gui::Label> background);

    void addSlot(std::shared_ptr<gui::Button> button,
                 std::shared_ptr<gui::Button> action,
                 std::shared_ptr<gui::Button> up,
                 std::shared_ptr<gui::Button> down);

    void update(float dt);
    /** \p tag names the action button of one of the bar's slots. */
    bool ownsActionControl(const std::string &tag) const;
    void render(float layoutScale);

private:
    friend class ActionBarTestAccess;

    void handleMouseWheel(ActionSlot &slot, int x, int y);
    void handleMouseButtonDown(ActionSlot &slot);
    /** Select the entry of \p slot that is the leader's current form, implant mode or behaviour. */
    void showCurrentSelection(ActionSlot &slot, const Creature &leader) const;

private:
    Game &_game;
    ServicesView &_services;

    std::shared_ptr<gui::Label> _desc;
    std::shared_ptr<gui::Label> _descBg;

    struct Slot {
        ActionSlot slot;
        std::shared_ptr<gui::Button> button;
        std::shared_ptr<gui::Button> action;
        std::shared_ptr<gui::Button> up;
        std::shared_ptr<gui::Button> down;
        std::shared_ptr<graphics::Texture> upBorderFill;
        std::shared_ptr<graphics::Texture> upHilightFill;
        std::shared_ptr<graphics::Texture> downBorderFill;
        std::shared_ptr<graphics::Texture> downHilightFill;
    };
    std::vector<Slot> _slots;
    ActionMenuFeedback _feedback;
    /** The leader the slots were last filled for. */
    uint32_t _leaderId {script::kObjectInvalid};
    /** The items the leader can draw on, gathered anew each frame. */
    std::vector<std::shared_ptr<Item>> _carried;
};

} // namespace game
} // namespace reone
