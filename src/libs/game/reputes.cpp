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

#include "reone/game/reputes.h"

#include <algorithm>

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/areaofeffect.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/encounter.h"
#include "reone/game/object/placeable.h"
#include "reone/game/object/trigger.h"
#include "reone/game/party.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include "reone/resource/provider/2das.h"

using namespace reone::resource;

namespace reone {

namespace game {

static constexpr int kDefaultRepute = 50;
static constexpr int kMinRepute = 0;
// A standing of at most 10 is hostile and one above 89 is friendly; the
// standings between are neutral.
static constexpr int kHighestEnemyRepute = 10;
static constexpr int kLowestFriendRepute = 90;

void Reputes::init() {
    replace(baseState());
}

IReputes::State Reputes::baseState() const {
    std::shared_ptr<TwoDA> repute(_twoDas.get("repute"));
    if (!repute) {
        return State();
    }

    std::vector<FactionDefinition> factions;
    std::vector<std::string> labels;
    factions.reserve(repute->getRowCount());
    labels.reserve(repute->getRowCount());
    for (int row = 0; row < repute->getRowCount(); ++row) {
        std::string label = boost::to_lower_copy(repute->getString(row, "label"));
        factions.push_back({label});
        labels.push_back(std::move(label));
    }

    std::vector<std::vector<int>> values;
    loadBase(*repute, factions.size(), values);

    return {std::move(factions), std::move(labels), std::move(values)};
}

IReputes::State Reputes::state() const {
    return {_factions, _factionLabels, _factionValues};
}

std::optional<IReputes::State> Reputes::parse(const resource::Gff &gff) const {
    std::shared_ptr<TwoDA> repute(_twoDas.get("repute"));
    if (!repute) {
        return std::nullopt;
    }

    auto hasList = [&gff](std::string_view label) {
        return std::any_of(gff.fields().begin(), gff.fields().end(), [label](const Gff::Field &field) {
            return field.type == Gff::FieldType::List && boost::iequals(field.label, label);
        });
    };
    if (!hasList("FactionList") || !hasList("RepList")) {
        return std::nullopt;
    }
    const auto factionList = gff.getList("FactionList");
    const auto repList = gff.getList("RepList");
    if (factionList.empty()) {
        return std::nullopt;
    }

    std::vector<FactionDefinition> factions;
    std::vector<std::string> labels;
    factions.reserve(std::max<size_t>(factionList.size(), repute->getRowCount()));
    labels.reserve(std::max<size_t>(factionList.size(), repute->getRowCount()));

    for (const auto &saved : factionList) {
        std::string name;
        uint32_t parentId = 0;
        if (!saved->readString(name, "FactionName") ||
            !saved->readDword(parentId, "FactionParentID")) {
            return std::nullopt;
        }
        uint16_t global = 1;
        saved->readWord(global, "FactionGlobal");
        factions.push_back({name, parentId, global != 0});
        labels.push_back(boost::to_lower_copy(name));
    }

    // The game preserves saved faction IDs by list order, then appends only the
    // base-table rows beyond the saved list's length.
    for (int row = static_cast<int>(factions.size()); row < repute->getRowCount(); ++row) {
        std::string label = boost::to_lower_copy(repute->getString(row, "label"));
        factions.push_back({label});
        labels.push_back(std::move(label));
    }

    std::vector<std::vector<int>> values;
    loadBase(*repute, factions.size(), values);

    for (const auto &saved : repList) {
        uint32_t source = 0;
        uint32_t target = 0;
        uint32_t reputation = 0;
        if (!saved->readDword(source, "FactionID1") ||
            !saved->readDword(target, "FactionID2") ||
            !saved->readDword(reputation, "FactionRep")) {
            return std::nullopt;
        }

        // FAC uses source rows and NPC target columns; player-target values
        // remain derived from the base table.
        if (source >= values.size() || target == 0 || target >= values.size()) {
            continue;
        }
        values[source][target] = std::clamp(
            static_cast<int64_t>(reputation),
            static_cast<int64_t>(kMinRepute),
            static_cast<int64_t>(kMaxReputation));
    }

    return State {std::move(factions), std::move(labels), std::move(values)};
}

// Saved state is replaced only after the complete FAC validates.
void Reputes::replace(State state) {
    _factions = std::move(state.factions);
    _factionLabels = std::move(state.labels);
    _factionValues = std::move(state.values);
}

void Reputes::loadBase(
    const TwoDA &repute,
    size_t factionCount,
    std::vector<std::vector<int>> &values) const {

    values.assign(factionCount, std::vector<int>(factionCount, kMaxReputation));
    // Row r holds faction r's standings and column c, after the label column,
    // the standing toward faction c. A blank cell is read as 0 and ends the
    // loading; cells beyond the factions are ignored.
    const auto &columns = repute.columns();
    for (int row = 0; row < repute.getRowCount(); ++row) {
        for (size_t column = 0; column < columns.size(); ++column) {
            if (column == 0) {
                if (repute.getString(row, columns[column]).empty()) {
                    return;
                }
                continue;
            }
            const auto reputation = repute.getIntOpt(row, columns[column]);
            if (static_cast<size_t>(row) < factionCount && column < factionCount) {
                values[row][column] = std::clamp(reputation.value_or(0), kMinRepute, kMaxReputation);
            }
            if (!reputation) {
                return;
            }
        }
    }
}

int Reputes::getReputation(Faction sourceFaction, Faction targetFaction) const {
    int source = static_cast<int>(sourceFaction);
    int target = static_cast<int>(targetFaction);
    const auto isNPC = [this](int id) { return id > 0 && id < static_cast<int>(_factionValues.size()); };

    // Only NPC factions are regarded. When the target is not one, the
    // relationship is read from the target's side; between two factions that
    // are not NPC factions the standing is the default.
    if (!isNPC(target)) {
        if (!isNPC(source)) {
            return kDefaultRepute;
        }
        std::swap(source, target);
    }
    if (source < 0 || source >= static_cast<int>(_factionValues.size()) ||
        target >= static_cast<int>(_factionValues[source].size()))
        return kDefaultRepute;

    return _factionValues[source][target];
}

void Reputes::adjustReputation(Faction sourceFaction, Faction targetFaction, int adjustment) {
    int source = static_cast<int>(sourceFaction);
    int target = static_cast<int>(targetFaction);

    // Only the source faction's view of the target moves. A faction's view of
    // itself is fixed, and out-of-range factions have no cell to adjust.
    if (source < 0 || source >= static_cast<int>(_factionValues.size()) ||
        target < 0 || target >= static_cast<int>(_factionValues[source].size()) ||
        source == target) {
        return;
    }

    int64_t adjusted = static_cast<int64_t>(_factionValues[source][target]) + adjustment;
    _factionValues[source][target] = static_cast<int>(
        std::clamp(adjusted, static_cast<int64_t>(kMinRepute), static_cast<int64_t>(kMaxReputation)));
}

bool Reputes::getIsEnemy(const Creature &source, const Creature &target) const {
    return getObjectReputation(source, target, source.game()) <= kHighestEnemyRepute;
}

bool Reputes::getIsFriend(const Creature &source, const Creature &target) const {
    return getObjectReputation(source, target, source.game()) >= kLowestFriendRepute;
}

bool Reputes::getIsNeutral(const Creature &source, const Creature &target) const {
    const int reputation = getObjectReputation(source, target, source.game());
    return reputation > kHighestEnemyRepute && reputation < kLowestFriendRepute;
}

std::optional<Faction> getObjectFaction(const Object &object) {
    switch (object.type()) {
    case ObjectType::Creature:
        return static_cast<const Creature &>(object).faction();
    case ObjectType::Door:
        return static_cast<const Door &>(object).faction();
    case ObjectType::Placeable:
        return static_cast<const Placeable &>(object).faction();
    case ObjectType::Trigger:
        return static_cast<const Trigger &>(object).faction();
    case ObjectType::Encounter:
        return static_cast<const Encounter &>(object).faction();
    default:
        return std::nullopt;
    }
}

int getObjectReputation(const Object &receiver, const Object &other, const Game &game) {
    // An area of effect stands for its creator, on either side.
    std::shared_ptr<Object> receiverCreator;
    std::shared_ptr<Object> otherCreator;
    const Object *source = &receiver;
    const Object *target = &other;
    if (const auto *areaOfEffect = dyn_cast<AreaOfEffect>(target)) {
        otherCreator = areaOfEffect->creator();
        if (!otherCreator) {
            return kDefaultRepute;
        }
        target = otherCreator.get();
    }
    if (const auto *areaOfEffect = dyn_cast<AreaOfEffect>(source)) {
        receiverCreator = areaOfEffect->creator();
        if (!receiverCreator) {
            return kDefaultRepute;
        }
        source = receiverCreator.get();
    }
    auto sourceFaction = getObjectFaction(*source);
    auto targetFaction = getObjectFaction(*target);
    if (!sourceFaction || !targetFaction) {
        return kDefaultRepute;
    }
    if (source == target) {
        return IReputes::kMaxReputation;
    }

    const auto &reputes = receiver.services().game.reputes;
    const size_t factionCount = reputes.state().factions.size();
    const auto isNPCFaction = [factionCount](Faction faction) {
        const int id = static_cast<int>(faction);
        return id > 0 && static_cast<size_t>(id) < factionCount;
    };
    // A relationship involving the player faction is evaluated from its side.
    if (!isNPCFaction(*targetFaction) && isNPCFaction(*sourceFaction)) {
        std::swap(source, target);
        std::swap(sourceFaction, targetFaction);
    }

    if (const auto *creature = dyn_cast<Creature>(source)) {
        if (const auto *otherCreature = dyn_cast<Creature>(target)) {
            return creature->getReputationToward(*otherCreature);
        }
        if (game.party().isMember(*creature)) {
            sourceFaction = Faction::Player;
        }
    }
    if (!isNPCFaction(*targetFaction)) {
        return kDefaultRepute;
    }
    return reputes.getReputation(*sourceFaction, *targetFaction);
}

} // namespace game

} // namespace reone
