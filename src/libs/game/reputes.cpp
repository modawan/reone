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

#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include "reone/resource/provider/2das.h"

#include "reone/game/object/creature.h"

using namespace reone::resource;

namespace reone {

namespace game {

static constexpr int kDefaultRepute = 50;
static constexpr int kMinRepute = 0;
static constexpr int kMaxRepute = 100;

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

    // Odyssey preserves saved faction IDs by list order, then appends only the
    // base-table rows beyond the saved list's length.
    for (int row = static_cast<int>(factions.size()); row < repute->getRowCount(); ++row) {
        std::string label = boost::to_lower_copy(repute->getString(row, "label"));
        factions.push_back({label});
        labels.push_back(std::move(label));
    }

    std::vector<std::vector<int>> values;
    loadBase(*repute, factions.size(), values);

    for (const auto &saved : repList) {
        uint32_t factionId1 = 0;
        uint32_t factionId2 = 0;
        uint32_t reputation = 0;
        if (!saved->readDword(factionId1, "FactionID1") ||
            !saved->readDword(factionId2, "FactionID2") ||
            !saved->readDword(reputation, "FactionRep")) {
            return std::nullopt;
        }

        // FAC stores the target as ID1 and the NPC source as ID2. Player (0)
        // is not an NPC source; invalid pairs are ignored by the retail setter.
        if (factionId1 >= values.size() || factionId2 == 0 || factionId2 >= values.size()) {
            continue;
        }
        values[factionId2][factionId1] = std::clamp(
            static_cast<int64_t>(reputation),
            static_cast<int64_t>(kMinRepute),
            static_cast<int64_t>(kMaxRepute));
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

    values.assign(factionCount, std::vector<int>(factionCount, kMaxRepute));
    size_t baseCount = std::min(factionCount, static_cast<size_t>(repute.getRowCount()));
    std::vector<std::string> baseLabels;
    baseLabels.reserve(baseCount);
    for (size_t index = 0; index < baseCount; ++index) {
        baseLabels.push_back(boost::to_lower_copy(repute.getString(static_cast<int>(index), "label")));
    }

    // Preserve Reone's established source-row/target-column runtime contract
    // for authored base relationships. Cells outside the base table retain the
    // retail faction-manager initialization value of 100.
    for (size_t row = 0; row < baseCount; ++row) {
        for (size_t column = 0; column < baseCount; ++column) {
            const std::string &label = baseLabels[column];
            if (label == "player" || label == "glb_xor") {
                values[row][column] = kDefaultRepute;
            } else {
                values[row][column] = repute.getInt(
                    static_cast<int>(row), label, kDefaultRepute);
            }
        }
    }
}

int Reputes::getReputation(Faction sourceFaction, Faction targetFaction) const {
    int source = static_cast<int>(sourceFaction);
    int target = static_cast<int>(targetFaction);

    if (source < 0 || source >= static_cast<int>(_factionValues.size()) ||
        target < 0 || target >= static_cast<int>(_factionValues[source].size()))
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
        std::clamp(adjusted, static_cast<int64_t>(kMinRepute), static_cast<int64_t>(kMaxRepute)));
}

bool Reputes::getIsEnemy(const Creature &source, const Creature &target) const {
    return getIsEnemy(source.faction(), target.faction());
}

bool Reputes::getIsEnemy(Faction sourceFaction, Faction targetFaction) const {
    return getReputation(sourceFaction, targetFaction) < 50;
}

bool Reputes::getIsFriend(const Creature &source, const Creature &target) const {
    return getReputation(source.faction(), target.faction()) > 50;
}

bool Reputes::getIsNeutral(const Creature &source, const Creature &target) const {
    return getReputation(source.faction(), target.faction()) == 50;
}

} // namespace game

} // namespace reone
