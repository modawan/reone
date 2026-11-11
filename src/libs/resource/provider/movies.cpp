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

#include "reone/resource/provider/movies.h"

#include <algorithm>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include "reone/movie/format/bikreader.h"
#include "reone/system/fileutil.h"

using namespace reone::movie;

namespace reone {

namespace resource {

// A movie plays once, so every playback reads the file afresh.
std::shared_ptr<IMovie> Movies::get(const std::string &name) {
    auto path = findFileIgnoreCase(_gamePath, "movies/" + name + ".bik");
    if (!path) {
        return nullptr;
    }

    BikReader bik(*path, _graphicsSvc, _audioPlayer);
    bik.load();

    return bik.movie();
}

std::vector<std::string> Movies::names() {
    std::vector<std::string> result;
    auto directory = findFileIgnoreCase(_gamePath, "movies");
    if (!directory) {
        return result;
    }
    for (const auto &entry : std::filesystem::directory_iterator(*directory)) {
        if (!entry.is_regular_file() || !boost::iequals(entry.path().extension().string(), ".bik")) {
            continue;
        }
        result.push_back(boost::to_lower_copy(entry.path().stem().string()));
    }
    std::sort(result.begin(), result.end());
    return result;
}

} // namespace resource

} // namespace reone
