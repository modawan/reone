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

#include "reone/game/difficultyoptions.h"

#include "reone/game/twodautil.h"
#include "reone/resource/2da.h"
#include "reone/system/exception/validation.h"

#include <utility>

namespace reone {

namespace game {

static constexpr int kRequiredDifficultyOptionCount = 4;
static constexpr int kDefaultDifficultyOptionRow = 3;

void DifficultyOptions::init() {
    auto table = getRequiredTwoDA(_twoDas, "difficultyopt");
    if (table->getRowCount() < kRequiredDifficultyOptionCount) {
        throw ValidationException(
            "difficultyopt.2da requires at least " +
            std::to_string(kRequiredDifficultyOptionCount) + " rows");
    }

    std::vector<DifficultyOption> options;
    options.reserve(table->getRowCount());

    for (int row = 0; row < table->getRowCount(); ++row) {
        DifficultyOption option;
        option.nameStrRef = table->getInt(row, "name", -1);
        option.description = table->getString(row, "desc");

        auto multiplier = table->getFloatOpt(row, "multiplier");
        if (multiplier) {
            option.damageMultiplier = *multiplier;
        } else if (row != kDefaultDifficultyOptionRow) {
            throw ValidationException(
                "difficultyopt.2da multiplier missing at row " +
                std::to_string(row));
        }

        options.push_back(std::move(option));
    }

    _options = std::move(options);
}

const DifficultyOption &DifficultyOptions::get(int difficulty) const {
    if (difficulty < 0 || difficulty >= static_cast<int>(_options.size())) {
        throw ValidationException(
            "difficultyopt.2da row out of range: " +
            std::to_string(difficulty) + "/" +
            std::to_string(_options.size()));
    }
    return _options[difficulty];
}

} // namespace game

} // namespace reone
