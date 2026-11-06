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

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <glm/vec3.hpp>

#include "reone/game/galaxymapstate.h"
#include "reone/game/runtimeref.h"
#include "reone/input/event.h"

namespace reone {

namespace resource {
class TwoDA;
}

namespace game {

class Area;
class Creature;
class Game;
class Item;
class Object;

/** Party slots, the leader's included. */
constexpr int kPartyFollowSlots = 3;

enum class XPSource {
    Plot,
    Combat,
    Stealth,
    Console,
    Skill, /**< disarming and recovering mines */
    Script /**< a script award naming a creature outside the party, or a class gained */
};

enum class RosterKind {
    Npc,
    Puppet,
};

/** Save-wide identity of a companion record, independent of every ObjectId. */
struct RosterIdentity {
    RosterKind kind {RosterKind::Npc};
    int slot {-1};

    bool operator<(const RosterIdentity &rhs) const {
        return std::tie(kind, slot) < std::tie(rhs.kind, rhs.slot);
    }

    bool operator==(const RosterIdentity &rhs) const {
        return kind == rhs.kind && slot == rhs.slot;
    }

    bool operator!=(const RosterIdentity &rhs) const {
        return !operator==(rhs);
    }
};

/**
 * A party AI style (aiscripts.2da): its name, description and AI state. A
 * blank name or state reads as 0; a style without a description (TSL's table
 * has none) has none (-1).
 */
struct AIScriptRow {
    int nameStrRef {-1};
    int descriptionStrRef {-1};
    int state {-1};
};

class Party {
public:
    // PARTYTABLE.res stores one ownership count per card type plus one spare
    // entry: KotOR I ships eighteen card types, KotOR II twenty-three.
    static constexpr size_t kK1PazaakCardCount = 19;
    static constexpr size_t kK2PazaakCardCount = 24;
    static constexpr size_t kMaxPazaakCardCount = kK2PazaakCardCount;
    static constexpr size_t kK1PazaakSideDeckSize = 10;
    using PazaakCardCounts = std::array<int, kMaxPazaakCardCount>;
    using PazaakSideDeck = std::array<int, kK1PazaakSideDeckSize>;
    static constexpr size_t kK1NpcCount = 9;
    static constexpr size_t kK2NpcCount = 12;
    static constexpr size_t kMaxNpcCount = kK2NpcCount;
    static constexpr size_t kMaxPuppetCount = 3;
    static constexpr size_t kGalaxyPlanetCount = 16;
    /** PT_TUT_WND_SHOWN holds one bit per tutorial window: six bytes in KotOR, 33 in TSL. */
    static constexpr size_t kK1TutorialShownBytes = 6;
    static constexpr size_t kTutorialShownBytes = 33;
    // Forfeit conditions: no Force powers, no items, no item but a shield.
    /** The combat-mode line shown while the leader moves to attack its target. */
    static constexpr int kMovingToAttackCombatMessage = 42477;

    static constexpr int kForfeitNoForcePowers = 1;
    static constexpr int kForfeitNoItems = 2;
    static constexpr int kForfeitNoItemButShield = 128;
    /** The saved influence of a party NPC whose influence was never set. */
    static constexpr int kInfluenceNeverSet = -1;

    struct PersistedState {
        std::string pcName;
        uint32_t itemComponent {0};
        uint32_t itemChemical {0};
        std::array<uint32_t, 3> swoopUpgrades {};
        uint32_t playedSeconds {0};
        int controlledNpc {-1};
        bool soloMode {false};
        std::vector<int> memberIds;
        int leader {-1};
        std::vector<int> puppetIds;
        std::array<bool, kMaxNpcCount> npcAvailable {};
        std::array<bool, kMaxNpcCount> npcSelectable {};
        std::array<int, kMaxNpcCount> influence {};
        std::array<bool, kMaxPuppetCount> puppetAvailable {};
        std::array<bool, kMaxPuppetCount> puppetSelectable {};
        int aiState {0};
        int followState {0};
        uint32_t galaxyPointCount {0};
        std::array<bool, kGalaxyPlanetCount> planetAvailable {};
        std::array<bool, kGalaxyPlanetCount> planetSelectable {};
        int selectedPlanet {-1};
        bool mapDisabled {false};
        bool regenerationDisabled {false};
        /** The tutorial windows already shown. */
        std::set<int> tutorialsShown;
        /** The forfeit conditions scripts set, and the last one a party member broke (TSL). */
        int forfeitConditions {0};
        int forfeitViolation {0};

