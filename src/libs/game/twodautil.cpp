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

#include "reone/game/twodautil.h"

#include <unordered_map>

#include <boost/algorithm/string.hpp>

#include "reone/resource/2da.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/provider/2das.h"
#include "reone/system/exception/validation.h"

using namespace reone::resource;

namespace reone {

namespace game {

std::shared_ptr<TwoDA> getRequiredTwoDA(
    ITwoDAs &twoDas,
    const std::string &resRef) {

    auto table = twoDas.get(resRef);
    if (!table) {
        throw ResourceNotFoundException("2DA not found: " + resRef);
    }
    return table;
}

void validateTwoDARow(
    const TwoDA &table,
    const std::string &resRef,
    int row) {

    validateTwoDARowIndex(resRef, row, table.getRowCount());
}

void validateTwoDARowIndex(
    const std::string &resRef,
    int row,
    int rowCount) {

    if (row < 0 || row >= rowCount) {
        throw ValidationException(str(boost::format(
                                          "%s.2da row out of range: %d/%d") %
                                      resRef % row % rowCount));
    }
}

std::optional<Ability> readAbilityColumn(const TwoDA &table, int row, const std::string &column) {
    static const std::unordered_map<std::string, Ability> kAbilityNames {
        {"STR", Ability::Strength},
        {"DEX", Ability::Dexterity},
        {"CON", Ability::Constitution},
        {"INT", Ability::Intelligence},
        {"WIS", Ability::Wisdom},
        {"CHA", Ability::Charisma}};
    const auto ability = kAbilityNames.find(boost::to_upper_copy(table.getString(row, column)));
    return ability != kAbilityNames.end() ? std::optional<Ability>(ability->second) : std::nullopt;
}

std::optional<int> readFeatName(const TwoDA &feats, int row) {
    const int name = feats.getColumnIndex("name");
    return feats.getIntOpt(row, name != -1 ? name : feats.getColumnIndex("feat"));
}

} // namespace game

} // namespace reone
