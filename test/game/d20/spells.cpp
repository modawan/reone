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
#include "reone/game/d20/spells.h"
#include "reone/resource/2da.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

using SpellRow = std::unordered_map<std::string, std::string>;

std::shared_ptr<TwoDA> makeSpellsTable(
    const std::vector<std::string> &columns,
    int rowCount,
    const std::unordered_map<int, SpellRow> &values) {
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

class SpellsTest : public Test {
protected:
    NiceMock<MockTextures> textures;
    NiceMock<MockAudioClips> audioClips;
    NiceMock<MockModels> models;
    NiceMock<MockStrings> strings;
    NiceMock<MockTwoDAs> twoDas;
    std::unique_ptr<Spells> spells;
    std::unique_ptr<Classes> classes;
    std::unique_ptr<CreatureClass> guardian;

    void load(std::shared_ptr<TwoDA> table) {
        EXPECT_CALL(twoDas, get("spells")).WillOnce(Return(std::move(table)));
        spells = std::make_unique<Spells>(textures, audioClips, models, strings, twoDas);
        spells->init();
    }

    CreatureClass &getGuardian() {
        if (!guardian) {
            classes = std::make_unique<Classes>(strings, twoDas);
            guardian = std::make_unique<CreatureClass>(ClassType::JediGuardian, *classes, strings, twoDas);
        }
        return *guardian;
    }

    static const SpellDisplayEntry &getEntry(const std::vector<SpellDisplayEntry> &entries, SpellType type) {
        auto maybeEntry = std::find_if(entries.begin(), entries.end(), [&](const SpellDisplayEntry &entry) {
            return entry.type == type;
        });
        EXPECT_NE(maybeEntry, entries.end());
        return *maybeEntry;
    }
};

} // namespace

TEST_F(SpellsTest, should_load_k1_force_power_chain_metadata_and_base_class_requirements) {
    auto table = makeSpellsTable(
        {"pips", "prerequisites", "masterspell", "usertype", "guardian", "consular", "sentinel", "category", "hostilesetting"},
        51,
        {{static_cast<int>(SpellType::ForcePush), {{"pips", "1"}, {"usertype", "1"}, {"guardian", "0"}, {"consular", "0"}, {"sentinel", "0"}, {"category", "0x1301"}, {"hostilesetting", "1"}}},
         {static_cast<int>(SpellType::ForceWhirlwind), {{"pips", "2"}, {"prerequisites", "23"}, {"usertype", "1"}}},
         {static_cast<int>(SpellType::ForceWave), {{"pips", "3"}, {"prerequisites", "23_27"}, {"masterspell", "23"}, {"usertype", "1"}}},
         {static_cast<int>(SpellType::Shock), {{"pips", "1"}, {"usertype", "1"}}},
         {static_cast<int>(SpellType::Lightning), {{"pips", "2"}, {"prerequisites", "43"}, {"usertype", "1"}}},
         {static_cast<int>(SpellType::ForceStorm), {{"pips", "3"}, {"prerequisites", "43_35"}, {"usertype", "1"}}}});

    load(std::move(table));

    auto push = spells->get(SpellType::ForcePush);
    auto whirlwind = spells->get(SpellType::ForceWhirlwind);
    auto wave = spells->get(SpellType::ForceWave);
    auto lightning = spells->get(SpellType::Lightning);
    auto storm = spells->get(SpellType::ForceStorm);

    ASSERT_TRUE(push);
    EXPECT_EQ(push->pips, 1);
    EXPECT_EQ(push->category, 0x1301);
    EXPECT_TRUE(push->hostile);
    EXPECT_EQ(push->userType, 1);
    EXPECT_EQ(push->getClassLevelRequirement(ClassType::JediGuardian), 0);
    EXPECT_EQ(push->getClassLevelRequirement(ClassType::JediConsular), 0);
    EXPECT_EQ(push->getClassLevelRequirement(ClassType::JediSentinel), 0);
    EXPECT_EQ(push->getClassLevelRequirement(ClassType::JediMaster), std::nullopt);

    ASSERT_TRUE(whirlwind);
    ASSERT_TRUE(wave);
    EXPECT_THAT(whirlwind->prerequisites, ElementsAre(SpellType::ForcePush));
    EXPECT_THAT(wave->prerequisites, ElementsAre(SpellType::ForcePush, SpellType::ForceWhirlwind));
    EXPECT_EQ(wave->masterSpell, SpellType::ForcePush);

    ASSERT_TRUE(lightning);
    ASSERT_TRUE(storm);
    EXPECT_THAT(lightning->prerequisites, ElementsAre(SpellType::Shock));
    EXPECT_THAT(storm->prerequisites, ElementsAre(SpellType::Shock, SpellType::Lightning));
}

