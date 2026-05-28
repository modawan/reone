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

#include "reone/resource/gff.h"
#include "reone/system/binaryreader.h"
#include "reone/system/exception/endofstream.h"
#include "reone/system/stream/memoryinput.h"
#include "reone/system/types.h"

namespace reone {
namespace resource {

static bool parseBooleans(const Gff &gff, std::vector<GVT::Boolean> &result) {
    ByteBuffer bytes = gff.getData("ValBoolean");
    auto names = gff.getList("CatBoolean");
    result.reserve(names.size());

    size_t i = 0;
    for (const std::shared_ptr<Gff> &name : names) {
        size_t byteIndex = i / 8;
        size_t bitIndex = i % 8;
        i += 1;

        if (byteIndex >= bytes.size()) {
            return false;
        }

        // Bits are packed MSB first.
        bool value = (bytes[byteIndex] >> (7 - bitIndex)) & 0x1;
        result.push_back({name->getString("Name"), value});
    }
    return true;
}

static bool parseNumbers(const Gff &gff, std::vector<GVT::Number> &result) {
    ByteBuffer bytes = gff.getData("ValNumber");
    auto names = gff.getList("CatNumber");
    result.reserve(names.size());

    size_t i = 0;
    for (const std::shared_ptr<Gff> &name : names) {
        if (i >= bytes.size()) {
            return false;
        }
        int value = bytes[i++];
        result.push_back({name->getString("Name"), value});
    }
    return true;
}

static bool parseStrings(const Gff &gff, std::vector<GVT::String> &result) {
    auto names = gff.getList("CatString");
    auto values = gff.getList("ValString");
    result.reserve(names.size());

    size_t i = 0;
    for (const std::shared_ptr<Gff> &name : names) {
        if (i >= values.size()) {
            return false;
        }
        const std::shared_ptr<Gff> &value = values[i++];
        result.push_back({name->getString("Name"), value->getString("String")});
    }
    return true;
}

static bool parseLocations(const Gff &gff, std::vector<GVT::Location> &result) {
    auto names = gff.getList("CatLocation");
    ByteBuffer values = gff.getData("ValString");
    MemoryInputStream stream(values);
    BinaryReader reader(stream);

    result.reserve(names.size());

    for (const std::shared_ptr<Gff> &name : names) {
        float fv[6];
        for (int i = 0; i < 6; ++i) {
            try {
                fv[i] = reader.readFloat();
            } catch (EndOfStreamException &e) {
                return false;
            }
        }
        glm::vec3 pos(fv[0], fv[1], fv[2]);
        glm::vec3 rot(fv[3], fv[4], fv[5]);
        result.push_back({name->getString("Name"), {pos, rot}});
    }
    return true;
}

GVT parseGVT(const Gff &gff) {
    GVT gvt;

    parseBooleans(gff, gvt.booleans);
    parseNumbers(gff, gvt.numbers);
    parseStrings(gff, gvt.strings);
    parseLocations(gff, gvt.locations);

    return gvt;
}

} // namespace resource
} // namespace reone
