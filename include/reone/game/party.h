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

#include "reone/input/event.h"

#include <array>

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

    void clear();
    void switchLeader();

    bool isEmpty() const;
    bool isSoloMode() const { return _solo; }

    int getSize() const;
    std::shared_ptr<Creature> getLeader() const;

    std::shared_ptr<Creature> player() const { return _player; }
    const std::vector<Member> &members() const { return _members; }

    const PersistedState &persistedState() const { return _persistedState; }
    void setPersistedState(PersistedState state);

    void setPartyLeader(int npc);
    void setPartyLeaderByIndex(int index);
    void setPlayer(const std::shared_ptr<Creature> &player);
    void setSoloMode(bool value) { _solo = value; }

    // Members

    /**
     * @param npc NPC number or kNpcPlayer for the player character
     */
    bool addMember(int npc, std::shared_ptr<Creature> creature);

    bool removeMember(int npc);

    bool isMember(int npc) const;
    bool isMember(const Object &object) const;

    std::shared_ptr<Creature> getMemberByNPC(int npc) const;
    std::shared_ptr<Creature> getMember(int index) const;
    int getNPCByMemberIndex(int index) const;

    // END Members

    // Available members

    bool addAvailableMember(int npc, const std::string &blueprint);
    bool addAvailableMember(int npc, std::shared_ptr<Creature> creature);
    bool removeAvailableMember(int npc);

    bool isMemberAvailable(int npc) const;

    std::shared_ptr<Creature> getAvailableMember(int npc) const;

    // END Available members

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
    /// Authored starting collection: two copies each of +1 through +5.
    void setDefaultPazaakData(size_t cardCount = kK1PazaakCardCount);

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
    std::map<int, std::shared_ptr<Creature>> _availableMembers;
    std::vector<Member> _members;
    bool _solo {false};
    int _gold {0};
    int _xp {0};
    bool _pazaakDataValid {false};
    size_t _pazaakCardCount {kK1PazaakCardCount};
    PazaakCardCounts _pazaakCardCounts {};
    PazaakSideDeck _pazaakSideDeck {};
    PersistedState _persistedState;

    bool handleKeyDown(const input::KeyEvent &event);

    // Apply the party XP pool value to every current member's creature XP.
    void syncMembersXP();

    void onLeaderChanged();
};

} // namespace game

} // namespace reone