        PersistedState() {
            npcSelectable.fill(true);
            influence.fill(kInfluenceNeverSet);
            puppetSelectable.fill(true);
        }
    };

    struct Member {
        int npc {0};
        std::shared_ptr<Creature> creature;
        RuntimeObjectRef<Object> lastTarget;
        /** The member's last combat-mode line; none is the idle line. */
        std::optional<int> combatMessage;
    };

    Party(Game &game) :
        _game(game) {
        resetFollowSlots();
    }

    /**
     * Read the party's tables: each roster slot's share of experience
     * (npc.2da), the party AI styles (aiscripts.2da) and, in TSL, the
     * companions' base influence (influence.2da).
     */
    void init();

    bool handle(const input::Event &event);

    // Clear the player, party members, available NPCs and set all other fields
    // to their default values.
    void reset();

    // Retire instantiated creature bindings while preserving save-wide logical
    // state. A later runtime reconstruction phase can materialize those
    // bindings again from the committed working state.
    void retireRuntimeSession();

    void clear();
    /** The next member takes the lead; the one leading goes to the back. */
    void switchLeader(bool sound = true);
    /**
     * Rotate the lead, up to three times, to the next member neither dead nor
     * downed. True when the lead moved; the select sound then plays once.
     */
    bool changeToNextLivingMember(bool sound);
    /**
     * Hands control to the next member standing, as the change-character key
     * does; with no one else standing, control stays where it is.
     */
    bool selectNextStandingMember();

    bool isEmpty() const;
    bool isSoloMode() const { return _solo; }

    int getSize() const;
    std::shared_ptr<Creature> getLeader() const;

    /** The combat-mode line shown while no other combat message is being presented. */
    int idleCombatMessage() const;
    /**
     * Post a member's combat-mode line. Each member keeps its last line; only
     * the controlled member's line is presented.
     *
     * @param creature the member the line is about
     * @param strref the line's string reference
     */
    void setCombatMessage(const Creature &creature, int strref);

    std::shared_ptr<Creature> player() const { return _player; }
    std::shared_ptr<Creature> actualPlayer() const { return _actualPlayer ? _actualPlayer : _player; }
    /** The player character's name as saves record it. */
    std::string playerCharacterName() const;
    const std::vector<Member> &members() const { return _members; }
    /** The leader's slot remembers the current target; the pass writes it every frame. */
    void setLeaderLastTarget(RuntimeObjectRef<Object> target) {
        if (!_members.empty()) {
            _members.front().lastTarget = std::move(target);
        }
    }

    const PersistedState &persistedState() const { return _persistedState; }
    /** The party-wide AI style scripts set and read. */
    int aiStyle() const { return _persistedState.aiState; }
    void setAIStyle(int style) { _persistedState.aiState = style; }
    bool isHealthRegenerationDisabled() const { return _persistedState.regenerationDisabled; }
    void setHealthRegenerationDisabled(bool disabled) { _persistedState.regenerationDisabled = disabled; }
    bool isTutorialShown(int id) const { return _persistedState.tutorialsShown.count(id) != 0; }
    void setTutorialShown(int id) { _persistedState.tutorialsShown.insert(id); }
    int forfeitConditions() const { return _persistedState.forfeitConditions; }
    void setForfeitConditions(int conditions) { _persistedState.forfeitConditions = conditions; }
    int lastForfeitViolation() const { return _persistedState.forfeitViolation; }
    void setLastForfeitViolation(int condition) { _persistedState.forfeitViolation = condition; }