TEST_F(SpellsTest, should_load_tsl_chain_metadata_and_prestige_class_requirements) {
    auto table = makeSpellsTable(
        {"pips", "prerequisites", "usertype", "guardian", "consular", "sentinel", "weapmstr", "jedimaster", "watchman", "marauder", "sithlord", "assassin"},
        201,
        {{static_cast<int>(SpellType::Cure), {{"pips", "2"}, {"usertype", "1"}}},
         {static_cast<int>(SpellType::Heal), {{"pips", "3"}, {"prerequisites", "10"}, {"usertype", "1"}}},
         {static_cast<int>(SpellType::MasterHeal), {{"pips", "3"}, {"prerequisites", "10_28"}, {"usertype", "1"}}},
         {static_cast<int>(SpellType::ForceBarrier), {{"pips", "3"}, {"usertype", "1"}}},
         {static_cast<int>(SpellType::ImprovedForceBarrier), {{"pips", "3"}, {"prerequisites", "135"}, {"usertype", "1"}}},
         {static_cast<int>(SpellType::MasterForceBarrier), {{"pips", "3"}, {"prerequisites", "135_136"}, {"usertype", "1"}}},
         {200, {{"pips", "3"}, {"prerequisites", "181"}, {"usertype", "1"}, {"guardian", "-1"}, {"consular", "-1"}, {"sentinel", "-1"}, {"weapmstr", "1"}, {"jedimaster", "1"}, {"watchman", "1"}, {"marauder", "1"}, {"sithlord", "1"}, {"assassin", "1"}}}});

    load(std::move(table));

    EXPECT_THAT(spells->get(SpellType::Heal)->prerequisites, ElementsAre(SpellType::Cure));
    EXPECT_THAT(spells->get(SpellType::MasterHeal)->prerequisites, ElementsAre(SpellType::Cure, SpellType::Heal));

    auto barrier = spells->get(SpellType::ForceBarrier);
    auto improvedBarrier = spells->get(SpellType::ImprovedForceBarrier);
    auto masterBarrier = spells->get(SpellType::MasterForceBarrier);
    ASSERT_TRUE(barrier);
    ASSERT_TRUE(improvedBarrier);
    ASSERT_TRUE(masterBarrier);
    EXPECT_EQ(barrier->pips, 3);
    EXPECT_EQ(improvedBarrier->pips, 3);
    EXPECT_EQ(masterBarrier->pips, 3);
    EXPECT_THAT(improvedBarrier->prerequisites, ElementsAre(SpellType::ForceBarrier));
    EXPECT_THAT(masterBarrier->prerequisites, ElementsAre(SpellType::ForceBarrier, SpellType::ImprovedForceBarrier));

    auto prestigePower = spells->get(static_cast<SpellType>(200));
    ASSERT_TRUE(prestigePower);
    EXPECT_EQ(prestigePower->getClassLevelRequirement(ClassType::JediGuardian), -1);
    EXPECT_EQ(prestigePower->getClassLevelRequirement(ClassType::JediWeaponMaster), 1);
    EXPECT_EQ(prestigePower->getClassLevelRequirement(ClassType::SithAssassin), 1);
}

TEST_F(SpellsTest, should_ignore_blank_and_malformed_prerequisites) {
    auto table = makeSpellsTable(
        {"prerequisites"},
        2,
        {{0, {{"prerequisites", "23_bad__27"}}},
         {1, {{"prerequisites", "****"}}}});

    load(std::move(table));

    EXPECT_THAT(spells->get(static_cast<SpellType>(0))->prerequisites, ElementsAre(SpellType::ForcePush, SpellType::ForceWhirlwind));
    EXPECT_TRUE(spells->get(static_cast<SpellType>(1))->prerequisites.empty());
}

