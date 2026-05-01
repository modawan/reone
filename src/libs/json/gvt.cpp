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

#include "reone/resource/parser/gff/gvt.h"
#include "reone/json/resource.h"
#include <boost/algorithm/hex.hpp>

using namespace reone::resource;

namespace reone {
namespace json {

static boost::json::object serializeGvt(const resource::GVT &gvt) {
    boost::json::object booleans;
    for (const GVT::Boolean &b : gvt.booleans) {
        booleans[b.first] = b.second;
    }

    boost::json::object numbers;
    for (const GVT::Number &num : gvt.numbers) {
        numbers[num.first] = num.second;
    }

    boost::json::object strings;
    for (const GVT::String &str : gvt.strings) {
        strings[str.first] = str.second;
    }

    boost::json::object locations;
    for (const GVT::Location &loc : gvt.locations) {
        glm::vec3 pos = loc.second.first;
        glm::vec3 rot = loc.second.second;

        boost::json::object jloc;
        jloc["position"] = {pos.x, pos.y, pos.z};
        jloc["rotation"] = {rot.x, rot.y, rot.z};
        locations[loc.first] = jloc;
    }

    return {{"booleans", booleans}, {"numbers", numbers}, {"strings", strings}, {"locations", locations}};
}

boost::json::object fromGvt(const resource::GVT &gvt) {
    boost::json::object obj;
    obj["signature"] = "GVT V3.2";
    obj["gvt"] = serializeGvt(gvt);
    return obj;
}

} // namespace json
} // namespace reone
