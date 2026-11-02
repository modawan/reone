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

#include <algorithm>
#include <cassert>

#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/game/action.h"
#include "reone/game/action/closedoor.h"
#include "reone/game/action/doorsaber.h"
#include "reone/game/action/lockobject.h"
#include "reone/game/action/opencontainer.h"
#include "reone/game/action/opendoor.h"
#include "reone/game/action/playanimation.h"
#include "reone/game/action/unlockobject.h"
#include "reone/game/action/wait.h"
#include "reone/game/di/services.h"
#include "reone/game/event.h"
#include "reone/game/game.h"
#include "reone/game/object.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/item.h"
#include "reone/game/object/placeable.h"
#include "reone/game/party.h"
#include "reone/game/script/runner.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/system/logutil.h"
#include "reone/system/randomutil.h"

#include "commonactions.h"

namespace reone {

namespace game {

static constexpr float kLockWorkDuration = 1.5f;
static constexpr int kLockedEvent = static_cast<int>(SavedSignalEventType::Locked);
static constexpr int kFailToOpenEvent = static_cast<int>(SavedSignalEventType::FailToOpen);
static constexpr int kUsedEvent = static_cast<int>(SavedSignalEventType::Used);
static constexpr int kKeyRequiredStrRef = 1431;

bool springTrapOnUse(Object &target, Object &actor) {
    auto creature = std::dynamic_pointer_cast<Creature>(actor.game().getObjectById(actor.id()));
    if (!creature) {
        return false;
    }
    if (auto *door = dyn_cast<Door>(&target); door && door->isTrapped() && door->isTrapHostileTo(*creature)) {
        door->triggerTrap(creature, false);
        return true;
    }
    if (auto *placeable = dyn_cast<Placeable>(&target);
        placeable && placeable->isTrapped() && placeable->isTrapHostileTo(*creature)) {
        placeable->triggerTrap(creature, false);
        return true;
    }
    return false;
}

void tryUnlockDoorWithKey(Game &game, Door &door, Object &actor, Party &party) {
    if (!door.isLocked() || !door.isKeyRequired() || door.keyName().empty()) {
        return;
    }
    // The key may be the actor's, or else the player's.
    std::reference_wrapper<Object> keyOwner = actor;
    auto key = actor.getItemByTag(door.keyName());
    if (!key) {
        auto player = party.player();
        if (player->id() != actor.id()) {
            key = player->getItemByTag(door.keyName());
            keyOwner = *player;
        }
    }
    if (!key) {
        return;
    }
    door.setLocked(false);
    if (door.isAutoRemoveKey()) {
        bool last = false;
        keyOwner.get().removeItem(key, last);
        if (last) {
            game.destroyRuntimeObjectGraph(key);
        }
    }
}

bool workAtLock(Action &action, Object &target, Object &actor, bool &working, float dt) {
    auto *creature = dyn_cast<Creature>(&actor);
    if (!creature || working) {
        return true;
    }
    // The walk to a lock takes the use point anew once when it changes, and runs.
    if (!creature->navigateToUse(target, 0.0f, dt, true, true)) {
        return false;
    }
    creature->turnToward(target);
    creature->playAnimation(AnimationType::LoopingUnlockDoor);
    working = true;
    actor.addActionBefore(action, actor.game().newAction<WaitAction>(kLockWorkDuration));
    return false;
}

void resumePoseAfterLockWork(Object &actor, bool working) {
    auto *creature = dyn_cast<Creature>(&actor);
    if (creature && working) {
        creature->refreshPauseAnimation();
    }
}

void signalFailToOpen(Object &target, Object &actor, bool quiet) {
    actor.game().queueScriptEvent(
        target, actor, quiet ? Event(kFailToOpenEvent, {1}, {}, {}, {}) : Event(kFailToOpenEvent));
}

// A lock that wants its key refuses the actor: the object is told it failed to
// open, and the creature the player controls hears the lock wants its key.
static void refuseKeyedLock(Object &target, Object &actor) {
    signalFailToOpen(target, actor);
    auto leader = actor.game().party().getLeader();
    if (leader && leader.get() == &actor) {
        actor.game().addFeedbackMessage(kKeyRequiredStrRef);
    }
}

void unlockDoor(Door &door, Object &actor) {
    if (!door.isLocked()) {
        return;
    }
    door.setLastEnteredId(actor.id());
    tryUnlockDoorWithKey(actor.game(), door, actor, actor.game().party());
    if (door.isLocked() && door.isKeyRequired()) {
        refuseKeyedLock(door, actor);
        return;
    }

    door.setLocked(false);
    door.open(actor);
    door.onOpen(actor.id());
}

void unlockPlaceable(Placeable &placeable, Object &actor) {
    if (!placeable.isLocked()) {
        return;
    }
    placeable.setLastEnteredId(actor.id());
    if (placeable.isKeyRequired()) {
        refuseKeyedLock(placeable, actor);
        return;
    }

    placeable.setLocked(false);
}

// Locking an object that is not locked: with its key the actor locks it; a
// lock that wants its key refuses anything else; an object locks itself; a
// creature locks it when a d20, or 20 out of combat, and its Security rank
// reach the object's close lock DC, at least 1. A lock made signals the
// object that it was locked.
void lockObject(Object &target, Object &actor) {
    auto *door = dyn_cast<Door>(&target);
    auto *placeable = dyn_cast<Placeable>(&target);
    if (!door && !placeable) {
        return;
    }
    if (door ? door->isLocked() : placeable->isLocked()) {
        return;
    }
    const std::string &keyName = door ? door->keyName() : placeable->keyName();
    bool locked = !keyName.empty() && actor.getItemByTag(keyName);
    if (!locked && (door ? door->isKeyRequired() : placeable->isKeyRequired())) {
        refuseKeyedLock(target, actor);
        return;
    }
    if (!locked) {
        if (&target == &actor) {
            locked = true;
        } else if (auto *creature = dyn_cast<Creature>(&actor)) {
            const int roll = creature->isInCombat() ? randomInt(1, 20) : 20;
            const int rank = creature->getUnopposedSkillRank(SkillType::Security);
            const int dc = std::max(1, static_cast<int>(door ? door->closeLockDC() : placeable->closeLockDC()));
            locked = roll + rank >= dc;
        }
    }
    if (!locked) {
        return;
    }
    if (door) {
        door->setLocked(true);
    } else {
        placeable->setLocked(true);
    }
    actor.game().queueScriptEvent(target, actor, Event(kLockedEvent));
}

void jumpToPositionFacing(Creature &creature, const glm::vec3 &position, float facing, float trailFacing,
                          float radius, bool clearLine, Game &game) {
    auto area = game.module()->area();
    auto spot = area->computeSafeLocation(position, radius, creature, clearLine);
    if (!spot) {
        return;
    }

    // The position changes at once; the creature's model turns to the new facing.
    creature.setPosition(*spot);
    creature.turnTo(facing);

    Room *roomBefore = creature.room();
    area->determineObjectRoom(creature);
    Room *roomAfter = creature.room();

    if (auto leader = game.party().getLeader()) {
        if (leader->id() == creature.id()) {
            game.party().resetFollowPath(*area, *spot, trailFacing, true);
            area->onPartyLeaderMoved(roomBefore != roomAfter);
        }
    }
    // Areas of effect the creature carries jump with it before its own
    // entries and exits are noted.
    if (auto live = game.getObjectById<Creature>(creature.id())) {
        area->jumpCarriedAreaEffects(*live);
        area->updateSubAreaOccupancy(live);
    }
}

// After walking up to a locked door it steps right up to, a creature waits
// this long, facing it, before it opens it.
static constexpr float kPreciseOpenWait = 0.5f;
// Having opened or closed a door, the actor holds this long before its next
// action.
static constexpr float kDoorMovedWait = 0.5f;

// Anything but a creature opens the door outright, with no key or lock to get
// past: a door told to open itself, or a placeable or trigger running a
// script.
void OpenDoorAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    if (actor.type() == ObjectType::Creature) {
        if (_door->actionState() != DoorState::Closed) {
            // A creature has nothing to do at a door that is open or on its way.
            complete();
            return;
        }
        auto &creature = static_cast<Creature &>(actor);
        if (!creature.navigateToUse(*_door, 0.0f, dt, true, false, false)) {
            if (creature.navigationFailed()) {
                // No path reaches the door: give up.
                complete();
                return;
            }
            _waitAfterApproach = _door->isPreciseUse() && _door->isLocked();
            return;
        }
        creature.turnToward(*_door);
        if (_waitAfterApproach) {
            _waitAfterApproach = false;
            actor.addActionBefore(*this, _game.newAction<WaitAction>(kPreciseOpenWait));
            return;
        }
        if (springTrapOnUse(*_door, actor)) {
            complete();
            return;
        }
        tryUnlockDoorWithKey(_game, *_door, actor, _game.party());
        if (_door->isLocked()) {
            signalFailToOpen(*_door, actor);
            complete();
            return;
        }
    }

