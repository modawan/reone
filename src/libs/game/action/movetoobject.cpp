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

#include "reone/game/action/movetoobject.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object.h"

namespace reone {

namespace game {

void MoveToObjectAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto dest = _moveTo->position();
    auto creatureActor = _game.getObjectById<Creature>(actor.id());

    creatureActor->addCombatActionToHistory(self);
    bool reached = creatureActor->navigateTo(dest, _run, _range, dt);
    if (reached) {
        complete();
    }
}

} // namespace game

} // namespace reone
