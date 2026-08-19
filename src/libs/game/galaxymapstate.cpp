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

#include "reone/game/galaxymapstate.h"

#include "reone/resource/gff.h"

namespace reone {

namespace game {

void GalaxyMapState::reset(resource::GameID gameId, int contentRowCount) {
    int rowCount = gameId == resource::GameID::TSL
                       ? kTSLRowCount
                       : std::max(0, contentRowCount);
    _available.assign(rowCount, false);
    _selectable.assign(rowCount, false);
    _selected = -1;
}

void GalaxyMapState::clear() {
    _available.assign(_available.size(), false);
    _selectable.assign(_selectable.size(), false);
    _selected = -1;
}

void GalaxyMapState::setAvailable(int row, bool value) {
    if (!isValidRow(row)) {
        return;
    }
    _available[row] = value;
}

bool GalaxyMapState::available(int row) const {
    return isValidRow(row) && _available[row];
}

void GalaxyMapState::setSelectable(int row, bool value) {
    if (!isValidRow(row)) {
        return;
    }
    _selectable[row] = value;
}

bool GalaxyMapState::selectable(int row) const {
    return isValidRow(row) && _selectable[row];
}

bool GalaxyMapState::trySelectPlanet(int row) {
    if (!available(row)) {
        return false;
    }
    _selected = row;
    return true;
}

void GalaxyMapState::loadFromPartyTable(const resource::Gff &ptGff) {
    auto galaxyMap = ptGff.findStruct("GlxyMap");
    if (!galaxyMap) {
        return;
    }

    // Both titles pack the planet flags into a single dword: the low half is
    // availability, the high half selectability. K2 additionally records how
    // many planets the map carries, which bounds the flags it takes.
    int maskBits = kPlanetMaskBits;
    uint32_t numPlanets = 0;
    if (galaxyMap->readDword(numPlanets, "GlxyMapNumPnts")) {
        maskBits = std::min<int>(static_cast<int>(numPlanets), kPlanetMaskBits);
    }

    uint32_t mask = 0;
    galaxyMap->readDword(mask, "GlxyMapPlntMsk");
    for (int row = 0; row < std::min(maskBits, rowCount()); ++row) {
        setAvailable(row, (mask & (1u << row)) != 0);
        setSelectable(row, (mask & (1u << (row + kPlanetMaskBits))) != 0);
    }

    int32_t selected = -1;
    galaxyMap->readInt(selected, "GlxyMapSelPnt");
    restoreSelectedPlanet(selected);
}

} // namespace game

} // namespace reone