    /**
     * A companion's base influence by its tag (influence.2da, TSL); none for a
     * tag the table does not name, and always none in KotOR, which has no
     * influence.
     */
    std::optional<int> baseInfluence(const std::string &tag) const;
    /** The npc.2da share of experience of a row, read as a whole percentage; a blank cell, or a row outside the table, is none. */
    int percentXP(int row) const;
    /**
     * The npc.2da share of experience of a row as the cell reads, fractions
     * kept, as kill experience reads its general share; a blank cell, or a
     * row outside the table, is none.
     */
    float percentXPFraction(int row) const;
    /** The party AI styles, in table order. */
    const std::vector<AIScriptRow> &aiScripts() const { return _aiScripts; }

    /** Whether npc names a party NPC slot. */
    static bool isNpcSlot(int npc) { return npc >= 0 && npc < static_cast<int>(kMaxNpcCount); }
    /** A party NPC's influence (TSL); none while never set. */
    std::optional<int> influence(int npc) const {
        const int value = _persistedState.influence.at(static_cast<size_t>(npc));
        return value == kInfluenceNeverSet ? std::nullopt : std::optional<int>(value);
    }
    void setInfluence(int npc, int value) { _persistedState.influence.at(static_cast<size_t>(npc)) = value; }

    /** Roster index of the actor standing in for the PC, or kNpcPlayer. */
    int controlledNpc() const { return _persistedState.controlledNpc; }

    /**
     * Hand control to a creature.
     *
     * Temporary control is a roster NPC taking the player's place: the
     * outgoing actor leaves the party rather than being demoted to a
     * companion. The incoming creature occupies the leading slot exactly
     * once, however it was represented before, and becomes a player
     * character. The character switch takes the outgoing actor out of the
     * world and removes the other companions before handing control over.
     */
    void setControlledMember(int npc, const std::shared_ptr<Creature> &creature);
    void setPersistedState(PersistedState state);
    /** Persisted party-table fields replace all bindings. */
    void loadPersistedState(PersistedState state);

    /**
     * Hand the lead to a member. For kNpcPlayer a downed player character is
     * stood up first and made raiseable, even when it already leads.
     *
     * @param npc roster NPC number, or kNpcPlayer for the player character
     * @return false when an NPC number names no member, true otherwise
     */
    bool setPartyLeader(int npc);
    /**
     * Hand the lead to the first member that is a player character; nothing
     * changes when it already leads or no member is one.
     */
    void leadWithPlayerCharacter();
    /**
     * Hand the lead to the member at an index: the members ahead of it move
     * to the back in their order.
     */
    void setPartyLeaderByIndex(int index);
    void setPlayer(const std::shared_ptr<Creature> &player);
    void setActualPlayer(const std::shared_ptr<Creature> &player) { _actualPlayer = player; }
    /** Leaving solo mode ends the party's stealth. */
    void setSoloMode(bool value);
    /** End stealth for the controlled creature and the other members. */
    void endStealth();
    /** Members other than the player's own creature. */
    int companionCount() const;
    /** Companions travelling with the party: neither the player character nor a companion standing in for it. */
    bool isCompanion(const Member &member) const;

    // Members

    /**
     * @param npc NPC number or kNpcPlayer for the player character
     */
    bool addMember(int npc, std::shared_ptr<Creature> creature);

    bool removeMember(int npc);

    bool isMember(int npc) const;
    bool isMember(const Object &object) const;
    /**
     * Whether the object moves the party between modules: only the player
     * character or the current leader does.
     */
    bool movesBetweenModules(const Object &object) const;
    /**
     * Whether a companion travels with the party as a follower. A follower
     * whose creature was destroyed still does: its place stays, naming
     * nothing. Neither the player character nor a companion standing in for
     * it is a follower.
     */
    bool isFollower(int npc) const;

    /**
     * Whether this exact Creature is retained by Party/session lifetime across
     * a module boundary instead of belonging to the outgoing module graph.
     */
    bool isRetainedRuntimeRepresentation(const Creature &creature) const;

    std::shared_ptr<Creature> getMemberByNPC(int npc) const;
    std::shared_ptr<Creature> getMember(int index) const;
    int getNPCByMemberIndex(int index) const;

