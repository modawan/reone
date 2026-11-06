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

#include "reone/system/smallvector.h"
#include "reone/system/timer.h"

#include "action.h"
#include "attack.h"
#include "effect/damage.h"
#include "object/creature.h"
#include "types.h"

namespace reone {

namespace game {

class Area;
class CastSpellAction;
class Game;
struct ServicesView;

/**
 * The command that hands its creature's scheduled round entries on to its
 * queue. It holds the head of the queue until the creature's round ends.
 */
class CombatDispatchAction : public Action {
public:
    CombatDispatchAction(Game &game, ServicesView &services, int mode = 1) :
        Action(game, services, ActionType::CombatDispatch), _mode(mode) {}
    static bool classof(Action *action) { return action->type() == ActionType::CombatDispatch; }
    uint32_t serializedActionId() const override { return static_cast<uint32_t>(SavedActionType::CombatDispatch); }
    void execute(std::shared_ptr<Action>, Object &, float) override;
    bool cancel(std::shared_ptr<Action>, Object &) override;
    std::optional<SavedActionRecord> saveFacingState() const override;
    /** A saved CombatDispatch command: its round slot. */
    static std::shared_ptr<Action> restore(const SavedActionRecord &record, Game &game);

private:
    /** The command's one parameter, as it is saved. */
    int _mode;
};

/**
 * Stance command. Total Defense holds its target and completes 1.5 seconds
 * after it last started the creature's round; Meditative holds until another
 * command is queued behind it.
 */
class CombatStanceAction : public Action {
public:
    CombatStanceAction(Game &game, ServicesView &services,
                       CombatStance stance, std::shared_ptr<Object> target = nullptr) :
        Action(game, services, ActionType::CombatStance),
        _stance(stance), _target(std::move(target)) {
        if (_target) {
            requireRuntimeObject(_target);
        }
    }
    static bool classof(Action *action) { return action->type() == ActionType::CombatStance; }
    uint32_t serializedActionId() const override {
        return static_cast<uint32_t>(_stance == CombatStance::TotalDefense ? SavedActionType::TotalDefense
                                                                           : SavedActionType::Meditate);
    }
    void execute(std::shared_ptr<Action>, Object &, float) override;
    bool cancel(std::shared_ptr<Action>, Object &) override;
    std::optional<SavedActionRecord> saveFacingState() const override;
    /** A saved TotalDefense or Meditate command; none once the Total Defense target is gone. */
    static std::shared_ptr<Action> restore(const SavedActionRecord &record, Game &game);

    CombatStance stance() const { return _stance; }
    const std::shared_ptr<Object> &target() const { return _target; }

private:
    CombatStance _stance;
    std::shared_ptr<Object> _target;

    void executeTotalDefense(Creature &actor);
    void executeMeditative(Creature &actor);
};

/**
 * Combat round consists of either one or two actions. Second action is only
 * present when both combatants are creatures. Each creature keeps its own
 * round in its action: its clock, length and pause, which nothing done to
 * the other creature moves.
 */
struct CombatRound {
    /** The length of a round, in seconds, until something rescales it. */
    static constexpr float kDuration = 3.0f;

    enum State {
        Pending,
        FirstAction,
        SecondAction,
        Finished
    };

    /** A creature's command in the round, with the creature's own round. */
    struct RoundAction {
        RoundAction(const std::shared_ptr<Action> &action,
                    const std::shared_ptr<Creature> &attacker,
                    const std::shared_ptr<Object> &target,
                    bool actorQueueAssociated) :
            action(action),
            attacker(attacker), target(target),
            actorQueueAssociated(actorQueueAssociated) {}

        std::shared_ptr<Action> action;
        RuntimeObjectRef<Creature> attacker;
        RuntimeObjectRef<Object> target;
        bool actorQueueAssociated {false};
        bool retired {false};
        /**
         * The action took over the round from an earlier, finished command of
         * its creature rather than starting it.
         */
        bool joined {false};
        int slot {0};
        /** How far the creature's round has got; Finished once it has ended. */
        State state {Pending};
        /** The seconds of the creature's round gone by. */
        float time {0.0f};
        /** The length of the creature's round, in seconds. */
        float duration {kDuration};
        /** The creature's round stands still for this many seconds, held by its pause owner. */
        float pauseRemaining {0.0f};
        RuntimeObjectRef<Object> pauseOwner;
        /** The round, restored without its command, is a spell round. */
        bool spellRound {false};