    _door->open(actor);
    _door->onOpen(actor.id());
    actor.addActionBefore(*this, _game.newAction<WaitAction>(kDoorMovedWait));
    complete();
}

// Only the door is kept, with the walk mode every open and close is queued
// with; one taken up again walks up afresh.
static constexpr int32_t kDoorWalkMode = 0;

std::optional<SavedActionRecord> OpenDoorAction::saveFacingState() const {
    return SavedActionRecord::command(SavedActionType::OpenDoor, {
                                                                     {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(_door->id())},
                                                                     {kSavedIntegerParameter, kDoorWalkMode},
                                                                 });
}

std::shared_ptr<Action> OpenDoorAction::restore(const SavedActionRecord &record, Game &game) {
    auto door = std::dynamic_pointer_cast<Door>(record.objectParameter(0).boundObject());
    if (!door) {
        return nullptr;
    }
    return game.newAction<OpenDoorAction>(std::move(door));
}

void CloseDoorAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto creatureActor = _game.getObjectById<Creature>(actor.id());

    bool reached = !creatureActor || creatureActor->navigateToUse(*_door, 0.0f, dt, true, false, false);
    if (!reached && creatureActor->navigationFailed()) {
        // No path reaches the door: give up.
        complete();
        return;
    }
    if (reached) {
        _door->close();
        _door->onClosed(actor.id());
        actor.addActionBefore(*this, _game.newAction<WaitAction>(kDoorMovedWait));
        complete();
    }
}

