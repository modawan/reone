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

#ifdef _WIN32
#include <windows.h>
#undef max
#undef min
#endif

#include <filesystem>
#include <iostream>
#include <memory>

#include <boost/format.hpp>

#include "reone/system/logger.h"
#include "reone/system/logutil.h"
#include "reone/system/threadutil.h"

#include "engine.h"

#ifndef __EMSCRIPTEN__
#include "optionsparser.h"
#endif

#ifdef __EMSCRIPTEN__
static std::unique_ptr<reone::Options> g_webOptions;
static std::unique_ptr<reone::Engine> g_webEngine;

static bool hasMountedGameData(const std::filesystem::path &gamePath) {
    return std::filesystem::exists(gamePath / "swkotor.exe") ||
           std::filesystem::exists(gamePath / "swkotor2.exe");
}
#endif

using namespace reone;
using namespace reone::graphics;

static constexpr char kLogFilename[] = "engine.log";
static constexpr char kEngineStartupMessage[] = "reone smoke signal: engine startup";

int main(int argc, char **argv) {
#ifdef _WIN32
    SetProcessDPIAware();
#endif
    markMainThread();

    std::unique_ptr<Options> options;
#ifdef __EMSCRIPTEN__
    options = std::make_unique<Options>();
    options->game.path = std::filesystem::path("/game");
    options->graphics.shadowResolution = 1;
    options->graphics.ssr = false;
    options->graphics.ssao = false;

    if (!hasMountedGameData(options->game.path)) {
        std::cerr
            << "reone web: no mounted game data at /game (need swkotor.exe or swkotor2.exe). "
            << "Pick your install folder in Chrome/Edge (File System Access), or use CMake embedded preload."
            << std::endl;
        return 0;
    }
#else
    OptionsParser optionsParser {argc, argv};
    try {
        options = optionsParser.parse();
    } catch (const std::exception &ex) {
        std::cerr << "Error parsing options: " << ex.what() << std::endl;
        return 1;
    }
#endif
    try {
        Logger::instance.init(options->logging.severity, options->logging.channels, kLogFilename);
        info(kEngineStartupMessage);
        Logger::instance.flush();
    } catch (const std::exception &ex) {
        std::cerr << "Error initializing logging: " << ex.what() << std::endl;
        return 2;
    }
    try {
#ifdef __EMSCRIPTEN__
        g_webOptions = std::move(options);
        g_webEngine = std::make_unique<Engine>(*g_webOptions);
        g_webEngine->init();
        int exitCode = g_webEngine->run();
#else
        Engine engine {*options};
        engine.init();
        int exitCode = engine.run();
#endif
        return exitCode;
    } catch (const std::exception &ex) {
        auto message = str(boost::format("Engine failure: %1%") % ex.what());
#ifdef __EMSCRIPTEN__
        std::cerr << message << std::endl;
#endif
        try {
            error(message);
        } catch (...) {
            std::cerr << message << std::endl;
        }
        return 3;
    }
}
