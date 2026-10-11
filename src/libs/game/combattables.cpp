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

#include "reone/game/combattables.h"

#include <boost/algorithm/string/case_conv.hpp>

#include "reone/game/twodautil.h"
#include "reone/resource/2da.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/provider/2das.h"

using namespace reone::resource;

namespace reone {

namespace game {

// The cost tables the rules name directly, besides those iprp_costtable names.
static constexpr const char *kNamedCostTables[] = {
    "iprp_bonuscost", "iprp_meleecost", "iprp_neg5cost", "iprp_resistcost",
    "iprp_soakcost", "iprp_damvulcost", "iprp_onhitdc"};

template <class Row>
static Row rowOrBlank(const std::vector<Row> &rows, int row) {
    return row >= 0 && row < static_cast<int>(rows.size()) ? rows[row] : Row();
}

// A row of iprp_costtable that names no table adds none.
static void readCostTable(ITwoDAs &twoDas, const std::string &resRef,
                          std::unordered_map<std::string, CostTable> &tables) {
    if (resRef.empty() || tables.count(resRef) != 0) {
        return;
    }
    auto table = getRequiredTwoDA(twoDas, resRef);
    CostTable result;
    for (int row = 0; row < table->getRowCount(); ++row) {
        result.rows.push_back({table->getIntOpt(row, "value"), table->getIntOpt(row, "amount")});
    }
    tables.emplace(resRef, std::move(result));
}

void CombatTables::init(ITwoDAs &twoDas, GameID gameId) {
    auto costTables = getRequiredTwoDA(twoDas, "iprp_costtable");
    for (int row = 0; row < costTables->getRowCount(); ++row) {
        _costTableNames.push_back(boost::to_lower_copy(costTables->getString(row, "name")));
        readCostTable(twoDas, _costTableNames.back(), _costTables);
    }
    for (const char *resRef : kNamedCostTables) {
        readCostTable(twoDas, resRef, _costTables);
    }

    // A blank dice cell is zero dice, not a literal damage amount.
    auto damageCosts = getRequiredTwoDA(twoDas, "iprp_damagecost");
    for (int row = 0; row < damageCosts->getRowCount(); ++row) {
        _damageCosts.push_back({damageCosts->getInt(row, "rank", 0),
                                damageCosts->getInt(row, "numdice", 0),
                                damageCosts->getInt(row, "die", 0)});
    }
    auto monsterCosts = getRequiredTwoDA(twoDas, "iprp_monstcost");
    for (int row = 0; row < monsterCosts->getRowCount(); ++row) {
        _monsterDamageCosts.push_back({monsterCosts->getInt(row, "numdice", 0), monsterCosts->getInt(row, "die", 0)});
    }
    _damageTypeCount = getRequiredTwoDA(twoDas, "iprp_damagetype")->getRowCount();
    _protectionCount = getRequiredTwoDA(twoDas, "iprp_protection")->getRowCount();

    auto onHit = getRequiredTwoDA(twoDas, "iprp_onhit");
    for (int row = 0; row < onHit->getRowCount(); ++row) {
        _onHitParameterTables.push_back(onHit->getInt(row, "param1resref", 0));
    }
    auto durations = getRequiredTwoDA(twoDas, "iprp_onhitdur");
    for (int row = 0; row < durations->getRowCount(); ++row) {
        _onHitDurations.push_back({durations->getInt(row, "effectchance", 0),
                                   durations->getInt(row, "durationrounds", 0)});
    }

    // The first column is the label; each further one is an immunity.
    auto gameEffects = getRequiredTwoDA(twoDas, "gameeffects");
    const auto &columns = gameEffects->columns();
    for (int row = 0; row < gameEffects->getRowCount(); ++row) {
        std::vector<int> immunities;
        for (int column = 1; column < static_cast<int>(columns.size()); ++column) {
            if (gameEffects->getInt(row, columns[column], 0) != 0) {
                immunities.push_back(column - 1);
            }
        }
        _gameEffectImmunities.push_back(std::move(immunities));
    }

    auto shields = getRequiredTwoDA(twoDas, "forceshields");
    for (int row = 0; row < shields->getRowCount(); ++row) {
        auto value = [&](const std::string &column) { return shields->getInt(row, column, 0); };
        ForceShieldRow shield;
        shield.visual = value("visualeffectdef");
        for (int index = 0; index < ForceShieldRow::kAppearanceCount; ++index) {
            const std::string suffix = "_0" + std::to_string(index + 1);
            shield.appearances[index] = shields->getIntOpt(row, "appearance" + suffix);
            shield.appearanceVisuals[index] = value("visualeffect" + suffix);
        }
        for (int index = 0; index < ForceShieldRow::kRadiusCount; ++index) {
            shield.radii[index] = shields->getFloat(row, "radius_0" + std::to_string(index));
        }
        shield.damageFlags = value("damageflags");
        shield.vulnerabilities = value("vulnerflags");
        shield.resistance = value("resistance");
        shield.amount = value("amount");
        _forceShields.emplace(boost::to_lower_copy(shields->rows()[row].label), shield);
    }

    auto adjustments = getRequiredTwoDA(twoDas, "forceadjust");
    for (int row = 0; row < adjustments->getRowCount(); ++row) {
        _forceCostMultipliers.push_back({adjustments->getFloat(row, "goodcost", 1.0f),
                                         adjustments->getFloat(row, "evilcost", 1.0f)});
    }
    auto excited = getRequiredTwoDA(twoDas, "excitedduration");
    for (int row = 0; row < excited->getRowCount(); ++row) {
        _excitedDurations.push_back(static_cast<uint32_t>(excited->getInt(row, "duration", 0)));
    }
    auto poisons = getRequiredTwoDA(twoDas, "poison");
    for (int row = 0; row < poisons->getRowCount(); ++row) {
        auto get = [&](const char *column) { return poisons->getInt(row, column, 0); };
        PoisonData poison;
        poison.nameStrRef = get("name");
        poison.difficultyClass = get("dc_save");
        poison.duration = get("duration");
        poison.period = get("period");
        poison.hitPointDamage = get("dam_hp");
        poison.forcePointDamage = get("dam_fp");
        poison.abilityDamage = {get("dam_str"), get("dam_dex"), get("dam_con"),
                                get("dam_int"), get("dam_wis"), get("dam_chr")};
        _poisons.push_back(poison);
    }
    auto stateScripts = getRequiredTwoDA(twoDas, "statescripts");
    for (int row = 0; row < stateScripts->getRowCount(); ++row) {
        _stateScripts.push_back(stateScripts->getString(row, "scriptname"));
    }

    if (gameId == GameID::TSL) {
        auto autoBalance = getRequiredTwoDA(twoDas, "autobalance");
        for (int row = 0; row < autoBalance->getRowCount(); ++row) {
            auto multiplier = [&](const char *column) { return autoBalance->getFloat(row, column, 0.0f); };
            AutoBalanceRow set;
            set.vitalityMultiplier = multiplier("vpmult");
            set.toHitMultiplier = multiplier("tohitmult");
            set.armorClassMultiplier = multiplier("armormult");
            set.damageMultiplier = multiplier("damagemult");
            set.savingThrowMultiplier = multiplier("savemult");
            set.challengeRatingModifier = autoBalance->getInt(row, "crmod", 0);
            set.levelMultiplier = multiplier("levelmult");
            _autoBalance.push_back(set);
        }
    }
}

const CostTable &CombatTables::costTable(const std::string &resRef) const {
    auto table = _costTables.find(resRef);
    if (table == _costTables.end()) {
        throw ResourceNotFoundException("2DA not found: " + resRef);
    }
    return table->second;
}

const CostTable &CombatTables::costTableAt(int index) const {
    validateTwoDARowIndex("iprp_costtable", index, static_cast<int>(_costTableNames.size()));
    return costTable(_costTableNames[index]);
}

DamageCost CombatTables::damageCost(int row) const {
    return rowOrBlank(_damageCosts, row);
}

MonsterDamageCost CombatTables::monsterDamageCost(int row) const {
    return rowOrBlank(_monsterDamageCosts, row);
}

int CombatTables::damageTypeCount() const {
    return _damageTypeCount;
}

int CombatTables::protectionCount() const {
    return _protectionCount;
}

int CombatTables::onHitParameterTable(int subtype) const {
    validateTwoDARowIndex("iprp_onhit", subtype, static_cast<int>(_onHitParameterTables.size()));
    return _onHitParameterTables[subtype];
}

OnHitDuration CombatTables::onHitDuration(int row) const {
    return rowOrBlank(_onHitDurations, row);
}

const std::vector<int> &CombatTables::gameEffectImmunities(int row) const {
    validateTwoDARowIndex("gameeffects", row, static_cast<int>(_gameEffectImmunities.size()));
    return _gameEffectImmunities[row];
}

std::optional<std::reference_wrapper<const ForceShieldRow>> CombatTables::findForceShield(int shield) const {
    auto row = _forceShields.find(std::to_string(shield));
    if (row == _forceShields.end()) {
        return std::nullopt;
    }
    return std::cref(row->second);
}

ForceShieldRow CombatTables::forceShield(int shield) const {
    auto row = findForceShield(shield);
    return row ? row->get() : ForceShieldRow();
}

float CombatTables::forceCostMultiplier(int row, bool good) const {
    validateTwoDARowIndex("forceadjust", row, static_cast<int>(_forceCostMultipliers.size()));
    const auto &multipliers = _forceCostMultipliers[row];
    return good ? multipliers.good : multipliers.evil;
}

uint32_t CombatTables::excitedDuration(int row) const {
    validateTwoDARowIndex("excitedduration", row, static_cast<int>(_excitedDurations.size()));
    return _excitedDurations[row];
}

PoisonData CombatTables::poison(int row) const {
    return rowOrBlank(_poisons, row);
}

const std::string &CombatTables::stateScript(int row) const {
    validateTwoDARowIndex("statescripts", row, static_cast<int>(_stateScripts.size()));
    return _stateScripts[row];
}

AutoBalanceRow CombatTables::autoBalance(int multiplierSet) const {
    return rowOrBlank(_autoBalance, multiplierSet);
}

} // namespace game

} // namespace reone
