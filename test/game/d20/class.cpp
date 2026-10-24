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
#include "reone/resource/2da.h"

#include "../../fixtures/game.h"
#include "../../fixtures/resource.h"

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

using Row = std::unordered_map<std::string, std::string>;

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

// Rows from the feat tables of both games, with the columns of the starting
// classes, the Tech Specialist and two TSL companions.
std::shared_ptr<TwoDA> makeGrantedFeatTable() {
    const std::vector<std::string> columns = {
        "name",
        "sol_list", "sol_granted", "sct_list", "sct_granted", "scd_list", "scd_granted",
        "jgd_list", "jgd_granted", "jgd_pc_granted", "jcn_list", "jcn_granted", "jcn_pc_granted",
        "jsn_list", "jsn_granted", "jsn_pc_granted", "tec_list", "tec_granted",
        "baodur", "atton", "kreia"};
    Row blaster {{"name", "1"}};
    Row melee {{"name", "1"}};
    for (const char *prefix : {"sol", "sct", "scd", "jgd", "jcn", "jsn", "tec"}) {
        blaster[std::string(prefix) + "_list"] = "3";
        blaster[std::string(prefix) + "_granted"] = "1";
        melee[std::string(prefix) + "_list"] = "3";
        melee[std::string(prefix) + "_granted"] = "1";
    }
    std::unordered_map<int, Row> rows {
        {static_cast<int>(FeatType::ArmourProfHeavy), {{"name", "1"}, {"sol_list", "3"}, {"sol_granted", "1"}, {"baodur", "255"}, {"kreia", "255"}}},
        {static_cast<int>(FeatType::ArmourProfLight), {{"name", "1"}, {"sol_list", "3"}, {"sol_granted", "1"}, {"sct_list", "3"}, {"sct_granted", "1"}, {"scd_list", "3"}, {"scd_granted", "1"}, {"jgd_list", "1"}, {"jgd_granted", "-1"}, {"jgd_pc_granted", "1"}, {"jcn_list", "1"}, {"jcn_granted", "-1"}, {"jcn_pc_granted", "1"}, {"jsn_list", "1"}, {"jsn_granted", "-1"}, {"jsn_pc_granted", "1"}}},
        {static_cast<int>(FeatType::CriticalStrike), {{"name", "1"}, {"scd_list", "3"}, {"scd_granted", "1"}, {"jgd_list", "1"}, {"jgd_granted", "-1"}, {"jgd_pc_granted", "1"}}},
        {static_cast<int>(FeatType::WeaponProficiencyBlaster), blaster},
        {static_cast<int>(FeatType::WeaponProficiencyLightsaber), {{"name", "1"}, {"sol_list", "4"}, {"sol_granted", "-1"}, {"jgd_list", "3"}, {"jgd_granted", "1"}, {"jcn_list", "3"}, {"jcn_granted", "1"}, {"jsn_list", "3"}, {"jsn_granted", "1"}}},
        {static_cast<int>(FeatType::WeaponProficiencyMeleeWeapons), melee},
        {static_cast<int>(FeatType::SneakAttack2d6), {{"name", "1"}, {"scd_list", "3"}, {"scd_granted", "3"}}},
        {static_cast<int>(FeatType::SneakAttack3d6), {{"name", "1"}, {"scd_list", "3"}, {"scd_granted", "5"}}},
        {116, {{"name", "1"}, {"jgd_list", "3"}, {"jgd_granted", "2"}}}, // Force Sensitive
        {static_cast<int>(FeatType::ForceChain), {{"name", "1"}, {"jgd_list", "4"}, {"jgd_granted", "-1"}, {"jgd_pc_granted", "1"}}},
        {static_cast<int>(FeatType::WarVeteran), {{"name", "1"}}},
        {212, {{"name", "1"}, {"jgd_list", "3"}, {"jgd_granted", "2"}, {"jcn_list", "3"}, {"jcn_granted", "2"}, {"baodur", "2"}}}, // Unarmed Specialist I
        {static_cast<int>(FeatType::FightingSpirit), {{"name", "1"}, {"atton", "8"}}}};
    return makeSparseTable(columns, 245, rows);
}

