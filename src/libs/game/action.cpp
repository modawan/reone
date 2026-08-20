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

#include "reone/game/action.h"

#include "reone/game/action/usefeat.h"
#include "reone/game/attack.h"
#include "reone/game/game.h"
#include "reone/game/object.h"
#include "reone/system/logutil.h"

namespace reone {

namespace game {

void Action::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    warn("Action execution not implemented: " + std::to_string(static_cast<int>(_type)));
    complete();
}

bool Action::hasValidUserActionTargets(const Object &actor) const {
    const Object *primary = userActionPrimaryTarget();
    const Object *secondary = userActionSecondaryTarget(actor);
    if (!primary || !secondary) {
        return false;
    }

    auto primaryResolved = _game.getObjectById(primary->id());
    auto secondaryResolved = _game.getObjectById(secondary->id());
    auto isValidRequiredTarget = [](const std::shared_ptr<Object> &object) {
        return object &&
               !(object->type() == ObjectType::Creature && object->isDead());
    };
    return isValidRequiredTarget(primaryResolved) &&
           isValidRequiredTarget(secondaryResolved);
}

bool isHostileAction(Action &action) {
    switch (action.type()) {
    case ActionType::AttackObject:
        return true;
    case ActionType::UseFeat:
        return isPhysicalAttackFeat(static_cast<UseFeatAction &>(action).feat());
    default:
        break;
    }
    return false;
}

} // namespace game

} // namespace reone
