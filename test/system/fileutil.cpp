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

#include "reone/system/exception/filenotfound.h"
#include "reone/system/fileutil.h"

using namespace reone;

namespace {

/// Temporary directory with a nested file, removed on destruction.
struct TmpTree {
    std::filesystem::path root;
    std::filesystem::path subDir;
    std::filesystem::path file;

    TmpTree() {
        root = std::filesystem::temp_directory_path() / "reone_test_file_util_tree";
        subDir = root / "SubDir";
        file = subDir / "MiXeD";
        std::filesystem::create_directories(subDir);
        std::ofstream(file, std::ios::binary).flush();
    }

    ~TmpTree() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

} // namespace

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

TEST(FileUtilities, should_find_nested_file_with_either_separator) {
    TmpTree tree;

    auto slashPath = findFileIgnoreCase(tree.root, "subdir/mixed");
    auto backslashPath = findFileIgnoreCase(tree.root, "subdir\\mixed");

    ASSERT_TRUE(slashPath);
    EXPECT_EQ(tree.file, *slashPath);
    ASSERT_TRUE(backslashPath);
    EXPECT_EQ(tree.file, *backslashPath);
}

TEST(FileUtilities, should_not_find_file_by_unsafe_relative_path) {
    TmpTree tree;

    EXPECT_FALSE(findFileIgnoreCase(tree.subDir, ""));
    EXPECT_FALSE(findFileIgnoreCase(tree.subDir, "."));
    EXPECT_FALSE(findFileIgnoreCase(tree.subDir, ".."));
    EXPECT_FALSE(findFileIgnoreCase(tree.subDir, "../subdir/mixed"));
    EXPECT_FALSE(findFileIgnoreCase(tree.subDir, "..\\subdir\\mixed"));
    EXPECT_FALSE(findFileIgnoreCase(tree.root, "subdir/../subdir/mixed"));
    EXPECT_FALSE(findFileIgnoreCase(tree.root, "subdir//mixed"));

    EXPECT_THROW(getFileIgnoreCase(tree.subDir, ".."), FileNotFoundException);
    EXPECT_THROW(getFileIgnoreCase(tree.subDir, "../subdir/mixed"), FileNotFoundException);
}

TEST(FileUtilities, should_validate_resrefs) {
    EXPECT_TRUE(isValidResRef("a"));
    EXPECT_TRUE(isValidResRef("abc_123"));
    EXPECT_TRUE(isValidResRef("abcdefgh12345678"));

    EXPECT_FALSE(isValidResRef(""));
    EXPECT_FALSE(isValidResRef("abcdefgh123456789"));
    EXPECT_FALSE(isValidResRef("a b"));
    EXPECT_FALSE(isValidResRef("a-b"));
    EXPECT_FALSE(isValidResRef("a/b"));
    EXPECT_FALSE(isValidResRef("a.b"));
}

TEST(FileUtilities, should_accept_safe_path_components) {
    EXPECT_TRUE(isSafePathComponent("000001"));
    EXPECT_TRUE(isSafePathComponent("QUICKSAVE"));
    EXPECT_TRUE(isSafePathComponent("AUTOSAVE"));
    EXPECT_TRUE(isSafePathComponent("000043 - Game42"));
    EXPECT_TRUE(isSafePathComponent("a_b-c 1"));
    EXPECT_TRUE(isSafePathComponent("name.with.dots"));
}

TEST(FileUtilities, should_reject_unsafe_path_components) {
    EXPECT_FALSE(isSafePathComponent(""));
    EXPECT_FALSE(isSafePathComponent("."));
    EXPECT_FALSE(isSafePathComponent(".."));
    EXPECT_FALSE(isSafePathComponent("a/b"));
    EXPECT_FALSE(isSafePathComponent("a\\b"));
    EXPECT_FALSE(isSafePathComponent("/absolute"));
    EXPECT_FALSE(isSafePathComponent("\\absolute"));
    EXPECT_FALSE(isSafePathComponent("C:"));
    EXPECT_FALSE(isSafePathComponent("C:\\saves"));
    EXPECT_FALSE(isSafePathComponent("../escape"));
    EXPECT_FALSE(isSafePathComponent("..\\escape"));
    EXPECT_FALSE(isSafePathComponent("bad\nname"));
    EXPECT_FALSE(isSafePathComponent("bad*name"));
    EXPECT_FALSE(isSafePathComponent("bad?name"));
    EXPECT_FALSE(isSafePathComponent("bad|name"));
    EXPECT_FALSE(isSafePathComponent("bad<name>"));
    EXPECT_FALSE(isSafePathComponent("bad\"name"));
}
