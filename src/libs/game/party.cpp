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

#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/types.h"
#include "reone/system/logutil.h"
#include "reone/system/randomutil.h"

namespace reone {

namespace game {

static constexpr char kBlueprintResRefCarth[] = "p_carth";
static constexpr char kBlueprintResRefBastila[] = "p_bastilla";
static constexpr char kBlueprintResRefAtton[] = "p_atton";
static constexpr char kBlueprintResRefKreia[] = "p_kreia";

void Party::setPersistedState(PersistedState state) {
    _solo = state.soloMode;
    _persistedState = std::move(state);
}

void Party::loadPersistedState(PersistedState state) {
    // Retail LoadTableInfo clears every transient object binding before it
    // publishes the persisted PartyTable fields. A new session must rebuild
    // active and inactive runtime representations explicitly.
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

void Party::setDefaultPazaakData(size_t cardCount) {
    PazaakCardCounts counts {};
    counts[0] = counts[1] = counts[2] = counts[3] = counts[4] = 2;
    PazaakSideDeck sideDeck;
    sideDeck.fill(-1);
    setPazaakData(std::move(counts), std::move(sideDeck), cardCount);
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
        switchLeader();
        return true;
    }

    return false;
}

bool Party::addAvailableMember(int npc, const std::string &blueprint) {
    return addAvailableRosterRecord(
        {RosterKind::Npc, npc}, blueprint);
}

bool Party::addAvailableMember(int npc, std::shared_ptr<Creature> creature) {
    return makeRosterAvailableAndBind(
        {RosterKind::Npc, npc}, creature);
}

bool Party::removeAvailableMember(int npc) {
    return setRosterAvailable({RosterKind::Npc, npc}, false);
}

bool Party::addAvailablePuppet(int puppet, std::shared_ptr<Creature> creature) {
    return makeRosterAvailableAndBind(
        {RosterKind::Puppet, puppet}, creature);
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
    // Retail ignores SetNPCSelectability for invalid or unavailable slots.
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
    if (!setRosterAvailable(identity, true) ||
        !bindRosterCreature(identity, creature)) {
        available = previousAvailable;
        selectable = previousSelectable;
        return false;
    }
    return true;
}

bool Party::addAvailableRosterRecord(
    const RosterIdentity &identity,
    const std::shared_ptr<Creature> &creature) {
    if (!creature || !isRosterIdentityValid(identity)) return false;
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
        for (auto &member : _members) {
            if (member.npc == identity.slot) {
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

bool Party::clearRosterCreature(
    const RosterIdentity &identity,
    const Creature *expected) {
    if (!isRosterIdentityValid(identity)) return false;
    auto &bindings = identity.kind == RosterKind::Npc
                         ? _npcBindings
                         : _puppetBindings;
    auto found = bindings.find(identity.slot);
    if (found == bindings.end() ||
        (expected && found->second.get() != expected)) {
        return false;
    }
    auto removed = found->second;
    bindings.erase(found);
    if (identity.kind == RosterKind::Npc) {
        _members.erase(
            std::remove_if(
                _members.begin(), _members.end(),
                [&identity, &removed](const Member &member) {
                    return member.npc == identity.slot ||
                           member.creature == removed;
                }),
            _members.end());
        if (_persistedState.controlledNpc == identity.slot &&
            _player == removed) {
            _player = _actualPlayer;
            _persistedState.controlledNpc = kNpcPlayer;
        }
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

bool Party::addMember(int npc, std::shared_ptr<Creature> creature) {
    if (!creature || isMember(npc) || isMember(*creature)) {
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
    // A creature joining the party derives its XP from the shared party pool.
    creature->setXP(_xp);

    Member member;
    member.npc = npc;
    member.creature = creature;
    _members.push_back(std::move(member));

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

void Party::giveGold(int amount) {
    _gold += amount;
}

void Party::takeGold(int amount) {
    _gold -= amount;
    if (_gold < 0) {
        _gold = 0;
    }
}

void Party::awardXP(int amount, XPSource source) {
    _xp += amount;
    syncMembersXP();
    // Preserve zero/negative pool semantics, but only positive awards are XP
    // received. Submit the call amount once, not the pool total or per member.
    if (amount <= 0) {
        return;
    }

    switch (source) {
    case XPSource::Plot:
    case XPSource::Console:
        _game.submitStatusSummary(StatusSummaryCategory::PlotXP, amount);
        break;
    case XPSource::Combat:
        // Combat rewards are intentionally silent in the Status Summary.
        break;
    case XPSource::Stealth:
        // Stealth accounting and presentation are implemented in a later slice.
        break;
    }
}

void Party::setXP(int xp) {
    _xp = xp;
    syncMembersXP();
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

void Party::switchLeader() {
    if (_members.size() <= 1) {
        return;
    }

    Member tmp(_members[0]);
    _members.erase(_members.begin());
    _members.push_back(tmp);

    onLeaderChanged();
}

void Party::onLeaderChanged() {
    auto entry = static_cast<resource::SoundSetEntry>(static_cast<int>(resource::SoundSetEntry::Select1) + randomInt(0, 2));
    _members[0].creature->playSound(entry, false);

    for (auto &member : _members) {
        member.creature->clearAllActions();
    }

    _game.module()->area()->onPartyLeaderMoved(true);
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
        if (member.npc == npc) {
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

bool Party::isMember(int npc) const {
    for (auto &member : _members) {
        if (member.npc == npc)
            return true;
    }
    return false;
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
        try {
            // Retail RemovePuppet snapshots the live representation before
            // KillPUPObject invalidates its transient object binding.
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

bool Party::isMember(const Object &object) const {
    for (auto &member : _members) {
        if (member.creature.get() == &object)
            return true;
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

void Party::setPartyLeader(int npc) {
    int memberIdx = -1;
    for (int i = 0; i < static_cast<int>(_members.size()); ++i) {
        if (_members[i].npc == npc) {
            memberIdx = i;
            break;
        }
    }
    if (memberIdx == -1) {
        warn("Party: NPC not found: " + std::to_string(npc));
        return;
    }
    if (memberIdx == 0)
        return;

    setPartyLeaderByIndex(memberIdx);
}

void Party::setPartyLeaderByIndex(int index) {
    if (index < 1 || index >= _members.size())
        return;

    Member tmp(_members[0]);
    _members[0] = _members[index];
    _members[index] = tmp;

    onLeaderChanged();
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
        // Retail removes an incoming companion from the two-member array
        // before it becomes the controlled player, including its active
        // assigned-puppet runtime representation.
        removeMember(npc);
    }
    // However the incoming creature was represented before, it ends up in the
    // leading slot once and only once.
    _members.erase(
        std::remove_if(
            _members.begin(), _members.end(),
            [&npc, &creature](const Member &member) {
                return member.npc == npc || member.creature == creature;
            }),
        _members.end());

    // The actor being relieved stops being a party member rather than becoming
    // a companion: the canonical PC waits in _actualPlayer and a relieved NPC
    // waits in the available roster, which is where each is looked up again.
    if (!_members.empty() && _player && _members.front().creature == _player) {
        _members.erase(_members.begin());
    }

    Member member;
    member.npc = npc;
    member.creature = creature;
    _members.insert(_members.begin(), std::move(member));

    _player = creature;
    _persistedState.controlledNpc = npc;
}

bool Party::removeMember(int npc) {
    if (npc == kNpcPlayer ||
        (npc == _persistedState.controlledNpc &&
         _player == rosterCreature({RosterKind::Npc, npc}))) {
        return false;
    }
    auto maybeMember = std::find_if(_members.begin(), _members.end(), [&npc](auto &member) { return member.npc == npc; });
    if (maybeMember != _members.end()) {
        const auto creature = maybeMember->creature;
        const int puppet = creature ? creature->assignedPuppet() : -1;
        if (creature) {
            try {
                // Retail RemoveMember saves the current bound NPC before its
                // runtime membership changes. Reone retains the binding, but
                // keeping the detached snapshot current preserves the same
                // explicit PartyTable repository semantics.
                _game.saveRosterState({RosterKind::Npc, npc}, *creature);
            } catch (const std::exception &e) {
                warn("Party: could not persist NPC before removal: " +
                     std::string(e.what()));
                return false;
            }
        }
        if (_game.isTSL() && puppet >= 0 && isPuppet(puppet)) {
            if (!removePuppet(puppet)) return false;
            _game.killRosterCreature({RosterKind::Puppet, puppet});
        }
        _members.erase(maybeMember);
        return true;
    }
    return false;
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

} // namespace game

} // namespace reone
