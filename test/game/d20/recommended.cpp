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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "reone/game/d20/attributes.h"
#include "reone/game/d20/classes.h"
#include "reone/game/d20/feats.h"
#include "reone/game/d20/races.h"
#include "reone/game/d20/recommended.h"
#include "reone/game/d20/spells.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/party.h"
#include "reone/resource/2da.h"

#include "../../fixtures/engine.h"
#include "../../fixtures/game.h"
#include "../../fixtures/resource.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

using Row = std::unordered_map<std::string, std::string>;

std::shared_ptr<TwoDA> makeTable(
    const std::vector<std::string> &columns,
    const std::vector<std::vector<std::string>> &rows) {
    TwoDA::Builder builder;
    builder.columns(columns);
    for (const auto &row : rows) {
        builder.row(row);
    }
    return std::shared_ptr<TwoDA>(builder.build());
}

std::shared_ptr<TwoDA> makeSparseTable(
    const std::vector<std::string> &columns,
    int rowCount,
    const std::unordered_map<int, Row> &values) {
    TwoDA::Builder builder;
    builder.columns(columns);
    for (int row = 0; row < rowCount; ++row) {
        std::vector<std::string> cells(columns.size());
        auto maybeRow = values.find(row);
        if (maybeRow != values.end()) {
            for (size_t column = 0; column < columns.size(); ++column) {
                auto maybeValue = maybeRow->second.find(columns[column]);
                if (maybeValue != maybeRow->second.end()) {
                    cells[column] = maybeValue->second;
                }
            }
        }
        builder.row(std::move(cells));
    }
    return std::shared_ptr<TwoDA>(builder.build());
}

// The KotOR classes.2da rows of the starting, Jedi and droid classes. Only
// the four classes whose feats these tests choose name a feats table.
std::shared_ptr<TwoDA> makeClassesTable() {
    const std::vector<std::string> columns = {
        "name", "description", "hitdie", "skillpointbase", "str", "dex", "con", "int", "wis", "cha", "primaryabil",
        "skillstable", "savingthrowtable", "attackbonustable", "featstable", "featgain", "spellgaintable"};
    std::vector<std::vector<std::string>> rows(17, std::vector<std::string>(columns.size()));
    rows[0] = {"1", "2", "10", "2", "16", "14", "14", "10", "12", "10", "STR", "SOL", "save", "attack", "SOL", "", ""};
    rows[1] = {"1", "2", "8", "6", "12", "16", "12", "14", "12", "10", "DEX", "SCT", "save", "attack", "SCT", "", ""};
    rows[2] = {"1", "2", "6", "8", "10", "16", "10", "14", "12", "14", "DEX", "SCD", "save", "attack", "SCD", "", ""};
    rows[3] = {"1", "2", "10", "2", "14", "14", "14", "12", "14", "10", "STR", "JGD", "save", "attack", "", "", ""};
    rows[4] = {"1", "2", "6", "2", "10", "16", "10", "14", "14", "12", "WIS", "JCN", "save", "attack", "JCN", "", ""};
    rows[5] = {"1", "2", "8", "4", "12", "14", "14", "14", "10", "12", "WIS", "JSN", "save", "attack", "", "", ""};
    rows[6] = {"1", "2", "8", "2", "14", "14", "16", "8", "8", "8", "CON", "DRC", "save", "attack", "", "", ""};
    rows[7] = {"1", "2", "6", "4", "10", "14", "10", "16", "16", "8", "DEX", "DRX", "save", "attack", "", "", ""};
    return makeTable(columns, rows);
}

// The KotOR skills.2da class and recommendation columns.
std::shared_ptr<TwoDA> makeSkillsTable() {
    const std::vector<std::string> columns = {
        "label",
        "scd_class", "scd_reco", "sol_class", "sol_reco", "sct_class", "sct_reco", "jcn_class", "jcn_reco",
        "jgd_class", "jgd_reco", "jsn_class", "jsn_reco", "drx_class", "drx_reco", "drc_class", "drc_reco"};
    return makeTable(columns, {
        {"ComputerUse", "0", "7", "0", "6", "1", "1", "1", "1", "0", "5", "0", "3", "1", "2", "1", "2"},
        {"Demolitions", "1", "2", "1", "3", "1", "2", "1", "3", "0", "4", "0", "4", "0", "4", "1", "3"},
        {"Stealth", "1", "1", "0", "7", "0", "7", "0", "6", "0", "7", "0", "5", "0", "", "0", ""},
        {"Awareness", "1", "4", "1", "2", "1", "4", "1", "5", "1", "2", "1", "1", "0", "5", "1", "5"},
        {"Persuade", "1", "8", "0", "", "0", "", "1", "", "1", "", "1", "", "0", "", "0", ""},
        {"Repair", "0", "6", "0", "4", "1", "3", "1", "2", "0", "3", "0", "6", "1", "1", "1", "1"},
        {"Security", "1", "3", "0", "5", "0", "6", "0", "7", "0", "6", "0", "7", "1", "3", "1", "4"},
        {"TreatInjury", "0", "5", "1", "1", "1", "5", "1", "4", "1", "1", "1", "2", "0", "", "0", ""}});
}

