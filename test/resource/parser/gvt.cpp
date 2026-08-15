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

#include <gtest/gtest.h>

#include "reone/resource/gff.h"
#include "reone/resource/parser/gff/gvt.h"
#include "reone/system/binarywriter.h"
#include "reone/system/stream/memoryoutput.h"

using namespace reone;
using namespace reone::resource;

namespace {

constexpr size_t kRetailLocationCapacity = 100;

std::shared_ptr<Gff> locationName(std::string name) {
    return Gff::Builder()
        .field(Gff::Field::newCExoString("Name", std::move(name)))
        .build();
}

ByteBuffer locationValues(
    std::initializer_list<std::array<float, 6>> values,
    size_t capacity = kRetailLocationCapacity) {

    ByteBuffer bytes;
    MemoryOutputStream stream(bytes);
    BinaryWriter writer(stream, boost::endian::order::little);
    for (const auto &value : values) {
        for (float component : value) {
            writer.writeFloat(component);
        }
    }
    bytes.resize(capacity * 6 * sizeof(float));
    return bytes;
}

std::shared_ptr<Gff> locations(
    std::vector<std::shared_ptr<Gff>> names,
    ByteBuffer values) {

    return Gff::Builder()
        .field(Gff::Field::newList("CatLocation", std::move(names)))
        .field(Gff::Field::newVoid("ValLocation", std::move(values)))
        .build();
}

} // namespace

TEST(GVT, should_decode_numeric_values_as_unsigned_retail_bytes) {
    auto gff = Gff::Builder()
        .field(Gff::Field::newList(
            "CatNumber", {locationName("zero"), locationName("maximum")}))
        .field(Gff::Field::newVoid(
            "ValNumber", {0, static_cast<char>(0xff)}))
        .build();

    auto parsed = parseGVT(*gff);

    ASSERT_EQ(parsed.numbers.size(), 2);
    EXPECT_EQ(parsed.numbers[0], GVT::Number("zero", 0));
    EXPECT_EQ(parsed.numbers[1], GVT::Number("maximum", 255));
}

TEST(GVT, should_decode_k1_saved_locations_from_the_retail_void_payload) {
    auto gff = locations(
        {locationName("first"), locationName("second")},
        locationValues({
            {108.0302048f, 84.3594818f, 0.075f, -0.5555791f, -0.8314637f, 0.0f},
            {107.5668488f, 82.5672379f, 0.075f, 0.3826801f, 0.9238809f, 0.0f}}));

    GVT parsed = parseGVT(*gff);

    ASSERT_EQ(2u, parsed.locations.size());
    EXPECT_EQ("first", parsed.locations[0].first);
    EXPECT_EQ(glm::vec3(108.0302048f, 84.3594818f, 0.075f), parsed.locations[0].second.first);
    EXPECT_EQ(glm::vec3(-0.5555791f, -0.8314637f, 0.0f), parsed.locations[0].second.second);
    EXPECT_EQ("second", parsed.locations[1].first);
    EXPECT_EQ(glm::vec3(107.5668488f, 82.5672379f, 0.075f), parsed.locations[1].second.first);
    EXPECT_EQ(glm::vec3(0.3826801f, 0.9238809f, 0.0f), parsed.locations[1].second.second);
}

TEST(GVT, should_decode_k2_saved_locations_with_three_dimensional_orientation) {
    auto gff = locations(
        {locationName("last"), locationName("temporary"), locationName("duel")},
        locationValues({
            {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
            {11.9097996f, 34.1651039f, 9.9961805f, 0.000078829f, -1.0f, 0.0f},
            {-24.5132046f, 120.9279175f, 25.5067997f, 0.9777699f, -0.2094091f, -0.0106688f}}));

    GVT parsed = parseGVT(*gff);

    ASSERT_EQ(3u, parsed.locations.size());
    EXPECT_EQ(glm::vec3(11.9097996f, 34.1651039f, 9.9961805f), parsed.locations[1].second.first);
    EXPECT_EQ(glm::vec3(0.000078829f, -1.0f, 0.0f), parsed.locations[1].second.second);
    EXPECT_EQ(glm::vec3(-24.5132046f, 120.9279175f, 25.5067997f), parsed.locations[2].second.first);
    EXPECT_EQ(glm::vec3(0.9777699f, -0.2094091f, -0.0106688f), parsed.locations[2].second.second);
}

TEST(GVT, should_leave_locations_empty_when_the_payload_is_missing_or_short) {
    auto missing = Gff::Builder()
        .field(Gff::Field::newList("CatLocation", {locationName("missing")}))
        .build();
    auto shortPayload = locations(
        {locationName("complete"), locationName("incomplete")},
        locationValues(
            {{1.0f, 2.0f, 3.0f, 0.0f, 1.0f, 0.0f}},
            1));

    EXPECT_TRUE(parseGVT(*missing).locations.empty());
    EXPECT_TRUE(parseGVT(*shortPayload).locations.empty());
}
