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

#include "reone/game/combat.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>

#include "reone/game/action/attackobject.h"
#include "reone/game/action/castspellatobject.h"
#include "reone/game/action/movetolocation.h"
#include "reone/game/action/movetoobject.h"
#include "reone/game/action/unequipitem.h"
#include "reone/game/action/usefeat.h"
#include "reone/game/action/wait.h"
#include "reone/game/d20/spell.h"
#include "reone/game/di/services.h"
#include "reone/game/effect/creaturestate.h"
#include "reone/game/equipmentoperation.h"
#include "reone/game/equipmentrules.h"
#include "reone/game/game.h"
#include "reone/game/object/door.h"
#include "reone/game/object/item.h"
#include "reone/game/object/placeable.h"
#include "reone/game/party.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/system/logutil.h"
#include "reone/system/randomutil.h"
#include "reone/system/smallset.h"

using namespace reone::graphics;
using namespace reone::scene;

namespace reone {

namespace game {

// Reputations: hostile up to this one, friendly from the other.
static constexpr int kHighestHostileReputation = 10;
static constexpr int kLowestFriendReputation = 90;

// The animations and times round entries start with.
static constexpr int kCastEntryAnimation = 10017;
static constexpr int kCastEntryMilliseconds = 500;
static constexpr int kSwitchWeaponsEntryMilliseconds = 1000;
// Taking up equipment rescales a round to leave this much of it.
static constexpr int kEquipmentRoundMilliseconds = 1500;

static bool isUnavailableAttackTarget(const Creature &creature) {
    return creature.isDead() || creature.isTemporarilyDead();
}

// A pause on the creature's round with no one behind it, or held by another,
// holds its entry. A knocked-down creature's entry waits until it is up again.
bool CombatRound::suspends(const Action &action) const {
    for (const auto &entry : actions) {
        if (entry.action.get() != &action && &entry.action->combatAction() != &action) {
            continue;
        }
        auto attacker = entry.attacker.resolve();
        if (attacker && attacker->isKnockedDown()) {
            return true;
        }
        if (!entry.paused()) {
            return false;
        }
        auto owner = entry.pauseOwner.resolve();
        return !owner || attacker != owner;
    }
    return false;
}

// The pauses the two creatures of a pair put on each other keep them apart;
// either takes its turn as soon as its own round runs and nothing holds it.
bool CombatRound::canExecute(Action &action) const {
    if (suspends(action)) {
        return false;
    }
    assert(actions.size() <= 2 && "no more than 2 actors in a round");
    for (const auto &entry : actions) {
        if (entry.action.get() == &action) {
            return !entry.retired && entry.participantBindingsLive() &&
                   entry.remainsInActorQueue() &&
                   action.runtimeDependenciesLive() &&
                   !action.isCompleted() &&
                   !action.isCancelled() &&
                   entry.state == CombatRound::Running;
        }
    }
    return false;
}

// A command that joined a running round answers only for its own objects, not
// for the target the round keeps.
bool CombatRound::RoundAction::participantBindingsLive() const {
    return attacker.resolve() &&
           (joined || target.empty() || target.resolve());
}

bool CombatRound::RoundAction::remainsInActorQueue() const {
    if (!actorQueueAssociated || action->isCompleted()) {
        return true;
    }
    auto actor = attacker.resolve();
    return actor && std::find(
                        actor->actions().begin(),
                        actor->actions().end(),
                        action) != actor->actions().end();
}

// The target of \p actor's round command. A cast or item use takes its round
// on whatever it is aimed at, hostile or not, unless that is \p actor itself.
static std::shared_ptr<Object> getTarget(Action &queued, const Object &actor) {
    Action &action = queued.combatAction();
    if (auto *cast = dyn_cast<CastSpellAction>(&action)) {
        return cast->target().get() != &actor ? cast->target() : nullptr;
    }
    if (auto *attack = dyn_cast<AttackObjectAction>(&action)) {
        return attack->target();
    }
    if (auto *feat = dyn_cast<UseFeatAction>(&action)) {
        return feat->target();
    }

    return nullptr;
}

// Starting a round on an attack records its target as the attempted attack
// target. A cast's attempted spell target is set as it is queued and handed on.
static void recordCombatAction(
    Object &actor,
    const std::shared_ptr<Object> &target,
    Action &action) {

    if (!target || isa<CastSpellAction>(&action.combatAction()) || !isHostileAction(action.combatAction())) {
        return;
    }

    if (auto *creature = dyn_cast<Creature>(&actor)) {
        creature->setAttemptedAttackTarget(target->id());
    }
}

// A cast or item use, whatever its spell, is attempted on its target object,
// or, at a location, on its caster's area.
static void recordAttemptedSpellTarget(Game &game, Creature &caster, const CastSpellAction &cast) {
    const auto &target = cast.target();
    caster.setAttemptedSpellTarget(target ? target->id() : game.module()->area()->id());
}

// An instant cast, or the attack an instant cast makes.
static bool isInstantCast(Action &command) {
    if (auto *cast = dyn_cast<CastSpellAction>(&command)) {
        return cast->instantSpell();
    }
    if (auto *feat = dyn_cast<UseFeatAction>(&command)) {
        return feat->isInstantCastAttack();
    }
    return false;
}

// A cast or item use, other than an instant cast, makes a spell round; a round
// restored without its command keeps what it was.
static bool isSpellRound(const CombatRound::RoundAction &entry) {
    Action &command = entry.action->combatAction();
    return entry.spellRound || (isa<CastSpellAction>(&command) && !isInstantCast(command));
}

static bool isItemUse(Action &command) {
    auto *cast = dyn_cast<CastSpellAction>(&command);
    return cast && cast->item().has_value();
}

static bool isHostileForContinuation(const Creature &source, const Creature &target) {
    return source.getReputationToward(target) <= kHighestHostileReputation;
}

std::shared_ptr<Creature> Combat::findNearestEnemy(
    Area &area,
    const Creature &attacker,
    const Creature &observer,
    float maxDistance,
    std::optional<std::reference_wrapper<const Object>> excluded) {

    std::optional<uint32_t> nearest;
    float nearestDistance = maxDistance;
    // The search walks the area's objects outward from the observer in X
    // order, alternately below and above it. Line of sight runs at eye height
    // from the observer to those below it and from the attacker to those above
    // it; doors and placeables stop it, a see-through door does not, creatures
    // never do. Each way stops once the X gap from the observer exceeds the nearest
    // distance found so far.
    area.forEachObjectOutwardInX(observer, [&](const Object &object, bool above) {
        if (object.id() == attacker.id() || (excluded && &object == &excluded->get())) {
            return true;
        }
        const auto *candidate = dyn_cast<const Creature>(&object);
        if (candidate && !isUnavailableAttackTarget(*candidate)) {
            const bool isEnemy = observer.id() == attacker.id()
                                     ? isHostileForContinuation(*candidate, attacker)
                                     : isHostileForContinuation(attacker, *candidate);
            const float distance = attacker.getDistanceTo(*candidate) -
                                   observer.creaturePersonalSpace() -
                                   candidate->creaturePersonalSpace();
            if (isEnemy && observer.perception().sees(candidate->id()) && distance < nearestDistance) {
                const Creature &from = above ? attacker : observer;
                const glm::vec3 lift {0.0f, 0.0f, Object::kEyeHeight};
                if (area.isEyeLineClear(from.position() + lift, candidate->position() + lift, from, *candidate,
                                        /*pastSeeThroughDoor=*/true)) {
                    nearest = candidate->id();
                    nearestDistance = distance;
                }
            }
        }
        return std::abs(object.position().x - observer.position().x) <= nearestDistance;
    });

    return nearest ? attacker.game().getObjectById<Creature>(*nearest) : nullptr;
}

std::shared_ptr<CombatRound> Combat::findRoundForAction(
    const std::shared_ptr<Action> &action, const Creature &attacker) const {

    for (const auto &round : _rounds) {
        for (const CombatRound::RoundAction &roundAction : round->actions) {
            auto boundAttacker = roundAction.attacker.resolve();
            if (!roundAction.retired && boundAttacker.get() == &attacker &&
                roundAction.action == action) {
                return round;
            }
        }
    }
    return nullptr;
}

// If there is an incomplete combat round where attacker and target roles
// are reversed, append to that round.
std::shared_ptr<CombatRound> Combat::tryAppendAction(
    const std::shared_ptr<Action> &action,
    const std::shared_ptr<Creature> &attacker,
    const std::shared_ptr<Object> &target,
    bool actorQueueAssociated) {

    for (auto &round : _rounds) {
        if (round->actions.size() > 1) {
            continue;
        }
        // A round whose creature's round has ended takes no one new.
        const auto reversed = std::find_if(round->actions.begin(), round->actions.end(), [&](const auto &roundAction) {
            return roundAction.running() && roundAction.attacker.resolve().get() == target.get() &&
                   roundAction.target.resolve().get() == attacker.get();
        });
        if (reversed == round->actions.end()) {
            continue;
        }

        // Found a round to append. The new side starts in step with the
        // creature it answers, and its round goes on from there on its own.
        const auto state = reversed->state;
        const float time = reversed->time;
        const float duration = reversed->duration;
        round->actions.emplace_back(
            action, attacker, target, actorQueueAssociated);
        auto &appended = round->actions.back();
        appended.slot = 1;
        appended.state = state;
        appended.time = time;
        appended.duration = duration;
        round->duel = true;
        return round;
    }
    return nullptr;
}

const CombatRound &Combat::addAction(const std::shared_ptr<Action> &action, Object &actor) {
    auto attacker = _game.getObjectById<Creature>(actor.id());
    assert(attacker && attacker.get() == &actor && "Combat action actor is not the live registered Creature");
    attacker->setCurrentCombatAction(action);
    bool actorQueueAssociated = std::find(
                                    attacker->actions().begin(),
                                    attacker->actions().end(),
                                    action) != attacker->actions().end();

    // If attacker has already started a combat round, return it.
    if (auto round = findRoundForAction(action, *attacker)) {
        return *round;
    }

    auto &command = action->combatAction();
    std::shared_ptr<Object> target = getTarget(*action, *attacker);
    // Physical activation belongs to accepted submission and attacked events,
    // not to creating (or appending) a presentation round. Of the other
    // commands only a hostile item use puts its user in combat; a cast's
    // target enters combat when a harmful spell reaches it. Combat state is
    // set before the round starts, so a creature that cannot enter combat
    // leaves it without ending the round it is about to start.
    const auto *feat = dyn_cast<UseFeatAction>(&command);
    const bool physical = isa<AttackObjectAction>(&command) ||
                          (feat && isPhysicalAttackFeat(feat->feat()));
    auto activateCombat = [&]() {
        if (!physical && isHostileAction(command)) {
            attacker->setCombatState(true);
        }
    };

    // A creature fights one round at a time. A round that an earlier command
    // began, and that command has finished with, is still the creature's.
    std::shared_ptr<CombatRound> joined;
    for (auto &round : _rounds) {
        if (joined) {
            break;
        }
        for (auto &entry : round->actions) {
            if (!entry.running() || entry.attacker.resolve() != attacker || entry.action == action ||
                !entry.action->isCompleted())
                continue;
            // A cast or item use takes its turn inside that round, which
            // keeps its target, clock and engagement. So does an attack by a
            // creature engaged by a target that masters their exchange, which
            // starts no round of its own, unless it is an instant cast's
            // attack.
            if (command.joinsRunningRound() ||
                (physical && !isInstantCast(command) && target && answersMaster(*attacker, *target))) {
                joined = round;
                break;
            }
            // Any other command starts the round afresh; the round it gives up
            // has no end, and a partner it holds is left as it is.
            entry.retired = true;
            entry.action->retireCombatRound();
            entry.target.reset();
        }
    }
    // Combat state is set before the command takes its turn in the round, as
    // it is before a round starts: a creature that cannot enter combat ends
    // that round, and the command starts one afresh.
    if (joined) {
        activateCombat();
        for (auto &entry : joined->actions) {
            if (!entry.running() || entry.attacker.resolve() != attacker || !entry.action->isCompleted()) {
                continue;
            }
            entry.action = action;
            entry.actorQueueAssociated = actorQueueAssociated;
            entry.joined = true;
            recordCombatAction(actor, target, *action);
            return *joined;
        }
    }

    // Starting a round, or entering the one its target began, ends the pause
    // held while owning none, and the round has not yet told the attacker its
    // weapon is ineffective.
    _ownerPauses.erase(attacker->id());
    attacker->setWeaponIneffectiveReported(false);

    // Starting a round records its target, or none when it targets the actor itself.
    if (target && target.get() != attacker.get()) {
        attacker->setRoundTarget(target);
    } else {
        attacker->clearRoundTarget();
    }
    // The round's owner locks its orientation on the round's target.
    attacker->setOrientationLock(target ? target->id() : script::kObjectInvalid);
    // Find an existing round where target and attacker roles are reversed, and
    // append the action to this round. An instant cast starts a round of its
    // own that engages no one.
    if (target && !isInstantCast(command)) {
        if (auto round = tryAppendAction(
                action, attacker, target, actorQueueAssociated)) {
            recordCombatAction(actor, target, *action);
            configureSpellPair(*round, action, *attacker);
            debug(str(boost::format("Append attack: %s -> %s") % actor.tag() % target->tag()), LogChannel::Combat);
            return *round;
        }
    }

    activateCombat();

    // Otherwise, start a new combat round
    _rounds.emplace_back(std::make_shared<CombatRound>(
        action, attacker, target, actorQueueAssociated));
    CombatRound &newRound = *_rounds.back();
    newRound.id = _nextRoundId++;

    if (target) {
        recordCombatAction(actor, target, *action);
        debug(str(boost::format("Start round: %s -> %s") % actor.tag() % target->tag()), LogChannel::Combat);
    } else {
        debug(str(boost::format("Start round: %s") % actor.tag()), LogChannel::Combat);
    }

    configureSpellPair(newRound, action, *attacker);
    return newRound;
}

// The dispatcher holds the head of the queue until the owner's round ends.
// A round whose action still waits behind it in the queue does not hold it,
// or neither could advance.
void CombatDispatchAction::execute(std::shared_ptr<Action>, Object &actor, float) {
    // While the world is held, the round waits, and the queue behind it.
    if (_game.holdsWorld()) {
        return;
    }
    auto *creature = dyn_cast<Creature>(&actor);
    if (creature) {
        _game.combat().releaseHandedOn(*creature);
    }
    if (!_game.combat().hasScheduled(actor) && !(creature && _game.combat().ownsDetachedRound(*creature))) {
        complete();
    }
}

// Clearing the dispatcher leaves the round's pending entries as they are.
bool CombatDispatchAction::cancel(std::shared_ptr<Action>, Object &) {
    complete();
    return true;
}

std::optional<SavedActionRecord> CombatDispatchAction::saveFacingState() const {
    SavedActionRecord result;
    result.actionId = static_cast<uint32_t>(SavedActionType::CombatDispatch);
    result.declaredParameterCount = 1;
    result.parameters = {{kSavedIntegerParameter, int32_t {_mode}}};
    return result;
}

std::shared_ptr<Action> CombatDispatchAction::restore(const SavedActionRecord &record, Game &game) {
    return game.newAction<CombatDispatchAction>(record.integerParameter(0));
}

// A party member at zero vitality cannot hold a stance; others only need to live.
static bool canHoldStance(Game &game, const Creature &actor) {
    if (actor.isDead()) {
        return false;
    }
    return !game.party().isMember(actor) || actor.currentHitPoints() > 0;
}

void CombatStanceAction::execute(std::shared_ptr<Action>, Object &actor, float) {
    auto *creature = dyn_cast<Creature>(&actor);
    if (!creature || !canHoldStance(_game, *creature)) {
        complete();
        return;
    }
    if (creature->combatStance() != CombatStance::Meditative) {
        creature->setCombatStance(CombatStance::Meditative);
        return;
    }
    // The stance lasts until another command waits behind it.
    if (creature->actions().nodes.size() >= 2) {
        creature->setCombatStance(CombatStance::None);
        complete();
    }
}

bool CombatStanceAction::cancel(std::shared_ptr<Action>, Object &actor) {
    // Clearing a stance command always drops the stance.
    if (auto *creature = dyn_cast<Creature>(&actor)) {
        creature->setCombatStance(CombatStance::None);
    }
    return true;
}

std::optional<SavedActionRecord> CombatStanceAction::saveFacingState() const {
    SavedActionRecord result;
    result.actionId = serializedActionId();
    result.declaredParameterCount = 0;
    return result;
}

std::shared_ptr<Action> CombatStanceAction::restore(const SavedActionRecord &, Game &game) {
    return game.newAction<CombatStanceAction>();
}

bool Combat::ownsRound(const Creature &actor) const {
    return std::any_of(_rounds.begin(), _rounds.end(), [&actor](const auto &round) {
        return std::any_of(round->actions.begin(), round->actions.end(), [&actor](const auto &entry) {
            return entry.running() && entry.attacker.resolve().get() == &actor;
        });
    });
}

bool Combat::ownsDetachedRound(const Creature &actor) const {
    const auto &nodes = actor.actions().nodes;
    bool owned = false;
    for (const auto &round : _rounds) {
        for (const auto &entry : round->actions) {
            if (!entry.running() || entry.attacker.resolve().get() != &actor) {
                continue;
            }
            owned = true;
            if (std::any_of(nodes.begin(), nodes.end(), [&](const auto &node) { return node->action == entry.action; })) {
                return false;
            }
        }
    }
    return owned;
}

bool Combat::hasScheduled(const Object &actor) const {
    auto found = _scheduled.find(actor.id());
    return found != _scheduled.end() && found->second->actor.resolve().get() == &actor &&
           !found->second->actions.empty();
}

void Combat::pendingScheduled(const Object &actor, ISmallVector<PendingEntry> &entries) const {
    entries.clear();
    auto found = _scheduled.find(actor.id());
    if (found == _scheduled.end() || found->second->actor.resolve().get() != &actor) {
        return;
    }
    for (const auto &entry : found->second->actions) {
        if (!entry || entry->saved.applied) {
            continue;
        }
        PendingEntry pending;
        pending.type = entry->saved.type;
        if (entry->node && entry->node->action) {
            pending.command = *entry->node->action;
        }
        entries.push_back(pending);
    }
}

std::optional<Combat::PendingEntry> Combat::currentRoundAction(const Creature &actor) const {
    const auto found = _handedOn.find(actor.id());
    if (found == _handedOn.end() || found->second.actor.resolve().get() != &actor) {
        return std::nullopt;
    }
    PendingEntry current;
    current.type = found->second.type;
    if (found->second.command) {
        current.command = *found->second.command;
    }
    return current;
}

void Combat::handOn(Creature &actor, const ScheduledEntry &entry) {
    auto &handedOn = _handedOn[actor.id()];
    handedOn.actor = _game.getObjectById<Creature>(actor.id());
    handedOn.type = entry.saved.type;
    handedOn.command = entry.node ? entry.node->action : nullptr;
}

bool Combat::isRoundPaused(const Creature &actor) const {
    if (isOwnerPaused(actor)) {
        return true;
    }
    const auto found = _scheduled.find(actor.id());
    if (found != _scheduled.end() && found->second->actor.resolve().get() == &actor && !found->second->actions.empty() &&
        found->second->actions.front()->saved.applied)
        return true;
    return std::any_of(_rounds.begin(), _rounds.end(), [&actor](const auto &round) {
        return std::any_of(round->actions.begin(), round->actions.end(), [&actor](const auto &entry) {
            return entry.running() && entry.attacker.resolve().get() == &actor && entry.paused();
        });
    });
}

void Combat::releaseHandedOn(const Creature &actor) {
    if (!isRoundPaused(actor)) {
        _handedOn.erase(actor.id());
    }
}

void Combat::ensureDispatcher(Creature &actor, bool toFront) {
    for (const auto &node : actor.actions().nodes)
        if (node->type() == SavedActionType::CombatDispatch && node->action && !node->action->isCompleted() && !node->action->isCancelled()) {
            return;
        }
    if (!toFront) {
        actor.addAction(_game.newAction<CombatDispatchAction>());
        return;
    }
    actor.addActionOnTop(_game.newAction<CombatDispatchAction>());
}

// A cutscene attacker that masters an engaged exchange starts its partner's
// round as a cutscene round too: neither of them runs the end-of-round script
// or fights on from it. The pair's first attacker masters it.
static bool isCutsceneRoundFor(const CombatRound &round, const CombatRound::RoundAction &entry) {
    if (entry.action->combatAction().isCutsceneAttack()) {
        return true;
    }
    if (!round.duel || round.actions.empty()) {
        return false;
    }
    const auto &first = round.actions.front();
    return first.attacker.resolve().get() != entry.attacker.resolve().get() &&
           first.action->combatAction().isCutsceneAttack();
}

// The end of a round pause ends the creature's round action under way, and
// returns a living, standing, able creature to its pause or ready pose,
// except in a cutscene round.
static void readyAfterRoundPause(Creature &creature, bool cutsceneRound) {
    creature.setRoundActionKind(0);
    if (creature.isDead() || creature.isKnockedDown() || creature.movementType() != Creature::MovementType::None) {
        return;
    }
    // Unable to act for its round, meditation included.
    if (creature.isDebilitated(true) || cutsceneRound) {
        return;
    }
    creature.showPauseReadyAnimation(true);
}

// A scheduled entry's pause ends like any round pause: the creature, and the
// partner of a pair it masters, come back to their pose.
void Combat::readyAfterScheduledPause(Creature &actor) {
    std::vector<std::pair<std::shared_ptr<Creature>, bool>> posing;
    bool own = false;
    for (const auto &round : _rounds) {
        if (round->actions.empty()) {
            continue;
        }
        const auto &first = round->actions.front();
        const bool master = round->duel && first.running() && first.attacker.resolve().get() == &actor;
        for (const auto &entry : round->actions) {
            auto creature = entry.running() ? entry.attacker.resolve() : nullptr;
            if (!creature || (creature.get() != &actor && !master)) {
                continue;
            }
            own = own || creature.get() == &actor;
            posing.emplace_back(std::move(creature), isCutsceneRoundFor(*round, entry));
        }
    }
    for (const auto &[creature, cutscene] : posing) {
        readyAfterRoundPause(*creature, cutscene);
    }
    if (!own) {
        readyAfterRoundPause(actor, false);
    }
}

void Combat::reactToDamage(Creature &victim, const std::shared_ptr<Object> &damager, const DamageReaction &reaction) {
    if (victim.isDebilitated(true)) {
        return;
    }
    // Any other damage type makes a living creature flinch, whatever it is doing.
    if (!reaction.jolt) {
        if (victim.currentHitPoints() > 0) {
            victim.playDamageFlinch();
        }
        return;
    }
    // Only a creature standing in its plain pause or ready loop is jolted.
    if (!victim.showsPauseReadyAnimation()) {
        return;
    }
    const int milliseconds = reaction.milliseconds;
    auto pausedBy = damager ? _game.getObjectById(damager->id()) : nullptr;
    const float seconds = std::max(0, milliseconds) / 1000.0f;
    // Only the victim's own round is held; a round already held keeps its pause.
    bool owned = false;
    for (const auto &round : _rounds) {
        for (auto &entry : round->actions) {
            if (!entry.running() || entry.attacker.resolve().get() != &victim) {
                continue;
            }
            owned = true;
            if (!entry.paused()) {
                rescaleRound(entry, std::max(0, milliseconds), false);
                entry.pauseRemaining = seconds;
                entry.pauseOwner = pausedBy;
            }
        }
    }
    if (!owned) {
        auto &pause = _ownerPauses[victim.id()];
        if (!pause.paused()) {
            pause.actor = _game.getObjectById<Creature>(victim.id());
            pause.pausedBy = pausedBy;
            pause.remaining = seconds;
        }
    }
    // A creature the damage has brought down shows no spasm.
    if (victim.currentHitPoints() > 0) {
        victim.playAnimation(AnimationType::LoopingSpasm);
    }
}

std::optional<Combat::OwnerHold> Combat::savedOwnerHold(const Object &actor) const {
    const auto paused = _ownerPauses.find(actor.id());
    if (paused == _ownerPauses.end() || !paused->second.engaged || !paused->second.paused() ||
        paused->second.actor.resolve().get() != &actor)
        return std::nullopt;
    return OwnerHold {paused->second.remaining, paused->second.pausedBy.resolve(), paused->second.openEnded};
}

void Combat::restoreOwnerHold(const std::shared_ptr<Creature> &actor, float remaining,
                              const std::shared_ptr<Object> &pausedBy) {
    auto &hold = _ownerPauses[actor->id()];
    hold.actor = actor;
    hold.pausedBy = pausedBy;
    hold.remaining = remaining;
    hold.engaged = true;
}

bool Combat::isOwnerPaused(const Creature &actor) const {
    if (actor.isKnockedDown()) {
        return true;
    }
    const auto paused = _ownerPauses.find(actor.id());
    return paused != _ownerPauses.end() && paused->second.paused();
}

// Going down replaces the pause on the creature's own round, and the one it
// has while owning no round; a partner's round is left alone.
void Combat::holdKnockedDown(const Creature &creature) {
    clearPauses(creature, 0.0f);
}

bool Combat::isEngaged(const Creature &creature) const {
    for (const auto &round : _rounds) {
        if (!round->duel) {
            continue;
        }
        for (const auto &entry : round->actions)
            if (entry.running() && entry.attacker.resolve().get() == &creature) {
                return true;
            }
    }
    return false;
}

// A round not yet started always has room; one paused by someone else has
// none; otherwise a reaction fits while half a second of the round remains.
bool Combat::hasReactionRoom(const Creature &creature, const Creature &attacker) const {
    for (const auto &round : _rounds) {
        const auto owned = std::find_if(round->actions.begin(), round->actions.end(), [&](const auto &entry) {
            return entry.running() && entry.attacker.resolve().get() == &creature;
        });
        if (owned == round->actions.end()) {
            continue;
        }
        if (owned->paused() && owned->pauseOwner.resolve().get() != &attacker) {
            return false;
        }
        if (owned->state == CombatRound::Pending) {
            return true;
        }
        static constexpr int kReactionRoomMilliseconds = 500;
        const int remaining = static_cast<int>(owned->duration * 1000.0f) - static_cast<int>(owned->time * 1000.0f);
        return remaining >= kReactionRoomMilliseconds;
    }
    const auto paused = _ownerPauses.find(creature.id());
    return paused == _ownerPauses.end() || !paused->second.paused() ||
           paused->second.pausedBy.resolve().get() == &attacker;
}

static bool isAttackEntry(const SavedScheduledAction &saved) {
    return saved.type == SavedScheduledAction::kAttackType || saved.type == SavedScheduledAction::kFeatAttackType;
}

static bool isCastEntry(const SavedScheduledAction &saved) {
    return saved.type == SavedScheduledAction::kCastSpellType || saved.type == SavedScheduledAction::kItemCastSpellType;
}

static bool scheduledCommandMatches(const SavedScheduledAction &saved) {
    if (!saved.command) {
        return saved.type == SavedScheduledAction::kPauseType;
    }
    const auto type = saved.command->type();
    switch (saved.type) {
    case SavedScheduledAction::kAttackType:
    case SavedScheduledAction::kFeatAttackType:
        return type == SavedActionType::AttackObject;
    case SavedScheduledAction::kEquipType:
        return type == SavedActionType::EquipItem;
    case SavedScheduledAction::kUnequipType:
        return type == SavedActionType::UnequipItem;
    case SavedScheduledAction::kCastSpellType:
        return type == SavedActionType::CastSpell;
    case SavedScheduledAction::kItemCastSpellType:
        return type == SavedActionType::ItemCastSpell;
    case SavedScheduledAction::kMoveType:
        return type == SavedActionType::MoveToPoint || type == SavedActionType::MoveToObject;
    case SavedScheduledAction::kCombatStanceType:
        return type == SavedActionType::Meditate;
    case SavedScheduledAction::kSwitchWeaponsType:
        return type == SavedActionType::SwitchWeapons;
    default:
        return false;
    }
}

bool Combat::scheduleEquipment(Object &object, const OrdinaryActionQueue::Node &node) {
    auto actor = std::dynamic_pointer_cast<Creature>(_game.getObjectById(object.id()));
    if (!actor) {
        return false;
    }
    const auto type = node->type();
    const bool equip = type == SavedActionType::EquipItem;
    if (!equip && type != SavedActionType::UnequipItem) {
        return false;
    }
    auto command = node->action->saveFacingState();
    if (!command) {
        return false;
    }
    // Both commands name the item first and carry their instant flag third; an
    // equip names the slot second.
    const auto target = std::get<SavedObjectReference>(command->parameters[0].payload);
    const auto flags = std::get<int32_t>(command->parameters[2].payload);
    const uint32_t slot = equip ? static_cast<uint32_t>(std::get<int32_t>(command->parameters[1].payload)) : 0;
    // An item already worn is not queued again for the head, the body or a
    // primary hand.
    if (equip) {
        const uint32_t primary = equipmentSlotMask(InventorySlots::head) | equipmentSlotMask(InventorySlots::body) |
                                 equipmentSlotMask(InventorySlots::rightWeapon) |
                                 equipmentSlotMask(InventorySlots::leftWeapon);
        const bool worn = std::any_of(actor->equipment().begin(), actor->equipment().end(), [&](const auto &entry) {
            return entry.second && entry.second->id() == target.id;
        });
        if ((slot & primary) && worn) {
            node->action->markCancelled();
            return true;
        }
    }
    if (!actor->isCommandable()) {
        node->action->markCancelled();
        return true;
    }
    // Body armour neither goes on nor comes off in direct combat.
    if (isArmorChangeRefused(*actor)) {
        const bool armorOn = equip && slot == equipmentSlotMask(InventorySlots::body);
        auto body = actor->getEquippedItem(InventorySlots::body);
        const bool armorOff = !equip && body && body->id() == target.id;
        if (armorOn || armorOff) {
            node->action->markCancelled();
            reportArmorChangeRefused(_game, *actor, armorOff);
            return true;
        }
    }
    command->bindObjectReferences(_game);
    auto state = scheduledOwner(*actor);
    if (equip) {
        for (auto &pending : state->actions) {
            if (pending->saved.type != SavedScheduledAction::kEquipType || pending->saved.applied || !pending->node || !pending->node->action ||
                (pending->saved.inventorySlot != slot && pending->saved.target.id != target.id))
                continue;
            // Replacing a scheduled item retains its slot in the queue and its timing.
            if (pending->saved.command && pending->saved.command->parameters.size() == 3) {
                command->parameters[2] = pending->saved.command->parameters[2];
            }
            auto replacement = command->toRuntimeAction(_game);
            if (!replacement) {
                return false;
            }
            pending->node->action->markCancelled();
            pending->node->action = std::move(replacement);
            pending->saved.target = target;
            pending->saved.inventorySlot = slot;
            pending->saved.command = std::move(command);
            return true;
        }
        for (auto &queued : actor->actions().nodes) {
            if (queued->type() != SavedActionType::EquipItem || !queued->action || queued->action->isCompleted() || queued->action->isCancelled()) {
                continue;
            }
            auto previous = queued->action->saveFacingState();
            if (!previous || previous->parameters.size() != 3) {
                continue;
            }
            auto item = std::get_if<SavedObjectReference>(&previous->parameters[0].payload);
            auto oldSlot = std::get_if<int32_t>(&previous->parameters[1].payload);
            // A queued equip takes the new request's item, slot and instant flag.
            if (item && oldSlot && (item->id == target.id || static_cast<uint32_t>(*oldSlot) == slot)) {
                auto replacement = command->toRuntimeAction(_game);
                if (!replacement) {
                    return false;
                }
                queued->action->markCancelled();
                queued->action = std::move(replacement);
                return true;
            }
        }
    } else {
        // An unequip of an item already waiting to come off, in the round or
        // in the queue, adds nothing. A queued one takes the new request's
        // instant flag as its container, so it takes nothing off either.
        const bool scheduled = std::any_of(state->actions.begin(), state->actions.end(), [&](const auto &pending) {
            return pending->saved.type == SavedScheduledAction::kUnequipType && !pending->saved.applied && pending->node && pending->node->action &&
                   pending->saved.target.id == target.id;
        });
        UnequipItemAction *queued = nullptr;
        if (!scheduled) {
            for (const auto &entry : actor->actions().nodes) {
                if (entry->type() != SavedActionType::UnequipItem || !entry->action || entry->action->isCompleted() ||
                    entry->action->isCancelled())
                    continue;
                auto *unequip = dyn_cast<UnequipItemAction>(entry->action.get());
                if (unequip && unequip->item() && unequip->item()->id() == target.id) {
                    queued = unequip;
                    break;
                }
            }
        }
        if (queued) {
            queued->overwriteContainer(flags);
        }
        if (scheduled || queued) {
            node->action->markCancelled();
            return true;
        }
    }
    // A script equip or unequip waits for the round whether or not it is instant.
    if (!actor->isInCombat() || actor->combatActivationType() != CombatActivation::Direct) {
        return false;
    }
    auto entry = std::make_shared<ScheduledEntry>();
    entry->node = node;
    entry->saved.type = equip ? SavedScheduledAction::kEquipType : SavedScheduledAction::kUnequipType;
    entry->saved.target = target;
    entry->saved.inventorySlot = slot;
    if (!equip) {
        entry->saved.repository = std::get<SavedObjectReference>(command->parameters[1].payload);
    }
    entry->saved.command = std::move(command);
    entry->referencesBound = entry->saved.bindObjectReferences(_game);
    // Only the creature's own round is rescaled.
    for (const auto &round : _rounds) {
        for (auto &action : round->actions) {
            if (action.running() && action.attacker.resolve() == actor) {
                rescaleRound(action, kEquipmentRoundMilliseconds, true);
            }
        }
    }
    state->actions.push_front(std::move(entry));
    ensureDispatcher(*actor);
    return true;
}

std::shared_ptr<Combat::ScheduledOwner> Combat::scheduledOwner(Creature &actor) {
    auto live = std::dynamic_pointer_cast<Creature>(_game.getObjectById(actor.id()));
    auto &state = _scheduled[actor.id()];
    if (!state || state->actor.resolve() != live) {
        state = std::make_shared<ScheduledOwner>();
        state->actor = live;
        for (const auto &round : _rounds) {
            for (const auto &entry : round->actions)
                if (entry.running() && entry.attacker.resolve() == live) {
                    state->time = entry.time;
                }
        }
    }
    return state;
}

static constexpr size_t kScheduledCapacity = 4;

bool Combat::appendScheduled(Creature &actor, std::shared_ptr<ScheduledEntry> entry, bool dispatcherToFront) {
    if (!addScheduled(actor, std::move(entry))) {
        return false;
    }
    ensureDispatcher(actor, dispatcherToFront);
    return true;
}

bool Combat::addScheduled(Creature &actor, std::shared_ptr<ScheduledEntry> entry) {
    auto owner = scheduledOwner(actor);
    // A round holds at most four pending entries; later requests are dropped.
    if (owner->actions.size() >= kScheduledCapacity) {
        return false;
    }
    if (entry->saved.command) {
        entry->referencesBound = entry->saved.bindObjectReferences(_game);
        if (auto action = entry->saved.command->toRuntimeAction(_game)) {
            entry->node = std::make_shared<ActionQueueNode>();
            entry->node->action = std::move(action);
            entry->node->actionId = entry->saved.command->actionId;
        }
    }
    owner->actions.push_back(std::move(entry));
    return true;
}

bool Combat::scheduleSwitchWeapons(Creature &actor) {
    auto entry = std::make_shared<ScheduledEntry>();
    entry->saved.type = SavedScheduledAction::kSwitchWeaponsType;
    entry->saved.animationTime = kSwitchWeaponsEntryMilliseconds;
    SavedActionRecord command;
    command.actionId = static_cast<uint32_t>(SavedActionType::SwitchWeapons);
    entry->saved.command = std::move(command);
    // A queued swap waits behind the commands already queued.
    return appendScheduled(actor, std::move(entry), false);
}

void Combat::scheduleCast(Creature &actor, const std::shared_ptr<Action> &cast) {
    const auto &spellCast = static_cast<const CastSpellAction &>(*cast);
    const auto &target = spellCast.target();
    const bool itemUse = spellCast.item().has_value();
    auto entry = std::make_shared<ScheduledEntry>();
    entry->saved.type = itemUse ? SavedScheduledAction::kItemCastSpellType : SavedScheduledAction::kCastSpellType;
    entry->saved.animation = itemUse ? 0 : kCastEntryAnimation;
    entry->saved.animationTime = itemUse ? 0 : kCastEntryMilliseconds;
    entry->saved.target = SavedObjectReference::fromRuntimeId(target ? target->id() : kSavedRuntimeInvalidObjectId);
    // The entry names its command as a cast or an item use; the command itself
    // is the one given.
    const auto commandType = itemUse ? SavedActionType::ItemCastSpell : SavedActionType::CastSpell;
    SavedActionRecord command;
    command.actionId = static_cast<uint32_t>(commandType);
    entry->saved.command = std::move(command);
    if (!itemUse) {
        entry->spell = spellCast.spell()->type;
    }
    entry->node = std::make_shared<ActionQueueNode>();
    entry->node->action = cast;
    entry->node->actionId = static_cast<uint32_t>(commandType);
    // A creature attempting no spell attempts the queued one's target object,
    // whether or not the entry finds room.
    if (target && !actor.attemptedSpellTarget()) {
        actor.setAttemptedSpellTarget(target->id());
    }
    auto owner = scheduledOwner(actor);
    // A round holds at most four pending entries; later requests are dropped.
    if (owner->actions.size() < kScheduledCapacity) {
        owner->actions.push_back(std::move(entry));
    }
    // Out of combat an item use clears the creature's actions while an entry
    // is due; a cast does so only while none is.
    const bool due = !owner->actions.empty() &&
                     owner->actions.front()->saved.timer <= static_cast<int>(owner->time * 1000.0f);
    if (!actor.isInCombat() && due == itemUse) {
        clearControlledActions(actor);
    }
    ensureDispatcher(actor, false);
}

void Combat::clearActions(Creature &actor) {
    // A creature that cannot be commanded keeps everything.
    actor.clearAllActions();
}

void Combat::clearControlledActions(Creature &actor) {
    if (&actor == _game.party().getLeader().get()) {
        actor.clearAllActions(true);
    }
}

void Combat::clearAllOrders(Creature &actor) {
    // The actions go as a forced ClearAllActions takes them, the one its
    // round has taken up included; the pending entries, the targets and the
    // spell targets go whether or not the creature can be commanded.
    actor.clearAllActions(true);
    discardEquipment(actor);
    actor.clearRoundTarget();
    actor.setAttemptedAttackTarget(script::kObjectInvalid);
    actor.clearCurrentAttackTarget();
    actor.setAttemptedSpellTarget(script::kObjectInvalid);
    actor.spellScriptContext().clearActiveTarget();
}

std::shared_ptr<Combat::ScheduledEntry> Combat::makeAttackEntry(const std::shared_ptr<Object> &target, FeatType feat,
                                                                const std::optional<CutsceneAttack> &cutscene) const {
    if (!target) {
        return nullptr;
    }
    // An ordinary attack needs a living target and a closed door; TSL also
    // refuses a door or container at no hit points.
    if (!cutscene) {
        const auto door = dyn_cast<Door>(target.get());
        if (target->isDead() || (door && door->isOpen())) {
            return nullptr;
        }
        if (_game.isTSL() && (door || isa<Placeable>(target.get())) && target->currentHitPoints() <= 0) {
            return nullptr;
        }
    }
    auto entry = std::make_shared<ScheduledEntry>();
    entry->saved.type = feat != FeatType::Invalid ? SavedScheduledAction::kFeatAttackType : SavedScheduledAction::kAttackType;
    entry->saved.animation = SavedAttackCommand::kAnimation;
    entry->saved.animationTime = SavedAttackCommand::kAnimationMilliseconds;
    // A cutscene attack carries no attack count.
    entry->saved.numAttacks = cutscene ? 0 : 1;
    entry->saved.target = SavedObjectReference::fromRuntimeId(target->id());
    SavedActionRecord command;
    SavedAttackCommand {entry->saved.target, feat, cutscene}.toAction(command);
    entry->saved.command = std::move(command);
    return entry;
}

std::shared_ptr<Action> Combat::scheduleAttack(Creature &actor, const std::shared_ptr<Object> &target,
                                               FeatType feat, const std::optional<CutsceneAttack> &cutscene) {
    auto entry = makeAttackEntry(target, feat, cutscene);
    // The entry is added first, up to the round's four, and a target it
    // accepts becomes the attempted target if there is none, even when the
    // entry did not fit; then, out of combat, the controlled creature drops
    // what it was doing, but not its pending entries; then the dispatcher goes
    // in, whether or not there was an entry. It goes in front of a TSL player
    // attack; KotOR and cutscene attacks append it.
    const bool toFront = _game.isTSL() && !cutscene;
    const auto scheduled = entry;
    const bool added = entry && addScheduled(actor, std::move(entry));
    if (scheduled && actor.getAttemptedAttackTarget() == script::kObjectInvalid) {
        actor.setAttemptedAttackTarget(target->id());
    }
    if (!actor.isInCombat()) {
        clearControlledActions(actor);
    }
    ensureDispatcher(actor, toFront);
    if (!added) {
        return nullptr;
    }
    return scheduled->node ? scheduled->node->action : nullptr;
}

// The move closes to a fifth of a metre and is forced to its end after a
// tenth of a second.
static constexpr float kCutsceneMoveRange = 0.2f;
static constexpr float kCutsceneMoveTimeout = 0.1f;

void Combat::scheduleCutsceneMove(Creature &actor, const std::shared_ptr<Object> &object, const glm::vec3 &point, bool run) {
    auto entry = std::make_shared<ScheduledEntry>();
    entry->saved.type = SavedScheduledAction::kMoveType;
    entry->saved.target = SavedObjectReference::fromRuntimeId(object ? object->id() : kSavedRuntimeInvalidObjectId);
    // The point and the run flag travel in a forced walk's command.
    SavedMoveCommand move;
    move.destination = point;
    move.area = SavedObjectReference::fromRuntimeId(_game.module()->area()->id());
    move.target = entry->saved.target;
    move.run = run;
    move.range = kCutsceneMoveRange;
    move.forcedPending = true;
    move.timeout = kCutsceneMoveTimeout;
    SavedActionRecord command;
    move.toAction(command);
    entry->saved.command = std::move(command);
    // The entry goes in first, up to the round's four; then, out of combat,
    // the controlled creature drops what it was doing; then the dispatcher is
    // appended.
    addScheduled(actor, std::move(entry));
    if (!actor.isInCombat()) {
        clearControlledActions(actor);
    }
    ensureDispatcher(actor, false);
}

void Combat::dispatchCutsceneMove(Creature &actor, const ScheduledEntry &entry) {
    const auto move = entry.saved.command ? SavedMoveCommand::fromAction(*entry.saved.command) : std::nullopt;
    if (!move) {
        return;
    }
    const auto object = entry.saved.target.boundObject();
    if (object) {
        actor.addActionOnTop(_game.newAction<MoveToObjectAction>(
            object, move->run, kCutsceneMoveRange, true, kCutsceneMoveTimeout, false, kCutsceneMoveRange));
    } else {
        actor.addActionOnTop(_game.newAction<MoveToLocationAction>(
            std::make_shared<Location>(move->destination, actor.getFacing()), move->run, true, kCutsceneMoveTimeout,
            kCutsceneMoveRange));
    }
}

std::shared_ptr<Action> Combat::addRoundAttack(Creature &actor, const std::shared_ptr<Object> &target) {
    auto entry = makeAttackEntry(target, FeatType::Invalid, std::nullopt);
    if (!entry) {
        return nullptr;
    }
    const auto scheduled = entry;
    // The target becomes the attempted target if there is none, even when the
    // entry does not fit.
    const bool added = addScheduled(actor, std::move(entry));
    if (actor.getAttemptedAttackTarget() == script::kObjectInvalid) {
        actor.setAttemptedAttackTarget(target->id());
    }
    if (!added) {
        return nullptr;
    }
    return scheduled->node ? scheduled->node->action : nullptr;
}

void Combat::takeNewAttackTarget(Creature &attacker, const std::shared_ptr<Creature> &target) {
    attacker.setOrientationLock(target->id());
    attacker.setAttemptedAttackTarget(target->id());
    auto found = _scheduled.find(attacker.id());
    if (found == _scheduled.end() || found->second->actor.resolve().get() != &attacker) {
        return;
    }
    const auto reference = SavedObjectReference::fromRuntimeId(target->id());
    for (auto &pending : found->second->actions) {
        // A feat attack keeps its target.
        if (pending->saved.type != SavedScheduledAction::kAttackType || !pending->saved.retargettable ||
            !pending->saved.command || !pending->node || !pending->node->action)
            continue;
        auto *attack = dyn_cast<AttackObjectAction>(pending->node->action.get());
        if (!attack) {
            continue;
        }
        // The attack stays what it was, the player's order or the round's
        // continuation, and is only aimed anew.
        attack->retarget(target);
        pending->saved.command->parameters[1].payload = reference;
        pending->saved.target = reference;
        pending->referencesBound = pending->saved.bindObjectReferences(_game);
    }
}

bool Combat::removeLastScheduled(Creature &actor) {
    auto found = _scheduled.find(actor.id());
    if (found == _scheduled.end() || found->second->actor.resolve().get() != &actor) {
        return false;
    }
    auto &actions = found->second->actions;
    // An entry already under way is no longer pending.
    if (actions.empty() || actions.back()->saved.applied) {
        return false;
    }
    const auto entry = actions.back();
    actions.pop_back();
    if (entry->node && entry->node->action) {
        entry->node->action->markCancelled();
    }
    return true;
}

void Combat::removeAllScheduled(Creature &actor) {
    while (removeLastScheduled(actor)) {
    }
}

// An item use is handed on unless it is aimed at a creature that has died. A
// party member who is down is still walked up to; the use fails there.
// A hostile item spell takes its user out of stealth. An item spell the
// hostile menu does not list, aimed at another object, is used at the use's
// location instead; in TSL only a hostile spell, and never a grenade's or a
// rocket's.
bool Combat::dispatchItemUse(Creature &actor, ScheduledEntry &entry) {
    const auto &use = static_cast<const CastSpellAction &>(*entry.node->action);
    const auto &target = use.target();
    if (const auto creature = Creature::asCreature(target); creature && creature->get().isRuntimeLive() && creature->get().isDead())
        return false;
    const auto &item = use.item();
    const auto &property = use.itemProperty();
    if (!item || !*item || !(*item)->isRuntimeLive() || !property || *property >= (*item)->properties().size()) {
        return true;
    }
    const auto propertySpell = static_cast<SpellType>((*item)->properties()[*property].subtype);
    const auto listed = _services.game.spells.get(propertySpell);
    const auto spell = _services.game.spells.get(itemSpellForActor(actor, propertySpell));
    const bool hostile = spell && spell->hostile;
    if (hostile) {
        actor.setStealthMode(false);
    }
    if ((listed && listed->hostileSlot > 0) || !target || target.get() == &actor) {
        return true;
    }
    if (_game.isTSL()) {
        const int itemType = (*item)->itemType();
        if (!hostile || itemType == Item::kGrenadeItemType || itemType == Item::kRocketItemType) {
            return true;
        }
    }
    entry.node->action = static_cast<const CastSpellAtObjectAction &>(use).toLocationUse();
    entry.saved.target = SavedObjectReference::fromRuntimeId(kSavedRuntimeInvalidObjectId);
    return true;
}

// TSL powers whose cast, handed on by the dispatcher, leaves its caster in stealth.
static bool isStealthPower(SpellType spell) {
    static constexpr std::array<SpellType, 6> kStealthPowers {
        SpellType::MindTrick, SpellType::BeastTrick, SpellType::BeastConfusion,
        SpellType::Confusion, SpellType::DroidTrick, SpellType::DroidConfusion};
    return std::find(kStealthPowers.begin(), kStealthPowers.end(), spell) != kStealthPowers.end();
}

bool Combat::entryKeepsStealth(const ScheduledEntry &entry) const {
    const int type = entry.saved.type;
    if (type == SavedScheduledAction::kItemCastSpellType) {
        return true;
    }
    if (!_game.isTSL()) {
        return false;
    }
    return type == SavedScheduledAction::kCombatStanceType || (entry.spell && isStealthPower(*entry.spell));
}

void Combat::dispatchScheduled(Creature &actor, const ScheduledEntry &entry) {
    actor.setRoundActionKind(entry.saved.type);
    handOn(actor, entry);
    if (entry.saved.type == SavedScheduledAction::kCombatStanceType) {
        actor.addStanceActions(static_cast<CombatStance>(entry.saved.numAttacks));
    } else if (entry.saved.type == SavedScheduledAction::kSwitchWeaponsType) {
        actor.addSwitchWeaponsAction(true);
    }
}

void Combat::rescaleRound(CombatRound::RoundAction &round, int milliseconds, bool force) {
    int length = static_cast<int>(round.duration * 1000.0f);
    if (length < milliseconds) {
        if (milliseconds - length <= 1000) {
            length = milliseconds + 1;
        } else if (force) {
            length = milliseconds;
        }
    }
    const int next = length - milliseconds;
    // The creature's scheduled entries are rescaled with its round.
    const auto actor = round.attacker.resolve();
    const auto found = actor ? _scheduled.find(actor->id()) : _scheduled.end();
    if (found != _scheduled.end() && found->second->actor.resolve() == actor) {
        const auto &owner = found->second;
        for (auto &entry : owner->actions) {
            entry->saved.timer = length > 0 ? static_cast<int>(static_cast<float>(entry->saved.timer) / length * next) : 0;
        }
        owner->time = length > 0 ? owner->time / length * next : 0.0f;
    }
    const int elapsed = static_cast<int>(round.time * 1000.0f);
    const int scaled = length ? static_cast<int>(static_cast<float>(elapsed) / length * next) : next;
    round.time = scaled / 1000.0f;
    round.duration = scaled < 0 ? 0.0f : std::max(0, next) / 1000.0f;
}

void Combat::holdRound(CombatRound::RoundAction &round, int milliseconds, const std::shared_ptr<Object> &owner, bool rescale) {
    if (rescale) {
        rescaleRound(round, milliseconds, false);
    }
    round.pauseRemaining += milliseconds / 1000.0f;
    round.pauseOpenEnded = false;
    round.pauseOwner = owner;
}

bool Combat::holdRounds(const Creature &creature, int milliseconds, const std::shared_ptr<Object> &owner, bool openEnded) {
    bool held = false;
    for (const auto &round : _rounds) {
        for (auto &entry : round->actions) {
            if (!entry.running() || entry.attacker.resolve().get() != &creature) {
                continue;
            }
            if (openEnded) {
                entry.pauseRemaining = milliseconds / 1000.0f;
                entry.pauseOpenEnded = true;
                entry.pauseOwner = owner;
            } else {
                holdRound(entry, milliseconds, owner, false);
            }
            held = true;
        }
    }
    return held;
}

void Combat::holdCreature(const Creature &creature, int milliseconds, const std::shared_ptr<Object> &owner, bool openEnded) {
    if (holdRounds(creature, milliseconds, owner, openEnded)) {
        return;
    }
    auto &pause = _ownerPauses[creature.id()];
    pause.actor = _game.getObjectById<Creature>(creature.id());
    pause.pausedBy = owner;
    pause.remaining = openEnded ? milliseconds / 1000.0f : pause.remaining + milliseconds / 1000.0f;
    pause.openEnded = openEnded;
    pause.engaged = true;
}

bool Combat::clearPauses(const Creature &creature, float credit) {
    _ownerPauses.erase(creature.id());
    bool cutscene = false;
    for (const auto &round : _rounds) {
        for (auto &entry : round->actions) {
            if (!entry.running() || entry.attacker.resolve().get() != &creature) {
                continue;
            }
            entry.pauseRemaining = 0.0f;
            entry.pauseOpenEnded = false;
            entry.pauseOwner.reset();
            entry.time += credit;
            cutscene = cutscene || isCutsceneRoundFor(*round, entry);
        }
    }
    return cutscene;
}

std::vector<std::shared_ptr<Creature>> Combat::partnersOf(const Creature &actor) const {
    std::vector<std::shared_ptr<Creature>> partners;
    for (const auto &round : _rounds) {
        if (round->master.resolve().get() != &actor) {
            continue;
        }
        if (auto engaged = std::dynamic_pointer_cast<Creature>(round->engaged.resolve()); engaged && engaged.get() != &actor) {
            partners.push_back(std::move(engaged));
        }
        for (const auto &entry : round->actions) {
            if (auto partner = entry.attacker.resolve(); partner && partner.get() != &actor) {
                partners.push_back(std::move(partner));
            }
        }
    }
    return partners;
}

void Combat::endPartnerPauses(const Creature &actor, float overshoot, std::vector<std::pair<std::shared_ptr<Creature>, bool>> &released) {
    for (const auto &partner : partnersOf(actor)) {
        if (std::any_of(released.begin(), released.end(), [&](const auto &posed) { return posed.first == partner; })) {
            continue;
        }
        const bool cutscene = clearPauses(*partner, overshoot);
        released.emplace_back(partner, cutscene);
    }
}

// The round dispatcher is the creature's current action.
static bool dispatcherHeadsQueue(const Creature &actor) {
    const auto &nodes = actor.actions().nodes;
    if (nodes.empty() || nodes.front()->type() != SavedActionType::CombatDispatch) {
        return false;
    }
    const auto &head = nodes.front()->action;
    return head && !head->isCompleted() && !head->isCancelled();
}

// Having handed on an entry, the dispatcher waits behind every queued command.
static void moveDispatcherBehind(Creature &actor) {
    for (const auto &node : actor.actions().nodes) {
        if (node->type() == SavedActionType::CombatDispatch && node->action && !node->action->isCompleted() &&
            !node->action->isCancelled()) {
            actor.moveActionNodeToBack(node);
            return;
        }
    }
}

// Confusion, and a droid's confusion, turn a creature on anyone.
static bool turnsOnAnyone(int effectState) {
    return effectState == static_cast<int>(CreatureState::Confusion) ||
           effectState == static_cast<int>(CreatureState::DroidConfused);
}

bool Combat::refusesAttackOnFriend(const Creature &attacker, const Object &target) {
    const auto *creature = dyn_cast<const Creature>(&target);
    const int state = attacker.effectState();
    return creature && creature->getReputationToward(attacker) >= kLowestFriendReputation && !turnsOnAnyone(state);
}

bool Combat::refusesUnperceivedTarget(const Creature &caster, const CastSpellAction &cast) {
    // Only a cast at a creature can be refused; a fake cast is exempt, a fake
    // item use is not.
    const auto target = Creature::asCreature(cast.target());
    if (!target || (!cast.item() && cast.fake())) {
        return false;
    }
    if (&target->get() == &caster || caster.isPartyMember()) {
        return false;
    }
    const auto &perception = caster.perception();
    const uint32_t id = target->get().id();
    return !perception.has(id) || (perception.isInvisible(id) && !perception.sees(id));
}

bool Combat::refusesAttack(const Creature &attacker) {
    if (attacker.isCommandable()) {
        return false;
    }
    return !attacker.game().isTSL() || !turnsOnAnyone(attacker.effectState());
}

bool Combat::anyOtherRunningEntry(const Creature &actor, std::optional<std::reference_wrapper<const Action>> self,
                                  const std::function<bool(const CombatRound::RoundAction &)> &test) const {
    for (const auto &round : _rounds) {
        for (const auto &entry : round->actions) {
            if (!entry.running() || entry.attacker.resolve().get() != &actor) {
                continue;
            }
            if (self && (entry.action.get() == &self->get() || &entry.action->combatAction() == &self->get())) {
                continue;
            }
            if (test(entry)) {
                return true;
            }
        }
    }
    return false;
}

bool Combat::awaitsRoundEnd(const Creature &actor, std::optional<std::reference_wrapper<const Action>> self) const {
    // A creature held while it owns no round waits as well.
    if (isOwnerPaused(actor)) {
        return true;
    }
    return anyOtherRunningEntry(actor, self, [](const CombatRound::RoundAction &entry) {
        // A round that a finished command began holds the next one while it
        // is paused.
        return entry.action->combatAction().tookRoundAction() || (entry.action->isCompleted() && entry.paused());
    });
}

bool Combat::awaitsRoundPause(const Creature &actor, const Action &self) const {
    if (isOwnerPaused(actor)) {
        return true;
    }
    return anyOtherRunningEntry(actor, self, [](const CombatRound::RoundAction &entry) {
        return entry.paused();
    });
}

bool Combat::blocksOwnerActions(const Creature &actor) const {
    if (!actor.actions().nodes.empty()) {
        const auto &head = actor.actions().nodes.front()->action;
        if (head && head->isScheduledCommand() && !head->isCompleted() && !head->isCancelled()) {
            return false;
        }
    }
    const auto found = _scheduled.find(actor.id());
    if (found == _scheduled.end() || found->second->actor.resolve().get() != &actor || found->second->actions.empty()) {
        return false;
    }
    const auto &head = found->second->actions.front();
    if (head->saved.applied) {
        return true;
    }
    // A due entry waits for the dispatcher, and a round action for the end of
    // the round its creature has acted in; whatever runs ahead of it goes on.
    if (head->saved.type != SavedScheduledAction::kPauseType && (!dispatcherHeadsQueue(actor) ||
                                                                 (!head->saved.isEquipment() && awaitsRoundEnd(actor, std::nullopt))))
        return false;
    return head->saved.timer <= static_cast<int>(found->second->time * 1000.0f);
}

// A due entry's command is lost when it could not come back from a save, does
// not match the entry's kind, or names an object that could not be found.
static bool commandLost(const SavedScheduledAction &saved, bool referencesBound, const OrdinaryActionQueue::Node &node) {
    return !referencesBound || !scheduledCommandMatches(saved) || !node || !node->action;
}

void Combat::beginEquipmentPause(Creature &actor, ScheduledEntry &entry) {
    entry.saved.applied = true;
    entry.saved.remainingPause = std::max(0, entry.saved.animationTime) / 1000.0f;
    actor.setRoundActionKind(entry.saved.type);
    handOn(actor, entry);
    actor.setStealthMode(false);
    actor.setMovementType(Creature::MovementType::None);
}

Combat::EntryTake Combat::useUpEntry(Creature &actor, ScheduledOwner &owner, ScheduledEntry &entry) {
    // An equip or unequip is still taken: its creature leaves stealth and
    // holds its round for the entry's pause, but nothing is equipped or taken
    // off. Any other entry still becomes the round's action and takes its
    // creature out of stealth as a carried-out one would; it is dropped, and
    // the dispatcher goes behind the queued commands. A cast or item use is
    // let go as it is handed on; any other entry is kept as handed on.
    if (entry.saved.isEquipment()) {
        beginEquipmentPause(actor, entry);
        return EntryTake::Paused;
    }
    actor.setRoundActionKind(entry.saved.type);
    if (!isCastEntry(entry.saved)) {
        handOn(actor, entry);
    }
    if (!entryKeepsStealth(entry)) {
        actor.setStealthMode(false);
    }
    owner.actions.pop_front();
    moveDispatcherBehind(actor);
    return EntryTake::Left;
}

Combat::EntryTake Combat::takeEntry(Creature &actor, ScheduledOwner &owner, const std::shared_ptr<ScheduledEntry> &entry) {
    auto &saved = entry->saved;
    if (saved.type == SavedScheduledAction::kPauseType) {
        saved.applied = true;
        saved.remainingPause = std::max(0, saved.animationTime) / 1000.0f;
        actor.setRoundActionKind(SavedScheduledAction::kPauseType);
        handOn(actor, *entry);
        actor.setStealthMode(false);
        return EntryTake::Paused;
    }
    if (saved.type == SavedScheduledAction::kMoveType) {
        // A cutscene move is taken whether or not its object is still there,
        // and let go as it is handed on. One restored without its point moves
        // nowhere.
        owner.actions.pop_front();
        actor.setRoundActionKind(SavedScheduledAction::kMoveType);
        actor.setStealthMode(false);
        dispatchCutsceneMove(actor, *entry);
        moveDispatcherBehind(actor);
        return EntryTake::Left;
    }
    if (isAttackEntry(saved)) {
        return takeAttack(actor, owner, entry);
    }
    if (isCastEntry(saved)) {
        return takeCast(actor, owner, entry);
    }
    // Any other entry can no longer be carried out once its command is lost
    // or an object its command acts on is gone.
    if (commandLost(saved, entry->referencesBound, entry->node) || !entry->node->action->runtimeDependenciesLive()) {
        return useUpEntry(actor, owner, *entry);
    }
    auto action = entry->node->action;
    // A party member at zero vitality uses the entry up: what it would hand
    // on fails.
    if (!actor.permitsAction(*action)) {
        return useUpEntry(actor, owner, *entry);
    }
    if (saved.type == SavedScheduledAction::kCombatStanceType || saved.type == SavedScheduledAction::kSwitchWeaponsType) {
        owner.actions.pop_front();
        if (!entryKeepsStealth(*entry)) {
            actor.setStealthMode(false);
        }
        dispatchScheduled(actor, *entry);
        moveDispatcherBehind(actor);
        return EntryTake::Left;
    }
    // An equip or unequip is carried out where it stands and holds its
    // creature for its pause.
    beginEquipmentPause(actor, *entry);
    action->execute(action, actor, 0.0f);
    return EntryTake::Paused;
}

Combat::EntryTake Combat::takeAttack(Creature &actor, ScheduledOwner &owner, const std::shared_ptr<ScheduledEntry> &entry) {
    auto &saved = entry->saved;
    if (commandLost(saved, entry->referencesBound, entry->node) || !entry->node->action->runtimeDependenciesLive()) {
        return useUpEntry(actor, owner, *entry);
    }
    auto action = entry->node->action;
    // An attack taken by a creature that cannot be commanded, or aimed at a
    // friend, is used up without an attack. One taken up by a creature no
    // longer able to attack fails at once and ends its combat modes.
    const auto target = saved.target.boundObject();
    const bool refused = refusesAttack(actor) || (target && refusesAttackOnFriend(actor, *target));
    if (refused || !actor.permitsAction(*action)) {
        if (!refused) {
            actor.cancelAllCombatModes();
        }
        return useUpEntry(actor, owner, *entry);
    }
    actor.setRoundActionKind(saved.type);
    handOn(actor, *entry);
    actor.setStealthMode(false);
    // Taking an attack entry queues its attack as the attack command does.
    if (target) {
        actor.recordQueuedAttack(*target);
    }
    owner.actions.pop_front();
    action->setScheduledCommand(true);
    actor.requeueActionNode(entry->node);
    moveDispatcherBehind(actor);
    return EntryTake::Left;
}

Combat::EntryTake Combat::takeCast(Creature &actor, ScheduledOwner &owner, const std::shared_ptr<ScheduledEntry> &entry) {
    auto &saved = entry->saved;
    if (commandLost(saved, entry->referencesBound, entry->node)) {
        return useUpEntry(actor, owner, *entry);
    }
    // A cast or item use is let go as it is handed on.
    actor.setRoundActionKind(saved.type);
    if (!entryKeepsStealth(*entry)) {
        actor.setStealthMode(false);
    }
    // An item use aimed at a creature that has died is dropped, and the
    // dispatcher stays at the head.
    if (saved.type == SavedScheduledAction::kItemCastSpellType && !dispatchItemUse(actor, *entry)) {
        owner.actions.pop_front();
        return EntryTake::Left;
    }
    // A cast handed on starts without spell targets. One whose target or item
    // is gone, taken by a creature that cannot be commanded, or aimed at a
    // creature its caster does not perceive, is used up without casting and
    // leaves none; any other is the caster's attempted spell.
    actor.setAttemptedSpellTarget(script::kObjectInvalid);
    actor.spellScriptContext().clearActiveTarget();
    auto action = entry->node->action;
    if (!actor.isCommandable() || !action->runtimeDependenciesLive() ||
        refusesUnperceivedTarget(actor, static_cast<const CastSpellAction &>(*action))) {
        return useUpEntry(actor, owner, *entry);
    }
    recordAttemptedSpellTarget(_game, actor, static_cast<const CastSpellAction &>(*action));
    owner.actions.pop_front();
    action->setScheduledCommand(true);
    actor.requeueActionNode(entry->node);
    moveDispatcherBehind(actor);
    return EntryTake::Left;
}

void Combat::updateEquipment(float dt) {
    _equipmentTime.clear();
    // Updating an owner may add or remove others; the owners of this frame
    // are taken first, into a buffer kept from frame to frame.
    auto owners = std::move(_ownersBuffer);
    owners.clear();
    for (const auto &[id, owner] : _scheduled) {
        owners.push_back(owner);
    }
    for (const auto &owner : owners) {
        auto actor = owner->actor.resolve();
        if (!actor) {
            for (auto it = _scheduled.begin(); it != _scheduled.end();) {
                if (it->second == owner) {
                    it = _scheduled.erase(it);
                } else {
                    ++it;
                }
            }
            continue;
        }
        if (actor->isDead()) {
            discardEquipment(*actor);
            continue;
        }
        // A creature held by a time stop keeps its schedule where it is.
        if (_game.isFrozenByTimeStop(*actor)) {
            continue;
        }
        if (!actor->actions().nodes.empty()) {
            const auto &head = actor->actions().nodes.front()->action;
            if (head && head->isScheduledCommand() && !head->isCompleted() && !head->isCancelled()) {
                continue;
            }
        }
        // A knocked-down creature's schedule stands still.
        if (actor->isKnockedDown()) {
            continue;
        }
        // A pause with no end of its own holds the schedule for the whole frame.
        float externalPause = 0.0f;
        for (const auto &round : _rounds) {
            for (const auto &entry : round->actions) {
                if (entry.running() && entry.attacker.resolve() == actor) {
                    externalPause = std::max(externalPause, entry.pauseOpenEnded ? dt : entry.pauseRemaining);
                }
            }
        }
        if (const auto paused = _ownerPauses.find(actor->id()); paused != _ownerPauses.end()) {
            externalPause = std::max(externalPause, paused->second.openEnded ? dt : paused->second.remaining);
        }
        if (externalPause >= dt) {
            continue;
        }
        float frame = std::max(0.0f, dt - externalPause);
        while (!owner->actions.empty()) {
            auto entry = owner->actions.front();
            if (!entry->saved.applied) {
                const float wait = std::max(0.0f, entry->saved.timer / 1000.0f - owner->time);
                const float used = std::min(frame, wait);
                owner->time += used;
                frame -= used;
                if (used < wait) {
                    break;
                }
                // Only the dispatcher, running at the head of the queue, takes an entry,
                // and it takes no round action while its creature's round action is spent.
                if (entry->saved.type != SavedScheduledAction::kPauseType && (!dispatcherHeadsQueue(*actor) ||
                                                                              (!entry->saved.isEquipment() && awaitsRoundEnd(*actor, std::nullopt))))
                    break;
                const auto taken = takeEntry(*actor, *owner, entry);
                if (taken == EntryTake::Left) {
                    frame = 0.0f;
                    break;
                }
                // Carrying out an equip or unequip may end the schedule or the creature.
                const auto active = _scheduled.find(actor->id());
                if (active == _scheduled.end() || active->second != owner || !actor->isRuntimeLive()) {
                    break;
                }
                if (owner->actions.empty() || owner->actions.front() != entry) {
                    continue;
                }
            }
            const float used = std::min(frame, entry->saved.remainingPause);
            entry->saved.remainingPause -= used;
            frame -= used;
            _equipmentTime[actor->id()] += used;
            if (entry->saved.remainingPause > 0.0f) {
                break;
            }
            owner->actions.pop_front();
            readyAfterScheduledPause(*actor);
            if (frame <= 0.0f) {
                break;
            }
        }
        owner->time += frame;
        const auto active = _scheduled.find(actor->id());
        if (active != _scheduled.end() && active->second == owner && owner->actions.empty()) {
            _scheduled.erase(active);
        }
    }
    owners.clear();
    _ownersBuffer = std::move(owners);
}

void Combat::transferEquipment(Creature &actor) {
    const auto found = _scheduled.find(actor.id());
    if (found == _scheduled.end() || found->second->actor.resolve().get() != &actor) {
        return;
    }
    const auto owner = found->second;
    const std::vector<std::shared_ptr<ScheduledEntry>> pending(owner->actions.begin(), owner->actions.end());
    // Reverse traversal with front insertion preserves the remaining schedule order.
    for (auto it = pending.rbegin(); it != pending.rend(); ++it) {
        const auto &entry = *it;
        if (entry->saved.applied || !entry->saved.isEquipment() || !entry->referencesBound ||
            !entry->node || !entry->node->action || !entry->node->action->runtimeDependenciesLive())
            continue;
        auto position = std::find(owner->actions.begin(), owner->actions.end(), entry);
        if (position == owner->actions.end()) {
            continue;
        }
        owner->actions.erase(position);
        actor.requeueActionNode(entry->node);
    }
    if (owner->actions.empty()) {
        _scheduled.erase(found);
    }
}

void Combat::discardEquipment(Object &actor) {
    const auto found = _scheduled.find(actor.id());
    if (found == _scheduled.end() || found->second->actor.resolve().get() != &actor) {
        return;
    }
    auto owner = found->second;
    _scheduled.erase(found);
    for (const auto &entry : owner->actions) {
        if (entry->node && entry->node->action && !entry->saved.applied) {
            auto action = entry->node->action;
            action->markCancelled();
            // A pending cast has not reached its creature's queue, so nothing
            // of it is cleared from the creature.
            if (!isCastEntry(entry->saved)) {
                action->cancel(action, actor);
            }
        }
    }
}

std::vector<SavedScheduledAction> Combat::saveScheduled(const Object &actor) const {
    std::vector<SavedScheduledAction> result;
    const auto found = _scheduled.find(actor.id());
    if (found == _scheduled.end() || found->second->actor.resolve().get() != &actor) {
        return result;
    }
    for (const auto &entry : found->second->actions) {
        auto saved = entry->saved;
        if (entry->node && entry->node->action) {
            if (auto command = entry->node->action->saveFacingState()) {
                command->groupActionId = entry->node->groupId;
                // A pending attack entry keeps no cutscene values: it comes
                // back as an ordinary attack.
                if (isAttackEntry(saved)) {
                    if (auto attack = SavedAttackCommand::fromAction(*command); attack && attack->cutscene) {
                        attack->cutscene.reset();
                        attack->toAction(*command);
                    }
                }
                saved.command = std::move(command);
            } else if (isAttackEntry(saved)) {
                // An attack whose target is gone is not kept.
                continue;
            } else if (!saved.applied) {
                throw ValidationException("Scheduled equipment action has no save representation");
            }
        }
        result.push_back(std::move(saved));
    }
    return result;
}

float Combat::scheduledTime(const Object &actor) const {
    const auto found = _scheduled.find(actor.id());
    return found != _scheduled.end() && found->second->actor.resolve().get() == &actor ? found->second->time : 0.0f;
}

void Combat::restoreScheduled(const std::shared_ptr<Creature> &actor,
                              const std::vector<SavedScheduledAction> &records,
                              const std::vector<bool> &bound, float time) {
    if (!actor || records.empty()) {
        return;
    }
    auto state = std::make_shared<ScheduledOwner>();
    state->actor = actor;
    state->time = time;
    for (size_t index = 0; index < records.size(); ++index) {
        auto entry = std::make_shared<ScheduledEntry>();
        entry->saved = records[index];
        entry->referencesBound = index < bound.size() && bound[index];
        if (entry->referencesBound && entry->saved.command && scheduledCommandMatches(entry->saved)) {
            auto action = entry->saved.command->toRuntimeAction(_game);
            if (action) {
                entry->node = std::make_shared<ActionQueueNode>();
                entry->node->action = std::move(action);
                entry->node->actionId = entry->saved.command->actionId;
                entry->node->groupId = entry->saved.command->groupActionId;
            }
        }
        // Even unexecutable records retain their ordered head position and payload.
        state->actions.push_back(std::move(entry));
    }
    _scheduled[actor->id()] = std::move(state);
    actor->setCombatState(true);
}

void Combat::cancelActions(Object &actor) {
    discardEquipment(actor);
    std::vector<std::shared_ptr<Action>> actions;
    for (const auto &round : _rounds) {
        for (const auto &entry : round->actions) {
            if (entry.attacker.resolve().get() == &actor &&
                std::find(actions.begin(), actions.end(), entry.action) == actions.end())
                actions.push_back(entry.action);
        }
    }
    // An action that cannot be cleared runs on. The rounds and the pairs they
    // make are left as they are.
    for (const auto &action : actions) {
        if (action->isCompleted() || action->isCancelled() || !action->isClearable()) {
            continue;
        }
        action->markCancelled();
        action->cancel(action, actor);
    }
}

// Every clear drops the hits still to land of each round the creature runs.
void Combat::discardSwingHits(const Creature &actor) {
    for (const auto &round : _rounds) {
        for (const auto &entry : round->actions) {
            if (!entry.running() || entry.attacker.resolve().get() != &actor) {
                continue;
            }
            if (auto attacks = entry.action->combatAction().combatAttacks()) {
                attacks->get().discardPending();
            }
        }
    }
}

// The master's round end undoes its pair: the partner's round loses its
// pause, whoever set it, as does the hold of a partner owning no round.
void Combat::releasePartner(Creature &actor) {
    const auto partners = partnersOf(actor);
    for (const auto &round : _rounds) {
        if (round->master.resolve().get() != &actor) {
            continue;
        }
        round->master.reset();
        round->engaged.reset();
        round->duel = false;
    }
    for (const auto &partner : partners) {
        clearPauses(*partner, 0.0f);
    }
}

void Combat::finishOwner(Creature &actor, int runEndRound, bool suppressScript) {
    // A round target that is down, or a door or container now open, is forgotten.
    if (auto target = actor.getRoundTarget()) {
        const auto *creature = dyn_cast<Creature>(target.get());
        const auto *door = dyn_cast<Door>(target.get());
        const auto *placeable = dyn_cast<Placeable>(target.get());
        if ((creature && isUnavailableAttackTarget(*creature)) ||
            (door && door->state() != DoorState::Closed) || (placeable && placeable->isOpen()))
            actor.clearRoundTarget();
    }
    actor.forgetFallenCombatTargets();
    actor.setMovementRestricted(false);
    actor.setMovementType(Creature::MovementType::None);
    // Every round end shows the pause or ready pose, cutscene rounds included.
    if (!actor.isDead() && !actor.isTemporarilyDead()) {
        actor.showPauseReadyAnimation(false);
    }
    const auto leader = _game.party().getLeader();
    // The script is also withheld from a party member at zero vitality.
    if (runEndRound && !suppressScript && actor.currentSerializedActionId() != static_cast<uint32_t>(SavedActionType::MoveToPoint) &&
        leader.get() != &actor && !actor.isDead() && !actor.isTemporarilyDead())
        actor.runEndRoundScript();
    // The controlled leader's round end can pause play while it is fighting.
    if (runEndRound && leader.get() == &actor && actor.partyCombatMode()) {
        _game.requestAutoPause(AutoPauseReason::EndOfCombatRound);
    }
}

void Combat::endRound(Creature &actor, int runEndRound) {
    std::vector<std::pair<CombatRound::RoundAction, bool>> finished;
    bool suppressed = false;
    actor.refreshBodyFuel();
    for (const auto &round : _rounds) {
        for (auto &entry : round->actions) {
            if (entry.attacker.resolve().get() != &actor || !entry.running()) {
                continue;
            }
            const bool cutscene = isCutsceneRoundFor(*round, entry);
            finished.emplace_back(entry, cutscene);
            suppressed |= entry.action->combatAction().suppressesEndRoundScript() || cutscene;
            entry.retired = true;
            entry.action->retireCombatRound();
            entry.target.reset();
        }
    }
    releasePartner(actor);
    transferEquipment(actor);
    closeRound(actor, finished, runEndRound, suppressed, false);
}

void Combat::closeRound(Creature &actor, const std::vector<std::pair<CombatRound::RoundAction, bool>> &ended,
                        int runEndRound, bool suppressScript, bool ranOut) {
    finishOwner(actor, runEndRound, suppressScript);
    if (runEndRound) {
        for (const auto &[entry, cutscene] : ended) {
            continueOwner(entry, cutscene, ranOut);
        }
    }
    actor.recordCombatRound();
}

void Combat::update(float dt) {
    pruneInvalidRounds();
    updateEquipment(dt);
    // A pause held while owning no round ends like any round pause; one with
    // no end of its own lasts until the pause of the creature holding it ends.
    // Once the creature holding it is gone, the held creature's round ends.
    std::vector<std::shared_ptr<Creature>> holderGone;
    for (auto it = _ownerPauses.begin(); it != _ownerPauses.end();) {
        auto actor = it->second.actor.resolve();
        if (actor && _game.isFrozenByTimeStop(*actor)) {
            ++it;
            continue;
        }
        it->second.remaining = it->second.openEnded ? std::max(0.0f, it->second.remaining - dt) : it->second.remaining - dt;
        const bool ownerGone = it->second.openEnded && !it->second.pausedBy.resolve();
        if (actor && it->second.paused() && !ownerGone) {
            ++it;
            continue;
        }
        it = _ownerPauses.erase(it);
        if (actor && ownerGone) {
            holderGone.push_back(std::move(actor));
        } else if (actor) {
            readyAfterRoundPause(*actor, false);
        }
    }
    for (const auto &actor : holderGone) {
        endRound(*actor, 1);
    }
    // Updating a round may end or start others; the rounds of this frame are
    // taken first, into a buffer kept from frame to frame.
    auto rounds = std::move(_roundsBuffer);
    rounds.assign(_rounds.begin(), _rounds.end());
    for (const auto &round : rounds) {
        if (std::find(_rounds.begin(), _rounds.end(), round) == _rounds.end()) {
            continue;
        }
        updateRound(*round, dt);
    }
    rounds.clear();
    _roundsBuffer = std::move(rounds);
    pruneInvalidRounds();
}

// The entry is done with: it has left the round, or its creature's round has
// ended and its command is over.
static bool isEntryDone(const CombatRound::RoundAction &entry) {
    return entry.retired ||
           (entry.state == CombatRound::Finished && (entry.action->isCompleted() || entry.action->isCancelled()));
}

void Combat::pruneInvalidRounds() {
    auto rounds = std::move(_pruneBuffer);
    rounds.assign(_rounds.begin(), _rounds.end());
    for (const auto &round : rounds) {
        if (std::find(_rounds.begin(), _rounds.end(), round) == _rounds.end()) {
            continue;
        }
        // A command that is cleared, or whose objects are gone, is given up,
        // but its creature's round runs on to its end; a creature that is gone
        // leaves the round. A live partner keeps its command.
        SmallVector<CombatRound::RoundAction, 2> abandoned;
        for (auto &entry : round->actions) {
            if (entry.retired) {
                continue;
            }
            // A creature that is gone leaves the round with a command it had
            // finished with too.
            if (entry.action->isCompleted() && !entry.attacker.resolve()) {
                entry.retired = true;
                entry.action->retireCombatRound();
                continue;
            }
            if (!entry.action->isCompleted() && (entry.action->isCancelled() ||
                                                 !entry.participantBindingsLive() || !entry.remainsInActorQueue() ||
                                                 !entry.action->runtimeDependenciesLive())) {
                abandoned.push_back(entry);
                if (!entry.attacker.resolve()) {
                    entry.retired = true;
                }
            }
        }
        for (const auto &entry : abandoned) {
            entry.action->retireCombatRound();
            auto actor = entry.attacker.resolve();
            if (actor && !entry.action->isCancelled()) {
                entry.action->cancel(entry.action, *actor);
            }
            entry.action->markCancelled();
            entry.action->complete();
        }
        if (std::all_of(round->actions.begin(), round->actions.end(), isEntryDone)) {
            auto position = std::find(_rounds.begin(), _rounds.end(), round);
            if (position != _rounds.end()) {
                _rounds.erase(position);
            }
        }
    }
    rounds.clear();
    _pruneBuffer = std::move(rounds);
}

void Combat::updateRound(CombatRound &round, float dt) {
    for (size_t index = 0; index < round.actions.size(); ++index) {
        const auto &entry = round.actions[index];
        auto actor = entry.running() ? entry.attacker.resolve() : nullptr;
        // A creature held by a time stop keeps its round where it is.
        if (!actor || _game.isFrozenByTimeStop(*actor)) {
            continue;
        }
        // The time the creature's scheduled entries took this frame is not
        // its round's.
        updateEntry(round, index, std::max(0.0f, dt - _equipmentTime[actor->id()]));
    }
}

void Combat::updateEntry(CombatRound &round, size_t index, float dt) {
    auto &entry = round.actions[index];
    auto actor = entry.attacker.resolve();
    // A knocked-down creature's round stands still.
    if (!entry.running() || !actor || actor->isKnockedDown()) {
        return;
    }
    float advance = std::max(0.0f, dt);
    // A pause with no end of its own holds the round until the pause of the
    // creature holding it ends; once that creature is gone, the round ends.
    if (entry.pauseOpenEnded) {
        if (!entry.pauseOwner.resolve()) {
            entry.pauseRemaining = 0.0f;
            entry.pauseOpenEnded = false;
            entry.pauseOwner.reset();
            finishEntry(round, index);
            return;
        }
        entry.pauseRemaining = std::max(0.0f, entry.pauseRemaining - advance);
        return;
    }
    // The round's clock stands still while it is paused, and goes on with
    // only the time left over once the pause runs out. A creature's hits
    // come due as its round's pause runs: by the swing's length less the
    // pause left. A pause another creature's swing has lengthened beyond the
    // swing's leaves them all due, one an update, and the rest land as it
    // runs out. Whoever's round pause runs out comes back to its pose, after
    // any round end the leftover time brings: the creature, and the partners
    // of the pairs it masters, whose pauses end with its own.
    std::vector<std::pair<std::shared_ptr<Creature>, bool>> posing;
    bool pauseEnded = false;
    if (entry.pauseRemaining > 0.0f) {
        const float used = std::min(advance, entry.pauseRemaining);
        entry.pauseRemaining -= used;
        advance -= used;
        const bool runsOut = entry.pauseRemaining <= 0.0f;
        const int left = static_cast<int>(std::ceil(static_cast<double>(entry.pauseRemaining) * 1000.0));
        entry.action->combatAction().releaseSwingHits(*actor, left, runsOut);
        // A hit that brings a creature down may end the round meanwhile.
        if (!runsOut || !entry.running()) {
            return;
        }
        entry.pauseOwner.reset();
        pauseEnded = true;
        posing.emplace_back(actor, isCutsceneRoundFor(round, entry));
        endPartnerPauses(*actor, advance, posing);
    }
    entry.time += advance;
    if (entry.state == CombatRound::Pending) {
        entry.state = CombatRound::Running;
    }
    // A timer driven below zero by a rescale ends the round at once.
    const bool overdrawn = entry.time < 0.0f;
    const bool held = blocksOwnerActions(*actor) || (!entry.action->isCancelled() && entry.action->holdsCombatRound());
    // A round whose target is down ends early only as a pause on it runs out;
    // otherwise it runs to its end.
    bool targetDown = false;
    if (pauseEnded) {
        auto target = std::dynamic_pointer_cast<Creature>(entry.target.resolve());
        targetDown = target && isUnavailableAttackTarget(*target);
    }
    const bool expired = overdrawn || entry.time >= entry.duration;
    if (!held && (expired || targetDown)) {
        finishEntry(round, index);
    }
    for (const auto &[creature, cutscene] : posing) {
        readyAfterRoundPause(*creature, cutscene);
    }
}

bool Combat::isActionPaused(const Action &action) const {
    for (const auto &round : _rounds) {
        for (const auto &entry : round->actions) {
            if (entry.running() && (entry.action.get() == &action || &entry.action->combatAction() == &action)) {
                auto actor = entry.attacker.resolve();
                return (actor && blocksOwnerActions(*actor)) || round->suspends(action);
            }
        }
    }
    return false;
}

// The attacker masters when it is in the party, or when the partner is
// attempting to attack no one, or to attack it while the partner's own round
// was not last started as a master.
bool Combat::mastersExchange(const Creature &attacker, const Creature &partner) const {
    const auto attempted = partner.getAttemptedAttackTarget();
    return _game.party().isMember(attacker) || attempted == script::kObjectInvalid ||
           (attempted == attacker.id() && !partner.roundMaster());
}

bool Combat::answersMaster(const Creature &attacker, const Object &target) const {
    const auto *partner = dyn_cast<Creature>(&target);
    return partner && isEngagedBy(attacker, *partner) && !mastersExchange(attacker, *partner);
}

bool Combat::awaitsMaster(const Creature &attacker, const Object &target) const {
    return answersMaster(attacker, target) && !ownsRound(attacker);
}

bool Combat::isEngagedBy(const Creature &attacker, const Creature &target) const {
    if (target.isDebilitated(true)) {
        return false;
    }
    const auto attempted = target.getAttemptedAttackTarget();
    const bool attempting = attempted != script::kObjectInvalid;
    const auto spell = target.attemptedSpellTarget();
    bool engaged = !attempting || attempted == attacker.id() || spell.get() == &attacker;
    if (_game.party().getLeader().get() == &target) {
        if (!attempting && !spell) {
            return false;
        }
        if ((attempting && attempted != attacker.id()) || (spell && spell.get() != &attacker)) {
            return false;
        }
        const auto ordered = target.getOrderedAttackTarget();
        if (ordered != script::kObjectInvalid && ordered != attacker.id()) {
            return false;
        }
    }
    return engaged;
}

bool Combat::engagePhysicalTarget(const Creature &attacker, Object &target, int pauseMilliseconds) {
    auto *partner = dyn_cast<Creature>(&target);
    const bool engaged = partner && isEngagedBy(attacker, *partner);
    const bool masters = engaged && mastersExchange(attacker, *partner);
    // An attack that engages no one starts its round as no master, and one
    // that masters the exchange as its master; an attacker its partner
    // masters starts no round of its own.
    if (!engaged || masters) {
        _game.getObjectById<Creature>(attacker.id())->setRoundMaster(masters);
    }
    if (!engaged) {
        return false;
    }
    // An attacker that masters the exchange starts its partner's side of it,
    // as no master, which locks the partner's orientation on the attacker.
    if (masters) {
        partner->setRoundMaster(false);
        partner->setOrientationLock(attacker.id());
    }
    // The attacker masters the attack's round, engaged with the partner, when
    // it masters the exchange or no one else does. A round mastered by another
    // keeps its master and engagement.
    std::shared_ptr<CombatRound> round;
    if (auto action = attacker.currentCombatAction()) {
        round = findRoundForAction(action, attacker);
    }
    if (round && (masters || round->master.empty())) {
        round->master = _game.getObjectById(attacker.id());
        round->engaged = _game.getObjectById(partner->id());
    }
    const auto master = _game.getObjectById(attacker.id());
    // A partner the attacker does not master has its round held by the swing
    // on top of any pause it has, or, owning none, the hold it has.
    if (!masters) {
        holdCreature(*partner, pauseMilliseconds, master, false);
        return true;
    }
    // The master restarts its partner's round under it. A round the partner
    // has acted in, or one of its own, ends as it would at its end. The
    // master starts its partner's side of the round, whether or not the
    // partner had one: a new side, with no command yet, aimed at the master,
    // behind the master's own. A side of the round the two share whose
    // command is still to act keeps it. Either way the shared round starts
    // again from the beginning for both.
    std::optional<size_t> shared;
    if (round) {
        for (size_t index = 0; index < round->actions.size(); ++index) {
            const auto &entry = round->actions[index];
            if (entry.running() && entry.attacker.resolve().get() == partner) {
                shared = index;
            }
        }
    }
    const bool keepsSide = shared && !round->actions[*shared].action->isCompleted();
    if (!keepsSide && ownsRound(*partner)) {
        endRound(*partner, 1);
    }
    if (round && !keepsSide) {
        for (auto it = round->actions.begin(); it != round->actions.end();) {
            if (!it->running() && it->attacker.resolve().get() == partner) {
                round->actions.erase(it);
            } else {
                ++it;
            }
        }
        if (round->actions.size() == 1) {
            auto placeholder = _game.newAction<WaitAction>(0.0f);
            placeholder->complete();
            round->actions.front().slot = 0;
            round->actions.emplace_back(placeholder, _game.getObjectById<Creature>(partner->id()), master, false);
            auto &side = round->actions.back();
            side.slot = 1;
            side.state = CombatRound::Running;
            round->duel = true;
            partner->setRoundTarget(master);
            // The new side ends any pause the partner held while owning no
            // round; the master's hold below is its only one.
            _ownerPauses.erase(partner->id());
        }
    }
    if (shared) {
        for (auto &entry : round->actions) {
            if (!entry.running()) {
                continue;
            }
            entry.state = CombatRound::Running;
            entry.time = 0.0f;
            entry.duration = CombatRound::kDuration;
            entry.pauseRemaining = 0.0f;
            entry.pauseOpenEnded = false;
            entry.pauseOwner.reset();
        }
    }
    // The partner's round, or the hold of a partner owning none, stands
    // still until the master's pause ends, whoever held it before; the
    // partner may act as soon as it does.
    holdCreature(*partner, pauseMilliseconds, master, true);
    return true;
}

void Combat::pauseForSwing(const Creature &attacker, int pauseMilliseconds) {
    holdRounds(attacker, pauseMilliseconds, _game.getObjectById(attacker.id()), false);
}

void Combat::shortenRounds(const Creature &creature, int milliseconds) {
    for (const auto &round : _rounds) {
        for (auto &entry : round->actions) {
            if (entry.running() && entry.attacker.resolve().get() == &creature) {
                rescaleRound(entry, milliseconds, false);
            }
        }
    }
}

bool Combat::isRoundEngaged(const Action &action) const {
    for (const auto &round : _rounds) {
        for (const auto &entry : round->actions) {
            if (entry.running() && (entry.action.get() == &action || &entry.action->combatAction() == &action)) {
                return round->duel || round->engaged.resolve() != nullptr;
            }
        }
    }
    return false;
}

void Combat::configureSpellPair(CombatRound &round, const std::shared_ptr<Action> &action, Creature &caster) {
    auto *cast = dyn_cast<CastSpellAction>(&action->combatAction());
    // An instant cast's round engages no one.
    if (!cast || cast->instantSpell()) {
        return;
    }
    auto target = std::dynamic_pointer_cast<Creature>(getTarget(*action, caster));
    const bool engaged = target && isEngagedBy(caster, *target);
    const bool master = engaged && mastersExchange(caster, *target);
    // The cast starts its caster's round as the master of the exchange, or as no master.
    caster.setRoundMaster(master);
    round.master = engaged ? (master ? _game.getObjectById(caster.id()) : std::static_pointer_cast<Object>(target)) : nullptr;
    round.engaged = engaged ? target : nullptr;
    if (engaged && round.actions.size() == 2 &&
        std::all_of(round.actions.begin(), round.actions.end(), [](const auto &entry) { return entry.state == CombatRound::Pending; })) {
        auto masterObject = round.master.resolve();
        for (auto &entry : round.actions) {
            entry.slot = entry.attacker.resolve() == masterObject ? 0 : 1;
        }
    }
    // When the second command arrives while still pending, its master owns slot zero.
}
void Combat::beginCast(const std::shared_ptr<Action> &action, Creature &caster, float seconds, bool itemUse, bool fake) {
    const auto owner = findRoundForAction(action, caster);
    if (!owner) {
        return;
    }
    const auto own = std::find_if(owner->actions.begin(), owner->actions.end(), [&](const auto &entry) {
        return entry.running() && entry.action == action;
    });
    if (own == owner->actions.end()) {
        return;
    }
    const bool joined = own->joined;
    // In a round its creature was already in, an item use neither holds nor
    // shortens the round, and a cast sets it up only while nothing holds it.
    if (joined && (itemUse || own->paused())) {
        return;
    }
    std::shared_ptr<Creature> target;
    if (joined) {
        // Such a round keeps its engagement; while it is engaged, the cast holds
        // the creature it is aimed at.
        auto *cast = dyn_cast<CastSpellAction>(&action->combatAction());
        if (cast && (owner->duel || owner->engaged.resolve())) {
            target = std::dynamic_pointer_cast<Creature>(cast->target());
        }
    } else {
        // Preserve the role selected when the pair was admitted. Selecting it again
        // would count this pair's own master as an unrelated engaged round.
        if (owner->master.empty()) {
            configureSpellPair(*owner, action, caster);
        }
        // A fake cast that starts its round holds no partner, nor does an item use.
        if (!itemUse && !fake) {
            target = std::dynamic_pointer_cast<Creature>(owner->engaged.resolve());
        }
    }
    if (target.get() == &caster) {
        target.reset();
    }
    // The caster's round, and its partner's, are each held and shortened.
    const int pause = std::max(0, static_cast<int>(seconds * 1000.0f));
    bool partnerHeld = false;
    for (auto &candidate : _rounds) {
        auto &round = *candidate;
        bool partnerHere = false;
        for (auto &entry : round.actions) {
            if (!entry.running()) {
                continue;
            }
            const bool partner = target && entry.attacker.resolve() == target;
            if (!partner && (&round != owner.get() || entry.action != action)) {
                continue;
            }
            partnerHere = partnerHere || partner;
            holdRound(entry, pause, _game.getObjectById(caster.id()), true);
        }
        partnerHeld = partnerHeld || partnerHere;
        // A cast that begins its round brings its target's round into the
        // pair; one that joins a round only holds it.
        if (partnerHere && &round != owner.get() && !joined) {
            round.master = owner->master;
            round.engaged = _game.getObjectById(caster.id());
        }
    }
    // A partner that owns no round is held all the same, by itself, and loses
    // no round time. The cast that begins a round engages it.
    if (target && !partnerHeld) {
        auto &hold = _ownerPauses[target->id()];
        hold.actor = target;
        hold.pausedBy = std::static_pointer_cast<Object>(target);
        hold.remaining += pause / 1000.0f;
        hold.openEnded = false;
        hold.engaged = hold.engaged || !joined;
    }
}

void Combat::beginInstantCast(const std::shared_ptr<Action> &action, Creature &caster, const std::shared_ptr<Object> &target) {
    // The cast starts its caster's round as no master.
    caster.setRoundMaster(false);
    // The attack on a creature, door or container makes the round: a Power
    // Attack in front of the queue that needs no approach, engages no one and
    // leaves the round free once its pause is over. It starts its caster's
    // round afresh, whatever round the caster was in, and rolls at once,
    // inside the cast's own update, where it is over with its swing.
    if (target && (isa<Creature>(target.get()) || isa<Door>(target.get()) || isa<Placeable>(target.get()))) {
        auto attack = _game.newAction<UseFeatAction>(FeatType::PowerAttack, target);
        attack->markInstantCastAttack();
        caster.addActionOnTop(attack);
        addAction(attack, caster);
        for (auto &entry : findRoundForAction(attack, caster)->actions) {
            if (entry.action == attack) {
                entry.state = CombatRound::Running;
            }
        }
        attack->execute(attack, caster, 0.0f);
        return;
    }
    // With any other target the round runs its full length at once.
    addAction(action, caster);
}

SavedRoundClock Combat::saveClock(const CombatRound &round, const CombatRound::RoundAction &entry) const {
    // Each command keeps the clock of its own creature's round.
    SavedRoundClock result;
    result.id = round.id;
    result.slot = entry.slot;
    result.state = entry.state;
    result.joined = entry.joined;
    result.duel = round.duel;
    result.elapsed = entry.time;
    result.duration = entry.duration;
    result.pauseRemaining = entry.pauseRemaining;
    auto owner = entry.pauseOwner.resolve();
    result.pauseOwner = SavedObjectReference::fromRuntimeId(owner ? owner->id() : script::kObjectInvalid);
    _game.bindSavedObjectReference(result.pauseOwner);
    auto master = round.master.resolve();
    auto engaged = round.engaged.resolve();
    result.master = SavedObjectReference::fromRuntimeId(master ? master->id() : script::kObjectInvalid);
    result.engaged = SavedObjectReference::fromRuntimeId(engaged ? engaged->id() : script::kObjectInvalid);
    _game.bindSavedObjectReference(result.master);
    _game.bindSavedObjectReference(result.engaged);
    return result;
}

std::optional<SavedRoundClock> Combat::saveRound(const Action &action) const {
    for (const auto &round : _rounds)
        for (const auto &entry : round->actions) {
            if (entry.action.get() != &action || entry.retired) {
                continue;
            }
            return saveClock(*round, entry);
        }
    return std::nullopt;
}

std::vector<SavedCreatureRound> Combat::saveRounds(const Object &actor) const {
    std::vector<SavedCreatureRound> result;
    const auto &nodes = actor.actions().nodes;
    for (const auto &round : _rounds) {
        for (const auto &entry : round->actions) {
            if (entry.retired || entry.attacker.resolve().get() != &actor) {
                continue;
            }
            // A command still waiting in the queue carries its round itself.
            const bool carried = std::any_of(nodes.begin(), nodes.end(), [&](const auto &node) {
                return node->action == entry.action && !node->action->isCompleted() && !node->action->isCancelled();
            });
            if (carried) {
                continue;
            }
            SavedCreatureRound saved;
            saved.clock = saveClock(*round, entry);
            saved.spellRound = isSpellRound(entry);
            auto target = entry.target.resolve();
            saved.target = SavedObjectReference::fromRuntimeId(target ? target->id() : script::kObjectInvalid);
            _game.bindSavedObjectReference(saved.target);
            // A swung attack is kept, with the hits it has not landed; one
            // that was cleared is not.
            Action &command = entry.action->combatAction();
            if (command.isCompleted() && !command.isCancelled() && command.combatAttacks()) {
                saved.command = command.saveFacingState();
            }
            result.push_back(std::move(saved));
        }
    }
    return result;
}

void Combat::restoreRound(const std::shared_ptr<Action> &action,
                          const std::shared_ptr<Creature> &actor, const SavedRoundClock &saved) {
    if (findRoundForAction(action, *actor)) {
        return;
    }
    actor->setCurrentCombatAction(action);
    if (isHostileAction(action->combatAction())) {
        actor->setCombatState(true);
    }
    restoreEntry(action, actor, getTarget(*action, *actor), saved, true, false);
}

void Combat::restoreRound(const std::shared_ptr<Creature> &actor, const SavedCreatureRound &saved) {
    // A swung attack comes back over, with its target and the hits it has
    // not landed; otherwise a finished wait stands in for the command given
    // up, and so it does when the attack's target is gone, and the round runs
    // on with no target. The round keeps whether it is a spell round.
    auto command = saved.command ? AttackObjectAction::restore(*saved.command, _game) : nullptr;
    if (!command) {
        command = _game.newAction<WaitAction>(0.0f);
    }
    command->complete();
    std::shared_ptr<Object> target = saved.target.boundObject();
    if (!target) {
        target = getTarget(*command, *actor);
    }
    restoreEntry(command, actor, target, saved.clock, false, saved.spellRound);
    // The swung attack is still the creature's current combat action, unless
    // a queued command has taken over.
    if (command->combatAttacks() && !actor->currentCombatAction()) {
        actor->setCurrentCombatAction(command);
    }
}

void Combat::restoreEntry(const std::shared_ptr<Action> &action, const std::shared_ptr<Creature> &actor,
                          const std::shared_ptr<Object> &target, const SavedRoundClock &saved, bool queued,
                          bool spellRound) {
    // The two sides of a shared round restore into the same round.
    auto found = std::find_if(_rounds.begin(), _rounds.end(), [&](const auto &candidate) { return candidate->id == saved.id; });
    std::shared_ptr<CombatRound> round = found != _rounds.end() ? *found : nullptr;
    if (!round) {
        round = std::make_shared<CombatRound>(action, actor, target, queued);
        _rounds.push_back(round);
        round->id = saved.id;
        round->master = saved.master.boundObject();
        round->engaged = saved.engaged.boundObject();
        _nextRoundId = std::max(_nextRoundId, saved.id + 1);
    } else {
        round->actions.emplace_back(action, actor, target, queued);
    }
    auto &restored = round->actions.back();
    restored.slot = saved.slot;
    // An item use starts over after a load inside the round it had begun,
    // which keeps the pause it was given then rather than taking a new one.
    restored.joined = saved.joined || isItemUse(action->combatAction());
    restored.spellRound = spellRound;
    restored.state = saved.state == CombatRound::Pending || saved.state == CombatRound::Finished
                         ? static_cast<CombatRound::State>(saved.state)
                         : CombatRound::Running;
    restored.time = saved.elapsed;
    restored.duration = saved.duration;
    restored.pauseOwner = saved.pauseOwner.boundObject();
    // A pause comes back with an end of its own: the seconds it had left. One
    // whose owner is gone comes back only while a swing still has hits to
    // land over it, then the creature's own.
    restored.pauseRemaining = restored.pauseOwner.resolve() ? saved.pauseRemaining : 0.0f;
    if (saved.pauseRemaining > 0.0f && !restored.pauseOwner.resolve()) {
        const auto attacks = action->combatAction().combatAttacks();
        if (attacks && (attacks->get().hasPendingMelee() || attacks->get().hasPendingDischarges())) {
            restored.pauseRemaining = saved.pauseRemaining;
            restored.pauseOwner = actor;
        }
    }
    // An attack an earlier save took past its swing holds and shortens its
    // round as a swing does, for what was left of the swing, and lands the
    // hits it has left over that pause, after any pause already on it.
    if (const int left = action->combatAction().pauseLeftAtLoadMilliseconds(); left > 0) {
        holdRound(restored, left, actor, true);
    }
    round->duel = saved.duel;
    if (round->actions.size() == 2 && round->actions[0].slot > round->actions[1].slot) {
        std::swap(round->actions[0], round->actions[1]);
    }
}

void Combat::finishEntry(CombatRound &round, size_t index) {
    auto &entry = round.actions[index];
    entry.state = CombatRound::Finished;
    const CombatRound::RoundAction ended = entry;
    const bool cutscene = isCutsceneRoundFor(round, ended);
    auto actor = ended.attacker.resolve();
    if (!actor) {
        return;
    }
    actor->refreshBodyFuel();
    // The round's end clears its attack records without ending the action's
    // queued lifetime. Undelivered events read the cleared records.
    ended.action->retireCombatRound();
    releasePartner(*actor);
    closeRound(*actor, {{ended, cutscene}}, 1, ended.action->combatAction().suppressesEndRoundScript() || cutscene, true);
}

// The leader's next attack is an entry on its round, as the player's orders
// are, but nothing is cleared for it and no dispatcher is queued for it: it
// waits for the dispatcher already queued, and after a clear that took the
// dispatcher, for the next one queued. Taking it on the target the player
// ordered forgets that order.
void Combat::scheduleContinuation(Creature &actor, const std::shared_ptr<Object> &target) {
    auto action = addRoundAttack(actor, target);
    if (actor.getOrderedAttackTarget() == target->id()) {
        actor.setOrderedAttackTarget(script::kObjectInvalid);
    }
    if (!action) {
        return;
    }
    if (auto *attack = dyn_cast<AttackObjectAction>(action.get())) {
        attack->markRoundContinuation();
    }
}

// A round that runs out hands the creature on whether or not its command was
// cleared; a round ended early does so only for a command still standing.
void Combat::continueOwner(const CombatRound::RoundAction &entry, bool cutsceneRound, bool ranOut) {
    auto actor = entry.attacker.resolve();
    if (!actor || (entry.action->isCancelled() && !ranOut) ||
        isUnavailableAttackTarget(*actor) || cutsceneRound)
        return;
    // Only the controlled creature fights on into the next round.
    if (actor != _game.party().getLeader()) {
        return;
    }
    Action &command = entry.action->combatAction();
    auto *cast = dyn_cast<CastSpellAction>(&command);
    // A cast or item use makes a spell round; an instant cast does not.
    const bool spellRound = isSpellRound(entry);
    // A cast's round is on the creature it is aimed at, unless that is the
    // caster; a cast at a location has none.
    std::shared_ptr<Object> roundTarget = entry.target.resolve();
    if (cast) {
        roundTarget = cast->target().get() != actor.get() ? cast->target() : nullptr;
    }
    auto roundCreature = std::dynamic_pointer_cast<Creature>(roundTarget);
    const bool died = roundCreature && isUnavailableAttackTarget(*roundCreature);
    // A spell round goes on with its creature's attempted spell target, as the
    // round's end has left it: the cast's target, the caster included; nothing
    // once the cast was cleared or an item use has finished.
    std::shared_ptr<Object> kept = roundTarget;
    if (spellRound) {
        kept = actor->attemptedSpellTarget();
    }
    // A spell round on a place finds nothing to attack in the next round: the
    // creature does nothing and keeps its modes.
    if (spellRound && kept && kept->type() == ObjectType::Area && !died) {
        return;
    }
    // A creature that is down is not kept, nor, after a round that is not a
    // spell round, an opened door or container.
    if (const auto creature = std::dynamic_pointer_cast<Creature>(kept); creature && isUnavailableAttackTarget(*creature)) {
        kept.reset();
    } else if (const auto door = std::dynamic_pointer_cast<Door>(kept); door && !spellRound && door->state() != DoorState::Closed) {
        kept.reset();
    } else if (const auto placeable = std::dynamic_pointer_cast<Placeable>(kept); placeable && !spellRound && placeable->isOpen()) {
        kept.reset();
    }
    // Round commands still waiting keep the round from staying on its target:
    // the round's pending entries, and the casts and attacks that wait in the
    // queue without one.
    bool pending = hasScheduled(*actor);
    for (const auto &node : actor->actions().nodes) {
        if (!node->action || node->action == entry.action || node->action->isCompleted() || node->action->isCancelled()) {
            continue;
        }
        Action &queued = node->action->combatAction();
        if (isa<AttackObjectAction>(&queued) || isa<UseFeatAction>(&queued) || isa<CastSpellAction>(&queued))
            pending = true;
    }
    // A target that died hands the round on to a new one in any case.
    bool retarget = died;
    bool stays = false;
    if (!kept) {
        retarget = retarget || !pending;
    } else if (!pending) {
        const auto keptCreature = std::dynamic_pointer_cast<Creature>(kept);
        if (!keptCreature) {
            stays = true;
        } else if (isHostileForContinuation(*actor, *keptCreature)) {
            // A spell round stays on an enemy only within cleave range.
            stays = !spellRound ||
                    glm::distance(actor->position(), keptCreature->position()) <= actor->maxCleaveRange(*keptCreature);
        } else if (_game.isTSL()) {
            // TSL stays on a creature that is not an enemy, and after a spell
            // on the caster itself looks for one.
            stays = true;
            retarget = retarget || keptCreature == actor;
        }
    }
    std::shared_ptr<Object> target = kept;
    if (retarget) {
        std::shared_ptr<Creature> enemy;
        auto &area = *_game.module()->area();
        const float searchRange = actor->maxCleaveRange(*actor);
        // After a kill the search starts around the fallen target.
        if (died) {
            enemy = findNearestEnemy(area, *actor, *roundCreature, searchRange);
        }
        if (!enemy) {
            enemy = findNearestEnemy(area, *actor, *actor, searchRange);
        }
        // With no enemy left the creature drops its combat modes; its combat
        // state runs out on its own.
        if (!enemy) {
            actor->cancelAllCombatModes();
            return;
        }
        takeNewAttackTarget(*actor, enemy);
        _game.setLastTarget(enemy->id(), true);
        target = enemy;
    } else if (!stays) {
        return;
    }
    scheduleContinuation(*actor, target);
}

} // namespace game

} // namespace reone
