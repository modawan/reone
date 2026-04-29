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

#include "reone/resource/format/erfreader.h"
#include "reone/resource/id.h"
#include "reone/system/stream/fileinput.h"

using namespace reone;
using namespace reone::resource;

// Writer a file from an ERF container into the current directory.
static void extract(ResourceId id, IInputStream &file, size_t size) {
    std::string name = id.string();
    std::ofstream outs(name, std::ios::binary);
    while (size--) {
        outs.put(file.readByte());
    }
}

// Extract all files from an ERF container and write them to the current
// directory.
static void extractAll(IInputStream &file) {
    ErfReader erf(file);
    erf.load();

    for (size_t i = 0; i < erf.keys().size(); ++i) {
        auto &key = erf.keys()[i];
        auto &res = erf.resources()[i];

        file.seek(res.offset, SeekOrigin::Begin);
        extract(key.resId, file, res.size);
    }
}

using CmdArgs = boost::program_options::variables_map;

static int run(const CmdArgs &args) {
    auto file = FileInputStream(args["input-file"].as<std::string>());
    extractAll(file);
    return 0;
}

static void parseOptions(int argc, char **argv, CmdArgs &vars) {
    using namespace boost::program_options;
    options_description description;
    description.add_options()("input-file", value<std::string>()->required());

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