    // END Members

    // Roster state and runtime bindings

    /** Whether this title owns the supplied logical roster slot. */
    bool isRosterIdentityValid(const RosterIdentity &identity) const;

    /** Persistent PartyTable availability; independent of runtime binding. */
    bool isRosterAvailable(const RosterIdentity &identity) const;
    bool setRosterAvailable(
        const RosterIdentity &identity,
        bool available,
        bool selectableWhenAdded = true);

    /** Persistent PartyTable selection policy for an available slot. */
    bool isRosterSelectable(const RosterIdentity &identity) const;
    bool setRosterSelectable(
        const RosterIdentity &identity,
        bool selectable);

    /**
     * Add or replace the detached persistent record from a live creature.
     * This does not implicitly bind the supplied module object.
     */
    bool addAvailableRosterRecord(
        const RosterIdentity &identity,
        const std::shared_ptr<Creature> &creature);

    /** Copy an authored UTC into a detached persistent roster record. */
    bool addAvailableRosterRecord(
        const RosterIdentity &identity,
        const std::string &blueprint);

    /**
     * A new persistent record replaced the slot's character: drop the runtime
     * creature still bound to the slot (unless it is in the party or is the
     * player), so the next spawn materializes the new record. The dropped
     * creature is destroyed unless it stands in the current area.
     */
    void retireStaleRosterBinding(
        const RosterIdentity &identity,
        const Creature *keep = nullptr);

    bool addAvailableMember(int npc, const std::string &blueprint);
    /**
     * Runtime-construction helper: make a slot available and bind this exact
     * representation. Script AddAvailableNPCByObject deliberately uses
     * addAvailableRosterRecord instead and does not bind its source object.
     */
    bool addAvailableMember(int npc, std::shared_ptr<Creature> creature);
    bool removeAvailableMember(int npc);

    bool isMemberAvailable(int npc) const;
    std::shared_ptr<Creature> getAvailableMember(int npc) const;
    std::shared_ptr<Creature> getAvailableMember(
        int npc, bool loadIfMissing);

    // END Available members

    // Available puppets

    /** K2 counterpart of the runtime-construction helper above. */
    bool addAvailablePuppet(int puppet, std::shared_ptr<Creature> creature);
    std::shared_ptr<Creature> getAvailablePuppet(int puppet) const;
    std::shared_ptr<Creature> getAvailablePuppet(
        int puppet, bool loadIfMissing);

    /**
     * Publish one materialized creature as the sole runtime binding for a
     * logical roster slot. Existing active-member views of that slot follow
     * the binding; another logical slot may never bind the same creature.
     */
    bool bindRosterCreature(
        const RosterIdentity &identity,
        const std::shared_ptr<Creature> &creature);
    /**
     * The creature standing in the area for TSL companion slot npc, found by the
     * companion's tag, becomes that slot's creature. Returns it; null when no
     * creature with the companion's tag stands in the area.
     */
    std::shared_ptr<Creature> bindPlacedCompanion(int npc, const Area &area);
    bool clearRosterCreature(
        const RosterIdentity &identity,
        const Creature *expected = nullptr);
    bool clearRosterCreature(const Creature &creature);
    /**
     * The creature's member entries keep their places but name nothing, as
     * when the creature is taken out of the world while still in the party.
     * An entry naming nothing in the leading place leaves no one leading.
     */
    void vacateMemberEntries(const Creature &creature);
    /** The creature's member entries leave the party, the places behind them moving up. */
    void removeMemberEntries(const Creature &creature);
    /**
     * The roster slot naming the creature forgets it; the party's follower and
     * puppet lists stay as they are. A companion under control that is
     * forgotten leaves no one under control.
     */
    bool releaseRosterSlot(const Creature &creature);
    /**
     * Forming the party anew brings each follower whose place names nothing
     * back from its roster record, in that place.
     */
    void respawnVacantFollowers();
    /** Rebuilding the party drops the entries that name nothing. */
    void removeVacantMembers();
    std::shared_ptr<Creature> rosterCreature(
        const RosterIdentity &identity) const;
    std::shared_ptr<Creature> rosterCreature(
        const RosterIdentity &identity,
        bool loadIfMissing);
    std::optional<RosterIdentity> rosterIdentity(
        const Creature &creature) const;

