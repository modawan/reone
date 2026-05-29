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

#include "../../fixtures/resource.h"

#include "reone/game/d20/classes.h"
#include "reone/resource/2da.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

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

std::shared_ptr<TwoDA> makeClassesTable() {
    const std::vector<std::string> columns = {
        "name", "description", "hitdie", "skillpointbase", "str", "dex", "con", "int", "wis", "cha",
        "skillstable", "savingthrowtable", "attackbonustable", "featstable", "featgain", "spellgaintable"};
    std::vector<std::vector<std::string>> rows(17, std::vector<std::string>(columns.size()));
    rows[static_cast<int>(ClassType::JediGuardian)] = {"1", "2", "10", "1", "10", "10", "10", "10", "10", "10", "jgd", "save", "attack", "", "", "JGD"};
    rows[static_cast<int>(ClassType::JediMaster)] = {"1", "2", "6", "1", "10", "10", "10", "10", "10", "10", "jma", "save", "attack", "", "", "JMA"};
    return makeTable(columns, rows);
}

} // namespace

TEST(CreatureClass, should_load_power_gains_for_k1_base_and_tsl_prestige_classes) {
    NiceMock<MockStrings> strings;
    NiceMock<MockTwoDAs> twoDas;
    auto classesTable = makeClassesTable();
    auto skillsTable = makeTable({}, {});
    auto savingThrowsTable = makeTable({"level", "fortsave", "refsave", "willsave"}, {{"1", "0", "0", "0"}});
    auto attackBonusTable = makeTable({"bab"}, {{"0"}});
    auto powerGainTable = makeTable(
        {"label", "jgd", "jma"},
        {{"1", "2", "2"},
         {"2", "1", "1"},
         {"4", "1", "2"}});

    ON_CALL(twoDas, get("classes")).WillByDefault(Return(classesTable));
    ON_CALL(twoDas, get("skills")).WillByDefault(Return(skillsTable));
    ON_CALL(twoDas, get("save")).WillByDefault(Return(savingThrowsTable));
    ON_CALL(twoDas, get("attack")).WillByDefault(Return(attackBonusTable));
    ON_CALL(twoDas, get("classpowergain")).WillByDefault(Return(powerGainTable));

    Classes classes(strings, twoDas);
    auto guardian = classes.get(ClassType::JediGuardian);
    auto jediMaster = classes.get(ClassType::JediMaster);

    ASSERT_TRUE(guardian);
    EXPECT_EQ(guardian->getPowerGain(1), 2);
    EXPECT_EQ(guardian->getPowerGain(2), 1);
    EXPECT_EQ(guardian->getPowerGain(99), 0);

    ASSERT_TRUE(jediMaster);
    EXPECT_EQ(jediMaster->getPowerGain(1), 2);
    EXPECT_EQ(jediMaster->getPowerGain(4), 2);
    EXPECT_EQ(jediMaster->getPowerGain(99), 0);
}
