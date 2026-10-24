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

#include <string>
#include <vector>

#include <boost/noncopyable.hpp>

namespace reone {

namespace resource {

class ITwoDAs;

} // namespace resource

namespace game {

/** How fast a creature on a creaturespeed row walks and runs, in metres a second. */
struct CreatureSpeed {
    float walkRate {0.0f};
    float runRate {0.0f};
};

/** The movement rates of creaturespeed.2da, by row. */
class ICreatureSpeeds {
public:
    virtual ~ICreatureSpeeds() = default;

    /** The speeds of a row of the table; a row outside it is invalid. */
    virtual const CreatureSpeed &get(int row) const = 0;
    /**
     * The row whose 2DA name is \p name, ignoring case, as an appearance's
     * MOVERATE names it; a name no row has is invalid.
     */
    virtual int find(const std::string &name) const = 0;
};

/** The movement rates read from creaturespeed.2da once at start-up. */
class CreatureSpeeds : public ICreatureSpeeds, boost::noncopyable {
public:
    explicit CreatureSpeeds(resource::ITwoDAs &twoDas) :
        _twoDas(twoDas) {
    }

    /** Reads the table, which the game requires. */
    void init();

    const CreatureSpeed &get(int row) const override;
    int find(const std::string &name) const override;

private:
    /** A row: its lower-case 2DA name and its speeds. */
    struct Row {
        std::string name;
        CreatureSpeed speed;
    };

    resource::ITwoDAs &_twoDas;
    std::vector<Row> _rows;
};

} // namespace game

} // namespace reone
