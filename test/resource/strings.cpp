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

#include <gtest/gtest.h>

#include "reone/resource/format/tlkwriter.h"
#include "reone/resource/odysseyroots.h"
#include "reone/resource/strings.h"
#include "reone/system/binarywriter.h"
#include "reone/system/logutil.h"
#include "reone/system/stream/fileoutput.h"

using namespace reone;
using namespace reone::resource;

namespace {

std::shared_ptr<TalkTable> table(std::initializer_list<TalkTable::String> strings) {
    return std::make_shared<TalkTable>(std::vector<TalkTable::String>(strings));
}

std::shared_ptr<TalkTable> tableWithWinningRow(std::size_t row, std::string text) {
    std::vector<TalkTable::String> strings(row + 1);
    strings[row].text = std::move(text);
    return std::make_shared<TalkTable>(std::move(strings));
}

} // namespace

TEST(Strings, should_init_talktable_and_get_string_and_sound) {
    // given

    auto tmpDirPath = std::filesystem::temp_directory_path();
    tmpDirPath.append("reone_test_strings");
    std::filesystem::create_directory(tmpDirPath);

    auto tlkPath = tmpDirPath;
    tlkPath.append("dialog.tlk");
    auto tlk = FileOutputStream(tlkPath);
    tlk.write("TLK V3.0", 8);
    tlk.write("\x00\x00\x00\x00", 4);
    tlk.write("\x01\x00\x00\x00", 4);
    tlk.write("\x3c\x00\x00\x00", 4);
    // String 0 Data
    tlk.write("\x03\x00\x00\x00", 4);
    tlk.write("some_sound\x00\x00\x00\x00\x00\x00", 16);
    tlk.write("\x00\x00\x00\x00", 4);
    tlk.write("\x00\x00\x00\x00", 4);
    tlk.write("\x00\x00\x00\x00", 4);
    tlk.write("\x0d\x00\x00\x00", 4);
    tlk.write("\x00\x00\x00\x00", 4);
    // Strings 0 Entry
    tlk.write("Hello, world!", 14);
    //
    tlk.close();

    auto strings = Strings();

    auto expectedText = std::string("Hello, world!");
    auto expectedSound = std::string("some_sound");

    // when

    strings.init(tmpDirPath);
    auto text = strings.getText(0);
    auto sound = strings.getSound(0);

    // then

    EXPECT_EQ(expectedText, text);
    EXPECT_EQ(expectedSound, sound);

    // cleanup

    std::filesystem::remove_all(tmpDirPath);
}

TEST(Strings, searches_loaded_slots_in_observable_order) {
    Strings strings;
    strings.setTalkTable(0, table({{"base zero", "base zero sound"}}));
    strings.setTalkTable(1, table({{"live1 zero", "live1 zero sound"},
                                   {"live1 one", "live1 one sound"}}));
    strings.setTalkTable(2, table({{"live2 zero", "live2 zero sound"},
                                   {"live2 one", "live2 one sound"},
                                   {"live2 two", "live2 two sound"}}));

    EXPECT_EQ("base zero", strings.getText(0));
    EXPECT_EQ("live1 one", strings.getText(1));
    EXPECT_EQ("live2 two", strings.getText(2));
    EXPECT_EQ("live1 one sound", strings.getSound(1));
}

TEST(Strings, skips_missing_intermediate_slots) {
    Strings strings;
    strings.setTalkTable(0, table({{"base", ""}}));
    strings.setTalkTable(2, table({{"live2 zero", ""}, {"live2 one", ""}}));

    EXPECT_EQ("live2 one", strings.getText(1));
}

TEST(Strings, the_first_table_containing_the_row_wins_even_when_fields_are_empty) {
    Strings strings;
    strings.setTalkTable(0, table({{"", ""}}));
    strings.setTalkTable(1, table({{"later text", "later sound"}}));

    EXPECT_EQ("", strings.getText(0));
    EXPECT_EQ("", strings.getSound(0));
}

TEST(Strings, live1_wins_a_collision_when_dialog_lacks_the_row) {
    Strings strings;
    strings.setTalkTable(0, table({}));
    strings.setTalkTable(1, table({{"live1", "live1 sound"}}));
    strings.setTalkTable(2, table({{"live2", "live2 sound"}}));

    EXPECT_EQ("live1", strings.getText(0));
    EXPECT_EQ("live1 sound", strings.getSound(0));
}

TEST(Strings, unresolved_rows_and_minus_one_are_empty) {
    Strings strings;
    strings.setTalkTable(0, table({{"base", "base sound"}}));

    EXPECT_EQ("", strings.getText(42));
    EXPECT_EQ("", strings.getSound(42));
    EXPECT_EQ("", strings.getText(-1));
    EXPECT_EQ("", strings.getSound(-1));
}

