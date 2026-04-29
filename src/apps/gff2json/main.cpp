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

#include "reone/json/resource.h"
#include "reone/json/util.h"
#include "reone/resource/format/gffreader.h"
#include "reone/system/stream/fileinput.h"

using namespace reone;
using namespace reone::resource;

using CmdArgs = boost::program_options::variables_map;

static int run(const CmdArgs &args) {
    auto file = FileInputStream(args["input-file"].as<std::string>());
    auto reader = GffReader(file);
    reader.load();
    std::shared_ptr<Gff> gff = reader.root();

    boost::json::object json = json::fromGff(*gff);
    json::print(std::cout, json);
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
