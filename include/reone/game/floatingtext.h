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

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace reone {

namespace graphics {

class Font;

}

namespace game {

class Creature;
class Game;
class Object;
struct ServicesView;

/**
 * Target-anchored floating feedback shown over objects in the active area.
 */
class FloatingText {
public:
    FloatingText(Game &game, ServicesView &services) :
        _game(game),
        _services(services) {
    }

    void addDamage(const Object &object, int amount, int adjustedAmount, uint32_t damager);
    void addHeal(const Object &object, int amount);
    void addMiss(const Creature &attacker, const Object &target);

    void update(float dt);
    void render();
    void reset();

private:
    enum class Style {
        Damage,
        Heal,
        Miss,
    };

    struct Entry {
        uint32_t objectId;
        std::string text;
        Style style;
        float remaining;
        int stack;
    };

    Game &_game;
    ServicesView &_services;

    std::vector<Entry> _entries;
    std::shared_ptr<graphics::Font> _font;
    std::string _fontResRef;

    void add(const Object &object, std::string text, Style style);
};

} // namespace game

} // namespace reone