std::optional<SavedActionRecord> CloseDoorAction::saveFacingState() const {
    return SavedActionRecord::command(SavedActionType::CloseDoor, {
                                                                      {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(_door->id())},
                                                                      {kSavedIntegerParameter, kDoorWalkMode},
                                                                  });
}

std::shared_ptr<Action> CloseDoorAction::restore(const SavedActionRecord &record, Game &game) {
    auto door = std::dynamic_pointer_cast<Door>(record.objectParameter(0).boundObject());
    if (!door) {
        return nullptr;
    }
    return game.newAction<CloseDoorAction>(std::move(door));
}

// A lock is worked at from the object's use point.
void LockObjectAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    if (!_target) {
        complete();
        return;
    }
    if (!workAtLock(*this, *_target, actor, _working, dt)) {
        // No path reaches the lock: give up, still locked or unlocked.
        if (auto *creature = dyn_cast<Creature>(&actor); creature && !_working && creature->navigationFailed()) {
            complete();
        }
        return;
    }
    lockObject(*_target, actor);
    complete();
}

bool LockObjectAction::cancel(std::shared_ptr<Action> self, Object &actor) {
    resumePoseAfterLockWork(actor, _working);
    return true;
}

// Only the object is kept; a lock taken up again walks up and works afresh.
std::optional<SavedActionRecord> LockObjectAction::saveFacingState() const {
    return SavedActionRecord::command(SavedActionType::Lock, {
                                                                 {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(_target ? _target->id() : kSavedRuntimeInvalidObjectId)},
                                                             });
}

std::shared_ptr<Action> LockObjectAction::restore(const SavedActionRecord &record, Game &game) {
    return game.newAction<LockObjectAction>(record.objectParameter(0).boundObject());
}