TEST_F(SpellsTest, should_build_k1_display_chains_from_prerequisites_in_source_root_order) {
    auto table = makeSpellsTable(
        {"prerequisites", "usertype", "guardian"},
        51,
        {{static_cast<int>(SpellType::ForcePush), {{"usertype", "1"}, {"guardian", "0"}}},
         {static_cast<int>(SpellType::ForceWhirlwind), {{"prerequisites", "23"}, {"usertype", "1"}, {"guardian", "0"}}},
         {static_cast<int>(SpellType::ForceWave), {{"prerequisites", "23_27"}, {"usertype", "1"}, {"guardian", "0"}}},
         {static_cast<int>(SpellType::Shock), {{"usertype", "1"}, {"guardian", "0"}}},
         {static_cast<int>(SpellType::Lightning), {{"prerequisites", "43"}, {"usertype", "1"}, {"guardian", "0"}}},
         {static_cast<int>(SpellType::ForceStorm), {{"prerequisites", "43_35"}, {"usertype", "1"}, {"guardian", "0"}}}});
    load(std::move(table));

    CreatureAttributes attributes;
    auto entries = spells->getLevelUpDisplayEntries(attributes, getGuardian(), {});
    std::vector<SpellType> visibleTypes;
    for (const auto &entry : entries) {
        if (entry.visible) {
            visibleTypes.push_back(entry.type);
        }
    }

    EXPECT_THAT(visibleTypes, ElementsAre(
                                  SpellType::ForcePush,
                                  SpellType::ForceWhirlwind,
                                  SpellType::ForceWave,
                                  SpellType::Shock,
                                  SpellType::Lightning,
                                  SpellType::ForceStorm));

    const auto &wave = getEntry(entries, SpellType::ForceWave);
    EXPECT_EQ(wave.displayParent, SpellType::ForceWhirlwind);
    EXPECT_EQ(wave.chainRoot, SpellType::ForcePush);
    EXPECT_EQ(wave.visualDepth, 2);

    const auto &storm = getEntry(entries, SpellType::ForceStorm);
    EXPECT_EQ(storm.displayParent, SpellType::Lightning);
    EXPECT_EQ(storm.chainRoot, SpellType::Shock);
    EXPECT_EQ(storm.visualDepth, 2);
}

TEST_F(SpellsTest, should_expose_known_and_chosen_prerequisites_as_candidate_context) {
    auto table = makeSpellsTable(
        {"prerequisites", "usertype", "guardian"},
        28,
        {{static_cast<int>(SpellType::ForcePush), {{"usertype", "1"}, {"guardian", "0"}}},
         {static_cast<int>(SpellType::ForceWhirlwind), {{"prerequisites", "23"}, {"usertype", "1"}, {"guardian", "0"}}},
         {static_cast<int>(SpellType::ForceWave), {{"prerequisites", "23_27"}, {"usertype", "1"}, {"guardian", "0"}}}});
    load(std::move(table));

    CreatureAttributes attributes;
    auto entries = spells->getLevelUpDisplayEntries(attributes, getGuardian(), {});
    EXPECT_EQ(getEntry(entries, SpellType::ForcePush).availability, SpellAvailability::Selectable);
    EXPECT_EQ(getEntry(entries, SpellType::ForceWhirlwind).availability, SpellAvailability::LockedMissingPrerequisite);
    EXPECT_FALSE(spells->isLevelUpCandidate(SpellType::ForceWhirlwind, attributes, getGuardian(), {}));

    attributes.addSpell(SpellType::ForcePush);
    entries = spells->getLevelUpDisplayEntries(attributes, getGuardian(), {});
    EXPECT_EQ(getEntry(entries, SpellType::ForcePush).availability, SpellAvailability::Known);
    EXPECT_EQ(getEntry(entries, SpellType::ForceWhirlwind).availability, SpellAvailability::Selectable);
    EXPECT_FALSE(spells->isLevelUpCandidate(SpellType::ForcePush, attributes, getGuardian(), {}));
    EXPECT_TRUE(spells->isLevelUpCandidate(SpellType::ForceWhirlwind, attributes, getGuardian(), {}));
    EXPECT_THAT(spells->getLevelUpCandidates(attributes, getGuardian(), {}), Contains(SpellType::ForceWhirlwind));

    std::set<SpellType> chosen {SpellType::ForceWhirlwind};
    entries = spells->getLevelUpDisplayEntries(attributes, getGuardian(), chosen);
    EXPECT_EQ(getEntry(entries, SpellType::ForceWhirlwind).availability, SpellAvailability::Chosen);
    EXPECT_EQ(getEntry(entries, SpellType::ForceWave).availability, SpellAvailability::Selectable);
    EXPECT_FALSE(spells->isLevelUpCandidate(SpellType::ForceWhirlwind, attributes, getGuardian(), chosen));
    EXPECT_TRUE(spells->isLevelUpCandidate(SpellType::ForceWave, attributes, getGuardian(), chosen));
}