class GrantedFeatsTest : public Test {
protected:
    NiceMock<MockTextures> textures;
    NiceMock<MockStrings> strings;
    NiceMock<MockTwoDAs> twoDas;
    Races races;
    NiceMock<MockSkills> skills;
    std::unique_ptr<Classes> classes;
    std::unique_ptr<Feats> feats;

    void SetUp() override {
        const std::vector<std::string> columns = {
            "name", "description", "hitdie", "skillpointbase", "str", "dex", "con", "int", "wis", "cha",
            "skillstable", "savingthrowtable", "attackbonustable", "featstable", "featgain", "spellgaintable"};
        std::vector<std::vector<std::string>> rows(17, std::vector<std::string>(columns.size()));
        auto classRow = [](const std::string &prefix) {
            return std::vector<std::string> {"1", "2", "8", "1", "10", "10", "10", "10", "10", "10", prefix, "save", "attack", prefix, "", ""};
        };
        rows[static_cast<int>(ClassType::Soldier)] = classRow("SOL");
        rows[static_cast<int>(ClassType::Scout)] = classRow("SCT");
        rows[static_cast<int>(ClassType::Scoundrel)] = classRow("SCD");
        rows[static_cast<int>(ClassType::JediGuardian)] = classRow("JGD");
        rows[static_cast<int>(ClassType::JediConsular)] = classRow("JCN");
        rows[static_cast<int>(ClassType::JediSentinel)] = classRow("JSN");
        rows[static_cast<int>(ClassType::TechSpecialist)] = classRow("TEC");
        auto classesTable = makeTable(columns, rows);
        auto featTable = makeGrantedFeatTable();

        ON_CALL(twoDas, get("classes")).WillByDefault(Return(classesTable));
        ON_CALL(twoDas, get("skills")).WillByDefault(Return(makeTable({}, {})));
        ON_CALL(twoDas, get("save")).WillByDefault(Return(makeTable({"level", "fortsave", "refsave", "willsave"}, {{"1", "0", "0", "0"}})));
        ON_CALL(twoDas, get("attack")).WillByDefault(Return(makeTable({"bab"}, {{"0"}})));
        ON_CALL(twoDas, get("feat")).WillByDefault(Return(featTable));
        // The races up to Human, none of which grants feats.
        ON_CALL(twoDas, get("racialtypes")).WillByDefault(Return(makeTable({"featstable"}, std::vector<std::vector<std::string>>(static_cast<int>(RacialType::Human) + 1, {""}))));

        races.init(twoDas);
        classes = std::make_unique<Classes>(strings, twoDas);
        feats = std::make_unique<Feats>(textures, strings, twoDas, races, skills);
        feats->init();
    }

    CreatureAttributes newCharacter(ClassType type, bool tsl) {
        CreatureAttributes attributes(classes->get(type)->defaultAttributes());
        feats->addGrantedFeats(attributes, RacialType::Human, kObjectTagPlayer, tsl, true);
        return attributes;
    }
};

} // namespace