    /** K2 active-puppet and assignment operations. */
    bool addPuppet(int puppet, const std::shared_ptr<Creature> &creature);
    bool removePuppet(int puppet);
    bool isPuppet(int puppet) const;
    bool assignPuppet(int puppet, int npc);
    std::optional<int> assignedNpcForPuppet(int puppet) const;
    std::shared_ptr<Creature> puppetOwner(int puppet) const;
    /** The owner of the puppet the creature is bound to, or none. */
    std::shared_ptr<Creature> puppetOwner(const Creature &puppet) const;

    /** Complete live object graph retained by the session across Areas. */
    std::vector<std::shared_ptr<Object>> runtimeObjects() const;

    // END Available puppets

    // Default party

    void defaultMembers(std::string &member1, std::string &member2, std::string &member3) const;

    // END Default party

    // Credits
    //
    // KOTOR stores credits as a single party-shared pool, not per creature.
    // Gold script routines (GetGold/GiveGoldToCreature/TakeGoldFromCreature)
    // that target a party member operate on this pool.

    int gold() const { return _gold; }
    /** The party's credits rise by amount, up to 999999999. */
    void giveGold(int amount);
    /** The party's credits fall by amount, down to none. */
    void takeGold(int amount);

    /** A creature's credits: the shared pool for a party member, its own otherwise. */
    int creatureGold(const Creature &creature) const;
    /**
     * Credits a creature gains, up to 999999999, or loses, down to none. The
     * creature the player controls is told the change. Returns the change.
     */
    int addCreatureGold(Creature &creature, int amount);
    int removeCreatureGold(Creature &creature, int amount);

    // END Credits

    /**
     * Credits, chemicals (TSL), components (TSL) and pazaak cards a creature
     * acquires are counted, not kept.
     */
    bool isCountedItem(const Item &item) const;
    /**
     * A creature acquires a counted item: credits become its credits,
     * chemicals and components the party's counts, and pazaak cards join the
     * party's collection. The caller discards the item.
     */
    void acquireCountedItem(Creature &acquirer, const Item &item);

    // Pazaak state stored in PARTYTABLE.res. The final ownership slot is retained
    // verbatim even though side decks only use the card-type IDs before it.
    bool hasValidPazaakData() const { return _pazaakDataValid; }
    const PazaakCardCounts &pazaakCardCounts() const { return _pazaakCardCounts; }
    /// Number of ownership entries the loaded table actually carries.
    size_t pazaakCardCount() const { return _pazaakCardCount; }
    const PazaakSideDeck &pazaakSideDeck() const { return _pazaakSideDeck; }
    void setPazaakData(
        PazaakCardCounts counts,
        PazaakSideDeck sideDeck,
        size_t cardCount = kK1PazaakCardCount);
    void setPazaakSideDeck(PazaakSideDeck sideDeck);
    /** Takes up to count cards of one kind from the collection. */
    void removePazaakCards(int card, int count);
    /** Adds count cards of one kind to the collection. */
    void addPazaakCards(int card, int count);
    /** Establish title-correct durable Party defaults for a fresh new game. */
    void initializeNewGameState();

    // Experience
    //
    // The party pool records every award at its face value. Each award pays
    // the companions travelling with the party and the creature under the
    // player's control their own share (npc.2da PercentXP); roster companions
    // who were away catch up with the pool when they are brought into the
    // world.

    int xp() const { return _xp; }

    /**
     * Record an award in the pool and pay it, with the TSL companion bonus,
     * to the travelling companions and the creature under the player's
     * control; feedback and the Status Summary follow the award source.
     */
    void awardXP(int amount, XPSource source);
    /** The TSL mentor's bonus on an award, paid to the recipients but not recorded in the pool. */
    int companionXPBonus(int amount) const;
    /** Set the pool; current members take it as their experience. */
    void setXP(int xp);
    /**
     * A roster companion brought into the world is paid up to its joining
     * experience plus its share of the pool.
     */
    void catchUpExperience(int npc, Creature &creature);