        /** The attacker and the target of the command are still the objects it was given. */
        bool participantBindingsLive() const;
        /** The command is still in its creature's queue, if it was put there. */
        bool remainsInActorQueue() const;
        /** The command takes part in the round and the creature's round has not ended. */
        bool running() const { return !retired && state != Finished; }
        /** \p dt scaled from the length of the creature's round to a round of the usual length. */
        float actionDelta(float dt) const { return duration > 0.0f ? dt * kDuration / duration : kDuration; }
    };

    SmallVector<RoundAction, 2> actions;

    /** Two creatures take their turns in the round. */
    bool duel {false};
    uint64_t id {0};
    /** The creature that masters an engaged exchange, and the one it engages. */
    RuntimeObjectRef<Object> master;
    RuntimeObjectRef<Object> engaged;

    explicit CombatRound(const std::shared_ptr<Action> &action,
                         const std::shared_ptr<Creature> &attacker,
                         const std::shared_ptr<Object> &target,
                         bool actorQueueAssociated) {
        actions.emplace_back(
            action, attacker, target, actorQueueAssociated);
    }

    /**
     * The round keeps \p action from going on: its creature's round is paused
     * by another or by no one, or its creature is down.
     */
    bool suspends(const Action &action) const;
    /** \p dt scaled from the length of the round of \p action's creature to a round of the usual length. */
    float actionDelta(const Action &action, float dt) const;
    /** The round of \p action's creature has ended. */
    bool hasEnded(const Action &action) const;

    /** \p action may take its turn in the round now. */
    bool canExecute(Action &action) const;
};

/**
 * Combat is used schedule attacks and form rounds. When a creature starts an
 * attack, Combat creates a new round for it. If the target attacks back, and
 * the original attack is not finished yet, the corresponding round becomes a
 * duel (a CombatRound with more than 1 attack).
 *
 * A physical attack also engages a target that attacks no one else, whether
 * or not it attacks back; engaged melee exchanges enable "cinematic"
 * animations.
 */
class Combat {
public:
    /**
     * The hold of a creature owning no round that the engagement of an attack
     * or cast put on it: its remaining seconds and who holds it. The hold of
     * damage on a creature owning no round is not kept in a save.
     */
    struct OwnerHold {
        float remaining {0.0f};
        std::shared_ptr<Object> pausedBy;
    };

    /** A round entry of a creature not yet taken up. */
    struct PendingEntry {
        /** The entry's kind, one of SavedScheduledAction's k*Type. */
        int type {0};
        /** The command the entry runs, once it has one. */
        std::optional<std::reference_wrapper<Action>> command;
    };

    Combat(
        Game &game,
        ServicesView &services) :
        _game(game),
        _services(services) {
    }

    /**
     * A creature does not attack a friend, unless it is under one of the two
     * states that turn it on anyone. The attack is used up without a swing.
     */
    static bool refusesAttackOnFriend(const Creature &attacker, const Object &target);
    /**
     * A creature outside the party does not cast, or use an item, at another
     * creature it has not perceived or that is invisible to it and unseen; a
     * fake cast is exempt. The entry is used up without casting.
     */
    static bool refusesUnperceivedTarget(const Creature &caster, const CastSpellAction &cast);
    /**
     * A creature that cannot be commanded takes up no attack, save in TSL one
     * under either state that turns it on anyone.
     */
    static bool refusesAttack(const Creature &attacker);
    /**
     * The nearest living creature, other than attacker, observer and
     * \p excluded, that is hostile to attacker, that observer perceives and
     * that is in line of sight, closer than maxDistance beyond both personal
     * spaces. The search walks the area's objects outward from observer in X
     * order; line of sight is taken from observer to those below it and from
     * attacker to those above it. Nothing is found when observer is not in
     * the area.
     */
    static std::shared_ptr<Creature> findNearestEnemy(
        Area &area,
        const Creature &attacker,
        const Creature &observer,
        float maxDistance,
        std::optional<std::reference_wrapper<const Object>> excluded = std::nullopt);

