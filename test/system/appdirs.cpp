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

#include "reone/system/appdirs.h"
#include "reone/system/buildinfo.h"
#include "reone/system/exception/filenotfound.h"

using namespace reone;

namespace {

/// A fresh temporary directory, removed on destruction.
struct TmpDir {
    std::filesystem::path root;

    explicit TmpDir(const std::string &name) {
        root = std::filesystem::temp_directory_path() / ("reone_test_appdirs_" + name);
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
    }

    ~TmpDir() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    std::filesystem::path touch(const std::filesystem::path &relPath) const {
        auto path = root / relPath;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << relPath.string();
        return path;
    }
};

std::string contents(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

TEST(AppDirectories, data_directories_are_the_executable_then_the_installed_data_then_cwd) {
    auto dirs = dataDirectories("/opt/reone/libexec/reone", "../../share/reone", "/home/user/games");

    ASSERT_EQ(3u, dirs.size());
    EXPECT_EQ(std::filesystem::path("/opt/reone/libexec/reone"), dirs[0]);
    EXPECT_EQ(std::filesystem::path("/opt/reone/share/reone"), dirs[1]);
    EXPECT_EQ(std::filesystem::path("/home/user/games"), dirs[2]);
}

TEST(AppDirectories, data_directories_without_an_install_layout_are_the_executable_then_cwd) {
    auto dirs = dataDirectories("/build/bin", "", "/home/user/games");

    ASSERT_EQ(2u, dirs.size());
    EXPECT_EQ(std::filesystem::path("/build/bin"), dirs[0]);
    EXPECT_EQ(std::filesystem::path("/home/user/games"), dirs[1]);
}

TEST(AppDirectories, data_directories_list_cwd_once_when_it_is_the_executable_directory) {
    auto dirs = dataDirectories("/build/bin", "", "/build/bin");

    ASSERT_EQ(1u, dirs.size());
    EXPECT_EQ(std::filesystem::path("/build/bin"), dirs[0]);
}

TEST(AppDirectories, data_directories_fall_back_to_cwd_when_the_executable_is_unknown) {
    auto dirs = dataDirectories("", "../../share/reone", "/home/user/games");

    ASSERT_EQ(1u, dirs.size());
    EXPECT_EQ(std::filesystem::path("/home/user/games"), dirs[0]);
}

TEST(AppDirectories, data_file_is_taken_from_the_first_directory_that_has_it) {
    TmpDir tmp("first");
    tmp.touch("exe/other.erf");
    tmp.touch("share/shaderpack.erf");
    tmp.touch("cwd/shaderpack.erf");

    auto found = findDataFile("shaderpack.erf", {tmp.root / "exe", tmp.root / "share", tmp.root / "cwd"});

    ASSERT_TRUE(found.has_value());
    EXPECT_EQ("share/shaderpack.erf", contents(*found));
}

TEST(AppDirectories, data_file_lookup_ignores_case_and_missing_directories) {
    TmpDir tmp("case");
    tmp.touch("share/ShaderPack.ERF");

    auto found = findDataFile("shaderpack.erf", {tmp.root / "missing", tmp.root / "share"});

    ASSERT_TRUE(found.has_value());
    EXPECT_EQ("share/ShaderPack.ERF", contents(*found));
}

TEST(AppDirectories, data_file_is_absent_when_no_directory_has_it) {
    TmpDir tmp("absent");

    EXPECT_FALSE(findDataFile("shaderpack.erf", {tmp.root}).has_value());
}

TEST(AppDirectories, missing_data_file_names_every_directory_searched) {
    try {
        getDataFile("reone-test-no-such-file.erf");
        FAIL() << "expected FileNotFoundException";
    } catch (const FileNotFoundException &ex) {
        std::string message = ex.what();
        EXPECT_NE(std::string::npos, message.find("reone-test-no-such-file.erf"));
        EXPECT_NE(std::string::npos, message.find("searched"));
        EXPECT_NE(std::string::npos, message.find(std::filesystem::current_path().string()));
    }
}

TEST(AppDirectories, user_directory_is_cwd_when_reone_cfg_is_already_there) {
    TmpDir tmp("cfg_in_cwd");
    tmp.touch("reone.cfg");

    EXPECT_EQ(tmp.root, userDirectory(tmp.root, "/home/user/.config", "/home/user"));
}

#if !defined(_WIN32) && !defined(__APPLE__)

TEST(AppDirectories, user_directory_is_under_xdg_config_home) {
    TmpDir tmp("xdg");

    EXPECT_EQ(std::filesystem::path("/home/user/.xdg/reone"), userDirectory(tmp.root, "/home/user/.xdg", "/home/user"));
}

TEST(AppDirectories, user_directory_ignores_a_relative_or_empty_xdg_config_home) {
    TmpDir tmp("xdg_relative");

    EXPECT_EQ(std::filesystem::path("/home/user/.config/reone"), userDirectory(tmp.root, "relative/config", "/home/user"));
    EXPECT_EQ(std::filesystem::path("/home/user/.config/reone"), userDirectory(tmp.root, "", "/home/user"));
    EXPECT_EQ(std::filesystem::path("/home/user/.config/reone"), userDirectory(tmp.root, nullptr, "/home/user"));
}

TEST(AppDirectories, user_directory_is_cwd_without_xdg_config_home_or_home) {
    TmpDir tmp("no_home");

    EXPECT_EQ(tmp.root, userDirectory(tmp.root, nullptr, nullptr));
    EXPECT_EQ(tmp.root, userDirectory(tmp.root, "", ""));
}

TEST(AppDirectories, installed_data_directory_is_relative_to_the_installed_applications) {
    std::filesystem::path relative = relativeDataDirectory();

    ASSERT_FALSE(relative.empty());
    EXPECT_FALSE(relative.is_absolute());
    // Wherever libexec is (lib, libexec, lib/<triplet>), the data directory
    // is <datadir>/reone, and datadir is share.
    auto data = (std::filesystem::path("/prefix/libexec/reone") / relative).lexically_normal();
    EXPECT_EQ(std::filesystem::path("reone"), data.filename());
    EXPECT_EQ(std::filesystem::path("share"), data.parent_path().filename());
}

#else

TEST(AppDirectories, user_directory_is_cwd_on_windows_and_macos) {
    TmpDir tmp("cwd_only");

    EXPECT_EQ(tmp.root, userDirectory(tmp.root, "/home/user/.config", "/home/user"));
}

#endif

TEST(AppDirectories, executable_directory_holds_the_running_test_binary) {
    auto dir = executableDirectory();

    ASSERT_FALSE(dir.empty());
#ifdef _WIN32
    EXPECT_TRUE(std::filesystem::exists(dir / "tests.exe"));
#else
    EXPECT_TRUE(std::filesystem::exists(dir / "tests"));
#endif
}

TEST(BuildInfo, version_is_never_empty) {
    EXPECT_NE(std::string(), versionString());
}
