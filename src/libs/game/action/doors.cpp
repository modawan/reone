/*
 * Copyright (c) 2025 The reone project contributors
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
#include "reone/game/action/closedoor.h"
#include "reone/game/action/lockobject.h"
#include "reone/game/action/opencontainer.h"
#include "reone/game/action/opendoor.h"
#include "reone/game/action/openlock.h"
#include "reone/game/action/unlockobject.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/item.h"
#include "reone/game/object/placeable.h"
#include "reone/game/party.h"
#include "reone/game/script/runner.h"
#include "reone/system/logutil.h"

#include "commonactions.h"

namespace reone {

namespace game {

void tryUnlockDoorWithKey(Game &game, Door &door, Object &actor, Party &party) {
    if (!door.isLocked() || !door.isKeyRequired() || door.keyName().empty()) {
        return;
    }
    Object *keyOwner = &actor;
    auto key = actor.getItemByTag(door.keyName());
    if (!key) {
        auto player = party.player();
        if (player && player->id() != actor.id()) {
            key = player->getItemByTag(door.keyName());
            keyOwner = player.get();
        }
    }
    if (!key) {
        return;
    }
    door.setLocked(false);
    if (door.isAutoRemoveKey()) {
        bool last = false;
        keyOwner->removeItem(key, last);
        if (last) {
            game.destroyRuntimeObjectGraph(key);
        }
    }
}

bool unlockDoor(Door &door, Object &actor, float distance, float dt) {
    if (actor.type() == ObjectType::Creature) {
        auto &creature = static_cast<Creature &>(actor);
        bool reached = creature.navigateTo(door.position(), true, distance, dt);
        if (!reached) {
            return false;
        }
        creature.face(door);
        creature.playAnimation(AnimationType::LoopingUnlockDoor);
    }

    // FIXME: wait for animation to play

    if (door.isKeyRequired()) {
        // FIXME: run onFailedToOpen?
        return true;
    }

    door.setLocked(false);
    door.open();
    door.onOpen(actor.id());

    return true;
}

bool unlockPlaceable(Placeable &placeable, Object &actor, float distance, float dt) {
    if (auto *creature = dyn_cast<Creature>(&actor)) {
        bool reached = creature->navigateTo(placeable.position(), true, distance, dt);
        if (!reached) {
            return false;
        }
        creature->face(placeable);
        creature->playAnimation(AnimationType::LoopingUnlockDoor);
    }

    // FIXME: wait for animation to play

    if (placeable.isKeyRequired()) {
        return true;
    }

    placeable.setLocked(false);

    return true;
}

void jumpToPositionFacing(Object &actor, const glm::vec3 &position,
                          float facing, Game &game) {
    actor.setPosition(position);
    actor.setFacing(facing);

    auto module = game.module();
    if (!module) {
        return;
    }

    auto area = game.module()->area();
    if (!area) {
        return;
    }

    Room *roomBefore = actor.room();
    area->determineObjectRoom(actor);
    Room *roomAfter = actor.room();

    if (auto leader = game.party().getLeader()) {
        if (leader->id() == actor.id()) {
            area->onPartyLeaderMoved(roomBefore != roomAfter);
        }
    }
}

void OpenDoorAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    if (actor.type() == ObjectType::Creature) {
        auto &creature = static_cast<Creature &>(actor);
        bool reached = creature.navigateTo(_door->position(), true, kDefaultMaxObjectDistance, dt);
        if (!reached) {
            return;
        }
        creature.face(*_door);
    }

    // Allow a door to open itself, bypassing all requirements. This is used by
    // scripts that assign actions to doors.
    bool isObjectSelf = _door->id() == actor.id();
    if (isObjectSelf) {
        _door->open();
        _door->onOpen(actor.id());
        complete();
        return;
    }

    tryUnlockDoorWithKey(_game, *_door, actor, _game.party());

    if (!_door->isLocked()) {
        _door->open();
        _door->onOpen(actor.id());
    } else {
        _door->onFailToOpen(actor);
    }

    complete();
}

void CloseDoorAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto creatureActor = _game.getObjectById<Creature>(actor.id());

    bool reached = !creatureActor || creatureActor->navigateTo(_door->position(), true, kDefaultMaxObjectDistance, dt);
    if (reached) {
        _door->close();
        complete();
    }
}

void LockObjectAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    // TODO: implement

    complete();
}

void UnlockObjectAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    if (!_target || _target->isDead()) {
        complete();
        return;
    }

    switch (_target->type()) {
    case ObjectType::Door:
        static_cast<Door &>(*_target).setLocked(false);
        break;
    case ObjectType::Placeable:
        static_cast<Placeable &>(*_target).setLocked(false);
        break;
    default:
        break;
    }
    complete();
}

void OpenLockAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    if (_target->type() == ObjectType::Door) {
        if (unlockDoor(static_cast<Door &>(*_target), actor, kDefaultMaxObjectDistance, dt)) {
            complete();
        }
        return;
    }

    warn("ActionExecutor: unsupported OpenLockAction target");
    complete();
}

void OpenContainerAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    if (!_object || !isa<Creature>(actor)) {
        complete();
        return;
    }
    if (auto *placeable = dyn_cast<Placeable>(_object.get())) {
        if (placeable->isLocked()) {
            complete();
            return;
        }
    }

    auto &creatureActor = cast<Creature>(actor);
    bool reached = creatureActor.navigateTo(_object->position(), true, kDefaultMaxObjectDistance, dt);
    if (reached) {
        _game.openContainer(_object);
        complete();
    }
}

} // namespace game

} // namespace reone
