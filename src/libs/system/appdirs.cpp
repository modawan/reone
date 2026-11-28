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

#include "reone/system/appdirs.h"

#include "reone/system/buildinfo.h"
#include "reone/system/exception/filenotfound.h"
#include "reone/system/fileutil.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace reone {

std::filesystem::path executableDirectory() {
#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return std::filesystem::path(buffer).parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return {};
    }
    std::error_code ec;
    auto path = std::filesystem::canonical(buffer.c_str(), ec);
    return ec ? std::filesystem::path() : path.parent_path();
#else
    // Linux and the BSDs that mount a Linux-compatible procfs. Resolves
    // symlinks, so bin/reone -> ../libexec/reone/engine gives libexec/reone.
    std::error_code ec;
    auto path = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path() : path.parent_path();
#endif
}

std::vector<std::filesystem::path> dataDirectories(const std::filesystem::path &exeDir,
                                                   const std::filesystem::path &relativeDataDir,
                                                   const std::filesystem::path &cwd) {
    std::vector<std::filesystem::path> dirs;
    if (!exeDir.empty()) {
        dirs.push_back(exeDir);
        if (!relativeDataDir.empty()) {
            dirs.push_back((exeDir / relativeDataDir).lexically_normal());
        }
    }
    if (std::find(dirs.begin(), dirs.end(), cwd) == dirs.end()) {
        dirs.push_back(cwd);
    }
    return dirs;
}

std::vector<std::filesystem::path> dataDirectories() {
    std::error_code ec;
    auto cwd = std::filesystem::current_path(ec);
    return dataDirectories(executableDirectory(), relativeDataDirectory(), cwd);
}

std::optional<std::filesystem::path> findDataFile(std::string_view filename,
                                                  const std::vector<std::filesystem::path> &dirs) {
    for (const auto &dir : dirs) {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) {
            continue;
        }
        if (auto path = findFileIgnoreCase(dir, filename)) {
            return path;
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> findDataFile(std::string_view filename) {
    return findDataFile(filename, dataDirectories());
}

std::filesystem::path getDataFile(std::string_view filename) {
    return getDataFile(filename, dataDirectories());
}

std::filesystem::path getDataFile(std::string_view filename, const std::vector<std::filesystem::path> &dirs) {
    if (auto path = findDataFile(filename, dirs)) {
        return *path;
    }
    std::string searched;
    for (const auto &dir : dirs) {
        if (!searched.empty()) {
            searched += ", ";
        }
        searched += dir.string();
    }
    throw FileNotFoundException(str(boost::format("%1% (searched %2%)") % filename % searched));
}

std::filesystem::path userDirectory(const std::filesystem::path &cwd, const char *xdgConfigHome, const char *home) {
    std::error_code ec;
    if (std::filesystem::exists(cwd / kConfigFilename, ec)) {
        return cwd;
    }
#if defined(_WIN32) || defined(__APPLE__)
    (void)xdgConfigHome;
    (void)home;
    return cwd;
#else
    // The XDG Base Directory specification says a relative XDG_CONFIG_HOME is
    // invalid and must be ignored.
    if (xdgConfigHome && *xdgConfigHome && std::filesystem::path(xdgConfigHome).is_absolute()) {
        return std::filesystem::path(xdgConfigHome) / "reone";
    }
    if (home && *home) {
        return std::filesystem::path(home) / ".config" / "reone";
    }
    return cwd;
#endif
}

std::filesystem::path userDirectory() {
    std::error_code ec;
    auto cwd = std::filesystem::current_path(ec);
    auto dir = userDirectory(cwd, std::getenv("XDG_CONFIG_HOME"), std::getenv("HOME"));
    // Failing here is not fatal: whatever then opens a file in it reports
    // the path it could not open.
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::filesystem::path configFilePath() {
    return userDirectory() / kConfigFilename;
}

std::filesystem::path userFilePath(std::string_view filename) {
    std::error_code ec;
    auto inCwd = std::filesystem::current_path(ec) / filename;
    if (!ec && std::filesystem::exists(inCwd, ec)) {
        return inCwd;
    }
    return userDirectory() / filename;
}

} // namespace reone