TEST(Strings, negative_values_other_than_minus_one_are_masked) {
    Strings strings;
    strings.setTalkTable(0, table({{"zero", ""}, {"one", "one sound"}}));
    auto strRefWithNegativeSignAndRowOne = static_cast<int>(0xff000001u);

    EXPECT_LT(strRefWithNegativeSignAndRowOne, 0);
    EXPECT_EQ("one", strings.getText(strRefWithNegativeSignAndRowOne));
    EXPECT_EQ("one sound", strings.getSound(strRefWithNegativeSignAndRowOne));
}

TEST(Strings, replacing_a_slot_changes_only_that_slot_and_never_reorders_it) {
    Strings strings;
    strings.setTalkTable(1, table({{"live1 old", ""}}));
    strings.setTalkTable(2, table({{"live2", ""}, {"live2 one", ""}}));

    EXPECT_EQ("live1 old", strings.getText(0));
    EXPECT_EQ("live2 one", strings.getText(1));

    strings.setTalkTable(1, table({{"live1 new", ""}}));

    EXPECT_EQ("live1 new", strings.getText(0));
    EXPECT_EQ("live2 one", strings.getText(1));
}

TEST(Strings, a_failed_file_replacement_leaves_only_that_slot_empty) {
    auto dir = std::filesystem::temp_directory_path() / "reone_test_strings_replace";
    std::filesystem::create_directories(dir);

    Strings strings;
    strings.setTalkTable(1, table({{"live1", ""}}));
    strings.setTalkTable(2, table({{"live2", ""}}));

    EXPECT_FALSE(strings.loadTalkTable(1, dir / "missing.tlk"));
    EXPECT_EQ("live2", strings.getText(0));

    auto malformedPath = dir / "malformed.tlk";
    FileOutputStream malformed(malformedPath);
    malformed.write("not a tlk", 9);
    malformed.close();
    strings.setTalkTable(1, table({{"live1 again", ""}}));
    EXPECT_FALSE(strings.loadTalkTable(1, malformedPath));
    EXPECT_EQ("live2", strings.getText(0));

    std::filesystem::remove_all(dir);
}

TEST(Strings, every_observable_slot_is_searchable) {
    for (std::size_t target = 0; target < Strings::kTalkTableSlotCount; ++target) {
        Strings strings;
        strings.setTalkTable(target, tableWithWinningRow(target, "slot " + std::to_string(target)));

        EXPECT_EQ("slot " + std::to_string(target), strings.getText(static_cast<int>(target)));
    }
}

TEST(Strings, a_slot_beyond_the_observable_seven_is_rejected) {
    Strings strings;

    EXPECT_THROW(strings.setTalkTable(Strings::kTalkTableSlotCount, table({{"eighth", ""}})),
                 std::out_of_range);
    EXPECT_THROW(strings.loadTalkTable(Strings::kTalkTableSlotCount, "unused.tlk"),
                 std::out_of_range);
}

TEST(Strings, shared_roots_populate_bound_live_slots_without_disturbing_dialog) {
    auto dir = std::filesystem::temp_directory_path() / "reone_test_live_tlk_composition";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "game");
    std::filesystem::create_directories(dir / "live1");
    std::filesystem::create_directories(dir / "live2");
    std::filesystem::create_directories(dir / "live3");
    std::filesystem::create_directories(dir / "live4");

    TalkTable dialog(std::vector<TalkTable::String> {{"dialog", ""}});
    TlkWriter(dialog).save(dir / "game" / "dialog.tlk");
    TalkTable live1(std::vector<TalkTable::String> {
        {"live1 collision", ""}, {"live1 fallback", ""}});
    TlkWriter(live1).save(dir / "live1" / "live1.tlk");
    TalkTable live2(std::vector<TalkTable::String> {
        {"", ""}, {"live2 collision", ""}, {"live2 fallback", ""}});
    TlkWriter(live2).save(dir / "live2" / "live2.tlk");
    FileOutputStream malformed(dir / "live3" / "live3.tlk");
    malformed.write("broken", 6);
    malformed.close();
    TalkTable live4(std::vector<TalkTable::String> {
        {"", ""}, {"", ""}, {"", ""}, {"live4 after malformed", ""}});
    TlkWriter(live4).save(dir / "live4" / "live4.tlk");

    OdysseyResourceRoots roots;
    roots.livePackages[0] = dir / "live1";
    roots.livePackages[1] = dir / "live2";
    roots.livePackages[2] = dir / "live3";
    roots.livePackages[3] = dir / "live4";

    Strings strings;
    strings.init(dir / "game");
    ASSERT_NO_THROW(loadLiveTalkTables(strings, roots));

    EXPECT_EQ("dialog", strings.getText(0));
    EXPECT_EQ("live1 fallback", strings.getText(1));
    EXPECT_EQ("live2 fallback", strings.getText(2));
    EXPECT_EQ("live4 after malformed", strings.getText(3));

    std::filesystem::remove_all(dir);
}
