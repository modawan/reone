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

#include "reone/game/action/followleader.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/party.h"

namespace reone {

namespace game {

void FollowLeaderAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    // The party has no leader while it is empty: before it is first populated,
    // after a module transition resets it, and once the last member is removed
    // by RemovePartyMember. A FollowLeader action queued on a creature can still
    // be executed in those windows, so there is nothing to follow and the action
    // is dropped instead of dereferencing a null leader.
    auto leader = _game.party().getLeader();
    if (!leader) {
        complete();
        return;
    }

    auto creatureActor = _game.getObjectById<Creature>(actor.id());
    glm::vec3 destination(leader->position());
    float distance2 = creatureActor->getSquareDistanceTo(glm::vec2(destination));
    bool run = distance2 > kDistanceWalk;

    if (creatureActor->navigateTo(destination, run, kDefaultFollowDistance, dt)) {
        complete();
    }
}

} // namespace game

} // namespace reone