    // END Experience

    /**
     * In TSL a roster companion brought into the world for the party joins
     * the player character's faction.
     */
    void spawnIntoPlayerFaction(Creature &creature) const;

    // Galaxy map
    //
    // Planet availability, selectability and the current travel destination
    // are party-wide runtime state, carried in PARTYTABLE.

    GalaxyMapState &galaxyMap() { return _galaxyMap; }
    const GalaxyMapState &galaxyMap() const { return _galaxyMap; }

    // END Galaxy map

    // Inventory
    //
    // KOTOR keeps a single shared party inventory, modelled here as the player
    // creature's item list. Non-equipped items acquired by any party member
    // belong to that shared inventory.

    // Returns the object that should receive a newly acquired, non-equipped
    // item: the player creature when the intended receiver is a party member,
    // otherwise the receiver unchanged (so non-party inventories stay separate).
    std::shared_ptr<Object> sharedInventoryReceiver(const std::shared_ptr<Object> &receiver) const;

    // END Inventory

    // Follow path

    /**
     * The leader's walked trail: the last hundred points it moved through
     * with its facing, a point dropped when it moved under half a metre in a
     * clear line, and the last clear point added when the way from the
     * previous one is blocked. Once a follower falls seven metres behind, the
     * trail from the nearest follower on is pulled straight where it can be
     * walked and laid out again a metre apart. Each follower keeps a follow
     * point a follow range back along the trail from the leader, the first
     * follower a metre nearer and the second a metre further, never behind its
     * previous one.
     */
    void recordLeaderStep(const Area &area, const glm::vec3 &position, float facing);
    /**
     * Start the trail over at a point: every follow point becomes that point
     * and members standing where they do not fit move to the nearest spot
     * that does. Seeding first lays the followers' spots behind it.
     */
    void resetFollowPath(Area &area, const glm::vec3 &position, float facing, bool seed);
    /**
     * Outside conversations, once the furthest follower falls seven metres
     * behind, the follow points are recomputed; when the nearest follower
     * faces against the leader's way, the formation also swaps sides.
     */
    void updateFollowPath();
    /**
     * The way a following member faces: where it last moved, or the leader
     * it turned to. Its model turns to it, and the party turning back is
     * noticed from it.
     */
    void noteFollowerFacing(const Creature &follower, const glm::vec3 &forward);
    const glm::vec3 &followerFacing(int slot) const { return _followSlots[slot].lastFacing; }
    const glm::vec3 &followPoint(int slot) const { return _followSlots[slot].point; }
    /** Where a slot stands relative to the leader, in the leader's frame with forward along +Y. */
    const glm::vec3 &formationOffset(int slot) const { return _followSlots[slot].formationOffset; }
    /**
     * Where a follower stands at its follow point: its formation offset turned
     * to the facing of the trail step the point lies on.
     */
    glm::vec3 formationSpot(int slot) const;

    // END Follow path

private:
    /** The leader's trail keeps this many steps. */
    static constexpr int kFollowSteps = 100;

    /** How a straight walk along the leader's trail goes. */
    enum class TrailLine {
        Clear,
        /** Something stands in the way. */
        Blocked,
        /** The walk runs out of the rooms before anything stands in its way. */
        LeavesRooms
    };

    /** A point of the leader's trail and the way the leader faced there. */
    struct FollowStep {
        glm::vec3 position {0.0f};
        float facing {0.0f};
    };

    /** A party slot's place in the formation. */
    struct FollowSlot {
        glm::vec3 point {0.0f};
        /** The trail step the point lies on. */
        int index {0};
        glm::vec3 formationOffset {0.0f};
        glm::vec3 lastFacing {0.0f, 1.0f, 0.0f};
    };

    Game &_game;

    // Tables