// An unlock is worked at from the object's use point; a trap on the object
// then goes off instead.
void UnlockObjectAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    if (!_target || _target->isDead()) {
        complete();
        return;
    }
    if (!workAtLock(*this, *_target, actor, _working, dt)) {
        // No path reaches the lock: give up, still locked or unlocked.
        if (auto *creature = dyn_cast<Creature>(&actor); creature && !_working && creature->navigationFailed()) {
            complete();
        }
        return;
    }
    if (springTrapOnUse(*_target, actor)) {
        complete();
        return;
    }

    switch (_target->type()) {
    case ObjectType::Door:
        unlockDoor(static_cast<Door &>(*_target), actor);
        break;
    case ObjectType::Placeable:
        unlockPlaceable(static_cast<Placeable &>(*_target), actor);
        break;
    default:
        break;
    }
    complete();
}

bool UnlockObjectAction::cancel(std::shared_ptr<Action> self, Object &actor) {
    resumePoseAfterLockWork(actor, _working);
    return true;
}

// An unlock is kept as the object, no item used on the lock and no item
// property; taken up again it is worked as a Security unlock.
std::optional<SavedActionRecord> UnlockObjectAction::saveFacingState() const {
    return SavedActionRecord::command(SavedActionType::OpenLock, {
                                                                     {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(_target ? _target->id() : kSavedRuntimeInvalidObjectId)},
                                                                     {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(kSavedRuntimeInvalidObjectId)},
                                                                     {kSavedIntegerParameter, int32_t {0}},
                                                                 });
}

// A door saber's work is the burn-door clip of the wielder's sword class on a
// character model, one row per class from single sword on; anything else
// works for two seconds without a clip.
static constexpr int kBurnDoorAnimationRow = 387;
static constexpr float kDoorSaberWorkSeconds = 2.0f;
static constexpr int kNotLockedStrRef = 1430;
static constexpr int kUnlockVoiceChance = 20;

// Every pass powers the creature's lightsabers and holds it in combat state
// without a battle cry. It goes to within a metre of the door, turns to it,
// then works at it. When the work is done a hostile trap on the
// door goes off instead; a door no longer locked is left as it is, and the
// creature the player controls hears that it is not locked; otherwise the
// door is unlocked and opened, and one time in five the creature says the
// unlock went well. No key is wanted and no experience is given.
void DoorSaberAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    assert(actor.type() == ObjectType::Creature);
    auto &creature = static_cast<Creature &>(actor);
    if (creature.isDead() || creature.isTemporarilyDead()) {
        complete();
        return;
    }

    creature.setLightsabersPowered(true, true);
    creature.setCombatState(true, CombatActivation::Indirect, false);
    if (!creature.navigateToUse(*_door, 0.0f, dt, true, true)) {
        if (creature.navigationFailed()) {
            // No path reaches the door: give up.
            complete();
        }
        return;
    }
    if (!_approached) {
        _approached = true;
        creature.turnToward(*_door);
        return;
    }
    if (!_working) {
        _working = true;
        if (_game.party().getLeader().get() == &creature) {
            if (auto clip = _services.resource.audioClips.get("gui_doorsaber")) {
                _services.audio.mixer.play(std::move(clip), audio::AudioType::Sound);
            }
        }
        const auto wield = creature.getWieldType();
        const bool swordClass = wield == CreatureWieldType::SingleSword ||
                                wield == CreatureWieldType::DoubleBladedSword ||
                                wield == CreatureWieldType::DualSwords;
        if (creature.modelType() != Creature::ModelType::Creature && swordClass) {
            // An animation ID below 10000 names its animation row.
            const int animation = kBurnDoorAnimationRow + static_cast<int>(wield);
            actor.addActionBefore(*this, _game.newAction<PlayAnimationAction>(animation, 1.0f, kDoorSaberWorkSeconds));
        } else {
            actor.addActionBefore(*this, _game.newAction<WaitAction>(kDoorSaberWorkSeconds));
        }
        return;
    }

    if (springTrapOnUse(*_door, actor)) {
        complete();
        return;
    }
    if (!_door->isLocked()) {
        if (_game.party().getLeader().get() == &creature) {
            _game.addFeedbackMessage(kNotLockedStrRef);
        }
        complete();
        return;
    }
    _door->setLocked(false);
    _door->open(actor);
    _door->onOpen(actor.id());
    if (randomInt(0, 99) < kUnlockVoiceChance && creature.isHeardByLeader()) {
        creature.playSound(resource::SoundSetEntry::UnlockSuccess);
    }
    complete();
}

