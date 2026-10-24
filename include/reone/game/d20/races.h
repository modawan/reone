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

#include <array>
#include <unordered_set>
#include <vector>

#include "../types.h"

namespace reone {

namespace resource {

class ITwoDAs;

} // namespace resource

namespace game {

/** A favoured class of this value names none. */
constexpr int kNoFavoredClass = 0xff;

/** A race: a row of racialtypes.2da. */
struct Race {
    /** The race's name, a string reference; a blank one is 0. */
    int name {0};
    /** The name conversations use, and its lower-case form; a blank one is 0. */
    int converName {0};
    int converNameLower {0};
    /** The adjustment to each ability, in Ability order: a signed byte, blank is 0. */
    std::array<int, 6> abilityAdjustments {};
    /** The favoured class (low byte), or kNoFavoredClass. */
    int favoredClass {kNoFavoredClass};
    /** The feats the race grants at the first character level. */
    std::unordered_set<FeatType> firstLevelFeats;
};

class IRaces {
public:
    virtual ~IRaces() = default;

    /** The race of type \p type; a type without a row of the table is invalid. */
    virtual const Race &get(RacialType type) const = 0;
};

/** The races, read once from racialtypes.2da. */
class Races : public IRaces, boost::noncopyable {
public:
    /**
     * Reads the races. The table's row count is held in a byte, and a table
     * that cannot be loaded has no races.
     */
    void init(resource::ITwoDAs &twoDas);

    const Race &get(RacialType type) const override;

private:
    std::vector<Race> _races;
};

} // namespace game

} // namespace reone
