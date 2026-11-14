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

#include <boost/algorithm/string.hpp>

#include "reone/game/action.h"
#include "reone/game/action/closedoor.h"
#include "reone/game/action/doorsaber.h"
#include "reone/game/action/lockobject.h"
#include "reone/game/action/opencontainer.h"
#include "reone/game/action/opendoor.h"
#include "reone/game/action/playanimation.h"
#include "reone/game/action/unlockobject.h"
#include "reone/game/action/wait.h"
#include "reone/game/animationutil.h"
#include "reone/game/di/services.h"
#include "reone/game/event.h"
#include "reone/game/game.h"
#include "reone/game/object.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/item.h"
#include "reone/game/object/lock.h"
#include "reone/game/object/placeable.h"
#include "reone/game/party.h"
#include "reone/game/script/runner.h"
#include "reone/system/logutil.h"
#include "reone/system/randomutil.h"

#include "commonactions.h"

namespace reone {

namespace game {

static constexpr float kLockWorkSeconds = 1.5f;
static constexpr int kLockedEvent = static_cast<int>(SavedSignalEventType::Locked);
static constexpr int kUnlockedEvent = static_cast<int>(SavedSignalEventType::Unlocked);
static constexpr int kFailToOpenEvent = static_cast<int>(SavedSignalEventType::FailToOpen);
static constexpr int kUsedEvent = static_cast<int>(SavedSignalEventType::Used);
static constexpr int kKeyRequiredStrRef = 1431;
static constexpr int kKeyUsedStrRef = 1432;
// TSL says a lock line one time in five; KotOR always does.
static constexpr int kTSLLockVoiceChance = 20;
// From this DC a take of 20 is hopeless.
static constexpr int kHopelessLockDC = 60;
// TSL's unlock experience: the award per level of the controlled creature,
// and the higher one for a DC at least this far above that level.
static constexpr int kUnlockXPPerLevel = 5;
static constexpr int kHardUnlockXPPerLevel = 10;
static constexpr int kHardUnlockDCMargin = 20;

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

static bool isLeader(Game &game, const Object &object) {
    return game.party().getLeader().get() == &object;
}

bool useCarriedKey(Creature &actor, Object &target, Lock &lock, bool wantLocked) {
    const std::string &keyName = lock.keyName();
    if (keyName.empty()) {
        return false;
    }

    // Item tags are in lower case; the key name is matched as it is, except
    // that a TSL door finds its key among the carried items in lower case.
    Game &game = actor.game();
    auto holder = game.getObjectById(actor.itemRepositoryOwner().id());
    auto key = holder->getItemByTag(game.isTSL() && isa<Door>(target) ? boost::to_lower_copy(keyName) : keyName);
    bool equipped = false;
    if (!key) {
        key = actor.getEquippedItemByTag(keyName);
        equipped = static_cast<bool>(key);
    }
    if (!key) {
        return false;
    }

    // One key of a stack is used up; the last one is spent.
    std::shared_ptr<Item> spent;
    if (lock.isAutoRemoveKey()) {
        if (equipped) {
            if (key->stackSize() > 1) {
                key->setStackSize(key->stackSize() - 1);
            } else if (actor.takeEquippedItem(key)) {
                spent = key;
            }
        } else {
            // A carried key goes from whatever holds it, which may be an item
            // it is inside.
            bool last = false;
            const auto keyHolder = game.getObjectById(key->owner());
            if (keyHolder && keyHolder->removeItem(key, last) && last) {
                spent = key;
            }
        }
    }
    lock.setLocked(wantLocked);
    if (isLeader(game, actor)) {
        game.addFeedbackMessage(kKeyUsedStrRef);
    }
    // A spent key is destroyed after the events about it, so they still
    // name it.
    if (spent) {
        spent->announceLoss(true);
        game.postObjectDestruction(*spent, actor.id(), 0.0f);
    }
    return true;
}

bool workAtLock(Action &action, Object &target, Creature &creature, LockWorkState &work, bool unlock, float dt) {
    // The walk to a lock takes the use point anew once when it changes, and
    // runs. A creature out of use range walks back to it, even once working.
    if (!creature.navigateToUse(target, 0.0f, dt, true, true)) {
        work.approached = true;
        return false;
    }
    if (work.working) {
        // Having walked back, it turns to the object again.
        if (work.approached) {
            work.approached = false;
            creature.turnToward(target);
        }
        return true;
    }
    Game &game = creature.game();
    if (!work.arrived) {
        work.arrived = true;
        if (unlock || work.approached) {
            creature.turnToward(target);
        }
        if (unlock && (!game.isTSL() || randomInt(0, 99) < kTSLLockVoiceChance) && creature.isHeardByLeader()) {
            creature.playSound(resource::SoundSetEntry::BeginUnlock);
        }
    }
    work.working = true;
    work.approached = false;
    const auto clip = isa<Door>(target) ? WorkAnimation::UnlockDoor : WorkAnimation::UnlockContainer;
    creature.addActionBefore(action, game.newAction<PlayAnimationAction>(static_cast<int>(clip), 1.0f, kLockWorkSeconds));
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
    if (isLeader(actor.game(), actor)) {
        actor.game().addFeedbackMessage(kKeyRequiredStrRef);
    }
}

static void openDoor(Door &door, Object &actor) {
    door.open(actor);
    door.onOpen(actor.id());
}

static void lockAndSignal(Object &target, Lock &lock, Object &actor) {
    lock.setLocked(true);
    actor.game().queueScriptEvent(target, actor, Event(kLockedEvent));
}

// An object with no lock is never locked or unlocked. Locking one reports a
// failure against this DC; unlocking one reports the roll against this DC as
// any roll is reported, but in TSL against the spike's (see noLockReportDC).
static constexpr int kNoLockLockReportDC = 0;
static constexpr int kNoLockUnlockReportDC = 1;
// TSL: the report DC of an unlock of an object with no lock, with a spike
// that has no use property.
static constexpr int kNoLockSpikeReportDC = 6;

// Locking an object that is not locked: a creature with its key locks it
// quietly; a creature out of use range gives up; a lock that wants its key
// refuses anything else; an object locks itself; a creature locks it when a
// d20, or 20 out of combat, and its Security rank reach the object's close
// lock DC, at least 1, and reports the roll. A lock made signals the object
// that it was locked. A creature trying an object with no lock rolls and
// reports a failure.
void lockObject(Object &target, Object &actor) {
    auto objectLock = target.findLock();
    auto *creature = dyn_cast<Creature>(&actor);
    if (creature) {
        if (objectLock && (objectLock->get().isLocked() || useCarriedKey(*creature, target, *objectLock, true))) {
            return;
        }
        if (!creature->isInUseRange(target, 0.0f)) {
            return;
        }
    }
    if (objectLock) {
        Lock &lock = *objectLock;
        if (lock.isLocked()) {
            return;
        }
        if (lock.isKeyRequired()) {
            refuseKeyedLock(target, actor);
            return;
        }
        if (&target == &actor) {
            lockAndSignal(target, lock, actor);
            return;
        }
    }
    if (!creature) {
        return;
    }
    const bool take20 = !creature->isInCombat();
    const int roll = take20 ? 20 : randomInt(1, 20);
    const int rank = creature->getUnopposedSkillRank(SkillType::Security);
    const int dc = objectLock ? std::max(1, objectLock->get().closeDC()) : kNoLockLockReportDC;
    const bool locked = objectLock && roll + rank >= dc;
    if (locked) {
        lockAndSignal(target, *objectLock, actor);
    }
    reportSkillUse(actor.game(), *creature, nullptr, kSecurityStrRef, kSecurityStrRef, roll, rank,
                   dc, locked ? SkillResult::Success : take20 ? SkillResult::Take20Failure
                                                              : SkillResult::Failure);
}

bool jumpToPositionFacing(Creature &creature, const glm::vec3 &position, float facing, float trailFacing,
                          float radius, bool clearLine, Game &game) {
    auto area = game.module()->area();
    auto spot = area->computeSafeLocation(position, radius, creature, clearLine);
    if (!spot) {
        return false;
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
    return true;
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
        if (!creature.navigateToUse(*_door, 0.0f, dt, true, false, false, false)) {
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
        // A creature opens a locked door with a key it carries.
        if (_door->isLocked() && !useCarriedKey(creature, *_door, _door->lock(), false)) {
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

    bool reached = !creatureActor || creatureActor->navigateToUse(*_door, 0.0f, dt, true, false, false, false);
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

// A dead or downed creature can no longer work at a lock.
static bool isDown(const Object &actor) {
    auto *creature = dyn_cast<Creature>(&actor);
    return creature && (creature->isDead() || creature->isTemporarilyDead());
}

bool LockObjectAction::admits(const Object &actor) {
    const auto *creature = dyn_cast<const Creature>(&actor);
    return (!creature || creature->canUseSkill(SkillType::Security)) && actor.isCommandable();
}

// A lock is worked at from the object's use point.
void LockObjectAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto *creature = dyn_cast<Creature>(&actor);
    if (!_target || isDown(actor)) {
        complete();
        return;
    }
    if (creature && !workAtLock(*this, *_target, *creature, _work, false, dt)) {
        // No path reaches the lock: give up, still locked or unlocked.
        if (!_work.working && creature->navigationFailed()) {
            complete();
        }
        return;
    }
    lockObject(*_target, actor);
    complete();
}

bool LockObjectAction::cancel(std::shared_ptr<Action> self, Object &actor) {
    resumePoseAfterLockWork(actor, _work.working);
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

// A spike adds the cost value of its first used property, when that property
// is active. The property the unlock was queued with is not looked at. A spike
// already used up adds nothing.
int UnlockObjectAction::spikeBonus() const {
    if (!_item || !_item->isRuntimeLive()) {
        return 0;
    }
    const auto index = _item->firstUseProperty();
    if (!index) {
        return 0;
    }
    const auto &property = _item->properties()[*index];
    return _item->isPropertyActive(property) ? property.costValue : 0;
}

// In TSL, a spike used on an object with no lock sets the report DC: the
// upgrade type of its first used property, whether or not that property is in
// force (so 255 for one that needs no upgrade), or 6 when it has none.
int UnlockObjectAction::noLockReportDC() const {
    if (!_game.isTSL() || !_item || !_item->isRuntimeLive()) {
        return kNoLockUnlockReportDC;
    }
    const auto index = _item->firstUseProperty();
    return index ? int {_item->properties()[*index].upgradeType} : kNoLockSpikeReportDC;
}

bool UnlockObjectAction::admits(const Object &actor) {
    const auto *creature = dyn_cast<const Creature>(&actor);
    return (!creature || creature->admitSkillUse(SkillType::Security)) && actor.isCommandable();
}

// Any object that is still there is worked at, dead or destroyed, with a
// lock or without.
void UnlockObjectAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto *creature = dyn_cast<Creature>(&actor);
    if (!_target || isDown(actor)) {
        complete();
        return;
    }
    if (creature && !workAtLock(*this, *_target, *creature, _work, true, dt)) {
        // No path reaches the lock: give up, still locked or unlocked.
        if (!_work.working && creature->navigationFailed()) {
            complete();
            return;
        }
        // Once a creature has worked on the unlock it saves as the object and
        // the item.
        _command = Command::Worked;
        return;
    }
    resolve(actor);
    complete();
}

bool UnlockObjectAction::cancel(std::shared_ptr<Action> self, Object &actor) {
    resumePoseAfterLockWork(actor, _work.working);
    return true;
}

// A hostile trap on the object goes off instead. A creature opens an object
// that is unlocked, or that a key it carries unlocks, with no check, and
// gives up when no longer in use range. A lock that wants its key refuses; an
// object unlocks itself; anything but a creature fails. A creature checks a
// d20, or 20 out of combat, plus its Security and spike against the
// open-lock DC, at least 1; an object with no lock always fails. Unlocking
// opens a door, and a placeable for the player's creature; TSL also tells
// the object who unlocked it, and gives the party experience. Failing in TSL
// quietly tells the object it failed to open.
void UnlockObjectAction::resolve(Object &actor) {
    auto objectLock = _target->findLock();
    auto *creature = dyn_cast<Creature>(&actor);
    auto *door = dyn_cast<Door>(_target.get());
    const bool tsl = _game.isTSL();
    if (creature && springTrapOnUse(*_target, actor)) {
        return;
    }
    if (creature) {
        if (objectLock && (!objectLock->get().isLocked() || useCarriedKey(*creature, *_target, *objectLock, false))) {
            if (door) {
                openDoor(*door, actor);
            } else {
                actor.addActionBefore(*this, _game.newAction<OpenContainerAction>(_target));
            }
            return;
        }
        if (!creature->isInUseRange(*_target, 0.0f)) {
            return;
        }
    }
    if (objectLock) {
        Lock &lock = *objectLock;
        if (!lock.isLocked()) {
            return;
        }
        if (lock.isKeyRequired()) {
            refuseKeyedLock(*_target, actor);
            if (creature) {
                reportSkillUse(_game, *creature, _target, kSecurityStrRef, kSecurityStrRef, 0, 0, 0, SkillResult::Hopeless);
                if (creature->isHeardByLeader()) {
                    creature->playSound(resource::SoundSetEntry::UnlockFailed);
                }
            }
            return;
        }
        if (_target.get() == &actor) {
            lock.setLocked(false);
            if (door) {
                openDoor(*door, actor);
            }
            if (tsl) {
                _game.queueScriptEvent(*_target, actor, Event(kUnlockedEvent));
            }
            return;
        }
    }
    if (!creature) {
        if (tsl) {
            signalFailToOpen(*_target, actor, true);
        }
        return;
    }

    const bool take20 = !creature->isInCombat();
    const int roll = take20 ? 20 : randomInt(1, 20);
    const int bonus = creature->getUnopposedSkillRank(SkillType::Security) + spikeBonus();
    const int dc = objectLock ? std::max(1, objectLock->get().unlockDC(_game)) : noLockReportDC();
    const bool success = objectLock && roll + bonus >= dc;
    SkillResult result;
    if (success) {
        objectLock->get().setLocked(false);
        if (door) {
            openDoor(*door, actor);
        } else if (creature->isPC() || isLeader(_game, *creature)) {
            actor.addActionBefore(*this, _game.newAction<OpenContainerAction>(_target));
        }
        if (tsl) {
            _game.queueScriptEvent(*_target, actor, Event(kUnlockedEvent));
        }
        if ((!tsl || randomInt(0, 99) < kTSLLockVoiceChance) && creature->isHeardByLeader()) {
            creature->playSound(resource::SoundSetEntry::UnlockSuccess);
        }
        if (tsl && _game.party().isMember(*creature)) {
            // The level is the controlled creature's; the award is shared as
            // every party award is.
            const int level = _game.party().getLeader()->attributes().getAggregateLevel();
            _game.party().awardXP(level * (dc >= level + kHardUnlockDCMargin ? kHardUnlockXPPerLevel : kUnlockXPPerLevel),
                                  XPSource::Skill);
        }
        // TSL reports an unlock past a hopeless DC as no success.
        result = tsl && dc >= kHopelessLockDC ? SkillResult::Failure : SkillResult::Success;
    } else {
        if (creature->isHeardByLeader()) {
            creature->playSound(resource::SoundSetEntry::UnlockFailed);
        }
        if (tsl) {
            signalFailToOpen(*_target, actor, true);
        }
        // The report judges the roll against the DC, so a failed unlock of an
        // object with no lock reads as a success when the roll reaches it.
        result = roll + bonus >= dc      ? SkillResult::Success
                 : !take20               ? SkillResult::Failure
                 : dc >= kHopelessLockDC ? SkillResult::Hopeless
                                         : SkillResult::Take20Failure;
    }
    reportSkillUse(_game, *creature, _target, kSecurityStrRef, kSecurityStrRef, roll, bonus, dc, result);
    useUpCarriedItem(_game, *creature, _item);
}

// An unlock a creature has worked on saves as the object and the item used on
// the lock; until then it keeps the shape it was queued with.
std::optional<SavedActionRecord> UnlockObjectAction::saveFacingState() const {
    const SavedObjectReference target = SavedObjectReference::fromRuntimeId(_target ? _target->id() : kSavedRuntimeInvalidObjectId);
    const SavedObjectReference item = SavedObjectReference::fromRuntimeId(_item ? _item->id() : kSavedRuntimeInvalidObjectId);
    if (_command == Command::Worked) {
        return SavedActionRecord::command(SavedActionType::OpenLock, {{kSavedObjectParameter, target}, {kSavedObjectParameter, item}});
    }
    if (_command == Command::ObjectAction) {
        return SavedActionRecord::command(SavedActionType::OpenLock, {{kSavedObjectParameter, target}});
    }
    return SavedActionRecord::command(SavedActionType::OpenLock, {
                                                                     {kSavedObjectParameter, target},
                                                                     {kSavedObjectParameter, item},
                                                                     {kSavedIntegerParameter, int32_t {_propertyIndex}},
                                                                 });
}

// A script's door or placeable action saves the object alone, an unlock a
// creature had begun the object and the item used on the lock, and Security
// those and the item property it was used through.
std::shared_ptr<Action> UnlockObjectAction::restore(const SavedActionRecord &record, Game &game) {
    auto target = record.objectParameter(0).boundObject();
    if (record.hasParameterTypes({SavedActionParameterType::Object})) {
        return game.newAction<UnlockObjectAction>(std::move(target), nullptr, 0, Command::ObjectAction);
    }
    auto item = std::dynamic_pointer_cast<Item>(record.objectParameter(1).boundObject());
    if (record.hasParameterTypes({SavedActionParameterType::Object, SavedActionParameterType::Object})) {
        return game.newAction<UnlockObjectAction>(std::move(target), std::move(item), 0, Command::Worked);
    }
    return game.newAction<UnlockObjectAction>(std::move(target), std::move(item), record.integerParameter(2));
}

// A door saber's work is the burn-door work clip for two seconds: the clip of
// the wielder's sword class on a character model.
static constexpr float kDoorSaberWorkSeconds = 2.0f;
static constexpr int kNotLockedStrRef = 1430;

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
        actor.addActionBefore(*this, _game.newAction<PlayAnimationAction>(static_cast<int>(WorkAnimation::DoorSaber), 1.0f,
                                                                          kDoorSaberWorkSeconds));
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
    if (randomInt(0, 99) < kTSLLockVoiceChance && creature.isHeardByLeader()) {
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
// lock the creature's facing. A placeable without an inventory is then used.
// A locked container opens only to a key the creature carries; otherwise the
// player's creature is told it is locked, and the container counts as used. A
// use of a container waits for it to swing open, then opens it for the
// controlled creature, which also counts as using it. An unusable or open
// container refuses the use, and a creature the player does not control only
// hears it open.
void OpenContainerAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    if (!_object || !isa<Creature>(actor)) {
        complete();
        return;
    }
    auto *placeable = dyn_cast<Placeable>(_object.get());

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
    if (placeable->isLocked() && !useCarriedKey(creatureActor, *placeable, placeable->lock(), false)) {
        if (isLeader(_game, actor)) {
            placeable->presentLocked(placeable->lock(), 0.0f);
        }
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
