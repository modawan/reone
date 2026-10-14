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
class Gff;
class TwoDA;

}

namespace game {

class Creature;
class Game;
class Object;

class IReputes {
public:
    struct FactionDefinition {
        std::string name;
        uint32_t parentId {std::numeric_limits<uint32_t>::max()};
        bool global {true};
    };

    struct State {
        std::vector<FactionDefinition> factions;
        std::vector<std::string> labels;
        std::vector<std::vector<int>> values;
    };

    /** The friendliest reputation a faction can have toward another. */
    static constexpr int kMaxReputation = 100;

    virtual ~IReputes() = default;

    virtual State baseState() const = 0;
    virtual State state() const = 0;
    virtual std::optional<State> parse(const resource::Gff &gff) const = 0;
    virtual void replace(State state) = 0;

    // Reputation is directed: it is the source faction's disposition toward the
    /**
     * target faction, and the reverse relationship is independent of it. Only
     * NPC factions are regarded: a relationship with the player faction is
     * read from the player faction's side.
     */
    virtual int getReputation(Faction sourceFaction, Faction targetFaction) const = 0;
    virtual void adjustReputation(Faction sourceFaction, Faction targetFaction, int adjustment) = 0;

    /**
     * Whether the source creature regards the target creature as an enemy (10
     * or less), a friend (90 or more) or neutral, by
     * getObjectReputation(source, target): a party member counts as the player
     * faction.
     */
    virtual bool getIsEnemy(const Creature &source, const Creature &target) const = 0;
    virtual bool getIsFriend(const Creature &source, const Creature &target) const = 0;
    virtual bool getIsNeutral(const Creature &source, const Creature &target) const = 0;
};

class Reputes : public IReputes, boost::noncopyable {
public:
    Reputes(resource::ITwoDAs &twoDas) :
        _twoDas(twoDas) {
    }

    void init();

    State baseState() const override;
    State state() const override;
    std::optional<State> parse(const resource::Gff &gff) const override;
    void replace(State state) override;

    const std::vector<FactionDefinition> &factions() const { return _factions; }

    int getReputation(Faction sourceFaction, Faction targetFaction) const override;
    void adjustReputation(Faction sourceFaction, Faction targetFaction, int adjustment) override;

    bool getIsEnemy(const Creature &source, const Creature &target) const override;
    bool getIsFriend(const Creature &source, const Creature &target) const override;
    bool getIsNeutral(const Creature &source, const Creature &target) const override;

private:
    resource::ITwoDAs &_twoDas;
    std::vector<FactionDefinition> _factions;
    std::vector<std::string> _factionLabels;
    std::vector<std::vector<int>> _factionValues;

    /**
     * Fill the standings of \p factionCount factions from repute.2da: row r
     * is faction r, and column c after the label column is the standing
     * toward faction c. Standings the table does not give are 100.
     */
    void loadBase(
        const resource::TwoDA &repute,
        size_t factionCount,
        std::vector<std::vector<int>> &values) const;
};

/**
 * The faction of a creature, door, placeable, trigger or encounter; other
 * objects have none. A companion joins the player's faction when it becomes
 * available to the party.
 */
std::optional<Faction> getObjectFaction(const Object &object);

/**
 * How the receiver regards the other object, from 0 to 100. An area of effect
 * stands for its creator. Objects without a faction are regarded at 50 and an
 * object regards itself at 100. A relationship with the player faction is
 * read from its side, and a party member counts as the player faction.
 */
int getObjectReputation(const Object &receiver, const Object &other, const Game &game);

} // namespace game

} // namespace reone
