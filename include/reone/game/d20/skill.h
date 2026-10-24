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

#include "reone/graphics/texture.h"

#include "../types.h"

namespace reone {

namespace game {

struct Skill {
    std::string name;
    std::string description;
    std::shared_ptr<graphics::Texture> icon;
    /** A creature without ranks may use the skill. */
    bool untrained {false};
    /** A creature of any class may use the skill untrained, whether or not its class names a skills table. */
    bool allClassesCanUse {false};
    /** The ability whose modifier the skill adds. */
    Ability keyAbility {Ability::Strength};
};

} // namespace game

} // namespace reone
