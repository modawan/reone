/*
 * Copyright (c) 2026 The reone project contributors
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

#include "reone/game/partyselectionstate.h"

#include <algorithm>

#include "reone/game/party.h"
#include "reone/game/types.h"
#include "reone/system/logutil.h"

namespace reone {

namespace game {

void PartySelectionState::openOn(const Party &party, std::vector<bool> usable, int forceNpc1, int forceNpc2) {
    std::vector<int> companions;
    std::vector<int> fixed;
    int reserved = 0;
    auto unusable = [&usable](int npc) {
        if (npc >= 0 && npc < static_cast<int>(usable.size())) {
            usable[npc] = false;
        }
    };
    for (const auto &member : party.members()) {
        if (member.npc == kNpcPlayer) {
            continue;
        }
        if (member.npc == party.controlledNpc()) {
            unusable(member.npc);
            continue;
        }
        if (!member.creature) {
            // It keeps its place and cannot join again.
            ++reserved;
            unusable(member.npc);
            continue;
        }
        if (party.canRemoveMember(member.npc)) {
            companions.push_back(member.npc);
        } else {
            fixed.push_back(member.npc);
        }
    }
    open(std::move(usable), companions, forceNpc1, forceNpc2, fixed, reserved);
}

void PartySelectionState::open(std::vector<bool> usable, const std::vector<int> &companions, int forceNpc1, int forceNpc2,
                               const std::vector<int> &fixed, int reserved) {
    _usable = std::move(usable);
    _selected.assign(_usable.size(), false);
    _forced.clear();
    _fixed.clear();
    _members.clear();
    _reserved = 0;
    _selectedCount = 0;

    for (int npc : {forceNpc1, forceNpc2}) {
        if (isValid(npc) && _usable[npc] && !isForced(npc)) {
            _forced.push_back(npc);
            select(npc);
        }
    }
    // Those that cannot leave, and the places held off the screen, are in the
    // party whatever is picked; the forced companions come before them.
    bool overfull = false;
    for (int npc : fixed) {
        if (!isValid(npc) || std::find(_fixed.begin(), _fixed.end(), npc) != _fixed.end()) {
            continue;
        }
        _fixed.push_back(npc);
        _members.push_back(npc);
        if (_selected[npc]) {
            continue;
        }
        if (availableSlots() > 0) {
            select(npc);
        } else {
            overfull = true;
        }
    }
    _reserved = std::min(std::max(0, reserved), availableSlots());
    if (overfull || _reserved < reserved) {
        warn("Party selection: forced companions leave no room for the members that cannot leave");
    }
    // A forced companion takes the place of a current one: those that no
    // longer fit start out of the selection.
    for (int npc : companions) {
        if (!isValid(npc)) {
            continue;
        }
        _members.push_back(npc);
        if (_usable[npc] && !_selected[npc] && availableSlots() > 0) {
            select(npc);
        }
    }
}

bool PartySelectionState::isSelected(int npc) const {
    return isValid(npc) && _selected[npc];
}

bool PartySelectionState::isForced(int npc) const {
    return std::find(_forced.begin(), _forced.end(), npc) != _forced.end();
}

bool PartySelectionState::isLocked(int npc) const {
    return isForced(npc) || std::find(_fixed.begin(), _fixed.end(), npc) != _fixed.end();
}

bool PartySelectionState::forcesNewcomer() const {
    return std::any_of(_forced.begin(), _forced.end(), [this](int npc) {
        return std::find(_members.begin(), _members.end(), npc) == _members.end();
    });
}

int PartySelectionState::availableSlots() const {
    return std::max(0, Party::kMaxCompanionCount - _reserved - _selectedCount);
}

bool PartySelectionState::add(int npc) {
    if (!isValid(npc) || !_usable[npc] || _selected[npc] || availableSlots() == 0) {
        return false;
    }
    select(npc);
    return true;
}

bool PartySelectionState::remove(int npc) {
    if (!isSelected(npc) || isLocked(npc)) {
        return false;
    }
    _selected[npc] = false;
    --_selectedCount;
    return true;
}

std::vector<int> PartySelectionState::request() const {
    std::vector<int> result;
    for (int npc : selection()) {
        if (std::find(_fixed.begin(), _fixed.end(), npc) == _fixed.end()) {
            result.push_back(npc);
        }
    }
    return result;
}

std::vector<int> PartySelectionState::selection() const {
    std::vector<int> result;
    for (int npc = 0; npc < static_cast<int>(_selected.size()); ++npc) {
        if (_selected[npc]) {
            result.push_back(npc);
        }
    }
    return result;
}

void PartySelectionState::select(int npc) {
    _selected[npc] = true;
    ++_selectedCount;
}

} // namespace game

} // namespace reone
