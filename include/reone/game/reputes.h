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

#include "types.h"

namespace reone {

namespace resource {

class ITwoDAs;

}

namespace game {

class Creature;

class IReputes {
public:
    virtual ~IReputes() = default;

    // Reputation is directed: it is the source faction's disposition toward the
    // target faction, and the reverse relationship is independent of it.
    virtual int getReputation(Faction sourceFaction, Faction targetFaction) const = 0;
    virtual void adjustReputation(Faction sourceFaction, Faction targetFaction, int adjustment) = 0;

    virtual bool getIsEnemy(const Creature &source, const Creature &target) const = 0;
    virtual bool getIsEnemy(Faction sourceFaction, Faction targetFaction) const = 0;
    virtual bool getIsFriend(const Creature &source, const Creature &target) const = 0;
    virtual bool getIsNeutral(const Creature &source, const Creature &target) const = 0;
};

class Reputes : public IReputes, boost::noncopyable {
public:
    Reputes(resource::ITwoDAs &twoDas) :
        _twoDas(twoDas) {
    }

    void init();

    int getReputation(Faction sourceFaction, Faction targetFaction) const override;
    void adjustReputation(Faction sourceFaction, Faction targetFaction, int adjustment) override;

    bool getIsEnemy(const Creature &source, const Creature &target) const override;
    bool getIsEnemy(Faction sourceFaction, Faction targetFaction) const override;
    bool getIsFriend(const Creature &source, const Creature &target) const override;
    bool getIsNeutral(const Creature &source, const Creature &target) const override;

private:
    resource::ITwoDAs &_twoDas;
    std::vector<std::string> _factionLabels;
    std::vector<std::vector<int>> _factionValues;
};

} // namespace game

} // namespace reone