    void update(float dt);
    void reset() {
        _rounds.clear();
        _scheduled.clear();
        _equipmentTime.clear();
        _ownerPauses.clear();
        _settleRequests.clear();
        _nextRoundId = 1;
    }

    // Rounds

    /**
     * Adds an action to an existing combat round, or starts a new round, based
     * on attacker and target. A cast or item use takes its turn in a round its
     * creature's earlier, finished command began, as does a command aimed at
     * the creature that masters that round; any other command gives such a
     * round up, without its end, and starts afresh.
     */
    const CombatRound &addAction(const std::shared_ptr<Action> &action, Object &actor);
    /**
     * A cast or item use has begun. A round the command started is held and
     * shortened for \p seconds; so is a round the caster was already in, for a
     * cast only and only while nothing holds that round. A cast also holds its
     * engaged partner, unless it is fake and started the round.
     */
    void beginCast(const std::shared_ptr<Action> &action, Creature &caster, float seconds, bool itemUse, bool fake);
    /**
     * An instant cast starts its caster's round afresh. Aimed at a creature,
     * door or container it makes an unengaged Power Attack, which needs no
     * approach, takes no action of the round and ends with its pause.
     */
    void beginInstantCast(const std::shared_ptr<Action> &action, Creature &caster, const std::shared_ptr<Object> &target);
    /** The creature masters an engaged exchange in a round that has not ended. */
    bool isRoundMaster(const Creature &creature) const;
    /** The action waits: its creature's scheduled entry holds it, or its round is suspended. */
    bool isActionPaused(const Action &action) const;
    /**
     * The target is engaged by the attacker: it is able to act, and it is
     * attempting to attack no one, or attempting to attack or cast at the
     * attacker. The controlled leader is engaged only while it acts against
     * the attacker and no one else, and the player has not ordered it onto
     * another target.
     */
    bool isEngagedBy(const Creature &attacker, const Creature &target) const;
    /**
     * Start a physical attack's exchange with its target and tell whether it
     * is engaged. An attacker that masters the exchange restarts its partner's
     * round: a round the partner began, or one of its own, ends. A round the
     * two share starts again from the beginning for both; a partner whose
     * round was its own gets a side, with no command yet, in the attacker's
     * round, held for the attack's pause and free to act as soon as that hold
     * ends. An engaged partner owning no round is held for the attack's pause.
     */
    bool engagePhysicalTarget(const Creature &attacker, Object &target, int pauseMilliseconds);
    /** The round the action runs in is engaged. */
    bool isRoundEngaged(const Action &action) const;
    /**
     * The creature has taken its attack, cast or item use in a round that has
     * not ended; its next round action waits for that end. It also waits while
     * a round that an earlier, finished command of the creature began is
     * paused, and while it is held without owning a round. \p self is left out.
     */
    bool awaitsRoundEnd(const Creature &actor, std::optional<std::reference_wrapper<const Action>> self) const;
    /** Whether the creature has a command in a round that has not ended. */
    bool ownsRound(const Creature &actor) const;
    /** The action carries out its creature's side of a round that has not ended. */
    bool runsRound(const Action &action) const;
    /** The creature owns a round that no action still waiting in its queue belongs to. */
    bool ownsDetachedRound(const Creature &actor) const;
    /** The creature takes part in an engaged (paired) round. */
    bool isEngaged(const Creature &creature) const;
    /** The creature's round leaves room for a reaction to an attack by \p attacker. */
    bool hasReactionRoom(const Creature &creature, const Creature &attacker) const;
    /**
     * Ask to settle the round of an attack whose pause has just ended. The
     * request is taken as soon as the attack has finished executing, still
     * inside its creature's update: when nothing holds the attacker's round
     * and it has expired or its target is down, it ends then, before the
     * events its last hits posted are delivered.
     */
    void requestSettle(const std::shared_ptr<Action> &action) { _settleRequests.push_back(action); }
    /** Settles the rounds requestSettle asked for. */
    void settleRequested();
    /**
     * Ends the creature's round: its commands leave their rounds, and, unless
     * \p runEndRound is zero, it runs its end-of-round script and fights on.
     */
    void endRound(Creature &actor, int runEndRound);
    /**
     * Cancels the creature's pending entries and the clearable commands of its
     * rounds. The rounds run on to their end, and their pairs stay.
     */
    void cancelActions(Object &actor);
    /**
     * Damage other than a weapon hit, just taken. Electrical or dark side
     * damage holds the victim's round for the reaction time and plays its
     * spasm; any other type makes it flinch.
     */
    void reactToDamage(Creature &victim, const std::shared_ptr<Object> &damager, const DamageReaction &reaction);
    /** The creature's round is held while it owns no round, or while it is knocked down. */
    bool isOwnerPaused(const Creature &actor) const;
    /**
     * A creature has been knocked down: until it is up again its own round
     * stands still and its commands and pending entries wait. The pauses it
     * had are replaced; a partner's round runs on.
     */
    void holdKnockedDown(const Creature &creature);
    /**
     * The attacker turns on a new enemy: it faces it, makes it its attempted
     * target, and the plain attacks still waiting on its round are aimed at it.
     */
    void takeNewAttackTarget(Creature &attacker, const std::shared_ptr<Creature> &target);
    size_t roundCount() const { return _rounds.size(); }

