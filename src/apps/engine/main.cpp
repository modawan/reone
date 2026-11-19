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

#include "reone/system/appdirs.h"
#include "reone/system/buildinfo.h"
#include "reone/system/logger.h"
#include "reone/system/logutil.h"
#include "reone/system/threadutil.h"

#include "engine.h"
#include "optionsparser.h"

using namespace reone;
using namespace reone::graphics;

static constexpr char kLogFilename[] = "engine.log";
static constexpr char kEngineStartupMessage[] = "reone smoke signal: engine startup";

int main(int argc, char **argv) {
#ifdef _WIN32
    SetProcessDPIAware();
#endif
    markMainThread();

    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--version") {
            std::cout << "reone " << versionString() << std::endl;
            return 0;
        }
    }

    std::unique_ptr<Options> options;
    OptionsParser optionsParser {argc, argv};
    try {
        options = optionsParser.parse();
    } catch (const std::exception &ex) {
        std::cerr << "Error parsing options: " << ex.what() << std::endl;
        return 1;
    }
    try {
        // The log goes beside the configuration: the current directory for a
        // setup that keeps reone.cfg there, the per-user directory otherwise.
        auto logPath = options->game.configurationPath.parent_path() / kLogFilename;
        Logger::instance.init(options->logging.severity, options->logging.channels, logPath.string());
        info(kEngineStartupMessage);
        info(str(boost::format("reone %1%") % versionString()));
        Logger::instance.flush();
    } catch (const std::exception &ex) {
        std::cerr << "Error initializing logging: " << ex.what() << std::endl;
        return 2;
    }
    Engine engine {*options};
    try {
        engine.init();
        int exitCode = engine.run();
        return exitCode;
    } catch (const std::exception &ex) {
        auto message = str(boost::format("Engine failure: %1%") % ex.what());
        try {
            error(message);
        } catch (...) {
            std::cerr << message << std::endl;
        }
        return 3;
    }
}
