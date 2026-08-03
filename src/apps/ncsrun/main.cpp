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

#include "reone/script/executioncontext.h"
#include "reone/script/format/ncsreader.h"
#include "reone/script/virtualmachine.h"
#include "reone/system/logger.h"
#include "reone/system/stream/fileinput.h"
#include "reone/system/threadutil.h"

using namespace reone;
using namespace reone::script;

using CmdArgs = boost::program_options::variables_map;

// Execute an NCS script and return the result.
static int run(const CmdArgs &args) {
    if (args["verbose"].as<bool>()) {
        markMainThread();
        std::set<LogChannel> channels;
        channels.insert(LogChannel::Script);
        channels.insert(LogChannel::Script2);
        channels.insert(LogChannel::Script3);
        Logger::instance.init(LogSeverity::Debug, channels, std::nullopt);
    }

    // Read program from the input NCS file.
    auto file = FileInputStream(args["input-file"].as<std::string>());
    auto reader = NcsReader(file, "input");
    reader.load();
    auto program = reader.program();

    // Run the program without actual game context. Routines are not handled.
    auto ctx = std::make_unique<ExecutionContext>();
    return VirtualMachine(program, std::move(ctx)).run();
}

static void parseOptions(int argc, char **argv, CmdArgs &vars) {
    using namespace boost::program_options;
    options_description description;
    description.add_options()
        ("input-file", value<std::string>()->required())
        ("verbose,v", bool_switch()->default_value(false));

    positional_options_description positional;
    positional.add("input-file", 1);

    basic_command_line_parser parser(argc, argv);
    store(parser.options(description).positional(positional).run(),
          vars, /*utf8=*/true);
}

int main(int argc, char **argv) {
    try {
        CmdArgs args;
        parseOptions(argc, argv, args);
        return run(args);
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return -1;
    }
}
