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

#include "reone/game/journal.h"
#include "reone/resource/gff.h"

#include "../fixtures/resource.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;

using testing::_;
using testing::NiceMock;
using testing::Return;

class JournalTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto entry1 = std::make_shared<Gff>(
            0,
            std::vector<Gff::Field> {
                Gff::Field::newDword("ID", 10),
                Gff::Field::newWord("End", 0),
                Gff::Field::newCExoLocString("Text", -1, "first step"),
                Gff::Field::newFloat("XP_Percentage", 0.5f)});

        auto entry2 = std::make_shared<Gff>(
            0,
            std::vector<Gff::Field> {
                Gff::Field::newDword("ID", 20),
                Gff::Field::newWord("End", 1),
                Gff::Field::newCExoLocString("Text", 123, ""),
                Gff::Field::newFloat("XP_Percentage", 1.0f)});

        auto category = std::make_shared<Gff>(
            0,
            std::vector<Gff::Field> {
                Gff::Field::newCExoString("Tag", "test_plot"),
                Gff::Field::newCExoLocString("Name", 42, ""),
                Gff::Field::newList("EntryList", {entry1, entry2})});

        _jrlGff = std::make_shared<Gff>(
            0xffffffff,
            std::vector<Gff::Field> {
                Gff::Field::newList("Categories", {category})});

        ON_CALL(_gffs, get(_, _)).WillByDefault(Return(_jrlGff));

        _journal = std::make_unique<Journal>(_gffs, _strings);
    }

    NiceMock<MockGffs> _gffs;
    NiceMock<MockStrings> _strings;
    std::shared_ptr<Gff> _jrlGff;
    std::unique_ptr<Journal> _journal;
};

TEST_F(JournalTest, should_add_entry_and_advance_to_higher_state) {
    EXPECT_TRUE(_journal->addEntry("test_plot", 10));
    EXPECT_EQ(10, _journal->getEntryState("test_plot"));

    EXPECT_TRUE(_journal->addEntry("test_plot", 20));
    EXPECT_EQ(20, _journal->getEntryState("test_plot"));

    EXPECT_EQ(1ll, _journal->quests().size());
}

TEST_F(JournalTest, should_not_regress_to_lower_state_without_override) {
    _journal->addEntry("test_plot", 20);

    EXPECT_FALSE(_journal->addEntry("test_plot", 10));
    EXPECT_EQ(20, _journal->getEntryState("test_plot"));
}

TEST_F(JournalTest, should_regress_to_lower_state_with_override) {
    _journal->addEntry("test_plot", 20);

    EXPECT_TRUE(_journal->addEntry("test_plot", 10, true));
    EXPECT_EQ(10, _journal->getEntryState("test_plot"));
}

TEST_F(JournalTest, should_treat_equal_state_as_no_change) {
    _journal->addEntry("test_plot", 10);

    EXPECT_FALSE(_journal->addEntry("test_plot", 10));
    EXPECT_FALSE(_journal->addEntry("test_plot", 10, true));
    EXPECT_EQ(10, _journal->getEntryState("test_plot"));
}

TEST_F(JournalTest, should_match_plot_ids_case_insensitively) {
    _journal->addEntry("Test_Plot", 10);

    EXPECT_EQ(10, _journal->getEntryState("TEST_PLOT"));

    _journal->addEntry("test_plot", 20);
    EXPECT_EQ(1ll, _journal->quests().size());
}

TEST_F(JournalTest, should_remove_entry) {
    _journal->addEntry("test_plot", 10);

    EXPECT_TRUE(_journal->removeEntry("test_plot"));
    EXPECT_EQ(0, _journal->getEntryState("test_plot"));
    EXPECT_TRUE(_journal->quests().empty());

    EXPECT_FALSE(_journal->removeEntry("test_plot"));
}

TEST_F(JournalTest, should_return_zero_state_for_unknown_plot) {
    EXPECT_EQ(0, _journal->getEntryState("unknown_plot"));
}

TEST_F(JournalTest, should_look_up_current_entry_by_id_not_index) {
    const JRL::Entry *entry = _journal->findEntry("test_plot", 20);

    ASSERT_TRUE(entry);
    EXPECT_EQ(20u, entry->id);

    EXPECT_FALSE(_journal->findEntry("test_plot", 1));
}

TEST_F(JournalTest, should_derive_completion_from_current_entry_end_flag) {
    _journal->addEntry("test_plot", 10);
    EXPECT_FALSE(_journal->isCompleted("test_plot"));

    _journal->addEntry("test_plot", 20);
    EXPECT_TRUE(_journal->isCompleted("test_plot"));
}

TEST_F(JournalTest, should_resolve_quest_name_and_entry_text) {
    EXPECT_CALL(_strings, getText(42)).WillOnce(Return("Test Quest"));
    EXPECT_EQ("Test Quest", _journal->getQuestName("test_plot"));

    EXPECT_EQ("first step", _journal->getEntryText("test_plot", 10));

    EXPECT_CALL(_strings, getText(123)).WillOnce(Return("all done"));
    EXPECT_EQ("all done", _journal->getEntryText("test_plot", 20));
}

TEST_F(JournalTest, should_restore_entry_verbatim) {
    _journal->addEntry("test_plot", 20);

    _journal->restoreEntry("test_plot", 10, 1, 2);

    EXPECT_EQ(10, _journal->getEntryState("test_plot"));
    ASSERT_EQ(1ll, _journal->quests().size());
    EXPECT_EQ(1u, _journal->quests()[0].date);
    EXPECT_EQ(2u, _journal->quests()[0].time);
}

TEST_F(JournalTest, should_track_state_for_plot_missing_from_global_journal) {
    EXPECT_TRUE(_journal->addEntry("unlisted_plot", 5));
    EXPECT_EQ(5, _journal->getEntryState("unlisted_plot"));
    EXPECT_FALSE(_journal->isCompleted("unlisted_plot"));
    EXPECT_EQ("", _journal->getQuestName("unlisted_plot"));
}

TEST_F(JournalTest, should_clear_state_on_reset) {
    _journal->addEntry("test_plot", 10);

    _journal->reset();

    EXPECT_EQ(0, _journal->getEntryState("test_plot"));
    EXPECT_TRUE(_journal->quests().empty());
}