    // END Rounds

    // Scheduled entries

    /**
     * Takes an equip or unequip command onto the creature's round while it
     * fights in direct combat, or drops one that would change nothing. False
     * when the command goes to the queue as usual.
     */
    bool scheduleEquipment(Object &actor, const OrdinaryActionQueue::Node &node);
    /** Schedule a Total Defense stance on \p target on the creature's round. */
    bool scheduleStance(Creature &actor, const std::shared_ptr<Object> &target);
    /** Schedule a weapon swap on the creature's round, behind the commands already queued. */
    bool scheduleSwitchWeapons(Creature &actor);
    /**
     * Schedule a physical attack on the creature's round: kind 1, or 11 with a
     * feat. A cutscene attack carries its forced values and accepts any
     * target. Out of combat the controlled creature's other actions are
     * cleared, and the dispatcher is installed, even when the target cannot be
     * attacked. Returns the attack to come, or nothing when nothing was
     * scheduled: the target cannot be attacked or four entries are already
     * pending.
     */
    std::shared_ptr<Action> scheduleAttack(Creature &actor, const std::shared_ptr<Object> &target,
                                           FeatType feat = FeatType::Invalid,
                                           const std::optional<CutsceneAttack> &cutscene = std::nullopt);
    /**
     * Schedule a cutscene move on the creature's round: kind 12. When taken,
     * it goes to the object where it stands then, or to the point when the
     * object is gone, and is forced there after a tenth of a second. Out of
     * combat the controlled creature's other actions are cleared.
     */
    void scheduleCutsceneMove(Creature &actor, const std::shared_ptr<Object> &object, const glm::vec3 &point, bool run);
    /**
     * Add an ordinary attack entry (kind 1) to the creature's round, and
     * nothing else: no dispatcher is installed and nothing is cleared, so the
     * entry waits until a dispatcher heads the creature's queue. Returns the
     * attack to come, or nothing when nothing was added.
     */
    std::shared_ptr<Action> addRoundAttack(Creature &actor, const std::shared_ptr<Object> &target);
    /**
     * Schedule a spell cast or item use (a CastSpellAction) on the creature's
     * round: kind 9, or 10 for an item use. The dispatcher goes behind the
     * creature's actions if it is not queued. Out of combat an item use then
     * clears the controlled creature's other actions while an entry is due, a
     * cast while none is. Nothing is scheduled when four entries are already
     * pending.
     */
    void scheduleCast(Creature &actor, const std::shared_ptr<Action> &cast);
    /** The creature's ClearAllActions: its clearable actions go, its pending round entries stay. */
    void clearActions(Creature &actor);
    /**
     * The clear that scheduling a round command out of combat makes: the
     * controlled creature's forced ClearAllActions. Any other creature keeps
     * its actions.
     */
    void clearControlledActions(Creature &actor);
    /**
     * The clear the player's controls make: the creature's forced
     * ClearAllActions, then, even for one that cannot be commanded, its round's pending
     * entries, its round, attempted and current attack targets and its spell
     * targets.
     */
    void clearAllOrders(Creature &actor);
    /** Drop the most recently scheduled entry that has not started; false when there is none. */
    bool removeLastScheduled(Creature &actor);
    /** Drop every scheduled entry that has not started. */
    void removeAllScheduled(Creature &actor);
    /** The creature has scheduled round entries. */
    bool hasScheduled(const Object &actor) const;
    /** Fills \p entries with the creature's round entries not yet taken up, in order. */
    void pendingScheduled(const Object &actor, ISmallVector<PendingEntry> &entries) const;
    /**
     * The creature's round action under way: the scheduled entry holding its
     * round for its pause, or else the command its running round carries
     * out, of the kind of round entry it stands for: a cast, an item use, a
     * feat attack, Total Defense or an attack. None while nothing is under
     * way, and none for a round with no command of its own: a duel partner's
     * side, or a round restored from a save.
     */
    std::optional<PendingEntry> currentRoundAction(const Creature &actor) const;
    /** Installs the round dispatcher: in front of the queue, or behind it when toFront is false. */
    void ensureDispatcher(Creature &actor, bool toFront = true);
    /** Hands the creature's pending equipment entries on to its queue. */
    void transferEquipment(Creature &actor);
    /** Drops the creature's scheduled entries. */
    void discardEquipment(Object &actor);
    /** The creature's scheduled entry keeps its other commands waiting. */
    bool blocksOwnerActions(const Creature &actor) const;

