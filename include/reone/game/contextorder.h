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

#include "contextaction.h"

#include <functional>

namespace reone {

namespace game {

class Creature;
class Game;
class Object;
struct ServicesView;

/** What came of choosing an entry of an action menu. */
struct ContextOrderOutcome {
    /** The entry could be chosen; whether its order then arrived is the game's affair. */
    bool available {true};
    /** Why not, for the menu to show. */
    PowerUnavailableReason reason {PowerUnavailableReason::None};
};

/**
 * Carries out the entry of an action menu chosen for \p actor against
 * \p target: an attack, a combat feat, a skill use, the door saber, a power
 * or an item's use. The entry runs inside the menu's wrapper: out of combat
 * mode it first drops the actor's pending round entries and, for a hostile
 * creature or a bash, posts the combat message; when it is done and combat
 * mode is still off, the actor enters combat mode for a hostile creature and
 * its actions are cleared, before the entry's order arrives.
 *
 * @param targetHostile the target is the actor's enemy
 * @param accepted called once the entry is known to be available, before its order
 */
ContextOrderOutcome giveContextOrder(
    Game &game,
    ServicesView &services,
    const std::shared_ptr<Creature> &actor,
    const std::shared_ptr<Object> &target,
    ContextAction &entry,
    bool targetHostile,
    const std::function<void()> &accepted = {});

} // namespace game

} // namespace reone
