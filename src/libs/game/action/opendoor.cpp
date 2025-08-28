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

#include "reone/game/action/opendoor.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/door.h"
#include "reone/game/script/runner.h"

namespace reone {

namespace game {

void OpenDoorAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    if (actor.type() == ObjectType::Creature) {
        auto &creature = static_cast<Creature &>(actor);
        bool reached = creature.navigateTo(_door->position(), true, kDefaultMaxObjectDistance, dt);
        if (!reached) {
            return;
        }
        creature.face(*_door);
    }

    if (!_door->isLocked()) {
        _door->open();
    }

    bool isObjectSelf = _door->id() == actor.id();
    if (!isObjectSelf) {
        if (_door->isLocked()) {
            _door->onFailToOpen(actor);
        } else {
            _door->onOpen(actor);
        }
    }

    complete();
}

} // namespace game

} // namespace reone