TEST(CreatureClass, should_load_granted_levels_and_player_character_feats) {
    NiceMock<MockStrings> strings;
    NiceMock<MockTwoDAs> twoDas;
    const std::vector<std::string> columns = {
        "name", "description", "hitdie", "skillpointbase", "str", "dex", "con", "int", "wis", "cha",
        "skillstable", "savingthrowtable", "attackbonustable", "featstable", "featgain", "spellgaintable"};
    std::vector<std::vector<std::string>> rows(17, std::vector<std::string>(columns.size()));
    rows[static_cast<int>(ClassType::JediGuardian)] = {"1", "2", "10", "1", "10", "10", "10", "10", "10", "10", "jgd", "save", "attack", "JGD", "", ""};
    auto featTable = makeGrantedFeatTable();

    ON_CALL(twoDas, get("classes")).WillByDefault(Return(makeTable(columns, rows)));
    ON_CALL(twoDas, get("skills")).WillByDefault(Return(makeTable({}, {})));
    ON_CALL(twoDas, get("save")).WillByDefault(Return(makeTable({"level", "fortsave", "refsave", "willsave"}, {{"1", "0", "0", "0"}})));
    ON_CALL(twoDas, get("attack")).WillByDefault(Return(makeTable({"bab"}, {{"0"}})));
    ON_CALL(twoDas, get("feat")).WillByDefault(Return(featTable));

    Classes classes(strings, twoDas);
    auto guardian = classes.get(ClassType::JediGuardian);

    ASSERT_TRUE(guardian);
    EXPECT_EQ(guardian->getFeatGrantedLevel(FeatType::WeaponProficiencyLightsaber).value_or(0), 1);
    EXPECT_EQ(guardian->getFeatGrantedLevel(static_cast<FeatType>(116)).value_or(0), 2);
    EXPECT_FALSE(guardian->getFeatGrantedLevel(FeatType::ArmourProfLight));
    EXPECT_TRUE(guardian->isPCGrantedFeat(FeatType::ArmourProfLight));
    // Feats off the class's list are not player-character feats either.
    EXPECT_FALSE(guardian->isPCGrantedFeat(FeatType::ForceChain));
    EXPECT_FALSE(guardian->isPCGrantedFeat(FeatType::WeaponProficiencyLightsaber));
}

TEST_F(GrantedFeatsTest, should_give_a_new_character_of_each_starting_class_its_first_level_feats) {
    const std::vector<std::pair<ClassType, bool>> startingClasses {
        {ClassType::Soldier, false},
        {ClassType::Scout, false},
        {ClassType::Scoundrel, false},
        {ClassType::JediGuardian, true},
        {ClassType::JediConsular, true},
        {ClassType::JediSentinel, true}};
    for (auto &[type, tsl] : startingClasses) {
        auto attributes = newCharacter(type, tsl);
        EXPECT_TRUE(attributes.hasFeat(FeatType::WeaponProficiencyBlaster)) << static_cast<int>(type);
        EXPECT_TRUE(attributes.hasFeat(FeatType::WeaponProficiencyMeleeWeapons)) << static_cast<int>(type);
        EXPECT_TRUE(attributes.hasFeat(FeatType::ArmourProfLight)) << static_cast<int>(type);
        EXPECT_EQ(attributes.hasFeat(FeatType::WarVeteran), tsl) << static_cast<int>(type);
    }

    auto soldier = newCharacter(ClassType::Soldier, false);
    EXPECT_TRUE(soldier.hasFeat(FeatType::ArmourProfHeavy));
    EXPECT_FALSE(soldier.hasFeat(FeatType::WeaponProficiencyLightsaber));

    auto scoundrel = newCharacter(ClassType::Scoundrel, false);
    EXPECT_TRUE(scoundrel.hasFeat(FeatType::CriticalStrike));
    EXPECT_FALSE(scoundrel.hasFeat(FeatType::SneakAttack2d6));

    auto guardian = newCharacter(ClassType::JediGuardian, true);
    EXPECT_TRUE(guardian.hasFeat(FeatType::WeaponProficiencyLightsaber));
    EXPECT_TRUE(guardian.hasFeat(FeatType::CriticalStrike));
    EXPECT_FALSE(guardian.hasFeat(FeatType::ForceChain));
    EXPECT_FALSE(guardian.hasFeat(static_cast<FeatType>(116)));
    EXPECT_FALSE(guardian.hasFeat(static_cast<FeatType>(212)));
}

