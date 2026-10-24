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

#include "reone/game/party.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <glm/gtc/quaternion.hpp>

#include "reone/audio/mixer.h"
#include "reone/game/d20/spells.h"
#include "reone/game/di/services.h"
#include "reone/game/effect/resurrection.h"
#include "reone/game/equipmentrules.h"
#include "reone/game/game.h"
#include "reone/game/gui/sounds.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/twodautil.h"
#include "reone/game/types.h"
#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/system/logutil.h"
#include "reone/system/randomutil.h"

namespace reone {

namespace game {

/** The script a new leader runs when the lead changes hands. */
static constexpr char kLeaderChangedScript[] = "k_hen_leadchng";

static constexpr int kPlayerPerceptionRangeRow = 12;
static constexpr int kPartyPerceptionRangeRow = 11;

static constexpr int kExperienceGainedStrRef = 42438;

// npc.2da: the companions' rows come first, then the share every recipient of
// an award takes. Shares are percentages.
static constexpr int kK1GeneralXPRow = 9;
static constexpr int kTSLGeneralXPRow = 13;
static constexpr int kFullShare = 100;

static constexpr char kBlueprintResRefCarth[] = "p_carth";
static constexpr char kBlueprintResRefBastila[] = "p_bastilla";
static constexpr char kBlueprintResRefAtton[] = "p_atton";
static constexpr char kBlueprintResRefKreia[] = "p_kreia";

void Party::setPersistedState(PersistedState state) {
    _solo = state.soloMode;
    _persistedState = std::move(state);
}

void Party::loadPersistedState(PersistedState state) {
    // Loading the party table clears transient object bindings before applying
    // persisted fields. Active module references are bound separately.
    for (const auto &[_, puppet] : _puppetBindings) {
        if (puppet) puppet->setPuppet(false);
    }
    _members.clear();
    _npcBindings.clear();
    _puppetBindings.clear();
    setPersistedState(std::move(state));
}

void Party::setPazaakData(
    PazaakCardCounts counts,
    PazaakSideDeck sideDeck,
    size_t cardCount) {

    _pazaakCardCounts = std::move(counts);
    _pazaakSideDeck = std::move(sideDeck);
    _pazaakCardCount = std::min(cardCount, kMaxPazaakCardCount);
    _pazaakDataValid = true;
}

void Party::setPazaakSideDeck(PazaakSideDeck sideDeck) {
    _pazaakSideDeck = std::move(sideDeck);
}

void Party::removePazaakCards(int card, int count) {
    // A card outside the collection takes nothing from it.
    if (card < 0 || static_cast<size_t>(card) >= _pazaakCardCount) {
        return;
    }
    auto &owned = _pazaakCardCounts[static_cast<size_t>(card)];
    owned -= std::min(owned, count);
}

void Party::addPazaakCards(int card, int count) {
    _pazaakCardCounts[static_cast<size_t>(card)] += count;
}

void Party::initializeNewGameState() {
    // Both titles start with two copies each of +1 through +5 and an empty
    // side deck, but their PARTYTABLE ownership arrays have different sizes.
    PazaakCardCounts counts {};
    counts[0] = counts[1] = counts[2] = counts[3] = counts[4] = 2;
    PazaakSideDeck sideDeck;
    sideDeck.fill(-1);
    setPazaakData(
        std::move(counts),
        std::move(sideDeck),
        _game.isTSL() ? kK2PazaakCardCount : kK1PazaakCardCount);
}

bool Party::handle(const input::Event &event) {
    if (event.type == input::EventType::KeyDown) {
        return handleKeyDown(event.key);
    }

    return false;
}

bool Party::handleKeyDown(const input::KeyEvent &event) {
    if (event.repeat)
        return false;

    switch (event.code) {
    case input::KeyCode::Tab:
        // The change-character key sounds the accepted cue whether or not
        // the lead moved.
        selectNextStandingMember();
        if (auto clip = _game.services().game.guiSounds.getActionAccepted()) {
            _game.services().audio.mixer.play(std::move(clip), audio::AudioType::Sound);
        }
        return true;
    }

    return false;
}

bool Party::addAvailableMember(int npc, const std::string &blueprint) {
    return addAvailableRosterRecord(
        {RosterKind::Npc, npc}, blueprint);
}

// Joining the roster puts a companion on the player's movement row, and a
// puppet on the Very Fast row.
static constexpr int kPlayerMovementRate = 0;
static constexpr int kPuppetMovementRate = 6;

bool Party::addAvailableMember(int npc, std::shared_ptr<Creature> creature) {
    if (!makeRosterAvailableAndBind({RosterKind::Npc, npc}, creature)) {
        return false;
    }
    creature->setMovementRate(kPlayerMovementRate);
    return true;
}

bool Party::removeAvailableMember(int npc) {
    return setRosterAvailable({RosterKind::Npc, npc}, false);
}

bool Party::addAvailablePuppet(int puppet, std::shared_ptr<Creature> creature) {
    if (!makeRosterAvailableAndBind({RosterKind::Puppet, puppet}, creature)) {
        return false;
    }
    creature->setMovementRate(kPuppetMovementRate);
    return true;
}

std::shared_ptr<Creature> Party::getAvailablePuppet(int puppet) const {
    if (!isRosterAvailable({RosterKind::Puppet, puppet})) {
        return nullptr;
    }
    auto found = _puppetBindings.find(puppet);
    return found == _puppetBindings.end() ? nullptr : found->second;
}

std::shared_ptr<Creature> Party::getAvailablePuppet(
    int puppet, bool loadIfMissing) {
    return rosterCreature({RosterKind::Puppet, puppet}, loadIfMissing);
}

std::shared_ptr<Creature> Party::rosterCreature(
    const RosterIdentity &identity) const {
    const auto &bindings = identity.kind == RosterKind::Npc
                               ? _npcBindings
                               : _puppetBindings;
    auto found = bindings.find(identity.slot);
    return found == bindings.end() ? nullptr : found->second;
}

std::shared_ptr<Creature> Party::rosterCreature(
    const RosterIdentity &identity,
    bool loadIfMissing) {
    if (!isRosterAvailable(identity)) {
        return nullptr;
    }
    auto creature = rosterCreature(identity);
    return creature || !loadIfMissing
               ? creature
               : _game.materializeRosterCreature(identity);
}

std::optional<RosterIdentity> Party::rosterIdentity(
    const Creature &creature) const {
    for (const auto &[slot, bound] : _npcBindings) {
        if (bound.get() == &creature) {
            return RosterIdentity {RosterKind::Npc, slot};
        }
    }
    for (const auto &[slot, bound] : _puppetBindings) {
        if (bound.get() == &creature) {
            return RosterIdentity {RosterKind::Puppet, slot};
        }
    }
    return std::nullopt;
}

std::vector<std::shared_ptr<Object>> Party::runtimeObjects() const {
    std::vector<std::shared_ptr<Object>> result;
    std::set<const Object *> seen;
    std::function<void(const std::shared_ptr<Object> &)> append;
    append = [&](const std::shared_ptr<Object> &object) {
        if (!object || !seen.insert(object.get()).second) return;
        result.push_back(object);
        for (const auto &item : object->items()) append(item);
        if (object->type() != ObjectType::Creature) return;
        auto creature = std::static_pointer_cast<Creature>(object);
        for (const auto &[_, item] : creature->equipment()) append(item);
    };

    append(_player);
    append(_actualPlayer);
    for (const auto &member : _members) append(member.creature);
    for (const auto &[_, creature] : _npcBindings) append(creature);
    for (const auto &[_, creature] : _puppetBindings) append(creature);
    return result;
}

bool Party::isRosterIdentityValid(const RosterIdentity &identity) const {
    if (identity.slot < 0) return false;
    if (identity.kind == RosterKind::Puppet) {
        return _game.isTSL() &&
               identity.slot < static_cast<int>(kMaxPuppetCount);
    }
    const size_t npcCount = _game.isTSL() ? kK2NpcCount : kK1NpcCount;
    return identity.slot < static_cast<int>(npcCount);
}

bool Party::isRosterAvailable(const RosterIdentity &identity) const {
    if (!isRosterIdentityValid(identity)) return false;
    return identity.kind == RosterKind::Npc
               ? _persistedState.npcAvailable[identity.slot]
               : _persistedState.puppetAvailable[identity.slot];
}

bool Party::setRosterAvailable(
    const RosterIdentity &identity,
    bool available,
    bool selectableWhenAdded) {
    if (!isRosterIdentityValid(identity)) return false;
    bool &current = identity.kind == RosterKind::Npc
                        ? _persistedState.npcAvailable[identity.slot]
                        : _persistedState.puppetAvailable[identity.slot];
    if (!available && !current) return false;
    current = available;
    if (available && selectableWhenAdded) {
        bool &selectable = identity.kind == RosterKind::Npc
                               ? _persistedState.npcSelectable[identity.slot]
                               : _persistedState.puppetSelectable[identity.slot];
        selectable = true;
    }
    return true;
}

bool Party::isRosterSelectable(const RosterIdentity &identity) const {
    if (!isRosterAvailable(identity)) return false;
    return identity.kind == RosterKind::Npc
               ? _persistedState.npcSelectable[identity.slot]
               : _persistedState.puppetSelectable[identity.slot];
}

bool Party::setRosterSelectable(
    const RosterIdentity &identity,
    bool selectable) {
    // Ignore selectability updates for invalid or unavailable roster slots.
    if (!isRosterAvailable(identity)) return false;
    bool &current = identity.kind == RosterKind::Npc
                        ? _persistedState.npcSelectable[identity.slot]
                        : _persistedState.puppetSelectable[identity.slot];
    current = selectable;
    return true;
}

bool Party::makeRosterAvailableAndBind(
    const RosterIdentity &identity,
    const std::shared_ptr<Creature> &creature) {
    if (!creature || !isRosterIdentityValid(identity)) return false;
    bool &available = identity.kind == RosterKind::Npc
                          ? _persistedState.npcAvailable[identity.slot]
                          : _persistedState.puppetAvailable[identity.slot];
    bool &selectable = identity.kind == RosterKind::Npc
                           ? _persistedState.npcSelectable[identity.slot]
                           : _persistedState.puppetSelectable[identity.slot];
    const bool previousAvailable = available;
    const bool previousSelectable = selectable;
    if (identity.kind == RosterKind::Npc && !previousAvailable) {
        applyJoiningExperience(identity.slot, *creature);
    }
    if (!setRosterAvailable(identity, true) ||
        !bindRosterCreature(identity, creature)) {
        available = previousAvailable;
        selectable = previousSelectable;
        return false;
    }
    joinRosterFaction(identity, *creature);
    if (identity.kind == RosterKind::Npc) {
        transferInventory(*creature);
    }
    return true;
}

bool Party::addAvailableRosterRecord(
    const RosterIdentity &identity,
    const std::shared_ptr<Creature> &creature) {
    if (!creature || !isRosterIdentityValid(identity)) return false;
    if (identity.kind == RosterKind::Npc && !isRosterAvailable(identity)) {
        applyJoiningExperience(identity.slot, *creature);
    }
    joinRosterFaction(identity, *creature);
    if (identity.kind == RosterKind::Npc) {
        transferInventory(*creature);
    }
    creature->setMovementRate(identity.kind == RosterKind::Npc ? kPlayerMovementRate : kPuppetMovementRate);
    // A companion joining the roster takes up the alignment its influence gives.
    if (identity.kind == RosterKind::Npc) {
        creature->recomputeInfluenceAlignment();
    }
    try {
        _game.saveRosterState(identity, *creature);
    } catch (const std::exception &e) {
        warn("Party: could not persist roster record: " +
             std::string(e.what()));
        return false;
    }
    return setRosterAvailable(identity, true);
}

bool Party::addAvailableRosterRecord(
    const RosterIdentity &identity,
    const std::string &blueprint) {
    if (!isRosterIdentityValid(identity)) return false;
    std::shared_ptr<Creature> creature;
    try {
        creature = _game.newCreatureFromBlueprint(blueprint);
        // A companion without a template is not added.
        if (!creature) {
            return false;
        }
        if (identity.kind == RosterKind::Npc && !isRosterAvailable(identity)) {
            applyJoiningExperience(identity.slot, *creature);
        }
        joinRosterFaction(identity, *creature);
        if (identity.kind == RosterKind::Npc) {
            transferInventory(*creature);
        }
        creature->setMovementRate(identity.kind == RosterKind::Npc ? kPlayerMovementRate : kPuppetMovementRate);
        if (identity.kind == RosterKind::Npc) {
            creature->recomputeInfluenceAlignment();
        }
        _game.saveRosterState(identity, *creature);
    } catch (const std::exception &e) {
        if (creature) _game.destroyRuntimeObjectGraph(creature);
        warn("Party: could not add roster blueprint '" + blueprint +
             "': " + e.what());
        return false;
    }
    _game.destroyRuntimeObjectGraph(creature);
    return setRosterAvailable(identity, true);
}

std::shared_ptr<Creature> Party::bindPlacedCompanion(int npc, const Area &area) {
    // The tag by which each TSL companion slot finds its creature in an area.
    static const std::array<const char *, kK2NpcCount> kCompanionTags {
        "atton", "baodur", "mand", "g0t0", "handmaiden", "hk47",
        "kreia", "mira", "t3m4", "visasmarr", "hanharr", "disciple"};
    const std::string tag = kCompanionTags.at(static_cast<size_t>(npc));
    for (const auto &object : area.objects()) {
        if (object->type() == ObjectType::Creature && boost::iequals(object->tag(), tag)) {
            auto creature = std::static_pointer_cast<Creature>(object);
            bindRosterCreature({RosterKind::Npc, npc}, creature);
            return creature;
        }
    }
    return nullptr;
}

bool Party::bindRosterCreature(
    const RosterIdentity &identity,
    const std::shared_ptr<Creature> &creature) {
    if (!creature || !_game.isRuntimeObjectLive(*creature) ||
        !isRosterAvailable(identity)) {
        return false;
    }
    if (creature == _actualPlayer ||
        (creature == _player &&
         (identity.kind != RosterKind::Npc ||
          _persistedState.controlledNpc != identity.slot))) {
        warn("Party: canonical player cannot be claimed by a roster slot");
        return false;
    }

    auto existingIdentity = rosterIdentity(*creature);
    if (existingIdentity && *existingIdentity != identity) {
        warn("Party: creature is already bound to another roster slot");
        return false;
    }
    auto previous = rosterCreature(identity);
    if (identity.kind == RosterKind::Npc &&
        _persistedState.controlledNpc == identity.slot && _player &&
        _player != previous && _player != creature) {
        warn("Party: controlled roster binding contradicts the current player");
        return false;
    }

    if (identity.kind == RosterKind::Npc) {
        _npcBindings.insert_or_assign(identity.slot, creature);
        if (creature->assignedPuppet() < -1 ||
            creature->assignedPuppet() >= static_cast<int>(kMaxPuppetCount)) {
            creature->setAssignedPuppet(-1);
        }
        // An entry left naming nothing stays so; rebinding the slot does not
        // fill it.
        for (auto &member : _members) {
            if (member.npc == identity.slot && member.creature) {
                member.creature = creature;
            }
        }
        if (_persistedState.controlledNpc == identity.slot &&
            (!_player || _player == previous || _player == creature)) {
            _player = creature;
        }
    } else {
        if (previous && previous != creature) {
            previous->setPuppet(false);
        }
        creature->setPuppet(true);
        _puppetBindings.insert_or_assign(identity.slot, creature);
    }
    return true;
}

std::shared_ptr<Creature> Party::releaseRosterSlot(
    const RosterIdentity &identity,
    std::optional<std::reference_wrapper<const Creature>> expected) {
    if (!isRosterIdentityValid(identity)) {
        return nullptr;
    }
    auto &bindings = identity.kind == RosterKind::Npc
                         ? _npcBindings
                         : _puppetBindings;
    auto found = bindings.find(identity.slot);
    if (found == bindings.end() ||
        (expected && found->second.get() != &expected->get())) {
        return nullptr;
    }
    auto removed = found->second;
    bindings.erase(found);
    // Clearing the slot releases only its creature: the controlled index
    // stays, and when that creature was the one under control nobody is
    // controlled afterwards.
    if (identity.kind == RosterKind::Npc && _player == removed) {
        _player.reset();
    }
    return removed;
}

bool Party::releaseRosterSlot(const Creature &creature) {
    auto identity = rosterIdentity(creature);
    return identity && releaseRosterSlot(*identity, creature);
}

bool Party::clearRosterCreature(
    const RosterIdentity &identity,
    const Creature *expected) {
    auto removed = releaseRosterSlot(
        identity, expected ? std::optional<std::reference_wrapper<const Creature>>(*expected) : std::nullopt);
    if (!removed) {
        return false;
    }
    if (identity.kind == RosterKind::Npc) {
        removeMemberEntries(*removed);
    } else {
        removed->setPuppet(false);
        _persistedState.puppetIds.erase(
            std::remove(
                _persistedState.puppetIds.begin(),
                _persistedState.puppetIds.end(), identity.slot),
            _persistedState.puppetIds.end());
    }
    return true;
}

bool Party::clearRosterCreature(const Creature &creature) {
    auto identity = rosterIdentity(creature);
    return identity && clearRosterCreature(*identity, &creature);
}

void Party::vacateMemberEntries(const Creature &creature) {
    for (auto &member : _members) {
        if (member.creature.get() == &creature) {
            member.creature.reset();
            member.lastTarget.reset();
        }
    }
}

void Party::removeMemberEntries(const Creature &creature) {
    _members.erase(
        std::remove_if(
            _members.begin(), _members.end(),
            [&creature](const Member &member) {
                return member.creature.get() == &creature;
            }),
        _members.end());
}

// A follower brought back perceives at the range of its place. Placing the
// party then spawns it as it does every other companion.
void Party::respawnVacantFollowers() {
    for (auto &member : _members) {
        if (member.creature || !isCompanion(member)) {
            continue;
        }
        if (auto creature = rosterCreature({RosterKind::Npc, member.npc}, true)) {
            creature->setPerceptionRangeRow(&member == &_members.front() ? kPlayerPerceptionRangeRow
                                                                         : kPartyPerceptionRangeRow);
            member.creature = std::move(creature);
            member.creature->refreshDeath();
        }
    }
}

void Party::removeVacantMembers() {
    _members.erase(
        std::remove_if(
            _members.begin(), _members.end(),
            [](const Member &member) { return !member.creature; }),
        _members.end());
}

bool Party::addMember(int npc, std::shared_ptr<Creature> creature) {
    // A follower whose creature was destroyed keeps its place and cannot join again.
    if (!creature || isMember(npc) || isFollower(npc) || isMember(*creature)) {
        return false;
    }
    if (npc != kNpcPlayer) {
        const auto activeCompanions = std::count_if(
            _members.begin(), _members.end(), [this](const Member &member) {
                return member.npc != kNpcPlayer &&
                       member.npc != _persistedState.controlledNpc;
            });
        if (!isRosterAvailable({RosterKind::Npc, npc}) ||
            (npc != _persistedState.controlledNpc && activeCompanions >= 2) ||
            !bindRosterCreature({RosterKind::Npc, npc}, creature)) {
            return false;
        }
    }
    if (npc != kNpcPlayer) {
        catchUpExperience(npc, *creature);
    }
    // A first companion joining a stealthed leader puts the party in solo mode.
    const bool firstCompanion = npc != kNpcPlayer && npc != _persistedState.controlledNpc && companionCount() == 0;
    if (firstCompanion && !_members.empty() && _members.front().creature && _members.front().creature->isStealthed()) {
        _solo = true;
    }

    Member member;
    member.npc = npc;
    member.creature = creature;
    _members.push_back(std::move(member));
    creature->refreshDeath();
    // A companion's carried items join the party's; then a member whose body
    // armour was refused on load joins in the party's clothing.
    if (npc != kNpcPlayer) {
        transferInventory(*creature);
    }
    creature->forceEquipClothing();
    // A joining member perceives at the party range; a first one leads.
    creature->setPerceptionRangeRow(_members.size() == 1 ? kPlayerPerceptionRangeRow : kPartyPerceptionRangeRow);

    if (_game.isTSL() && npc != kNpcPlayer &&
        npc != _persistedState.controlledNpc &&
        creature->assignedPuppet() >= 0) {
        const int puppet = creature->assignedPuppet();
        try {
            if (auto runtimePuppet = getAvailablePuppet(puppet, true)) {
                addPuppet(puppet, runtimePuppet);
            }
        } catch (const std::exception &e) {
            warn("Party: could not materialize assigned puppet " +
                 std::to_string(puppet) + ": " + e.what());
        }
    }

    return true;
}

void Party::setSoloMode(bool value) {
    if (!value) {
        endStealth();
    }
    _solo = value;
}

void Party::endStealth() {
    for (const auto &member : _members) {
        if (member.creature) {
            member.creature->setStealthMode(false);
        }
    }
}

bool Party::isCompanion(const Member &member) const {
    return member.npc != kNpcPlayer && member.npc != _persistedState.controlledNpc;
}

int Party::companionCount() const {
    return static_cast<int>(std::count_if(_members.begin(), _members.end(), [this](const Member &member) {
        return isCompanion(member);
    }));
}

std::string Party::playerCharacterName() const {
    if (!_persistedState.pcName.empty()) {
        return _persistedState.pcName;
    }
    auto player = actualPlayer();
    return player ? player->name() : std::string();
}

void Party::reset() {
    retireRuntimeSession();
    _solo = false;
    _gold = 0;
    _xp = 0;
    _galaxyMap.clear();
    _pazaakDataValid = false;
    _pazaakCardCounts.fill(0);
    _pazaakSideDeck.fill(-1);
    _persistedState = PersistedState();
    resetFollowSlots();
}

void Party::retireRuntimeSession() {
    for (const auto &[_, puppet] : _puppetBindings) {
        if (puppet) puppet->setPuppet(false);
    }
    _player.reset();
    _actualPlayer.reset();
    _npcBindings.clear();
    _members.clear();
    _puppetBindings.clear();
}

void Party::clear() {
    _members.clear();
}

static constexpr int kMaxGold = 999999999;
static constexpr int kGoldGainedStrRef = 1493;
static constexpr int kGoldLostStrRef = 1494;
static constexpr int kComponentsGainedStrRef = 128117;
static constexpr int kChemicalsGainedStrRef = 128118;
static constexpr char kPazaakSideboardResRef[] = "g_i_pazsidebd001";

// A count raised by amount, up to 999999999.
static int raisedCount(int count, int amount) {
    return static_cast<int>(std::min<int64_t>(static_cast<int64_t>(count) + amount, kMaxGold));
}

void Party::giveGold(int amount) {
    _gold = raisedCount(_gold, amount);
}

void Party::takeGold(int amount) {
    _gold = std::max(_gold - amount, 0);
}

int Party::creatureGold(const Creature &creature) const {
    return isMember(creature) ? _gold : creature.gold();
}

int Party::addCreatureGold(Creature &creature, int amount) {
    const int before = creatureGold(creature);
    const int after = raisedCount(before, amount);
    const int change = after - before;
    if (isMember(creature)) {
        giveGold(change);
    } else {
        creature.giveGold(change);
    }
    if (change != 0 && getLeader().get() == &creature) {
        _game.addFeedbackMessage(kGoldGainedStrRef, {{0, std::to_string(change)}});
    }
    return change;
}

int Party::removeCreatureGold(Creature &creature, int amount) {
    const int before = creatureGold(creature);
    const int after = std::max(before - amount, 0);
    const int change = before - after;
    if (isMember(creature)) {
        takeGold(change);
    } else {
        creature.takeGold(change);
    }
    if (change != 0 && getLeader().get() == &creature) {
        _game.addFeedbackMessage(kGoldLostStrRef, {{0, std::to_string(change)}});
    }
    return change;
}

// The collection slot a pazaak card item joins, from its model variation. KotOR
// maps every variation onto its eighteen cards; TSL maps the first eighteen
// the same way, the five that follow onto its further cards, and any other to
// the first slot.
static int acquiredPazaakCardSlot(int variation, bool tsl) {
    static constexpr int kCardCount = 18;
    if (!tsl || (variation >= 1 && variation <= kCardCount)) {
        return (variation + 11) % kCardCount;
    }
    static constexpr int kFurtherSlots[] = {22, 19, 18, 20, 21};
    if (variation > kCardCount && variation <= kCardCount + 5) {
        return kFurtherSlots[variation - kCardCount - 1];
    }
    return 0;
}

bool Party::isCountedItem(const Item &item) const {
    switch (item.itemType()) {
    case Item::kCreditsItemType:
    case Item::kPazaakCardItemType:
        return true;
    case Item::kChemicalsItemType:
    case Item::kComponentsItemType:
        return _game.isTSL();
    default:
        return false;
    }
}

// A party member's chemicals and components are the party's counts; the
// creature the player controls is told the change. The acquirer of a pazaak
// card is given a sideboard to hold the collection when it has none.
void Party::acquireCountedItem(Creature &acquirer, const Item &item) {
    const int amount = item.stackSize();
    auto raisePartyCount = [&](uint32_t &count, int strRef) {
        if (!isMember(acquirer)) {
            return;
        }
        const int before = static_cast<int>(count);
        const int after = raisedCount(before, amount);
        count = static_cast<uint32_t>(after);
        if (after != before && getLeader().get() == &acquirer) {
            _game.addFeedbackMessage(strRef, {{0, std::to_string(after - before)}});
        }
    };
    switch (item.itemType()) {
    case Item::kCreditsItemType:
        addCreatureGold(acquirer, amount);
        break;
    case Item::kChemicalsItemType:
        raisePartyCount(_persistedState.itemChemical, kChemicalsGainedStrRef);
        break;
    case Item::kComponentsItemType:
        raisePartyCount(_persistedState.itemComponent, kComponentsGainedStrRef);
        break;
    case Item::kPazaakCardItemType: {
        addPazaakCards(acquiredPazaakCardSlot(item.modelVariation(), _game.isTSL()), amount);
        const bool hasSideboard = std::any_of(acquirer.items().begin(), acquirer.items().end(),
                                              [](const std::shared_ptr<Item> &held) { return held->itemType() == Item::kPazaakSideboardItemType; });
        if (!hasSideboard) {
            acquirer.addItem(kPazaakSideboardResRef, 1, std::nullopt);
        }
        break;
    }
    default:
        break;
    }
}

// TSL: the first companion in the party with feat 202 adds 3%, 5% or 7% of
// an award by its own level (1-6, 7-12, 13 and up), rounded up.
int Party::companionXPBonus(int amount) const {
    if (!_game.isTSL()) {
        return 0;
    }
    for (const auto &member : _members) {
        if (!isCompanion(member) || !member.creature) {
            continue;
        }
        const auto &attributes = member.creature->attributes();
        if (!attributes.hasFeat(FeatType::Mentor)) {
            continue;
        }
        const int level = attributes.getAggregateLevel();
        if (level <= 0) {
            return 0;
        }
        const float rate = level > 12 ? 0.07f : (level > 6 ? 0.05f : 0.03f);
        return static_cast<int>(std::ceil(static_cast<float>(amount) * rate));
    }
    return 0;
}

void Party::awardXP(int amount, XPSource source) {
    // Only positive awards are distributed.
    if (amount <= 0) {
        return;
    }
    // The pool takes the award at face value; the companion bonus is paid to
    // the recipients only.
    _xp += amount;
    const int paid = amount + companionXPBonus(amount);
    // The followers are paid, then the creature under the player's control:
    // a companion controlled in the player character's place is paid in its
    // stead.
    for (const auto &member : _members) {
        if (isCompanion(member) && member.creature) {
            receiveExperience(*member.creature, paid);
        }
    }
    if (_player) {
        receiveExperience(*_player, paid);
    }
    // Every award but kill experience is announced with the amount paid.
    if (source != XPSource::Combat) {
        _game.addFeedbackMessage(kExperienceGainedStrRef, {{0, std::to_string(paid)}});
    }

    switch (source) {
    case XPSource::Plot:
    case XPSource::Console:
        _game.submitStatusSummary(StatusSummaryCategory::PlotXP, amount);
        break;
    case XPSource::Stealth:
        _game.submitStatusSummary(StatusSummaryCategory::StealthXP, amount);
        break;
    case XPSource::Combat:
    case XPSource::Skill:
    case XPSource::Script:
        break;
    }
}

void Party::setXP(int xp) {
    _xp = xp;
    syncMembersXP();
}

void Party::init() {
    auto &twoDas = _game.services().resource.twoDas;

    auto npcs = getRequiredTwoDA(twoDas, "npc");
    _percentXP.clear();
    _percentXPFraction.clear();
    for (int row = 0; row < npcs->getRowCount(); ++row) {
        _percentXP.push_back(npcs->getInt(row, "percentxp", 0));
        _percentXPFraction.push_back(npcs->getFloat(row, "percentxp", 0.0f));
    }

    auto scripts = getRequiredTwoDA(twoDas, "aiscripts");
    _aiScripts.clear();
    for (int row = 0; row < scripts->getRowCount(); ++row) {
        AIScriptRow script;
        script.nameStrRef = scripts->getInt(row, "name_strref", 0);
        script.descriptionStrRef = scripts->getInt(row, "description_strref", script.descriptionStrRef);
        script.state = scripts->getInt(row, "aistate", 0);
        _aiScripts.push_back(script);
    }

    // Influence is TSL's alone.
    _baseInfluence.clear();
    if (_game.isTSL()) {
        auto influence = getRequiredTwoDA(twoDas, "influence");
        for (int row = 0; row < influence->getRowCount(); ++row) {
            // The first row of a tag is the companion's.
            _baseInfluence.emplace(boost::to_lower_copy(influence->getString(row, "tag")), influence->getInt(row, "baseinfluence"));
        }
    }
}

std::optional<int> Party::baseInfluence(const std::string &tag) const {
    const auto it = _baseInfluence.find(boost::to_lower_copy(tag));
    return it != _baseInfluence.end() ? std::optional<int>(it->second) : std::nullopt;
}

// A blank or missing share pays nothing.
int Party::percentXP(int row) const {
    if (row < 0 || row >= static_cast<int>(_percentXP.size())) {
        return 0;
    }
    return _percentXP[row];
}

float Party::percentXPFraction(int row) const {
    if (row < 0 || row >= static_cast<int>(_percentXPFraction.size())) {
        return 0.0f;
    }
    return _percentXPFraction[row];
}

// A roster companion takes its npc.2da share and every recipient the
// general share, rounded up. TSL pays nothing to creatures tagged 204_b4d4 or
// remote, and keeps G_PC_LEVEL at the level the player character's
// experience reaches.
void Party::receiveExperience(Creature &creature, int amount) {
    if (_game.isTSL() &&
        (boost::iequals(creature.tag(), "204_b4d4") || boost::iequals(creature.tag(), "remote"))) {
        return;
    }
    auto identity = rosterIdentity(creature);
    float share = identity && identity->kind == RosterKind::Npc ? percentXP(identity->slot) / static_cast<float>(kFullShare) : 1.0f;
    share *= percentXP(_game.isTSL() ? kTSLGeneralXPRow : kK1GeneralXPRow) / static_cast<float>(kFullShare);
    creature.giveXP(static_cast<int>(std::ceil(static_cast<float>(amount) * share)));
    if (_game.isTSL() && creature.isPlayerCreated()) {
        _game.setGlobalNumber("G_PC_LEVEL", creature.potentialLevel());
    }
}

// The share of the pool a roster addition or catch-up is measured against;
// a zero entry counts as the whole pool.
static int rosterPercent(int percent) {
    return percent != 0 ? percent : kFullShare;
}

// The experience a level takes grows by this much per level: a character of
// level n has at least 500 * n * (n - 1).
static constexpr int kLevelXPStep = 500;

void Party::applyJoiningExperience(int npc, Creature &creature) {
    const int percent = rosterPercent(percentXP(npc));
    const int share = static_cast<int>(percent / 100.0f * static_cast<float>(_xp));
    const int level = creature.attributes().getAggregateLevel();
    // The experience a character of this level has at least.
    const int levelXP = kLevelXPStep * level * (level - 1);
    if (_game.isTSL()) {
        creature.setJoiningXP(0);
        receiveExperience(creature, levelXP - share > 0 ? levelXP - share : share);
    } else {
        creature.setJoiningXP(share >= levelXP ? 0 : levelXP - share);
    }
}

// A creature joining the party, or made available to it, hands its credits
// and carried items to the party; what it wears stays on it, and its own
// credit count is left as it was.
void Party::transferInventory(Creature &creature) {
    auto receiver = actualPlayer();
    _gold += creature.gold();
    const auto carried = creature.items();
    for (const auto &item : carried) {
        transferItemTo(_game, item, *receiver);
    }
}

void Party::joinRosterFaction(const RosterIdentity &identity, Creature &creature) const {
    if (identity.kind == RosterKind::Puppet) {
        creature.setFaction(Faction::PartyPuppet);
        return;
    }
    if (_player) {
        creature.setFaction(_player->faction());
    }
}

void Party::spawnIntoPlayerFaction(Creature &creature) const {
    if (_game.isTSL() && _player) {
        creature.setFaction(_player->faction());
    }
}

void Party::catchUpExperience(int npc, Creature &creature) {
    const int percent = rosterPercent(percentXP(npc));
    const int earned = creature.xp() - creature.joiningXP();
    if (earned >= static_cast<int>(static_cast<float>(_xp) * (percent / 100.0f))) {
        return;
    }
    receiveExperience(creature, _xp - static_cast<int>(static_cast<float>(earned) / static_cast<float>(percent) * 100.0f));
}

void Party::syncMembersXP() {
    // Set each member's creature XP to the pool value rather than adding, so the
    // award is not double-counted and members converge on the shared total.
    for (auto &member : _members) {
        if (member.creature) {
            member.creature->setXP(_xp);
        }
    }
}

static constexpr int kK1IdleCombatMessageStrRef = 48208;
static constexpr int kTSLIdleCombatMessageStrRef = 111303;

int Party::idleCombatMessage() const {
    return _game.isTSL() ? kTSLIdleCombatMessageStrRef : kK1IdleCombatMessageStrRef;
}

void Party::setCombatMessage(const Creature &creature, int strref) {
    auto member = std::find_if(_members.begin(), _members.end(),
                               [&creature](const Member &entry) { return entry.creature.get() == &creature; });
    if (member == _members.end()) {
        return;
    }
    member->combatMessage = strref;
    if (member == _members.begin()) {
        _game.presentCombatMessage(strref);
    }
}

void Party::switchLeader(bool sound) {
    if (_members.size() <= 1) {
        return;
    }

    saveLeaderAttackTarget();

    Member tmp(_members[0]);
    _members.erase(_members.begin());
    _members.push_back(tmp);

    onLeaderChanged(tmp.creature, sound);
}

bool Party::changeToNextLivingMember(bool sound) {
    if (_members.empty()) {
        return false;
    }
    const auto start = _members.front().creature;
    auto isDown = [](const Creature &creature) { return creature.isDead() || creature.isTemporarilyDead(); };
    // From a living leader the rotation only comes back round to it.
    const bool startDown = isDown(*start);
    int tries = 3;
    bool reached = false;
    while (!reached && tries > 0) {
        switchLeader(false);
        // An entry naming nothing cannot take the lead.
        const auto &leader = _members.front().creature;
        if (!leader || isDown(*leader)) {
            --tries;
            continue;
        }
        reached = startDown || leader == start;
    }
    if (!reached || _members.front().creature == start) {
        return false;
    }
    if (sound) {
        playLeaderSelectSound();
    }
    return true;
}

bool Party::selectNextStandingMember() {
    auto isDown = [](const Creature &creature) { return creature.isDead() || creature.isTemporarilyDead(); };
    for (size_t index = 1; index < _members.size(); ++index) {
        const auto &creature = _members[index].creature;
        if (!creature || isDown(*creature)) {
            continue;
        }
        for (size_t turn = 0; turn < index; ++turn) {
            switchLeader(false);
        }
        playLeaderSelectSound();
        return true;
    }
    return false;
}

void Party::saveLeaderAttackTarget() {
    auto &member = _members.front();
    member.lastTarget = member.creature ? member.creature->getAttackTarget() : nullptr;
}

void Party::onLeaderChanged(const std::shared_ptr<Creature> &previous, bool sound) {
    // The new leader perceives at the player range and takes over what the
    // old one perceived; the old one looks around again at the party range.
    // The lead changes only within a module's area.
    auto &area = *_game.module()->area();
    const auto &leader = _members[0].creature;
    if (leader && previous && leader != previous) {
        leader->setPerceptionRangeRow(kPlayerPerceptionRangeRow);
        previous->setPerceptionRangeRow(kPartyPerceptionRangeRow);
        leader->takePerception(*previous);
        area.perceiveNow(previous);
    }
    // A new leader starts the trail over where it stands.
    if (leader && leader != previous) {
        resetFollowPath(area, leader->position(), leader->getFacing(), true);
        _game.scriptRunner().run(kLeaderChangedScript, leader->id());
    }

    // A dead or downed leader makes no select sound, and nor does a place
    // that names no one.
    if (sound && leader && !leader->isDead() && !leader->isTemporarilyDead()) {
        playLeaderSelectSound();
    }
    auto lastTarget = _members[0].lastTarget.resolve();
    _game.setLastTarget(lastTarget ? lastTarget->id() : script::kObjectInvalid);
    // A new leader in combat mode shows its own last combat-mode line again.
    const auto &combatMessage = _members[0].combatMessage;
    if (leader && leader->clientCombatMode() && combatMessage && *combatMessage != idleCombatMessage()) {
        _game.presentCombatMessage(*combatMessage);
    }

    // The new leader stops following; everyone else carries on.
    if (leader) {
        leader->removeActionsOfType(ActionType::FollowLeader);
    }

    area.onPartyLeaderMoved(true);
}

void Party::playLeaderSelectSound() {
    auto entry = static_cast<resource::SoundSetEntry>(static_cast<int>(resource::SoundSetEntry::Select1) + randomInt(0, 2));
    _members[0].creature->playSound(entry);
}

std::shared_ptr<Creature> Party::getAvailableMember(int npc) const {
    if (!isMemberAvailable(npc)) {
        return nullptr;
    }
    auto member = _npcBindings.find(npc);
    if (member == _npcBindings.end()) return nullptr;
    return member->second;
}

std::shared_ptr<Creature> Party::getAvailableMember(
    int npc, bool loadIfMissing) {
    return rosterCreature({RosterKind::Npc, npc}, loadIfMissing);
}

std::shared_ptr<Creature> Party::getMember(int index) const {
    return _members.size() > index ? _members[index].creature : nullptr;
}

std::shared_ptr<Creature> Party::getMemberByNPC(int npc) const {
    for (auto &member : _members) {
        if (member.npc == npc && member.creature) {
            return member.creature;
        }
    }
    return nullptr;
}

int Party::getNPCByMemberIndex(int index) const {
    return _members.size() > index ? _members[index].npc : -1;
}

bool Party::isEmpty() const {
    return _members.empty();
}

int Party::getSize() const {
    return static_cast<int>(_members.size());
}

// An entry naming nothing makes no one a member.
bool Party::isMember(int npc) const {
    for (auto &member : _members) {
        if (member.npc == npc && member.creature) {
            return true;
        }
    }
    return false;
}

bool Party::isFollower(int npc) const {
    return std::any_of(_members.begin(), _members.end(), [this, npc](const Member &member) {
        return member.npc == npc && isCompanion(member);
    });
}

bool Party::isMemberAvailable(int npc) const {
    return isRosterAvailable({RosterKind::Npc, npc});
}

bool Party::addPuppet(
    int puppet,
    const std::shared_ptr<Creature> &creature) {
    if (!isRosterAvailable({RosterKind::Puppet, puppet}) || !creature ||
        isPuppet(puppet) || _persistedState.puppetIds.size() >= 2 ||
        !bindRosterCreature({RosterKind::Puppet, puppet}, creature)) {
        return false;
    }
    creature->setPuppet(true);
    _persistedState.puppetIds.push_back(puppet);
    return true;
}

bool Party::removePuppet(int puppet) {
    auto found = std::find(
        _persistedState.puppetIds.begin(),
        _persistedState.puppetIds.end(), puppet);
    if (found == _persistedState.puppetIds.end()) return false;
    if (auto creature = rosterCreature({RosterKind::Puppet, puppet})) {
        // A puppet leaving the party has its effects cleared before it is saved.
        creature->clearAllEffects();
        try {
            // Snapshot the puppet before retiring its transient object binding.
            _game.saveRosterState({RosterKind::Puppet, puppet}, *creature);
        } catch (const std::exception &e) {
            warn("Party: could not persist puppet before removal: " +
                 std::string(e.what()));
            return false;
        }
        creature->setPuppet(false);
    }
    _persistedState.puppetIds.erase(found);
    return true;
}

bool Party::isPuppet(int puppet) const {
    return std::find(
               _persistedState.puppetIds.begin(),
               _persistedState.puppetIds.end(), puppet) !=
           _persistedState.puppetIds.end();
}

bool Party::assignPuppet(int puppet, int npc) {
    if (!isRosterAvailable({RosterKind::Puppet, puppet}) ||
        !isRosterAvailable({RosterKind::Npc, npc}) ||
        !rosterCreature({RosterKind::Npc, npc})) {
        return false;
    }
    auto creature = rosterCreature({RosterKind::Npc, npc});
    creature->setAssignedPuppet(puppet);
    return true;
}

std::optional<int> Party::assignedNpcForPuppet(int puppet) const {
    for (const auto &member : _members) {
        if (member.npc != kNpcPlayer && member.creature &&
            member.creature->assignedPuppet() == puppet) {
            return member.npc;
        }
    }
    return std::nullopt;
}

std::shared_ptr<Creature> Party::puppetOwner(int puppet) const {
    if (puppet < 0 || puppet >= static_cast<int>(kMaxPuppetCount)) {
        return nullptr;
    }
    for (const auto &member : _members) {
        if (member.npc != kNpcPlayer && member.creature &&
            member.creature->assignedPuppet() == puppet) {
            return member.creature;
        }
    }
    return _player && _player->assignedPuppet() == puppet ? _player : nullptr;
}

std::shared_ptr<Creature> Party::puppetOwner(const Creature &puppet) const {
    const auto identity = rosterIdentity(puppet);
    if (!identity || identity->kind != RosterKind::Puppet) {
        return nullptr;
    }
    return puppetOwner(identity->slot);
}

bool Party::isMember(const Object &object) const {
    for (auto &member : _members) {
        if (member.creature.get() == &object)
            return true;
    }
    return false;
}

bool Party::isRetainedRuntimeRepresentation(const Creature &creature) const {
    if (_player.get() == &creature || _actualPlayer.get() == &creature) {
        return true;
    }
    if (isMember(creature)) {
        return true;
    }
    for (int puppet : _persistedState.puppetIds) {
        auto found = _puppetBindings.find(puppet);
        if (found != _puppetBindings.end() && found->second.get() == &creature) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<Object> Party::sharedInventoryReceiver(const std::shared_ptr<Object> &receiver) const {
    auto inventoryOwner = actualPlayer();
    // A party member's non-equipped items belong to the shared party inventory
    // (the actual player creature). Non-party receivers keep their own inventory.
    if (receiver && inventoryOwner && isMember(*receiver)) {
        return inventoryOwner;
    }
    return receiver;
}

std::shared_ptr<Creature> Party::getLeader() const {
    return !_members.empty() ? _members[0].creature : nullptr;
}

bool Party::setPartyLeader(int npc) {
    if (npc == kNpcPlayer) {
        // Handing the lead to the player character stands a downed player
        // character up first, whether or not it already leads, and leaves it
        // raiseable.
        for (const auto &member : _members) {
            const auto &creature = member.creature;
            if (!creature || !creature->isPC()) {
                continue;
            }
            if (creature->isTemporarilyDead()) {
                creature->applyEffect(_game.newEffect<ResurrectionEffect>(0), DurationType::Instant);
                creature->setRaiseable(true);
            }
        }
        leadWithPlayerCharacter();
        return true;
    }
    int memberIdx = -1;
    for (int i = 0; i < static_cast<int>(_members.size()); ++i) {
        if (_members[i].npc == npc && _members[i].creature) {
            memberIdx = i;
            break;
        }
    }
    if (memberIdx == -1) {
        warn("Party: NPC not found: " + std::to_string(npc));
        return false;
    }
    if (memberIdx > 0) {
        setPartyLeaderByIndex(memberIdx);
    }
    return true;
}

void Party::leadWithPlayerCharacter() {
    for (int i = 0; i < static_cast<int>(_members.size()); ++i) {
        const auto &creature = _members[i].creature;
        if (creature && creature->isPC()) {
            if (i > 0) {
                setPartyLeaderByIndex(i);
            }
            return;
        }
    }
}

void Party::setPartyLeaderByIndex(int index) {
    if (index < 1 || index >= _members.size())
        return;

    saveLeaderAttackTarget();

    // The members ahead of the new leader move to the back in their order.
    const auto previous = _members[0].creature;
    std::rotate(_members.begin(), _members.begin() + index, _members.end());

    onLeaderChanged(previous);
}

void Party::setPlayer(const std::shared_ptr<Creature> &player) {
    _player = player;
}

void Party::setControlledMember(int npc, const std::shared_ptr<Creature> &creature) {
    if (!creature ||
        (npc != kNpcPlayer &&
         !bindRosterCreature({RosterKind::Npc, npc}, creature))) {
        warn("Party: controlled creature cannot bind roster slot " +
             std::to_string(npc));
        return;
    }
    if (npc != kNpcPlayer && isMember(npc)) {
        // An incoming companion leaves the followers as any removed member
        // does, its assigned puppet included, before it takes control.
        removeMember(npc);
    }
    // However the incoming creature was represented before, it ends up in the
    // leading slot once and only once.
    // Entries left naming nothing do not survive the change of control.
    _members.erase(
        std::remove_if(
            _members.begin(), _members.end(),
            [&npc, &creature](const Member &member) {
                return member.npc == npc || member.creature == creature || !member.creature;
            }),
        _members.end());

    // The actor being relieved stops being a party member rather than becoming
    // a companion, whichever slot leads: the canonical PC waits in
    // _actualPlayer, where it is looked up again.
    if (_player) {
        _members.erase(
            std::remove_if(
                _members.begin(), _members.end(),
                [this](const Member &member) { return member.creature == _player; }),
            _members.end());
    }

    Member member;
    member.npc = npc;
    member.creature = creature;
    _members.insert(_members.begin(), std::move(member));
    creature->refreshDeath();

    // The controlled creature is a player character.
    creature->setPC(true);
    _player = creature;
    // The controlled creature perceives at the player range.
    creature->setPerceptionRangeRow(kPlayerPerceptionRangeRow);
    // A control transfer creates a new member entry, not a leader rotation.
    _game.setLastTarget(script::kObjectInvalid);
    _persistedState.controlledNpc = npc;
}

bool Party::removeMember(int npc) {
    const RosterIdentity identity {RosterKind::Npc, npc};
    // Removal needs a follower in the party and an available roster slot;
    // KotOR refuses it while another actor stands in for the player character.
    if (npc == kNpcPlayer || companionCount() == 0 || !isRosterAvailable(identity) ||
        (!_game.isTSL() && _persistedState.controlledNpc != kNpcPlayer)) {
        return false;
    }
    // The slot's live creature has its effects cleared, gives up its assigned
    // puppet and is saved, whether or not it is travelling with the party.
    if (const auto creature = rosterCreature(identity)) {
        creature->clearAllEffects();
        const int puppet = creature->assignedPuppet();
        if (_game.isTSL() && puppet >= 0 && isPuppet(puppet)) {
            if (!removePuppet(puppet)) return false;
            _game.killRosterCreature({RosterKind::Puppet, puppet});
        }
        // Keep the detached PartyTable record current while retaining the
        // live binding.
        _game.saveRosterState(identity, *creature);
    }
    // The actor standing in for the player character is not a follower.
    if (npc == _persistedState.controlledNpc) {
        return false;
    }
    auto maybeMember = std::find_if(_members.begin(), _members.end(), [&npc](auto &member) { return member.npc == npc; });
    if (maybeMember == _members.end()) {
        return false;
    }
    const auto removed = maybeMember->creature;
    _members.erase(maybeMember);
    if (removed) {
        removed->refreshDeath();
    }
    // Solo mode ends with the last companion.
    if (companionCount() == 0) {
        _solo = false;
    }
    return true;
}

void Party::defaultMembers(std::string &member1, std::string &member2, std::string &member3) const {
    if (_game.isTSL()) {
        member1 = kBlueprintResRefAtton;
        member2 = kBlueprintResRefKreia;
        member3.clear();
    } else {
        member1 = kBlueprintResRefCarth;
        member2 = kBlueprintResRefBastila;
        member3.clear();
    }
}

// Follow path

// Steps closer than this, in a clear line from the previous one, are dropped.
static constexpr float kFollowStepMinimum2 = 0.25f;
// The trail is straightened once a follower lags this far (squared metres).
static constexpr float kFollowerLagLimit2 = 49.0f;
// The followers' follow points sit this far either side of the follow range.
static constexpr float kFollowSpacing = 1.0f;
// The party's follow states that take their follow range from ranges.2da.
static constexpr int kCloseFollowState = 0;
static constexpr int kHoldFollowState = 1;
// Follow range when the follow state names no ranges row.
static constexpr float kDefaultFollowRange = 5.0f;
static constexpr int kCloseFollowRangeRow = 14;
static constexpr int kHoldFollowRangeRow = 15;
// A straightened trail is laid out at this spacing.
static constexpr float kFollowStepLength = 1.0f;
// Points this close together are taken as in line without a walk test.
static constexpr double kFollowSameStep2 = 1e-05;
// The most corners a straightened trail keeps.
static constexpr size_t kFollowCorners = 200;
// Seeded followers stand this far behind the leader, one and two spacings back.
static constexpr float kSeedFollowerSpacing = 1.5f;
static constexpr float kSeedPlacementRadius = 2.0f;
// A member that does not fit where it stands moves within this radius.
static constexpr float kResetPlacementRadius = 5.0f;

static const glm::vec3 kFirstFollowerFormation {1.5f, -0.7f, 0.0f};
static const glm::vec3 kSecondFollowerFormation {-1.5f, 0.8f, 0.0f};

static glm::vec3 forwardOf(float facing) {
    return glm::angleAxis(facing, glm::vec3(0.0f, 0.0f, 1.0f)) * glm::vec3(0.0f, 1.0f, 0.0f);
}

static float facingAlong(const glm::vec3 &direction) {
    return direction.x == 0.0f && direction.y == 0.0f ? 0.0f : -std::atan2(direction.x, direction.y);
}

static glm::vec3 unitOrForward(const glm::vec3 &vector) {
    const float length = glm::length(vector);
    return length < 1e-9f ? glm::vec3(1.0f, 0.0f, 0.0f) : vector / length;
}

void Party::resetFollowSlots() {
    _followSteps.fill(FollowStep());
    _followHead = 0;
    _followCount = 0;
    _lastClearStep = glm::vec3(0.0f);
    _followSlots.fill(FollowSlot());
    _followSlots[1].formationOffset = kFirstFollowerFormation;
    _followSlots[2].formationOffset = kSecondFollowerFormation;
}

bool Party::isTrailLineClear(
    const Area &area, const Creature &walker, const glm::vec3 &from, const glm::vec3 &to, TrailLine &line) const {
    const auto test = area.testDirectLine(walker, from, to);
    line = test.result == Area::DirectLine::Clear         ? TrailLine::Clear
           : test.result == Area::DirectLine::LeavesRooms ? TrailLine::LeavesRooms
                                                          : TrailLine::Blocked;
    return line == TrailLine::Clear || (test.result == Area::DirectLine::CreatureBlocked && test.blocker && isMember(test.blocker->get()));
}

void Party::recordLeaderStep(const Area &area, const glm::vec3 &position, float facing) {
    const glm::vec3 previous {_followSteps[newestStep()].position};
    if (_followCount == 0) {
        _lastClearStep = position;
    }
    const auto leader = getLeader();
    if (!leader) {
        return;
    }

    const glm::vec3 offset {position - previous};
    TrailLine line {TrailLine::Clear};
    if (!isTrailLineClear(area, *leader, previous, position, line)) {
        // The way round a corner goes through the last point reached in a clear line.
        _followSteps[_followHead].position = _lastClearStep;
        _followHead = stepAfter(_followHead);
    }
    _lastClearStep = position;
    if (glm::dot(offset, offset) < kFollowStepMinimum2 && line == TrailLine::Clear) {
        return;
    }

    _followSteps[_followHead] = FollowStep {position, facing};
    _followHead = stepAfter(_followHead);
    if (_followCount < kFollowSteps) {
        ++_followCount;
        if (_followCount <= 2) {
            return;
        }
    }
    if (getSize() >= 2 && followerLag2(furthestFollower()) >= kFollowerLagLimit2) {
        smoothFollowPath(area);
    }
}

void Party::resetFollowPath(Area &area, const glm::vec3 &position, float facing, bool seed) {
    for (auto &step : _followSteps) {
        step = FollowStep {position, facing};
    }
    _followHead = 0;
    _followCount = 0;
    // An unseeded reset records no step. A seeded one records the seeded
    // spots nearest first, then the position itself; while they are recorded
    // the trail is judged against the followers' points from before the reset.
    if (seed) {
        if (const auto leader = getLeader()) {
            const glm::vec3 back {forwardOf(facing) * -kSeedFollowerSpacing};
            if (auto spot = area.computeSafeLocation(position + back, kSeedPlacementRadius, *leader, true)) {
                recordLeaderStep(area, *spot, facing);
            }
            if (auto spot = area.computeSafeLocation(position + 2.0f * back, kSeedPlacementRadius, *leader, true)) {
                recordLeaderStep(area, *spot, facing);
            }
        }
        recordLeaderStep(area, position, facing);
    }
    for (auto &slot : _followSlots) {
        slot.point = position;
        slot.index = 0;
    }
    // A follower keeps the way it faces until it moves or turns to the leader;
    // the slots may now hold other members than before.
    for (int slot = 1; slot < kPartyFollowSlots && slot < getSize(); ++slot) {
        if (const auto member = getMember(slot)) {
            _followSlots[slot].lastFacing = forwardOf(member->getFacing());
        }
    }
    for (int slot = 0; slot < kPartyFollowSlots && slot < getSize(); ++slot) {
        const auto member = getMember(slot);
        if (!member || area.isSafeLocationPoint(member->position(), *member)) {
            continue;
        }
        const glm::vec3 standing {member->position()};
        member->setPosition(area.computeSafeLocation(standing, kResetPlacementRadius, *member, true).value_or(standing));
        area.determineObjectRoom(*member);
    }
}

void Party::updateFollowPath() {
    // Nobody follows while no one leads.
    if (!getLeader()) {
        return;
    }
    const int furthest = furthestFollower();
    if (!getMember(furthest) || _game.isConversationActive()) {
        return;
    }
    if (followerLag2(furthest) >= kFollowerLagLimit2 && !turnBack()) {
        recalculateFollowPoints();
    }
}

glm::vec3 Party::formationSpot(int slot) const {
    const auto &follow = _followSlots[slot];
    const glm::quat facing {glm::angleAxis(_followSteps[follow.index].facing, glm::vec3(0.0f, 0.0f, 1.0f))};
    return follow.point + facing * follow.formationOffset;
}

void Party::noteFollowerFacing(const Creature &follower, const glm::vec3 &forward) {
    for (int slot = 1; slot < kPartyFollowSlots && slot < getSize(); ++slot) {
        if (getMember(slot).get() == &follower) {
            _followSlots[slot].lastFacing = forward;
        }
    }
}

float Party::followRange() const {
    int row;
    switch (_persistedState.followState) {
    case kCloseFollowState:
        row = kCloseFollowRangeRow;
        break;
    case kHoldFollowState:
        row = kHoldFollowRangeRow;
        break;
    default:
        return kDefaultFollowRange;
    }
    return _game.services().game.spells.range(row).primary.value_or(0.0f);
}

// Walk back from the leader's newest step, segment by segment, until the
// distance is used up inside a segment or the slot's own step is reached.
// A trail of two steps or fewer has no point. A distance used up exactly on a
// step leaves the point as given.
bool Party::pointBackFromStart(float distance, int slot, glm::vec3 &point, int &step) const {
    int index = newestStep();
    if (_followCount <= 2) {
        step = 0;
        point = glm::vec3(0.0f);
        return false;
    }
    glm::vec3 current {_followSteps[index].position};
    const int own = slot != 0 ? _followSlots[slot].index : -1;
    int remaining = _followCount;
    step = index;
    if (!(distance > 0.0f)) {
        return true;
    }
    for (;;) {
        step = index;
        if (slot != 0 && index == own) {
            point = current;
            return true;
        }
        index = stepBefore(index);
        const glm::vec3 segment {_followSteps[index].position - current};
        const float length2 = glm::dot(segment, segment);
        if (length2 >= distance * distance) {
            point = current + unitOrForward(segment) * distance;
            return true;
        }
        if (remaining <= 0) {
            point = glm::vec3(0.0f);
            return false;
        }
        distance -= std::sqrt(length2);
        current = _followSteps[index].position;
        --remaining;
        if (!(distance > 0.0f)) {
            return true;
        }
    }
}

void Party::recalculateFollowPoints(std::optional<int> sightedStep) {
    const float range = followRange();
    glm::vec3 point {0.0f};
    int step = 0;
    if (pointBackFromStart(range - kFollowSpacing, 1, point, step)) {
        _followSlots[1].point = point;
        _followSlots[1].index = step;
    } else if (sightedStep) {
        _followSlots[1].index = *sightedStep;
    }
    if (pointBackFromStart(range + kFollowSpacing, 2, point, step)) {
        _followSlots[2].point = point;
        _followSlots[2].index = step;
    } else if (sightedStep) {
        _followSlots[2].index = *sightedStep;
    }
}

// Followers are ordered by how recent their trail step is; an absent follower
// counts as furthest back, and a tie goes to the second.
int Party::furthestFollower() const {
    auto recency = [this](int slot) {
        const int step = getMember(slot) ? _followSlots[slot].index : -1;
        return step != -1 && _followHead > step ? step + kFollowSteps : step;
    };
    return recency(1) >= recency(2) ? 2 : 1;
}

int Party::closestFollower() const {
    auto recency = [this](int slot) {
        const int step = getMember(slot) ? _followSlots[slot].index : -1;
        return step != -1 && _followHead > step ? step + kFollowSteps : step;
    };
    return recency(1) <= recency(2) ? 2 : 1;
}

float Party::followerLag2(int slot) const {
    const glm::vec3 offset {_followSlots[slot].point - getLeader()->position()};
    return glm::dot(offset, offset);
}

// From the nearest follower's step, keep the furthest trail step the leader
// could walk to straight from the last corner, cornering where it cannot, end
// on the newest step, and lay the result back into the trail a metre apart.
void Party::smoothFollowPath(const Area &area) {
    const auto leader = getLeader();
    const int start = newestStep();
    // In TSL, whether the leader can walk straight to the newest step: a
    // follower whose point cannot be laid out afterwards then goes for that
    // step.
    TrailLine line {TrailLine::Clear};
    const bool newestInSight = _game.isTSL() && isTrailLineClear(area, *leader, leader->position(), _followSteps[start].position, line);
    int anchor = _followSlots[closestFollower()].index;
    if (anchor < 0 || anchor >= kFollowSteps) {
        anchor = 0;
    }

    std::vector<glm::vec3> corners {_followSteps[anchor].position};
    int last = stepAfter(anchor);
    int current = last;
    while (current != start && start != anchor && corners.size() < kFollowCorners) {
        const glm::vec3 &from = corners.back();
        const glm::vec3 &to = _followSteps[current].position;
        const glm::vec3 gap {from - to};
        const bool visible = static_cast<double>(glm::dot(gap, gap)) <= kFollowSameStep2 ||
                             isTrailLineClear(area, *leader, from, to, line);
        if (visible) {
            last = current;
            current = stepAfter(current);
            continue;
        }
        if (from == _followSteps[last].position) {
            corners.push_back(_followSteps[current].position);
            current = stepAfter(current);
        } else {
            corners.push_back(_followSteps[last].position);
        }
        // A walk that runs out of the rooms ends the search for corners.
        if (line == TrailLine::LeavesRooms) {
            break;
        }
    }
    if (corners.size() < kFollowCorners) {
        isTrailLineClear(area, *leader, corners.back(), _followSteps[current].position, line);
        if (line != TrailLine::Clear) {
            corners.push_back(_followSteps[last].position);
        }
        corners.push_back(_followSteps[current].position);
    }

    glm::vec3 base {_followSteps[anchor].position};
    _followHead = anchor;
    size_t next = 0;
    do {
        const glm::vec3 toCorner {corners[next] - base};
        glm::vec3 placed;
        if (glm::dot(toCorner, toCorner) > kFollowStepLength * kFollowStepLength) {
            placed = base + unitOrForward(toCorner) * kFollowStepLength;
        } else {
            placed = corners[next++];
        }
        auto &step = _followSteps[_followHead];
        step.position = placed;
        if (!(toCorner.x == 0.0f && toCorner.y == 0.0f && toCorner.z == 0.0f)) {
            step.facing = facingAlong(unitOrForward(placed - base));
        }
        _followHead = stepAfter(_followHead);
        base = placed;
    } while (next < corners.size());
    recalculateFollowPoints(newestInSight ? std::optional<int>(start) : std::nullopt);
}

// When the nearest follower faces against the leader's newest step, the
// follow points are recomputed and the formation swaps sides.
bool Party::turnBack() {
    const glm::vec3 forward {forwardOf(_followSteps[newestStep()].facing)};
    if (!(glm::dot(forward, _followSlots[closestFollower()].lastFacing) < 0.0f)) {
        return false;
    }
    recalculateFollowPoints();
    for (int slot = 1; slot < kPartyFollowSlots; ++slot) {
        _followSlots[slot].formationOffset.x = -_followSlots[slot].formationOffset.x;
    }
    return true;
}

} // namespace game

} // namespace reone