// The KotOR feat.2da rows that Soldiers, Scouts, Scoundrels and Jedi
// Consulars are recommended, with Jedi Defense, which Consulars are granted.
std::shared_ptr<TwoDA> makeFeatTable() {
    const std::vector<std::string> columns = {
        "name", "mincharlevel", "minattackbonus", "prereqfeat1", "prereqfeat2",
        "sol_list", "sol_granted", "sol_recom", "sct_list", "sct_granted", "sct_recom",
        "scd_list", "scd_granted", "scd_recom", "jcn_list", "jcn_granted", "jcn_recom"};
    std::unordered_map<int, Row> rows {
        {1, {{"name", "1"}, {"mincharlevel", "4"}, {"prereqfeat1", "55"}, {"sol_list", "4"}, {"sol_granted", "-1"}, {"sct_list", "4"}, {"sct_granted", "-1"}, {"scd_list", "4"}, {"scd_granted", "-1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}, {"jcn_recom", "1"}}}, // ADVANCED_JEDI_DEFENSE
        {3, {{"name", "1"}, {"sol_list", "1"}, {"sol_granted", "-1"}, {"sol_recom", "21"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"sct_recom", "3"}, {"scd_list", "0"}, {"scd_granted", "-1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}}}, // TWO_WEAPON_FIGHTING
        {8, {{"name", "1"}, {"sol_list", "1"}, {"sol_granted", "-1"}, {"sol_recom", "18"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"sct_recom", "2"}, {"scd_list", "3"}, {"scd_granted", "1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}, {"jcn_recom", "8"}}}, // CRITICAL_STRIKE
        {9, {{"name", "1"}, {"mincharlevel", "4"}, {"prereqfeat1", "3"}, {"sol_list", "1"}, {"sol_granted", "-1"}, {"sol_recom", "22"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"sct_recom", "7"}, {"scd_list", "0"}, {"scd_granted", "-1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}}}, // TWO_WEAPON_ADVANCED
        {10, {{"name", "1"}, {"sol_list", "0"}, {"sol_granted", "-1"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"scd_list", "1"}, {"scd_granted", "-1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}, {"jcn_recom", "4"}}}, // EMPATHY
        {11, {{"name", "1"}, {"sol_list", "1"}, {"sol_granted", "-1"}, {"sct_list", "3"}, {"sct_granted", "1"}, {"sct_recom", "1"}, {"scd_list", "0"}, {"scd_granted", "-1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}}}, // FLURRY
        {12, {{"name", "1"}, {"sol_list", "0"}, {"sol_granted", "-1"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"scd_list", "1"}, {"scd_granted", "-1"}, {"scd_recom", "3"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}}}, // GEAR_HEAD
        {13, {{"name", "1"}, {"sol_list", "0"}, {"sol_granted", "-1"}, {"sol_recom", "5"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"scd_list", "0"}, {"scd_granted", "-1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}}}, // CONDITIONING
        {17, {{"name", "1"}, {"mincharlevel", "4"}, {"prereqfeat1", "28"}, {"sol_list", "1"}, {"sol_granted", "-1"}, {"sol_recom", "16"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"sct_recom", "8"}, {"scd_list", "0"}, {"scd_granted", "-1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}}}, // IMPROVED_POWER_ATTACK
        {18, {{"name", "1"}, {"mincharlevel", "4"}, {"prereqfeat1", "29"}, {"sol_list", "1"}, {"sol_granted", "-1"}, {"sol_recom", "7"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"scd_list", "0"}, {"scd_granted", "-1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}}}, // IMPROVED_POWER_BLAST
        {19, {{"name", "1"}, {"mincharlevel", "4"}, {"prereqfeat1", "8"}, {"sol_list", "1"}, {"sol_granted", "-1"}, {"sol_recom", "19"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"sct_recom", "6"}, {"scd_list", "0"}, {"scd_granted", "-1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}, {"jcn_recom", "9"}}}, // IMPROVED_CRITICAL_STRIKE
        {20, {{"name", "1"}, {"mincharlevel", "4"}, {"prereqfeat1", "31"}, {"sol_list", "1"}, {"sol_granted", "-1"}, {"sol_recom", "8"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"scd_list", "0"}, {"scd_granted", "-1"}, {"scd_recom", "4"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}}}, // IMPROVED_SNIPER_SHOT
        {21, {{"name", "1"}, {"mincharlevel", "4"}, {"prereqfeat1", "13"}, {"sol_list", "0"}, {"sol_granted", "-1"}, {"sol_recom", "9"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"scd_list", "0"}, {"scd_granted", "-1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}}}, // IMPROVED_CONDITIONING
        {22, {{"name", "1"}, {"mincharlevel", "8"}, {"prereqfeat1", "13"}, {"prereqfeat2", "21"}, {"sol_list", "0"}, {"sol_granted", "-1"}, {"sol_recom", "13"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"scd_list", "0"}, {"scd_granted", "-1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}}}, // MASTER_CONDITIONING
        {24, {{"name", "1"}, {"mincharlevel", "8"}, {"prereqfeat1", "55"}, {"prereqfeat2", "1"}, {"sol_list", "4"}, {"sol_granted", "-1"}, {"sct_list", "4"}, {"sct_granted", "-1"}, {"scd_list", "4"}, {"scd_granted", "-1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}, {"jcn_recom", "2"}}}, // MASTER_JEDI_DEFENSE
        {26, {{"name", "1"}, {"mincharlevel", "8"}, {"prereqfeat1", "30"}, {"prereqfeat2", "92"}, {"sol_list", "1"}, {"sol_granted", "-1"}, {"sol_recom", "11"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"scd_list", "0"}, {"scd_granted", "-1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}}}, // MULTI_SHOT
        {28, {{"name", "1"}, {"sol_list", "3"}, {"sol_granted", "1"}, {"sol_recom", "15"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"sct_recom", "4"}, {"scd_list", "0"}, {"scd_granted", "-1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}}}, // POWER_ATTACK
        {29, {{"name", "1"}, {"sol_list", "3"}, {"sol_granted", "1"}, {"sol_recom", "2"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"scd_list", "0"}, {"scd_granted", "-1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}}}, // POWER_BLAST
        {30, {{"name", "1"}, {"sol_list", "1"}, {"sol_granted", "-1"}, {"sol_recom", "1"}, {"sct_list", "3"}, {"sct_granted", "1"}, {"scd_list", "0"}, {"scd_granted", "-1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}}}, // RAPID_SHOT
        {31, {{"name", "1"}, {"sol_list", "1"}, {"sol_granted", "-1"}, {"sol_recom", "3"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"scd_list", "3"}, {"scd_granted", "1"}, {"scd_recom", "2"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}}}, // SNIPER_SHOT
        {53, {{"name", "1"}, {"mincharlevel", "8"}, {"prereqfeat1", "11"}, {"prereqfeat2", "91"}, {"sol_list", "1"}, {"sol_granted", "-1"}, {"sol_recom", "23"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"sct_recom", "9"}, {"scd_list", "0"}, {"scd_granted", "-1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}}}, // WHIRLWIND_ATTACK
        {55, {{"name", "1"}, {"sol_list", "4"}, {"sol_granted", "-1"}, {"sct_list", "4"}, {"sct_granted", "-1"}, {"scd_list", "4"}, {"scd_granted", "-1"}, {"jcn_list", "3"}, {"jcn_granted", "1"}}}, // JEDI_DEFENSE
        {77, {{"name", "1"}, {"mincharlevel", "8"}, {"prereqfeat1", "31"}, {"prereqfeat2", "20"}, {"sol_list", "1"}, {"sol_granted", "-1"}, {"sol_recom", "13"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"scd_list", "0"}, {"scd_granted", "-1"}, {"scd_recom", "7"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}}}, // MASTER_SNIPER_SHOT
        {81, {{"name", "1"}, {"mincharlevel", "8"}, {"prereqfeat1", "8"}, {"prereqfeat2", "19"}, {"sol_list", "1"}, {"sol_granted", "-1"}, {"sol_recom", "20"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"sct_recom", "10"}, {"scd_list", "0"}, {"scd_granted", "-1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}, {"jcn_recom", "11"}}}, // MASTER_CRITICAL_STRIKE
        {82, {{"name", "1"}, {"mincharlevel", "8"}, {"prereqfeat1", "29"}, {"prereqfeat2", "18"}, {"sol_list", "1"}, {"sol_granted", "-1"}, {"sol_recom", "12"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"scd_list", "0"}, {"scd_granted", "-1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}}}, // MASTER_POWER_BLAST
        {83, {{"name", "1"}, {"mincharlevel", "8"}, {"prereqfeat1", "28"}, {"prereqfeat2", "17"}, {"sol_list", "1"}, {"sol_granted", "-1"}, {"sol_recom", "17"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"sct_recom", "12"}, {"scd_list", "0"}, {"scd_granted", "-1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}}}, // MASTER_POWER_ATTACK
        {84, {{"name", "1"}, {"sol_list", "1"}, {"sol_granted", "-1"}, {"sol_recom", "4"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"sct_recom", "13"}, {"scd_list", "0"}, {"scd_granted", "-1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}}}, // TOUGHNESS
        {85, {{"name", "1"}, {"mincharlevel", "8"}, {"prereqfeat1", "3"}, {"prereqfeat2", "9"}, {"sol_list", "1"}, {"sol_granted", "-1"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"sct_recom", "11"}, {"scd_list", "0"}, {"scd_granted", "-1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}}}, // TWO_WEAPON_MASTERY
        {91, {{"name", "1"}, {"mincharlevel", "4"}, {"prereqfeat1", "11"}, {"sol_list", "1"}, {"sol_granted", "-1"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"sct_recom", "5"}, {"scd_list", "0"}, {"scd_granted", "-1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}}}, // IMPROVED_FLURRY
        {92, {{"name", "1"}, {"mincharlevel", "4"}, {"prereqfeat1", "30"}, {"sol_list", "1"}, {"sol_granted", "-1"}, {"sol_recom", "6"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"scd_list", "0"}, {"scd_granted", "-1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}}}, // IMPROVED_RAPID_SHOT
        {113, {{"name", "1"}, {"sol_list", "1"}, {"sol_granted", "-1"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"scd_list", "0"}, {"scd_granted", "-1"}, {"scd_recom", "1"}, {"jcn_list", "0"}, {"jcn_granted", "-1"}, {"jcn_recom", "3"}}}, // DUELING
        {114, {{"name", "1"}, {"mincharlevel", "4"}, {"prereqfeat1", "113"}, {"sol_list", "1"}, {"sol_granted", "-1"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"scd_list", "0"}, {"scd_granted", "-1"}, {"scd_recom", "5"}, {"jcn_list", "0"}, {"jcn_granted", "-1"}, {"jcn_recom", "5"}}}, // ADVANCED_DUELING
        {115, {{"name", "1"}, {"mincharlevel", "8"}, {"prereqfeat1", "113"}, {"prereqfeat2", "114"}, {"sol_list", "1"}, {"sol_granted", "-1"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"scd_list", "0"}, {"scd_granted", "-1"}, {"scd_recom", "8"}, {"jcn_list", "0"}, {"jcn_granted", "-1"}, {"jcn_recom", "7"}}}, // MASTER_DUELING
        {119, {{"name", "1"}, {"mincharlevel", "4"}, {"prereqfeat1", "12"}, {"sol_list", "0"}, {"sol_granted", "-1"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"scd_list", "1"}, {"scd_granted", "-1"}, {"scd_recom", "6"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}}}, // GEAR_HEAD_ADEPT
        {120, {{"name", "1"}, {"mincharlevel", "8"}, {"prereqfeat1", "12"}, {"prereqfeat2", "119"}, {"sol_list", "0"}, {"sol_granted", "-1"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"scd_list", "1"}, {"scd_granted", "-1"}, {"scd_recom", "9"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}}}, // GEAR_HEAD_MASTER
        {121, {{"name", "1"}, {"mincharlevel", "4"}, {"prereqfeat1", "10"}, {"sol_list", "0"}, {"sol_granted", "-1"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"scd_list", "1"}, {"scd_granted", "-1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}, {"jcn_recom", "6"}}}, // IMPROVED_EMPATHY
        {122, {{"name", "1"}, {"mincharlevel", "8"}, {"prereqfeat1", "10"}, {"prereqfeat2", "121"}, {"sol_list", "0"}, {"sol_granted", "-1"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"scd_list", "1"}, {"scd_granted", "-1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}, {"jcn_recom", "10"}}}, // MASTER__EMPATHY
        {123, {{"name", "1"}, {"mincharlevel", "4"}, {"prereqfeat1", "84"}, {"sol_list", "1"}, {"sol_granted", "-1"}, {"sol_recom", "10"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"sct_recom", "14"}, {"scd_list", "0"}, {"scd_granted", "-1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}}}, // IMPROVED_TOUGHNESS
        {124, {{"name", "1"}, {"mincharlevel", "8"}, {"prereqfeat1", "84"}, {"prereqfeat2", "123"}, {"sol_list", "1"}, {"sol_granted", "-1"}, {"sol_recom", "14"}, {"sct_list", "1"}, {"sct_granted", "-1"}, {"sct_recom", "15"}, {"scd_list", "0"}, {"scd_granted", "-1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}}}, // MASTER_TOUGHNESS
    };
    return makeSparseTable(columns, 125, rows);
}

// The KotOR spells.2da force power rows, with the columns the level-up's
// choice of powers reads.
std::shared_ptr<TwoDA> makeSpellsTable() {
    const std::vector<std::string> columns = {
        "usertype", "prerequisites", "guardian", "consular", "sentinel", "light_recom", "dark_recom"};
    std::unordered_map<int, Row> rows {
        {4, {{"usertype", "1"}, {"prerequisites", "49"}, {"guardian", "9"}, {"consular", "9"}, {"sentinel", "9"}, {"light_recom", "9"}, {"dark_recom", "22"}}}, // LIGHT_SABER_THROW_ADVANCED
        {6, {{"usertype", "1"}, {"guardian", "0"}, {"consular", "0"}, {"sentinel", "0"}, {"light_recom", "28"}, {"dark_recom", "29"}}}, // AFFECT_MIND
        {7, {{"usertype", "1"}, {"prerequisites", "45"}, {"guardian", "6"}, {"consular", "6"}, {"sentinel", "6"}, {"dark_recom", "10"}}}, // AFFLICTION
        {8, {{"usertype", "1"}, {"guardian", "0"}, {"consular", "0"}, {"sentinel", "0"}, {"light_recom", "16"}, {"dark_recom", "18"}}}, // SPEED_BURST
        {9, {{"usertype", "1"}, {"prerequisites", "50"}, {"guardian", "9"}, {"consular", "9"}, {"sentinel", "9"}, {"dark_recom", "5"}}}, // CHOKE
        {10, {{"usertype", "1"}, {"guardian", "6"}, {"consular", "6"}, {"sentinel", "6"}, {"light_recom", "6"}}}, // CURE
        {11, {{"usertype", "1"}, {"prerequisites", "15"}, {"guardian", "18"}, {"consular", "18"}, {"sentinel", "18"}, {"dark_recom", "8"}}}, // DEATH_FIELD
        {12, {{"usertype", "1"}, {"prerequisites", "47"}, {"guardian", "6"}, {"consular", "6"}, {"sentinel", "6"}, {"light_recom", "11"}}}, // DROID_DISABLE
        {13, {{"usertype", "1"}, {"prerequisites", "47_12"}, {"guardian", "12"}, {"consular", "12"}, {"sentinel", "12"}, {"light_recom", "12"}}}, // DROID_DESTROY
        {14, {{"usertype", "1"}, {"prerequisites", "6"}, {"guardian", "6"}, {"consular", "6"}, {"sentinel", "6"}, {"light_recom", "29"}, {"dark_recom", "30"}}}, // DOMINATE
        {15, {{"usertype", "1"}, {"guardian", "9"}, {"consular", "9"}, {"sentinel", "9"}, {"dark_recom", "7"}}}, // DRAIN_LIFE
        {16, {{"usertype", "1"}, {"guardian", "0"}, {"consular", "0"}, {"sentinel", "0"}, {"dark_recom", "12"}}}, // FEAR
        {17, {{"usertype", "1"}, {"prerequisites", "18_24"}, {"guardian", "12"}, {"consular", "12"}, {"sentinel", "12"}, {"light_recom", "15"}}}, // FORCE_ARMOR
        {18, {{"usertype", "1"}, {"guardian", "0"}, {"consular", "0"}, {"sentinel", "0"}, {"light_recom", "13"}}}, // FORCE_AURA
        {19, {{"usertype", "1"}, {"prerequisites", "48"}, {"guardian", "15"}, {"consular", "15"}, {"sentinel", "15"}, {"light_recom", "27"}, {"dark_recom", "24"}}}, // FORCE_BREACH
        {20, {{"usertype", "1"}, {"prerequisites", "41"}, {"guardian", "15"}, {"consular", "15"}, {"sentinel", "15"}, {"light_recom", "25"}, {"dark_recom", "26"}}}, // FORCE_IMMUNITY
        {22, {{"usertype", "1"}, {"guardian", "0"}, {"consular", "0"}, {"sentinel", "0"}, {"light_recom", "19"}}}, // FORCE_VALOR
        {23, {{"usertype", "1"}, {"guardian", "0"}, {"consular", "0"}, {"sentinel", "0"}, {"light_recom", "4"}, {"dark_recom", "15"}}}, // FORCE_PUSH
        {24, {{"usertype", "1"}, {"prerequisites", "18"}, {"guardian", "6"}, {"consular", "6"}, {"sentinel", "6"}, {"light_recom", "14"}}}, // FORCE_SHIELD
        {25, {{"usertype", "1"}, {"prerequisites", "43_35"}, {"guardian", "18"}, {"consular", "18"}, {"sentinel", "18"}, {"dark_recom", "3"}}}, // FORCE_STORM
        {26, {{"usertype", "1"}, {"prerequisites", "23_27"}, {"guardian", "15"}, {"consular", "15"}, {"sentinel", "15"}, {"light_recom", "6"}, {"dark_recom", "17"}}}, // FORCE_WAVE
        {27, {{"usertype", "1"}, {"prerequisites", "23"}, {"guardian", "9"}, {"consular", "9"}, {"sentinel", "9"}, {"light_recom", "5"}, {"dark_recom", "16"}}}, // FORCE_WHIRLWIND
        {28, {{"usertype", "1"}, {"prerequisites", "10"}, {"guardian", "12"}, {"consular", "12"}, {"sentinel", "12"}, {"light_recom", "7"}}}, // HEAL
        {29, {{"usertype", "1"}, {"prerequisites", "46"}, {"guardian", "9"}, {"consular", "9"}, {"sentinel", "9"}, {"light_recom", "2"}}}, // STASIS
        {30, {{"usertype", "1"}, {"prerequisites", "16"}, {"guardian", "6"}, {"consular", "6"}, {"sentinel", "6"}, {"dark_recom", "13"}}}, // HORROR
        {31, {{"usertype", "1"}, {"prerequisites", "16_30"}, {"guardian", "12"}, {"consular", "12"}, {"sentinel", "12"}, {"dark_recom", "14"}}}, // INSANITY
        {32, {{"usertype", "1"}, {"prerequisites", "50_9"}, {"guardian", "12"}, {"consular", "12"}, {"sentinel", "12"}, {"dark_recom", "6"}}}, // KILL
        {33, {{"usertype", "1"}, {"prerequisites", "22"}, {"guardian", "9"}, {"consular", "9"}, {"sentinel", "9"}, {"light_recom", "20"}}}, // KNIGHT_VALOR
        {34, {{"usertype", "1"}, {"prerequisites", "8"}, {"guardian", "9"}, {"consular", "9"}, {"sentinel", "9"}, {"light_recom", "17"}, {"dark_recom", "19"}}}, // KNIGHT_SPEED
        {35, {{"usertype", "1"}, {"prerequisites", "43"}, {"guardian", "9"}, {"consular", "9"}, {"sentinel", "9"}, {"dark_recom", "2"}}}, // LIGHTNING
        {36, {{"usertype", "1"}, {"prerequisites", "22_33"}, {"guardian", "15"}, {"consular", "15"}, {"sentinel", "15"}, {"light_recom", "21"}}}, // MASTER_VALOR
        {37, {{"usertype", "1"}, {"prerequisites", "8_34"}, {"guardian", "15"}, {"consular", "15"}, {"sentinel", "15"}, {"light_recom", "18"}, {"dark_recom", "20"}}}, // SPEED_MASTERY
        {38, {{"usertype", "1"}, {"prerequisites", "7_45"}, {"guardian", "12"}, {"consular", "12"}, {"sentinel", "12"}, {"dark_recom", "11"}}}, // PLAGUE
        {40, {{"usertype", "1"}, {"prerequisites", "42"}, {"guardian", "9"}, {"consular", "9"}, {"sentinel", "9"}, {"light_recom", "23"}, {"dark_recom", "28"}}}, // RESIST_ENERGY_2
        {41, {{"usertype", "1"}, {"guardian", "9"}, {"consular", "9"}, {"sentinel", "9"}, {"light_recom", "24"}, {"dark_recom", "25"}}}, // RESIST_FORCE
        {42, {{"usertype", "1"}, {"guardian", "0"}, {"consular", "0"}, {"sentinel", "0"}, {"light_recom", "22"}, {"dark_recom", "27"}}}, // RESIST_ENERGY_1
        {43, {{"usertype", "1"}, {"guardian", "0"}, {"consular", "0"}, {"sentinel", "0"}, {"dark_recom", "1"}}}, // SHOCK
        {44, {{"usertype", "1"}, {"prerequisites", "46_29"}, {"guardian", "15"}, {"consular", "15"}, {"sentinel", "15"}, {"light_recom", "3"}}}, // MASS_STASIS
        {45, {{"usertype", "1"}, {"guardian", "0"}, {"consular", "0"}, {"sentinel", "0"}, {"dark_recom", "9"}}}, // SLOW
        {46, {{"usertype", "1"}, {"guardian", "0"}, {"consular", "0"}, {"sentinel", "0"}, {"light_recom", "1"}}}, // STUN
        {47, {{"usertype", "1"}, {"guardian", "0"}, {"consular", "0"}, {"sentinel", "0"}, {"light_recom", "10"}}}, // DROID_STUN
        {48, {{"usertype", "1"}, {"guardian", "9"}, {"consular", "9"}, {"sentinel", "9"}, {"light_recom", "26"}, {"dark_recom", "23"}}}, // SUPRESS_FORCE
        {49, {{"usertype", "1"}, {"guardian", "0"}, {"consular", "0"}, {"sentinel", "0"}, {"light_recom", "8"}, {"dark_recom", "21"}}}, // LIGHT_SABER_THROW
        {50, {{"usertype", "1"}, {"guardian", "0"}, {"consular", "0"}, {"sentinel", "0"}, {"dark_recom", "4"}}}, // WOUND
    };
    return makeSparseTable(columns, 51, rows);
}

// Feat rows, by their feat.2da rows.
constexpr auto kAdvancedJediDefense = static_cast<FeatType>(1);
constexpr auto kCriticalStrike = static_cast<FeatType>(8);
constexpr auto kFlurry = static_cast<FeatType>(11);
constexpr auto kGearHead = static_cast<FeatType>(12);
constexpr auto kConditioning = static_cast<FeatType>(13);
constexpr auto kImprovedPowerBlast = static_cast<FeatType>(18);
constexpr auto kImprovedSniperShot = static_cast<FeatType>(20);
constexpr auto kImprovedConditioning = static_cast<FeatType>(21);
constexpr auto kMasterConditioning = static_cast<FeatType>(22);
constexpr auto kMasterJediDefense = static_cast<FeatType>(24);
constexpr auto kMultiShot = static_cast<FeatType>(26);
constexpr auto kPowerAttack = static_cast<FeatType>(28);
constexpr auto kPowerBlast = static_cast<FeatType>(29);
constexpr auto kRapidShot = static_cast<FeatType>(30);
constexpr auto kSniperShot = static_cast<FeatType>(31);
constexpr auto kJediDefense = static_cast<FeatType>(55);
constexpr auto kMasterSniperShot = static_cast<FeatType>(77);
constexpr auto kMasterPowerBlast = static_cast<FeatType>(82);
constexpr auto kToughness = static_cast<FeatType>(84);
constexpr auto kImprovedRapidShot = static_cast<FeatType>(92);
constexpr auto kDueling = static_cast<FeatType>(113);
constexpr auto kImprovedToughness = static_cast<FeatType>(123);
constexpr auto kMasterToughness = static_cast<FeatType>(124);

// Power rows, by their spells.2da rows.
constexpr auto kAffectMind = static_cast<SpellType>(6);
constexpr auto kCure = static_cast<SpellType>(10);
constexpr auto kForceWave = static_cast<SpellType>(26);
constexpr auto kForceWhirlwind = static_cast<SpellType>(27);
constexpr auto kMassStasis = static_cast<SpellType>(44);
constexpr auto kForcePush = static_cast<SpellType>(23);
constexpr auto kStasis = static_cast<SpellType>(29);
constexpr auto kShock = static_cast<SpellType>(43);
constexpr auto kStun = static_cast<SpellType>(46);
constexpr auto kWound = static_cast<SpellType>(50);

class RecommendedTest : public Test {
protected:
    NiceMock<MockTextures> textures;
    NiceMock<MockAudioClips> audioClips;
    NiceMock<MockModels> models;
    NiceMock<MockStrings> strings;
    NiceMock<MockTwoDAs> twoDas;
    NiceMock<MockSkills> skills;
    Races races;
    std::unique_ptr<Classes> classes;
    std::unique_ptr<Feats> feats;
    std::unique_ptr<Spells> spells;

    void SetUp() override {
        ON_CALL(twoDas, get("classes")).WillByDefault(Return(makeClassesTable()));
        ON_CALL(twoDas, get("skills")).WillByDefault(Return(makeSkillsTable()));
        ON_CALL(twoDas, get("save")).WillByDefault(Return(makeTable({"level", "fortsave", "refsave", "willsave"}, {{"1", "0", "0", "0"}})));
        ON_CALL(twoDas, get("attack")).WillByDefault(Return(makeTable({"bab"}, {{"0"}})));
        ON_CALL(twoDas, get("spells")).WillByDefault(Return(makeSpellsTable()));
        ON_CALL(twoDas, get("ranges")).WillByDefault(Return(std::shared_ptr<TwoDA>(TwoDA::Builder().build())));
        // The races up to Human, none of which grants feats.
        ON_CALL(twoDas, get("racialtypes")).WillByDefault(Return(makeTable({"featstable"}, std::vector<std::vector<std::string>>(static_cast<int>(RacialType::Human) + 1, {""}))));
        races.init(twoDas);
        useFeatTable(makeFeatTable());

        spells = std::make_unique<Spells>(textures, audioClips, models, strings, twoDas);
        spells->init(GameID::KotOR);
    }

    void useFeatTable(std::shared_ptr<TwoDA> featTable) {
        ON_CALL(twoDas, get("feat")).WillByDefault(Return(std::move(featTable)));
        classes = std::make_unique<Classes>(strings, twoDas);
        feats = std::make_unique<Feats>(textures, strings, twoDas, races, skills);
        feats->init();
    }

    const CreatureClass &getClass(ClassType type) {
        return *classes->get(type);
    }

    /** A new character of the class, with its class's first-level feats and no skill ranks. */
    CreatureAttributes newCharacter(ClassType type) {
        CreatureAttributes attributes(getClass(type).defaultAttributes());
        feats->addGrantedFeats(attributes, RacialType::Human, 0, kObjectTagPlayer, false, true);
        return attributes;
    }

    /** The character with another level of its newest class, and the feats that level grants. */
    void addLevel(CreatureAttributes &attributes) {
        attributes.addClassLevels(attributes.classLevels().back().first, 1);
        feats->addGrantedFeats(attributes, RacialType::Human, 1, kObjectTagPlayer, false, false);
    }

    /** The feats the character is recommended for the given picks of a level offering \p normalFeats. */
    std::vector<FeatType> recommend(const CreatureAttributes &attributes, int picks, int normalFeats = 1) {
        return recommendFor(attributes, picks, normalFeats, 0, false);
    }

    /**
     * The feats the character is recommended for the given picks of a level
     * offering \p normalFeats and \p bonusFeats, with the screen's one extra
     * pick of each kind it offers.
     */
    std::vector<FeatType> recommendFor(const CreatureAttributes &attributes, int picks, int normalFeats, int bonusFeats, bool fillWithUnranked) {
        std::set<FeatType> noEffectFeats;
        std::set<FeatType> noneChosen;
        FeatChoice choice {
            attributes,
            *attributes.classLevels().back().first,
            normalFeats > 0 ? normalFeats + 1 : 0,
            bonusFeats > 0 ? bonusFeats + 1 : 0,
            noEffectFeats,
            noneChosen,
            kObjectTagPlayer,
            false};
        return recommendFeats(*feats, choice, picks, fillWithUnranked);
    }

    static std::map<SkillType, int> ranks(const CreatureAttributes &attributes) {
        std::map<SkillType, int> result;
        for (int skill = 0; skill <= static_cast<int>(SkillType::TreatInjury); ++skill) {
            int rank = attributes.getSkillRank(static_cast<SkillType>(skill));
            if (rank > 0) {
                result.emplace(static_cast<SkillType>(skill), rank);
            }
        }
        return result;
    }
};

} // namespace

// Loaders

TEST_F(RecommendedTest, should_load_a_classes_skill_recommendations_and_leave_unranked_skills_without_one) {
    const CreatureClass &soldier = getClass(ClassType::Soldier);

    EXPECT_EQ(soldier.getSkillRecommendation(SkillType::TreatInjury), 1);
    EXPECT_EQ(soldier.getSkillRecommendation(SkillType::Awareness), 2);
    EXPECT_EQ(soldier.getSkillRecommendation(SkillType::Demolitions), 3);
    EXPECT_EQ(soldier.getSkillRecommendation(SkillType::Repair), 4);
    EXPECT_EQ(soldier.getSkillRecommendation(SkillType::Security), 5);
    EXPECT_EQ(soldier.getSkillRecommendation(SkillType::ComputerUse), 6);
    EXPECT_EQ(soldier.getSkillRecommendation(SkillType::Stealth), 7);
    EXPECT_FALSE(soldier.getSkillRecommendation(SkillType::Persuade));
}

TEST_F(RecommendedTest, should_load_a_classes_feat_recommendations_with_their_ties) {
    const CreatureClass &soldier = getClass(ClassType::Soldier);

    EXPECT_EQ(soldier.getFeatRecommendation(kRapidShot), 1);
    EXPECT_EQ(soldier.getFeatRecommendation(kMasterConditioning), 13);
    EXPECT_EQ(soldier.getFeatRecommendation(kMasterSniperShot), 13);
    EXPECT_FALSE(soldier.getFeatRecommendation(kDueling));
}

TEST_F(RecommendedTest, should_load_a_powers_light_and_dark_side_recommendations) {
    EXPECT_EQ(spells->get(kStun)->lightRecommendation, 1);
    EXPECT_FALSE(spells->get(kStun)->darkRecommendation);
    EXPECT_EQ(spells->get(kShock)->darkRecommendation, 1);
    EXPECT_FALSE(spells->get(kShock)->lightRecommendation);
    EXPECT_EQ(spells->get(kAffectMind)->lightRecommendation, 28);
    EXPECT_EQ(spells->get(kAffectMind)->darkRecommendation, 29);
}

TEST_F(RecommendedTest, should_load_a_classes_primary_ability) {
    EXPECT_EQ(getClass(ClassType::JediConsular).primaryAbility(), Ability::Wisdom);
    EXPECT_EQ(getClass(ClassType::CombatDroid).primaryAbility(), Ability::Constitution);
    EXPECT_EQ(getClass(ClassType::Soldier).primaryAbility(), Ability::Strength);
}

// Skills

TEST_F(RecommendedTest, should_spend_a_new_soldiers_points_on_its_first_recommended_skill) {
    CreatureAttributes attributes(newCharacter(ClassType::Soldier));
    int points = 4;

    recommendSkills(getClass(ClassType::Soldier), 1, attributes, points);

    EXPECT_EQ(ranks(attributes), (std::map<SkillType, int> {{SkillType::TreatInjury, 4}}));
    EXPECT_EQ(points, 0);
}

TEST_F(RecommendedTest, should_fill_each_recommended_skill_of_a_new_scout_to_its_cap_in_turn) {
    CreatureAttributes attributes(newCharacter(ClassType::Scout));
    int points = 16;

    recommendSkills(getClass(ClassType::Scout), 1, attributes, points);

    EXPECT_EQ(ranks(attributes), (std::map<SkillType, int> {{SkillType::ComputerUse, 4}, {SkillType::Demolitions, 4}, {SkillType::Repair, 4}, {SkillType::Awareness, 4}}));
    EXPECT_EQ(points, 0);
}

TEST_F(RecommendedTest, should_stop_a_cross_class_skill_at_half_the_class_skill_cap) {
    CreatureAttributes attributes(newCharacter(ClassType::Scoundrel));
    int points = 20;

    recommendSkills(getClass(ClassType::Scoundrel), 1, attributes, points);

    EXPECT_EQ(ranks(attributes), (std::map<SkillType, int> {{SkillType::Stealth, 4}, {SkillType::Demolitions, 4}, {SkillType::Security, 4}, {SkillType::Awareness, 4}, {SkillType::TreatInjury, 2}}));
    EXPECT_EQ(points, 0);
}

TEST_F(RecommendedTest, should_spend_a_point_too_few_for_a_cross_class_skill_on_a_later_class_skill) {
    // A level-1 Scoundrel with the recommended ranks levels to 2 with 5 points.
    CreatureAttributes attributes(newCharacter(ClassType::Scoundrel));
    int points = 20;
    recommendSkills(getClass(ClassType::Scoundrel), 1, attributes, points);
    points = 5;

    recommendSkills(getClass(ClassType::Scoundrel), 2, attributes, points);

    EXPECT_EQ(ranks(attributes), (std::map<SkillType, int> {{SkillType::Stealth, 5}, {SkillType::Demolitions, 5}, {SkillType::Security, 5}, {SkillType::Awareness, 5}, {SkillType::TreatInjury, 2}, {SkillType::Persuade, 1}}));
    EXPECT_EQ(points, 0);
}

TEST_F(RecommendedTest, should_leave_ranks_and_points_alone_when_every_recommended_skill_is_at_its_cap) {
    CreatureAttributes attributes(newCharacter(ClassType::Soldier));
    for (auto skill : {SkillType::TreatInjury, SkillType::Awareness, SkillType::Demolitions}) {
        attributes.setSkillRank(skill, 4);
    }
    for (auto skill : {SkillType::Repair, SkillType::Security, SkillType::ComputerUse, SkillType::Stealth}) {
        attributes.setSkillRank(skill, 2);
    }
    auto before = ranks(attributes);
    int points = 5;

    recommendSkills(getClass(ClassType::Soldier), 1, attributes, points);

    EXPECT_EQ(ranks(attributes), before);
    EXPECT_EQ(points, 5);
}

TEST_F(RecommendedTest, should_never_raise_a_skill_the_class_does_not_recommend) {
    CreatureAttributes soldier(newCharacter(ClassType::Soldier));
    int soldierPoints = 100;
    recommendSkills(getClass(ClassType::Soldier), 1, soldier, soldierPoints);
    EXPECT_EQ(soldier.getSkillRank(SkillType::Persuade), 0);
    EXPECT_GT(soldierPoints, 0);

    CreatureAttributes droid(getClass(ClassType::CombatDroid).defaultAttributes());
    int droidPoints = 100;
    recommendSkills(getClass(ClassType::CombatDroid), 1, droid, droidPoints);
    EXPECT_EQ(droid.getSkillRank(SkillType::TreatInjury), 0);
    EXPECT_EQ(droid.getSkillRank(SkillType::Stealth), 0);
    EXPECT_EQ(droid.getSkillRank(SkillType::Persuade), 0);
    EXPECT_EQ(droid.getSkillRank(SkillType::Repair), 4);
}

TEST_F(RecommendedTest, should_spend_nothing_for_a_class_whose_skills_table_ranks_no_skill) {
    ON_CALL(twoDas, get("skills")).WillByDefault(Return(makeTable({"label", "sol_class"}, {{"ComputerUse", "1"}, {"Demolitions", "1"}})));
    useFeatTable(makeFeatTable());
    CreatureAttributes soldier(getClass(ClassType::Soldier).defaultAttributes());
    int points = 4;

    recommendSkills(getClass(ClassType::Soldier), 1, soldier, points);

    EXPECT_TRUE(ranks(soldier).empty());
    EXPECT_EQ(points, 4);
}

// Feats

TEST_F(RecommendedTest, should_choose_each_starting_classes_first_recommended_feat_it_does_not_hold) {
    EXPECT_EQ(recommend(newCharacter(ClassType::Soldier), 1), std::vector<FeatType> {kRapidShot});
    // Flurry, the Scout's first recommendation, is granted.
    CreatureAttributes scout(newCharacter(ClassType::Scout));
    ASSERT_TRUE(scout.hasFeat(kFlurry));
    EXPECT_EQ(recommend(scout, 1), std::vector<FeatType> {kCriticalStrike});
    EXPECT_EQ(recommend(newCharacter(ClassType::Scoundrel), 1), std::vector<FeatType> {kDueling});
}

TEST_F(RecommendedTest, should_skip_held_recommendations_on_a_level_up) {
    CreatureAttributes scoundrel(newCharacter(ClassType::Scoundrel));
    scoundrel.addFeat(kDueling);
    addLevel(scoundrel);
    ASSERT_TRUE(scoundrel.hasFeat(kSniperShot));
    EXPECT_EQ(recommend(scoundrel, 1), std::vector<FeatType> {kGearHead});

    CreatureAttributes soldier(newCharacter(ClassType::Soldier));
    soldier.addFeat(kRapidShot);
    addLevel(soldier);
    ASSERT_TRUE(soldier.hasFeat(kPowerBlast));
    EXPECT_EQ(recommend(soldier, 1), std::vector<FeatType> {kSniperShot});
}

TEST_F(RecommendedTest, should_skip_recommendations_the_character_cannot_choose_yet) {
    CreatureAttributes consular(newCharacter(ClassType::JediConsular));
    ASSERT_TRUE(consular.hasFeat(kJediDefense));
    ASSERT_EQ(getClass(ClassType::JediConsular).getFeatRecommendation(kAdvancedJediDefense), 1);
    ASSERT_EQ(getClass(ClassType::JediConsular).getFeatRecommendation(kMasterJediDefense), 2);

    EXPECT_EQ(recommend(consular, 1), std::vector<FeatType> {kDueling});
}

TEST_F(RecommendedTest, should_choose_a_successor_its_first_pick_unlocks) {
    // A chain root ranked first, its successor second, and another feat third.
    useFeatTable(makeSparseTable(
        {"name", "prereqfeat1", "sol_list", "sol_recom"},
        3,
        {{0, {{"name", "1"}, {"sol_list", "1"}, {"sol_recom", "1"}}},
         {1, {{"name", "1"}, {"prereqfeat1", "0"}, {"sol_list", "1"}, {"sol_recom", "2"}}},
         {2, {{"name", "1"}, {"sol_list", "1"}, {"sol_recom", "3"}}}}));
    CreatureAttributes soldier(getClass(ClassType::Soldier).defaultAttributes());

    EXPECT_EQ(recommend(soldier, 2, 2), (std::vector<FeatType> {static_cast<FeatType>(0), static_cast<FeatType>(1)}));
}

TEST_F(RecommendedTest, should_break_a_tie_between_recommendations_by_the_lower_feat_row) {
    // A level-8 Soldier holding every feat it is recommended ahead of rank 13.
    CreatureAttributes soldier(newCharacter(ClassType::Soldier));
    soldier.addClassLevels(soldier.classLevels().back().first, 7);
    for (auto feat : {kRapidShot, kSniperShot, kToughness, kConditioning, kImprovedRapidShot, kImprovedPowerBlast,
                      kImprovedSniperShot, kImprovedConditioning, kImprovedToughness, kMultiShot, kMasterPowerBlast}) {
        soldier.addFeat(feat);
    }
    ASSERT_TRUE(soldier.hasFeat(kPowerBlast));

    EXPECT_EQ(recommend(soldier, 1), std::vector<FeatType> {kMasterConditioning});
    EXPECT_EQ(recommend(soldier, 3, 3), (std::vector<FeatType> {kMasterConditioning, kMasterSniperShot, kMasterToughness}));
}

TEST_F(RecommendedTest, should_choose_nothing_when_no_recommended_feat_can_be_chosen) {
    // A level-2 Scoundrel holding every recommendation open below level 4.
    CreatureAttributes scoundrel(newCharacter(ClassType::Scoundrel));
    scoundrel.addFeat(kDueling);
    scoundrel.addFeat(kGearHead);
    addLevel(scoundrel);

    EXPECT_TRUE(recommend(scoundrel, 1).empty());
}

TEST_F(RecommendedTest, should_never_choose_a_granted_feat_again) {
    CreatureAttributes soldier(newCharacter(ClassType::Soldier));
    ASSERT_TRUE(soldier.hasFeat(kPowerAttack));

    auto chosen = recommend(soldier, 20, 20);

    EXPECT_THAT(chosen, Not(Contains(kPowerBlast)));
    EXPECT_THAT(chosen, Not(Contains(kPowerAttack)));
}

TEST_F(RecommendedTest, should_leave_picks_open_that_no_recommended_feat_can_take) {
    CreatureAttributes scoundrel(newCharacter(ClassType::Scoundrel));
    scoundrel.addFeat(kDueling);
    addLevel(scoundrel);

    EXPECT_EQ(recommendFor(scoundrel, 3, 3, 0, false), std::vector<FeatType> {kGearHead});
}

TEST_F(RecommendedTest, should_fill_picks_no_recommended_feat_can_take_with_the_lowest_choosable_rows) {
    // A droid-like class: more picks than ranked feats. The ranked feat comes
    // first, then the lowest rows the class can choose.
    useFeatTable(makeSparseTable(
        {"name", "sol_list", "sol_recom"},
        6,
        {{1, {{"name", "1"}, {"sol_list", "4"}}},
         {2, {{"name", "1"}, {"sol_list", "1"}}},
         {3, {{"name", "1"}, {"sol_list", "0"}}},
         {4, {{"name", "1"}, {"sol_list", "1"}, {"sol_recom", "1"}}},
         {5, {{"name", "1"}, {"sol_list", "1"}}}}));
    CreatureAttributes soldier(getClass(ClassType::Soldier).defaultAttributes());

    EXPECT_EQ(recommendFor(soldier, 3, 3, 0, true), (std::vector<FeatType> {static_cast<FeatType>(4), static_cast<FeatType>(2), static_cast<FeatType>(3)}));
    // Fewer choosable feats than picks: every choosable feat, and no more.
    EXPECT_EQ(recommendFor(soldier, 6, 6, 0, true), (std::vector<FeatType> {static_cast<FeatType>(4), static_cast<FeatType>(2), static_cast<FeatType>(3), static_cast<FeatType>(5)}));
    EXPECT_EQ(recommendFor(soldier, 3, 3, 0, false), std::vector<FeatType> {static_cast<FeatType>(4)});
}

TEST_F(RecommendedTest, should_fill_bonus_picks_with_feats_the_class_can_take_as_bonus_feats) {
    // Row 0 is a bonus feat only, row 1 a normal feat only, row 2 either.
    useFeatTable(makeSparseTable(
        {"name", "sol_list", "sol_recom"},
        3,
        {{0, {{"name", "1"}, {"sol_list", "2"}, {"sol_recom", "2"}}},
         {1, {{"name", "1"}, {"sol_list", "0"}, {"sol_recom", "1"}}},
         {2, {{"name", "1"}, {"sol_list", "1"}, {"sol_recom", "3"}}}}));
    CreatureAttributes soldier(getClass(ClassType::Soldier).defaultAttributes());

    EXPECT_EQ(recommendFor(soldier, 2, 0, 2, false), (std::vector<FeatType> {static_cast<FeatType>(0), static_cast<FeatType>(2)}));
    EXPECT_EQ(recommendFor(soldier, 5, 0, 5, true), (std::vector<FeatType> {static_cast<FeatType>(0), static_cast<FeatType>(2)}));
}

// Powers

TEST_F(RecommendedTest, should_choose_a_new_jedis_powers_by_its_sides_recommendations) {
    for (auto type : {ClassType::JediGuardian, ClassType::JediConsular, ClassType::JediSentinel}) {
        CreatureClass jedi(type, *classes, strings, twoDas);
        CreatureAttributes attributes;

        EXPECT_EQ(recommendPowers(*spells, attributes, jedi, 2, Alignment::Neutral), (std::vector<SpellType> {kStun, kForcePush}));
        EXPECT_EQ(recommendPowers(*spells, attributes, jedi, 2, Alignment::LightSide), (std::vector<SpellType> {kStun, kForcePush}));
        EXPECT_EQ(recommendPowers(*spells, attributes, jedi, 2, Alignment::DarkSide), (std::vector<SpellType> {kShock, kWound}));
    }
}

TEST_F(RecommendedTest, should_choose_dark_side_powers_from_the_dark_side_band_edge) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto creature = game.newCreature();
    CreatureClass guardian(ClassType::JediGuardian, *classes, strings, twoDas);
    CreatureAttributes attributes;

    creature->setGoodEvil(40);
    EXPECT_EQ(recommendPowers(*spells, attributes, guardian, 1, creature->alignment()), std::vector<SpellType> {kShock});
    creature->setGoodEvil(41);
    EXPECT_EQ(recommendPowers(*spells, attributes, guardian, 1, creature->alignment()), std::vector<SpellType> {kStun});
}

TEST_F(RecommendedTest, should_fall_back_on_the_other_sides_recommendations) {
    ON_CALL(twoDas, get("spells")).WillByDefault(Return(makeSparseTable(
        {"usertype", "guardian", "light_recom", "dark_recom"},
        3,
        {{0, {{"usertype", "1"}, {"guardian", "0"}, {"dark_recom", "2"}}},
         {1, {{"usertype", "1"}, {"guardian", "0"}, {"dark_recom", "1"}}},
         {2, {{"usertype", "1"}, {"guardian", "2"}, {"light_recom", "1"}}}})));
    spells = std::make_unique<Spells>(textures, audioClips, models, strings, twoDas);
    spells->init(GameID::KotOR);
    CreatureClass guardian(ClassType::JediGuardian, *classes, strings, twoDas);
    CreatureAttributes attributes;

    // The light side power needs a class level the character does not reach.
    EXPECT_EQ(recommendPowers(*spells, attributes, guardian, 3, Alignment::LightSide),
              (std::vector<SpellType> {static_cast<SpellType>(1), static_cast<SpellType>(0)}));
}

TEST_F(RecommendedTest, should_choose_a_power_whose_prerequisite_it_chose_first) {
    CreatureClass guardian(ClassType::JediGuardian, *classes, strings, twoDas);
    // A guardian rising to class level 9, where Stasis follows Stun.
    CreatureAttributes attributes;
    attributes.addClassLevels(&guardian, 8);

    EXPECT_EQ(recommendPowers(*spells, attributes, guardian, 2, Alignment::LightSide), (std::vector<SpellType> {kStun, kStasis}));
}

TEST_F(RecommendedTest, should_break_a_tie_between_power_recommendations_by_the_lower_row) {
    CreatureClass guardian(ClassType::JediGuardian, *classes, strings, twoDas);
    // A guardian rising to class level 15 who knows every light side power
    // ranked ahead of Cure and Force Wave, which share rank 6.
    CreatureAttributes attributes;
    attributes.addClassLevels(&guardian, 14);
    for (auto power : {kStun, kStasis, kMassStasis, kForcePush, kForceWhirlwind}) {
        attributes.addSpell(power);
    }
    ASSERT_EQ(spells->get(kCure)->lightRecommendation, 6);
    ASSERT_EQ(spells->get(kForceWave)->lightRecommendation, 6);

    EXPECT_EQ(recommendPowers(*spells, attributes, guardian, 2, Alignment::LightSide), (std::vector<SpellType> {kCure, kForceWave}));
}

TEST_F(RecommendedTest, should_fill_power_picks_with_unranked_candidates_only_when_asked) {
    ON_CALL(twoDas, get("spells")).WillByDefault(Return(makeSparseTable(
        {"usertype", "guardian", "light_recom", "dark_recom"},
        3,
        {{0, {{"usertype", "1"}, {"guardian", "0"}}},
         {1, {{"usertype", "1"}, {"guardian", "0"}, {"dark_recom", "1"}}},
         {2, {{"usertype", "1"}, {"guardian", "0"}}}})));
    spells = std::make_unique<Spells>(textures, audioClips, models, strings, twoDas);
    spells->init(GameID::KotOR);
    CreatureClass guardian(ClassType::JediGuardian, *classes, strings, twoDas);
    CreatureAttributes attributes;

    EXPECT_EQ(recommendPowers(*spells, attributes, guardian, 3, Alignment::LightSide), std::vector<SpellType> {static_cast<SpellType>(1)});
    EXPECT_EQ(recommendPowers(*spells, attributes, guardian, 4, Alignment::LightSide, true),
              (std::vector<SpellType> {static_cast<SpellType>(1), static_cast<SpellType>(0), static_cast<SpellType>(2)}));
}

// Point costs shared by the screens and the recommendations

TEST_F(RecommendedTest, should_charge_one_point_for_a_class_skill_rank_and_two_for_a_cross_class_one) {
    const CreatureClass &soldier = getClass(ClassType::Soldier);
    ASSERT_TRUE(soldier.isClassSkill(SkillType::Awareness));
    ASSERT_FALSE(soldier.isClassSkill(SkillType::ComputerUse));

    EXPECT_EQ(soldier.skillPointCost(SkillType::Awareness), 1);
    EXPECT_EQ(soldier.skillPointCost(SkillType::ComputerUse), 2);
}

TEST_F(RecommendedTest, should_cap_a_class_skill_at_level_plus_three_and_a_cross_class_one_at_half_that) {
    const CreatureClass &soldier = getClass(ClassType::Soldier);

    EXPECT_EQ(soldier.maxSkillRank(SkillType::Awareness, 1), 4);
    EXPECT_EQ(soldier.maxSkillRank(SkillType::ComputerUse, 1), 2);
    EXPECT_EQ(soldier.maxSkillRank(SkillType::Awareness, 4), 7);
    EXPECT_EQ(soldier.maxSkillRank(SkillType::ComputerUse, 4), 3);
}

TEST_F(RecommendedTest, should_charge_a_new_characters_ability_point_by_the_abilitys_modifier) {
    CreatureAttributes attributes;
    // From 8 to 13 a point costs 1, at 14 and 15 it costs 2, from 16 on 3:
    // the modifier, which at 18 would be 4, held to 3.
    const std::vector<std::pair<int, int>> costByScore {{8, 1}, {9, 1}, {10, 1}, {11, 1}, {12, 1}, {13, 1}, {14, 2}, {15, 2}, {16, 3}, {17, 3}, {18, 3}};
    for (const auto &[score, cost] : costByScore) {
        attributes.setAbilityScore(Ability::Strength, score);
        EXPECT_EQ(attributes.getAbilityPointCost(Ability::Strength), cost) << "score " << score;
    }
}

// Auto Level Up

TEST_F(RecommendedTest, should_not_auto_level_a_leader_with_no_level_pending) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto leader = game.newCreature();
    CreatureClass soldier(ClassType::Soldier, *classes, strings, twoDas);
    leader->attributes().addClassLevels(&soldier, 1);
    game.party().addMember(kNpcPlayer, leader);
    game.party().setPlayer(leader);
    ASSERT_FALSE(leader->isLevelUpPending());

    EXPECT_FALSE(game.autoLevelUp());
    EXPECT_EQ(leader->attributes().getAggregateLevel(), 1);
}