    // END Scheduled entries

    // Persistence

    /** The clock of the round \p action runs in, kept with the command. */
    std::optional<SavedRoundClock> saveRound(const Action &action) const;
    /**
     * The creature's rounds that no command waiting in its queue carries: a
     * round still running after its command was cleared or finished, and a
     * side that has ended while its partner's runs on.
     */
    std::vector<SavedCreatureRound> saveRounds(const Object &actor) const;
    /** Restores the round of a queued command; the two sides of a shared round meet again. */
    void restoreRound(const std::shared_ptr<Action> &action, const std::shared_ptr<Creature> &actor, const SavedRoundClock &saved);
    /** Restores a round of the creature that no queued command carries; it comes back with no target. */
    void restoreRound(const std::shared_ptr<Creature> &actor, const SavedCreatureRound &saved);
    std::vector<SavedScheduledAction> saveScheduled(const Object &actor) const;
    /** The time on the creature's schedule, in seconds. */
    float scheduledTime(const Object &actor) const;
    /**
     * Restores the creature's scheduled entries; \p bound tells for each
     * whether the objects it names were found.
     */
    void restoreScheduled(const std::shared_ptr<Creature> &actor,
                          const std::vector<SavedScheduledAction> &records,
                          const std::vector<bool> &bound, float time);
    std::optional<OwnerHold> savedOwnerHold(const Object &actor) const;
    void restoreOwnerHold(const std::shared_ptr<Creature> &actor, float remaining,
                          const std::shared_ptr<Object> &pausedBy);

    // END Persistence

private:
    using RoundQueue = std::deque<std::shared_ptr<CombatRound>>;

    /** What taking up a due scheduled entry did with it. */
    enum class EntryTake {
        /** The entry left the schedule: handed on to its creature's queue, or used up. */
        Left,
        /** The entry holds its creature for its pause before it leaves. */
        Paused,
        /** The entry waits at the head until its creature may act. */
        Waiting
    };

    /** A scheduled round entry, with the command it runs once taken up. */
    struct ScheduledEntry {
        SavedScheduledAction saved;
        OrdinaryActionQueue::Node node;
        /** The objects the entry names were found when it was restored. */
        bool referencesBound {true};
        /** The power a cast entry casts; a save does not keep it, so a restored entry has none. */
        std::optional<SpellType> spell;
    };