TEST_F(GrantedFeatsTest, should_grant_the_feats_of_the_level_gained) {
    auto scoundrel = newCharacter(ClassType::Scoundrel, false);
    auto *clazz = scoundrel.classLevels().back().first;

    scoundrel.addClassLevels(clazz, 1);
    feats->addGrantedFeats(scoundrel, RacialType::Human, kObjectTagPlayer, false, false);
    EXPECT_FALSE(scoundrel.hasFeat(FeatType::SneakAttack2d6));

    scoundrel.addClassLevels(clazz, 1);
    feats->addGrantedFeats(scoundrel, RacialType::Human, kObjectTagPlayer, false, false);
    EXPECT_TRUE(scoundrel.hasFeat(FeatType::SneakAttack2d6));
    EXPECT_FALSE(scoundrel.hasFeat(FeatType::SneakAttack3d6));

    auto guardian = newCharacter(ClassType::JediGuardian, true);
    guardian.addClassLevels(guardian.classLevels().back().first, 1);
    feats->addGrantedFeats(guardian, RacialType::Human, kObjectTagPlayer, true, false);
    EXPECT_TRUE(guardian.hasFeat(static_cast<FeatType>(116)));
    EXPECT_TRUE(guardian.hasFeat(static_cast<FeatType>(212)));
}

TEST_F(GrantedFeatsTest, should_apply_the_tsl_companion_columns_on_level_up) {
    // Bao-Dur is never granted the class feats marked 255 in his column.
    CreatureAttributes baoDur;
    baoDur.addClassLevels(classes->get(ClassType::Soldier).get(), 1);
    feats->addGrantedFeats(baoDur, RacialType::Human, "BaoDur", true, false);
    EXPECT_TRUE(baoDur.hasFeat(FeatType::WeaponProficiencyBlaster));
    EXPECT_FALSE(baoDur.hasFeat(FeatType::ArmourProfHeavy));

    // A companion gains the feats of its own column at that character level.
    baoDur.addClassLevels(classes->get(ClassType::Soldier).get(), 1);
    feats->addGrantedFeats(baoDur, RacialType::Human, "BaoDur", true, false);
    EXPECT_TRUE(baoDur.hasFeat(static_cast<FeatType>(212)));

    CreatureAttributes atton;
    atton.addClassLevels(classes->get(ClassType::Scoundrel).get(), 7);
    feats->addGrantedFeats(atton, RacialType::Human, "Atton", true, false);
    EXPECT_FALSE(atton.hasFeat(FeatType::FightingSpirit));
    atton.addClassLevels(classes->get(ClassType::Scoundrel).get(), 1);
    feats->addGrantedFeats(atton, RacialType::Human, "Atton", true, false);
    EXPECT_TRUE(atton.hasFeat(FeatType::FightingSpirit));
}

TEST_F(GrantedFeatsTest, should_grant_unarmed_specialist_at_the_characters_jedi_level) {
    CreatureAttributes jedi;
    jedi.addClassLevels(classes->get(ClassType::JediGuardian).get(), 1);
    jedi.addClassLevels(classes->get(ClassType::JediConsular).get(), 1);
    feats->addGrantedFeats(jedi, RacialType::Human, kObjectTagPlayer, true, false);
    EXPECT_TRUE(jedi.hasFeat(static_cast<FeatType>(212)));
}

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
         {"3", "0", "0"},
         {"4", "1", "2"}});

    ON_CALL(twoDas, get("classes")).WillByDefault(Return(classesTable));
    ON_CALL(twoDas, get("skills")).WillByDefault(Return(skillsTable));
    ON_CALL(twoDas, get("save")).WillByDefault(Return(savingThrowsTable));
    ON_CALL(twoDas, get("attack")).WillByDefault(Return(attackBonusTable));
    ON_CALL(twoDas, get("classpowergain")).WillByDefault(Return(powerGainTable));
    ON_CALL(twoDas, get("cls_spgn_jedi")).WillByDefault(Return(makeTable({"level", "numspelllevels"}, {{"1", "1"}, {"2", "1"}, {"3", "1"}, {"4", "1"}})));

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
