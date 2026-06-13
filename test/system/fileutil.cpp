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

#include "reone/system/fileutil.h"

using namespace reone;

TEST(FileUtilities, should_find_file_ignoring_case) {
    // given
    auto tmpDirPath = std::filesystem::temp_directory_path();
    tmpDirPath.append("reone_test_file_util");
    auto tmpFilePath = tmpDirPath;
    tmpFilePath.append("MiXeD");
    std::filesystem::create_directory(tmpDirPath);
    auto tmpFile = std::ofstream(tmpFilePath, std::ios::binary);
    tmpFile.flush();
    tmpFile.close();

    // when
    auto lowerPath = findFileIgnoreCase(tmpDirPath, "mixed");
    auto upperPath = findFileIgnoreCase(tmpDirPath, "MIXED");
    auto superPath = findFileIgnoreCase(tmpDirPath, "MiXeDs");

    // then
    EXPECT_EQ(tmpFilePath, *lowerPath);
    EXPECT_FALSE(upperPath);
    EXPECT_FALSE(superPath);

    // cleanup
    std::filesystem::remove_all(tmpDirPath);
}

TEST(FileUtilities, should_reject_unsafe_relative_path_components) {
    auto tmpDirPath = std::filesystem::temp_directory_path() / "reone_test_file_util_unsafe";
    std::filesystem::remove_all(tmpDirPath);
    std::filesystem::create_directories(tmpDirPath);

    EXPECT_FALSE(findFileIgnoreCase(tmpDirPath, ".."));
    EXPECT_FALSE(findFileIgnoreCase(tmpDirPath, "foo/../bar"));
    EXPECT_FALSE(findFileIgnoreCase(tmpDirPath, "."));

    std::filesystem::remove_all(tmpDirPath);
}

TEST(FileUtilities, should_validate_resref_names) {
    EXPECT_TRUE(isValidResRef("end_m01aa"));
    EXPECT_TRUE(isValidResRef("000001"));
    EXPECT_FALSE(isValidResRef(""));
    EXPECT_FALSE(isValidResRef("../evil"));
    EXPECT_FALSE(isValidResRef("bad/name"));
    EXPECT_FALSE(isValidResRef("toolongresrefname1"));
}
