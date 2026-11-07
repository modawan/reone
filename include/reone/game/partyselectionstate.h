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

#pragma once

#include <vector>

namespace reone {

namespace game {

class Party;

/**
 * The companions picked on the party selection screen, as the screen shows
 * them and hands them to the party when it is confirmed.
 *
 * At most Party::kMaxCompanionCount companions travel with the player
 * character, and no more are ever picked. Those a script forces in are
 * picked first and cannot be put back. Companions that cannot leave the
 * party come next and stay picked, and a follower whose creature was
 * destroyed is not on the screen but takes up a place; when the forced ones
 * leave no room for these, the conflict is logged and the party will refuse
 * the selection. The party's other companions follow, in party order, while
 * there is room for them.
 */
class PartySelectionState {
public:
    /**
     * Start a selection on the party as it stands. The actor standing in for
     * the player character is not a companion to pick, and nor is a follower
     * whose creature was destroyed.
     *
     * \param usable for each NPC, whether it can be picked on the screen
     */
    void openOn(const Party &party, std::vector<bool> usable, int forceNpc1, int forceNpc2);

    /**
     * Start a selection.
     *
     * \param usable for each NPC, whether it can be picked on the screen
     * \param companions the party's companions that may leave, in party order
     * \param forceNpc1, forceNpc2 the NPCs a script forces in; one that is
     *        not usable is ignored
     * \param fixed the party's companions that cannot leave
     * \param reserved places taken by members not on the screen
     */
    void open(std::vector<bool> usable, const std::vector<int> &companions, int forceNpc1, int forceNpc2,
              const std::vector<int> &fixed = {}, int reserved = 0);

    bool isSelected(int npc) const;
    bool isForced(int npc) const;
    /** Whether a picked NPC cannot be put back: it is forced in or cannot leave. */
    bool isLocked(int npc) const;

    /** Companions that could still be picked, never below zero. */
    int availableSlots() const;

    /**
     * Pick a usable NPC while there is room.
     *
     * \returns whether the selection changed
     */
    bool add(int npc);

    /**
     * Put back a picked NPC that is not locked.
     *
     * \returns whether the selection changed
     */
    bool remove(int npc);

    /** Picked NPCs, in NPC order. */
    std::vector<int> selection() const;

    /**
     * The NPCs to ask the party for: those picked, less those that cannot
     * leave, which stay whatever is asked.
     */
    std::vector<int> request() const;

    /** Forced NPCs, without duplicates. */
    const std::vector<int> &forced() const { return _forced; }

    /** Whether a forced NPC was not in the party the selection opened on. */
    bool forcesNewcomer() const;

private:
    std::vector<bool> _usable;
    std::vector<bool> _selected;
    std::vector<int> _forced;
    std::vector<int> _fixed;
    std::vector<int> _members;
    int _reserved {0};
    int _selectedCount {0};

    bool isValid(int npc) const { return npc >= 0 && npc < static_cast<int>(_usable.size()); }
    void select(int npc);
};

} // namespace game

} // namespace reone
