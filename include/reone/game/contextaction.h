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

#include "types.h"

namespace reone {

namespace game {

class Spell;
class Item;

struct ContextAction {
    ActionType type {ActionType::Invalid};
    FeatType feat {FeatType::Invalid};
    SkillType skill {SkillType::Invalid};
    std::shared_ptr<Spell> spell;
    std::shared_ptr<Item> item;

    ContextAction(ActionType type) :
        type(type) {}
    ContextAction(FeatType feat) :
        type(ActionType::UseFeat), feat(feat) {}
    ContextAction(SkillType skill) :
        type(ActionType::UseSkill), skill(skill) {}
    ContextAction(std::shared_ptr<Item> item, std::shared_ptr<Spell> spell) :
        type(ActionType::CastSpellAtObject), spell(spell), item(item) {}
};

} // namespace game

} // namespace reone
