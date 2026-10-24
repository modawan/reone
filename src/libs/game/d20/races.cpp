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

#include "reone/game/d20/races.h"

#include <cstdint>

#include "reone/game/twodautil.h"
#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"

using namespace reone::resource;

namespace reone {

namespace game {

static const char kRacesTableResRef[] = "racialtypes";

void Races::init(ITwoDAs &twoDas) {
    // By Ability.
    static constexpr const char *kAdjustmentColumns[] = {
        "stradjust", "dexadjust", "conadjust", "intadjust", "wisadjust", "chaadjust"};

    _races.clear();
    std::shared_ptr<TwoDA> races(twoDas.get(kRacesTableResRef));
    if (!races) {
        return;
    }
    // The number of races is a byte.
    const int count = races->getRowCount() & 0xff;
    _races.resize(count);
    for (int row = 0; row < count; ++row) {
        Race &race = _races[row];
        race.name = races->getInt(row, "name", 0);
        race.converName = races->getInt(row, "convername", 0);
        race.converNameLower = races->getInt(row, "convernamelower", 0);
        for (size_t ability = 0; ability < race.abilityAdjustments.size(); ++ability) {
            race.abilityAdjustments[ability] = static_cast<int8_t>(races->getInt(row, kAdjustmentColumns[ability], 0));
        }
        // A blank favoured class names none.
        race.favoredClass = races->getInt(row, "favored", kNoFavoredClass) & 0xff;

        std::string featsTable(boost::to_lower_copy(races->getString(row, "featstable")));
        if (featsTable.empty()) {
            continue;
        }
        // A race's feats table that cannot be loaded lists no feats.
        std::shared_ptr<TwoDA> raceFeats(twoDas.get(featsTable));
        if (!raceFeats) {
            continue;
        }
        // Each row's feat is a word; a blank cell reads as feat 0.
        for (int featRow = 0; featRow < raceFeats->getRowCount(); ++featRow) {
            race.firstLevelFeats.insert(static_cast<FeatType>(raceFeats->getInt(featRow, "featindex") & 0xffff));
        }
    }
}

const Race &Races::get(RacialType type) const {
    const int row = static_cast<int>(type);
    validateTwoDARowIndex(kRacesTableResRef, row, static_cast<int>(_races.size()));
    return _races[row];
}

} // namespace game

} // namespace reone