    /** The npc.2da share of each row, read as a whole number; 0 where the cell is blank. */
    std::vector<int> _percentXP;
    /** The npc.2da share of each row, read with its fraction; 0 where the cell is blank. */
    std::vector<float> _percentXPFraction;
    std::vector<AIScriptRow> _aiScripts;
    /** Each companion's base influence by its lower-case tag (TSL). */
    std::map<std::string, int> _baseInfluence;

    // END Tables

    std::shared_ptr<Creature> _player;
    std::shared_ptr<Creature> _actualPlayer;
    std::map<int, std::shared_ptr<Creature>> _npcBindings;
    std::vector<Member> _members;
    bool _solo {false};
    int _gold {0};
    int _xp {0};
    GalaxyMapState _galaxyMap;
    bool _pazaakDataValid {false};
    size_t _pazaakCardCount {kK1PazaakCardCount};
    PazaakCardCounts _pazaakCardCounts {};
    PazaakSideDeck _pazaakSideDeck {};
    PersistedState _persistedState;
    std::map<int, std::shared_ptr<Creature>> _puppetBindings;

    // Follow path

    /** The trail, a ring of steps: the next step is written at the head. */
    std::array<FollowStep, kFollowSteps> _followSteps {};
    int _followHead {0};
    int _followCount {0};
    /** The last point the leader reached in a clear line from the trail. */
    glm::vec3 _lastClearStep {0.0f};
    std::array<FollowSlot, kPartyFollowSlots> _followSlots {};

    // END Follow path

    bool handleKeyDown(const input::KeyEvent &event);
    /**
     * The roster slot forgets its creature, when it binds the expected one or
     * no creature is expected. Returns the creature released.
     */
    std::shared_ptr<Creature> releaseRosterSlot(
        const RosterIdentity &identity,
        std::optional<std::reference_wrapper<const Creature>> expected);
    bool makeRosterAvailableAndBind(
        const RosterIdentity &identity,
        const std::shared_ptr<Creature> &creature);

    // Apply the party XP pool value to every current member's creature XP.
    void syncMembersXP();
    /** Pay a creature its share of an award. */
    void receiveExperience(Creature &creature, int amount);
    /** A companion added to the roster starts from the pool. */
    void applyJoiningExperience(int npc, Creature &creature);
    /**
     * A companion added to the roster joins the player character's faction;
     * a puppet joins the party puppet faction.
     */
    void joinRosterFaction(const RosterIdentity &identity, Creature &creature) const;
    void transferInventory(Creature &creature);

    void saveLeaderAttackTarget();
    void onLeaderChanged(const std::shared_ptr<Creature> &previous, bool sound = true);
    void playLeaderSelectSound();

    void resetFollowSlots();
    int newestStep() const { return _followHead > 0 ? _followHead - 1 : kFollowSteps - 1; }
    static int stepAfter(int step) { return step > kFollowSteps - 2 ? 0 : step + 1; }
    static int stepBefore(int step) { return step > 0 ? step - 1 : kFollowSteps - 1; }
    /** How far behind the leader the followers keep, by the party's follow state. */
    float followRange() const;
    /**
     * The point a distance back along the trail from the leader, not behind
     * the slot's own step, and the step it lies on. False for a trail too short.
     */
    bool pointBackFromStart(float distance, int slot, glm::vec3 &point, int &step) const;
    /**
     * Lay each follower slot's point back along the trail. A slot whose point
     * cannot be laid out takes \p sightedStep as its step when one is given.
     */
    void recalculateFollowPoints(std::optional<int> sightedStep = std::nullopt);
    /** The follower slot whose point lies furthest back along the trail. */
    int furthestFollower() const;
    /** The follower slot whose point lies nearest the leader along the trail. */
    int closestFollower() const;
    /** Squared distance from the leader to the slot's follow point. */
    float followerLag2(int slot) const;
    /**
     * A straight walk the trail can take: clear, or blocked only by a party
     * member. How the walk goes is kept in line.
     */
    bool isTrailLineClear(
        const Area &area, const Creature &walker, const glm::vec3 &from, const glm::vec3 &to, TrailLine &line) const;
    void smoothFollowPath(const Area &area);
    bool turnBack();
};

} // namespace game

} // namespace reone
