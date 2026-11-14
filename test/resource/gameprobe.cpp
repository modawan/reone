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

#include <gtest/gtest.h>

#include "reone/resource/gameprobe.h"

#include "../fixtures/archive.h"

using namespace reone;
using namespace reone::resource;
using namespace reone::test;

// A game directory as far as the probe looks at it: a key file and the given
// folders, with no executable or settings file.
static void makeGameDirectory(const std::filesystem::path &dir, std::initializer_list<const char *> folders) {
    detail::writeFile(dir / "chitin.key", "");
    for (const char *folder : folders) {
        std::filesystem::create_directories(dir / folder);
    }
}

TEST(GameProbe, a_directory_with_streamwaves_is_kotor) {
    TmpDir tmp("reone_test_gameprobe_kotor");
    makeGameDirectory(tmp.path, {"streamwaves"});

    EXPECT_EQ(GameID::KotOR, GameProbe(tmp.path).probe());
}

TEST(GameProbe, a_directory_with_streamvoice_in_any_case_is_tsl) {
    TmpDir tmp("reone_test_gameprobe_tsl");
    makeGameDirectory(tmp.path, {"StreamVoice"});

    EXPECT_EQ(GameID::TSL, GameProbe(tmp.path).probe());
}

TEST(GameProbe, a_directory_with_neither_voice_folder_is_not_identified) {
    TmpDir tmp("reone_test_gameprobe_neither");
    makeGameDirectory(tmp.path, {});

    EXPECT_THROW(GameProbe(tmp.path).probe(), std::runtime_error);
}

TEST(GameProbe, a_directory_with_both_voice_folders_is_not_identified) {
    TmpDir tmp("reone_test_gameprobe_both");
    makeGameDirectory(tmp.path, {"streamwaves", "streamvoice"});

    EXPECT_THROW(GameProbe(tmp.path).probe(), std::runtime_error);
}

TEST(GameProbe, a_directory_without_chitin_key_says_so) {
    TmpDir tmp("reone_test_gameprobe_no_key");
    std::filesystem::create_directories(tmp.path / "streamwaves");

    try {
        GameProbe(tmp.path).probe();
        FAIL() << "expected the probe to throw";
    } catch (const std::runtime_error &ex) {
        EXPECT_NE(std::string::npos, std::string(ex.what()).find("chitin.key"));
    }
}
