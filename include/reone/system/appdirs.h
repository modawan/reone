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

#pragma once

namespace reone {

/**
 * Where the applications find the files that ship with them and the files
 * they keep per user.
 *
 * Files that ship with reone (shaderpack.erf, cmudict.dict) used to be read
 * from the current directory only, so the applications worked only when
 * started from the build or release directory. They are now looked up next
 * to the executable first, then in the installed data directory, then in the
 * current directory as before.
 *
 * Per-user files (reone.cfg, engine.log, imgui.ini, pronouncing.dict) stay in the
 * current directory whenever a reone.cfg is already there, which keeps every
 * existing setup and the Windows release as they are. Otherwise, on Linux and
 * other Unix systems except macOS, they live in $XDG_CONFIG_HOME/reone
 * (~/.config/reone), so an installed or packaged reone, started from anywhere,
 * finds the same configuration every time.
 */

/** The configuration file every application shares. */
constexpr char kConfigFilename[] = "reone.cfg";

/** Directory of the running executable, or an empty path when unknown. */
std::filesystem::path executableDirectory();

/**
 * Directories searched, in order, for files that ship with reone: exeDir,
 * then exeDir joined with relativeDataDir when that is not empty, then cwd.
 * An empty exeDir is skipped.
 */
std::vector<std::filesystem::path> dataDirectories(const std::filesystem::path &exeDir,
                                                   const std::filesystem::path &relativeDataDir,
                                                   const std::filesystem::path &cwd);

/** dataDirectories for this process. */
std::vector<std::filesystem::path> dataDirectories();

/** The first of dirs holding filename (ignoring case), if any. Directories that do not exist are skipped. */
std::optional<std::filesystem::path> findDataFile(std::string_view filename,
                                                  const std::vector<std::filesystem::path> &dirs);

/** findDataFile in dataDirectories(). */
std::optional<std::filesystem::path> findDataFile(std::string_view filename);

/**
 * findDataFile in dataDirectories(), throwing FileNotFoundException naming
 * every directory searched when the file is in none of them.
 */
std::filesystem::path getDataFile(std::string_view filename);

/** getDataFile in dirs. */
std::filesystem::path getDataFile(std::string_view filename, const std::vector<std::filesystem::path> &dirs);

/**
 * The per-user directory, decided without touching anything but cwd: cwd if
 * cwd/reone.cfg exists; on Windows and macOS, cwd; otherwise xdgConfigHome/reone
 * when xdgConfigHome is an absolute path, home/.config/reone when home is not
 * empty, and cwd as a last resort.
 */
std::filesystem::path userDirectory(const std::filesystem::path &cwd, const char *xdgConfigHome, const char *home);

/** userDirectory for this process, created if it does not exist yet. */
std::filesystem::path userDirectory();

/** userDirectory() / kConfigFilename. */
std::filesystem::path configFilePath();

/**
 * A per-user file the user may have kept in the current directory: cwd/filename
 * when that exists, userDirectory()/filename otherwise.
 */
std::filesystem::path userFilePath(std::string_view filename);

} // namespace reone