// A door saber taken away puts the creature back in its usual pose.
bool DoorSaberAction::cancel(std::shared_ptr<Action> self, Object &actor) {
    auto &creature = static_cast<Creature &>(actor);
    creature.resumeStateDrivenAnimation();
    return true;
}

// Only the door is kept; a door saber taken up again walks up, turns and
// works afresh.
std::optional<SavedActionRecord> DoorSaberAction::saveFacingState() const {
    return SavedActionRecord::command(SavedActionType::DoorSaber, {
                                                                      {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(_door->id())},
                                                                  });
}

std::shared_ptr<Action> DoorSaberAction::restore(const SavedActionRecord &record, Game &game) {
    auto door = std::dynamic_pointer_cast<Door>(record.objectParameter(0).boundObject());
    if (!door) {
        return nullptr;
    }
    return game.newAction<DoorSaberAction>(std::move(door));
}

// After walking up to an object, a creature waits this long, facing it,
// before it uses it.
static constexpr float kUseWaitAfterApproach = 0.5f;

// A use is made from the object's use point: a creature out of use range goes
// there, turns to the object and waits half a second first. A use does not
// lock the creature's facing. A placeable without an inventory is then used. A use of a container waits for it to swing open, then opens it
// for the controlled creature, which also counts as using it. An unusable or
// open container refuses the use, and a creature the player does not control
// only hears it open.
void OpenContainerAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    if (!_object || !isa<Creature>(actor)) {
        complete();
        return;
    }
    auto *placeable = dyn_cast<Placeable>(_object.get());
    if (placeable && placeable->hasInventory() && placeable->isLocked()) {
        complete();
        return;
    }

    auto &creatureActor = cast<Creature>(actor);
    if (!creatureActor.navigateToUse(*_object, 0.0f, dt)) {
        if (creatureActor.navigationFailed()) {
            // No path reaches the object: give up.
            complete();
            return;
        }
        _approached = true;
        return;
    }
    if (_approached) {
        _approached = false;
        creatureActor.turnToward(*_object);
        actor.addActionBefore(*this, _game.newAction<WaitAction>(kUseWaitAfterApproach));
        return;
    }
    if (!placeable) {
        if (!springTrapOnUse(*_object, actor)) {
            _game.openContainer(_object);
        }
        complete();
        return;
    }
    if (!placeable->isUsable() || springTrapOnUse(*_object, actor)) {
        complete();
        return;
    }
    if (!placeable->hasInventory()) {
        _game.queueScriptEvent(*placeable, actor, Event(kUsedEvent));
        complete();
        return;
    }
    if (&actor != _game.party().getLeader().get()) {
        placeable->playOpenedSound();
        _game.queueScriptEvent(*placeable, actor, Event(kUsedEvent));
        complete();
        return;
    }
    if (placeable->isOpen()) {
        complete();
        return;
    }
    if (!placeable->isInventoryOpenPending()) {
        actor.addActionBefore(*this, _game.newAction<WaitAction>(placeable->beginOpeningInventory()));
        return;
    }
    placeable->completeOpeningInventory(actor);
    _game.queueScriptEvent(*placeable, actor, Event(kUsedEvent));
    complete();
}

// Only the object is kept; a use taken up again walks up and opens afresh.
std::optional<SavedActionRecord> OpenContainerAction::saveFacingState() const {
    return SavedActionRecord::command(SavedActionType::OpenContainer, {
                                                                          {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(_object ? _object->id() : kSavedRuntimeInvalidObjectId)},
                                                                      });
}

std::shared_ptr<Action> OpenContainerAction::restore(const SavedActionRecord &record, Game &game) {
    return game.newAction<OpenContainerAction>(record.objectParameter(0).boundObject());
}

} // namespace game

} // namespace reone
