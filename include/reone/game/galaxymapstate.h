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

#include "reone/resource/types.h"

namespace reone {

namespace resource {
class Gff;
}

namespace game {

/**
 * Runtime galaxy map planet state, as the galaxy map script routines and the
 * galaxy map GUI see it.
 *
 * The two titles size this differently. K1 takes its planet list from content,
 * so rows above 15 are valid whenever planetary.2da defines them. K2 always
 * carries exactly 16 rows, whatever the table says.
 *
 * Availability and selectability are independent: a planet can be shown on the
 * map while remaining an invalid travel destination. Selection follows
 * availability alone.
 */
class GalaxyMapState {
public:
    /** Row count K2 always carries, regardless of content. */
    static constexpr int kTSLRowCount = 16;

    /** Number of legacy planet mask bits per half of PARTYTABLE's GlxyMapPlntMsk. */
    static constexpr int kPlanetMaskBits = 16;

    /**
     * Resize to a new game and drop all planet state.
     *
     * \param contentRowCount rows content defines, honoured by K1 only
     */
    void reset(resource::GameID gameId, int contentRowCount);

    /** Drop all planet state, keeping the row count content established. */
    void clear();

    int rowCount() const { return static_cast<int>(_available.size()); }
    bool isValidRow(int row) const { return row >= 0 && row < rowCount(); }

    void setAvailable(int row, bool value);
    bool available(int row) const;
    void setSelectable(int row, bool value);
    bool selectable(int row) const;

    int selectedPlanet() const { return _selected; }

    /**
     * Select a planet on behalf of the player. An unavailable or out of range
     * row leaves the previous selection in place.
     *
     * \returns whether the selection changed hands
     */
    bool trySelectPlanet(int row);

    /** Put back a selection a save carries, without an availability check. */
    void restoreSelectedPlanet(int row) { _selected = isValidRow(row) ? row : -1; }

    /**
     * Apply the GalaxyMap struct of a PARTYTABLE. A save missing the struct or
     * any of its fields leaves planets unavailable, unselectable and nothing
     * selected.
     */
    void loadFromPartyTable(const resource::Gff &ptGff);

private:
    std::vector<bool> _available;
    std::vector<bool> _selectable;
    int _selected {-1};
};

} // namespace game

} // namespace reone