    /** A creature's scheduled round entries and the time on its schedule. */
    struct ScheduledOwner {
        RuntimeObjectRef<Creature> actor;
        float time {0.0f};
        std::deque<std::shared_ptr<ScheduledEntry>> actions;
    };

    /** A creature's round pause while it owns no round. */
    struct OwnerPause {
        RuntimeObjectRef<Creature> actor;
        RuntimeObjectRef<Object> pausedBy;
        float remaining {0.0f};
        /** The engagement of an attack or cast began the hold, which a save keeps. */
        bool engaged {false};
    };

    Game &_game;
    ServicesView &_services;

    RoundQueue _rounds;
    uint64_t _nextRoundId {1};
    std::map<uint32_t, std::shared_ptr<ScheduledOwner>> _scheduled;
    /** The seconds each creature's scheduled entries took this frame. */
    std::map<uint32_t, float> _equipmentTime;
    std::map<uint32_t, OwnerPause> _ownerPauses;
    std::vector<std::weak_ptr<Action>> _settleRequests;

    /** The rounds a frame walks, kept from frame to frame. */
    std::vector<std::shared_ptr<CombatRound>> _roundsBuffer;
    /** The rounds pruneInvalidRounds() walks, kept from frame to frame. */
    std::vector<std::shared_ptr<CombatRound>> _pruneBuffer;
    /** The scheduled owners updateEquipment() walks, kept from frame to frame. */
    std::vector<std::shared_ptr<ScheduledOwner>> _ownersBuffer;

    /** Advances every creature's scheduled entries by \p dt seconds, dispatching those that come due. */
    void updateEquipment(float dt);
    /** Takes up the creature's due entry at the head of its schedule. */
    EntryTake takeEntry(Creature &actor, ScheduledOwner &owner, const std::shared_ptr<ScheduledEntry> &entry);
    /** Takes up a due attack entry, or uses it up when its target can no longer be attacked. */
    EntryTake takeAttack(Creature &actor, ScheduledOwner &owner, const std::shared_ptr<ScheduledEntry> &entry);
    /** Takes up a due cast entry, or uses it up when its cast can no longer run. */
    EntryTake takeCast(Creature &actor, ScheduledOwner &owner, const std::shared_ptr<ScheduledEntry> &entry);
    /**
     * Uses up an entry that can no longer be carried out. Its command has no
     * effect, but the entry becomes the round's action and takes its creature
     * out of stealth as a carried-out one would; an equip or unequip still
     * holds its creature for its pause.
     */
    EntryTake useUpEntry(Creature &actor, ScheduledOwner &owner, ScheduledEntry &entry);
    /**
     * Whether taking up \p entry, carried out or not, leaves its creature in
     * stealth: an item use does, and in TSL so do a stance and a cast of a
     * power that keeps stealth; every other entry takes it out.
     */
    bool entryKeepsStealth(const ScheduledEntry &entry) const;
    /** Starts the pause of an equip or unequip: its creature leaves stealth and stops. */
    void beginEquipmentPause(Creature &actor, ScheduledEntry &entry);
    /** A scheduled entry's pause has ended: \p actor, and the partner of a pair it masters, take up their pose. */
    void readyAfterScheduledPause(Creature &actor);
    /** The scheduled entries of \p actor, started on the time of its running round when there are none yet. */
    std::shared_ptr<ScheduledOwner> scheduledOwner(Creature &actor);
    /**
     * Adds \p entry to the creature's round and queues the round dispatcher,
     * at the front with \p dispatcherToFront; false when the round is full.
     */
    bool appendScheduled(Creature &actor, std::shared_ptr<ScheduledEntry> entry, bool dispatcherToFront);
    /** Add an entry to the creature's round, at most four pending; false when it is full. */
    bool addScheduled(Creature &actor, std::shared_ptr<ScheduledEntry> entry);
    /** The attack entry of a physical attack on \p target, or nothing when the target cannot be attacked. */
    std::shared_ptr<ScheduledEntry> makeAttackEntry(const std::shared_ptr<Object> &target, FeatType feat,
                                                    const std::optional<CutsceneAttack> &cutscene) const;
    /** Hands a combat stance or weapon switch entry to \p actor as its round action. */
    void dispatchScheduled(Creature &actor, const ScheduledEntry &entry);
    /** Puts the move of a cutscene move entry at the front of \p actor's queue. */
    void dispatchCutsceneMove(Creature &actor, const ScheduledEntry &entry);
    /**
     * Prepares an item use entry for dispatch: a hostile item spell takes its
     * user out of stealth, and a spell the hostile menu does not list, aimed
     * at another object, is used at a location instead. False when the use is
     * dropped because its creature target has died.
     */
    bool dispatchItemUse(Creature &actor, ScheduledEntry &entry);
    /**
     * Rescales a creature's round, \p round, and its scheduled entries to what
     * is left after \p milliseconds; with \p force the round may be shortened
     * to that point. The round of its partner is left alone.
     */
    void rescaleRound(CombatRound::RoundAction &round, int milliseconds, bool force);
    /**
     * The end of \p actor's round undoes the pairs it masters: each partner's
     * round loses its pause, whoever set it, and so does the hold of a partner
     * owning no round.
     */
    void releasePartner(Creature &actor);
    /**
     * Ends \p actor's round: its round target, spell target and movement are
     * reset, and with \p runEndRound its end-of-round script runs unless
     * \p suppressScript, and the leader's round end may pause play.
     */
    void finishOwner(Creature &actor, int runEndRound, bool suppressScript);
    /**
     * The controlled creature fights on into the next round with the command
     * of \p entry. A round that \p ranOut does so even when that command was
     * cleared.
     */
    void continueOwner(const CombatRound::RoundAction &entry, bool cutsceneRound, bool ranOut);
    /** Schedules the leader's next attack on \p target as an entry on its round. */
    void scheduleContinuation(Creature &actor, const std::shared_ptr<Object> &target);