TEST_F(SpellsTest, should_build_tsl_chains_from_prerequisites_when_pips_do_not_express_depth) {
    auto table = makeSpellsTable(
        {"pips", "prerequisites", "usertype", "guardian"},
        138,
        {{static_cast<int>(SpellType::Cure), {{"pips", "2"}, {"usertype", "1"}, {"guardian", "0"}}},
         {static_cast<int>(SpellType::Heal), {{"pips", "3"}, {"prerequisites", "10"}, {"usertype", "1"}, {"guardian", "0"}}},
         {static_cast<int>(SpellType::MasterHeal), {{"pips", "3"}, {"prerequisites", "10_28"}, {"usertype", "1"}, {"guardian", "0"}}},
         {static_cast<int>(SpellType::ForceBarrier), {{"pips", "3"}, {"usertype", "1"}, {"guardian", "0"}}},
         {static_cast<int>(SpellType::ImprovedForceBarrier), {{"pips", "3"}, {"prerequisites", "135"}, {"usertype", "1"}, {"guardian", "0"}}},
         {static_cast<int>(SpellType::MasterForceBarrier), {{"pips", "3"}, {"prerequisites", "135_136"}, {"usertype", "1"}, {"guardian", "0"}}}});
    load(std::move(table));

    CreatureAttributes attributes;
    auto entries = spells->getLevelUpDisplayEntries(attributes, getGuardian(), {});

    EXPECT_EQ(getEntry(entries, SpellType::Heal).visualDepth, 1);
    EXPECT_EQ(getEntry(entries, SpellType::MasterHeal).visualDepth, 2);
    EXPECT_EQ(getEntry(entries, SpellType::ImprovedForceBarrier).visualDepth, 1);
    EXPECT_EQ(getEntry(entries, SpellType::MasterForceBarrier).visualDepth, 2);
    EXPECT_EQ(spells->get(SpellType::ForceBarrier)->pips, 3);
    EXPECT_EQ(spells->get(SpellType::ImprovedForceBarrier)->pips, 3);
    EXPECT_EQ(spells->get(SpellType::MasterForceBarrier)->pips, 3);
}

TEST_F(SpellsTest, should_filter_non_picker_rows_and_preserve_class_gate_locking) {
    auto table = makeSpellsTable(
        {"usertype", "guardian"},
        5,
        {{0, {{"usertype", "1"}, {"guardian", "2"}}},
         {1, {{"usertype", "4"}, {"guardian", "0"}}},
         {2, {{"usertype", "6"}, {"guardian", "0"}}},
         {3, {{"usertype", "-2"}, {"guardian", "0"}}},
         {4, {{"usertype", "1"}, {"guardian", "-1"}}}});
    load(std::move(table));

    CreatureAttributes attributes;
    auto entries = spells->getLevelUpDisplayEntries(attributes, getGuardian(), {});
    EXPECT_TRUE(getEntry(entries, static_cast<SpellType>(0)).visible);
    EXPECT_EQ(getEntry(entries, static_cast<SpellType>(0)).availability, SpellAvailability::LockedClassLevel);
    EXPECT_FALSE(getEntry(entries, static_cast<SpellType>(1)).visible);
    EXPECT_FALSE(getEntry(entries, static_cast<SpellType>(2)).visible);
    EXPECT_FALSE(getEntry(entries, static_cast<SpellType>(3)).visible);
    EXPECT_FALSE(getEntry(entries, static_cast<SpellType>(4)).visible);

    attributes.addClassLevels(&getGuardian(), 1);
    entries = spells->getLevelUpDisplayEntries(attributes, getGuardian(), {});
    EXPECT_EQ(getEntry(entries, static_cast<SpellType>(0)).availability, SpellAvailability::Selectable);
    EXPECT_TRUE(spells->isLevelUpCandidate(static_cast<SpellType>(0), attributes, getGuardian(), {}));
}
