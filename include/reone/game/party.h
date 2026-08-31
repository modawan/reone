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

#include "reone/game/galaxymapstate.h"
#include "reone/input/event.h"

#include <array>
#include <optional>
#include <tuple>

namespace reone {

namespace game {

class Creature;
class Game;
class Object;

enum class XPSource {
    Plot,
    Combat,
    Stealth,
    Console
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

    struct SavedDialogMessage {
        std::string speaker;
        std::string text;
    };

    struct SavedLogMessage {
        uint8_t color {0};
        uint32_t type {0};
        std::string text;
    };

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
        std::vector<SavedDialogMessage> dialogMessages;
        std::vector<SavedLogMessage> feedbackMessages;
        std::vector<SavedLogMessage> combatMessages;

        PersistedState() {
            npcSelectable.fill(true);
            influence.fill(-1);
            puppetSelectable.fill(true);
        }
    };

    struct Member {
        int npc {0};
        std::shared_ptr<Creature> creature;
    };

    Party(Game &game) :
        _game(game) {
    }

    bool handle(const input::Event &event);

    // Clear the player, party members, available NPCs and set all other fields
    // to their default values.
    void reset();

    // Retire instantiated creature bindings while preserving save-wide logical
    // state. A later runtime reconstruction phase can materialize those
    // bindings again from the committed working state.
    void retireRuntimeSession();

    void clear();
    void switchLeader();

    bool isEmpty() const;
    bool isSoloMode() const { return _solo; }

    int getSize() const;
    std::shared_ptr<Creature> getLeader() const;

    std::shared_ptr<Creature> player() const { return _player; }
    std::shared_ptr<Creature> actualPlayer() const { return _actualPlayer ? _actualPlayer : _player; }
    const std::vector<Member> &members() const { return _members; }

    const PersistedState &persistedState() const { return _persistedState; }

    /** Roster index of the actor standing in for the PC, or kNpcPlayer. */
    int controlledNpc() const { return _persistedState.controlledNpc; }

    /**
     * Hand control to a creature, leaving the rest of the party alone.
     *
     * Retail models temporary control as a roster NPC taking the player's
     * place: the outgoing actor is parked rather than demoted to a companion,
     * and the companions travelling with it are untouched. The incoming
     * creature occupies the leading slot exactly once, however it was
     * represented before.
     */
    void setControlledMember(int npc, const std::shared_ptr<Creature> &creature);
    void setPersistedState(PersistedState state);
    /** Retail LoadTableInfo semantics: persisted fields replace all bindings. */
    void loadPersistedState(PersistedState state);

    void setPartyLeader(int npc);
    void setPartyLeaderByIndex(int index);
    void setPlayer(const std::shared_ptr<Creature> &player);
    void setActualPlayer(const std::shared_ptr<Creature> &player) { _actualPlayer = player; }
    void setSoloMode(bool value) { _solo = value; }

    // Members

    /**
     * @param npc NPC number or kNpcPlayer for the player character
     */
    bool addMember(int npc, std::shared_ptr<Creature> creature);

    bool removeMember(int npc);

    /**
     * Persist and remove a TSL companion from the adventuring party before
     * its runtime representation is retired by RemoveNPCFromPartyToBase.
     * Logical availability and puppet assignment remain unchanged.
     */
    bool removeMemberToBase(int npc);

    bool isMember(int npc) const;
    bool isMember(const Object &object) const;

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
     * Retail AddNPC/AddPUP does not implicitly bind the supplied module object.
     */
    bool addAvailableRosterRecord(
        const RosterIdentity &identity,
        const std::shared_ptr<Creature> &creature);

    /** Copy an authored UTC into a detached persistent roster record. */
    bool addAvailableRosterRecord(
        const RosterIdentity &identity,
        const std::string &blueprint);

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
    bool clearRosterCreature(
        const RosterIdentity &identity,
        const Creature *expected = nullptr);
    bool clearRosterCreature(const Creature &creature);
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
    void giveGold(int amount);
    void takeGold(int amount);

    // END Credits

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
    /** Establish title-correct durable Party defaults for a fresh new game. */
    void initializeNewGameState();

    // Experience
    //
    // KOTOR stores experience as a single party-shared pool. XP awarded to a
    // party member feeds this pool; current members derive their creature XP
    // from it, and members added later are synced to it.

    int xp() const { return _xp; }

    /** Add to the shared pool and apply feedback appropriate to the award source. */
    void awardXP(int amount, XPSource source);
    void setXP(int xp);

    // END Experience

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

private:
    Game &_game;

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

    bool handleKeyDown(const input::KeyEvent &event);
    bool makeRosterAvailableAndBind(
        const RosterIdentity &identity,
        const std::shared_ptr<Creature> &creature);

    // Apply the party XP pool value to every current member's creature XP.
    void syncMembersXP();

    void onLeaderChanged();
};

} // namespace game

} // namespace reone