    /** The saved clock of \p entry, \p round's side of its creature. */
    SavedRoundClock saveClock(const CombatRound &round, const CombatRound::RoundAction &entry) const;
    /**
     * Restores \p actor's side of a round with \p action, into the round of the
     * same id when its partner's side is already back; \p queued tells whether
     * the action waits in the creature's queue, and \p spellRound whether a
     * side without its command is a spell round.
     */
    void restoreEntry(const std::shared_ptr<Action> &action, const std::shared_ptr<Creature> &actor,
                      const std::shared_ptr<Object> &target, const SavedRoundClock &saved, bool queued,
                      bool spellRound);

    /** Makes the round of a cast a pair when the caster and its target are engaged. */
    void configureSpellPair(CombatRound &round, const std::shared_ptr<Action> &action, Creature &caster);
    /**
     * Advances the round of each creature of \p round by \p dt seconds, less
     * the time its scheduled entries took this frame; the round of a creature
     * held by a time stop stands still.
     */
    void updateRound(CombatRound &round, float dt);
    /**
     * Advances the round of the creature of entry \p index by \p dt seconds
     * and ends it when it has run out. A knocked-down creature's round stands
     * still. A round whose target is down also ends as a pause on it runs out,
     * or, with \p settling, as its attack's pause ends.
     */
    void updateEntry(CombatRound &round, size_t index, float dt, bool settling);
    /** Ends the round of the creature of entry \p index: its end-of-round script runs and it fights on. */
    void finishEntry(CombatRound &round, size_t index);
    /** Retires the round entries whose command or participants are gone, and drops empty rounds. */
    void pruneInvalidRounds();

    /** The round \p attacker takes \p action in, if any. */
    std::shared_ptr<CombatRound> findRoundForAction(
        const std::shared_ptr<Action> &action,
        const Creature &attacker) const;
    /**
     * Appends the action to a round whose roles it reverses: one its target
     * started against the attacker, which becomes a duel.
     */
    std::shared_ptr<CombatRound> tryAppendAction(
        const std::shared_ptr<Action> &action,
        const std::shared_ptr<Creature> &attacker,
        const std::shared_ptr<Object> &target,
        bool actorQueueAssociated);
};

} // namespace game

} // namespace reone
