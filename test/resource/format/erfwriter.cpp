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

#include <gtest/gtest.h>

#include "reone/resource/format/erfreader.h"
#include "reone/resource/format/erfwriter.h"
#include "reone/system/exception/validation.h"
#include "reone/system/stream/memoryinput.h"
#include "reone/system/stream/memoryoutput.h"
#include "reone/system/stringbuilder.h"

#include "../../checkutil.h"

using namespace reone;
using namespace reone::resource;

TEST(ErfWriter, should_write_erf) {
    // given

    auto expectedOutput = StringBuilder()
                              // header
                              .append("ERF V1.0")
                              .append("\x00\x00\x00\x00", 4) // number of languages
                              .append("\x00\x00\x00\x00", 4) // size of localized strings
                              .append("\x01\x00\x00\x00", 4) // number of entries
                              .append("\xa0\x00\x00\x00", 4) // offset to localized strings
                              .append("\xa0\x00\x00\x00", 4) // offset to key list
                              .append("\xb8\x00\x00\x00", 4) // offset to resource list
                              .append("\x00\x00\x00\x00", 4) // build year
                              .append("\x00\x00\x00\x00", 4) // build day
                              .append("\xff\xff\xff\xff", 4) // description strref
                              .append('\x00', 116)           // reserved
                              // key list
                              .append("aa\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 16) // canonical resref
                              .append("\x00\x00\x00\x00", 4)                                            // resid
                              .append("\xe6\x07", 2)                                                    // restype
                              .append("\x00\x00", 2)                                                    // unused
                              // resource list
                              .append("\xc0\x00\x00\x00", 4) // offset to resource
                              .append("\x02\x00\x00\x00", 4) // resource size
                              // resource data
                              .append("Bb")
                              .string();

    auto bytes = ByteBuffer();
    auto stream = MemoryOutputStream(bytes);

    auto writer = ErfWriter();
    writer.add(ErfWriter::Resource {"Aa", ResType::Txi, ByteBuffer {'B', 'b'}});

    // when

    writer.save(ErfWriter::FileType::ERF, stream);

    // then

    auto actualOutput = std::string(&bytes[0], bytes.size());
    EXPECT_EQ(expectedOutput, actualOutput) << notEqualMessage(expectedOutput, actualOutput);
}

namespace {

ByteBuffer bytes(std::string_view value) {
    return ByteBuffer(value.begin(), value.end());
}

ByteBuffer archiveBytes(
    ErfWriter::FileType type,
    std::vector<ErfWriter::Resource> resources) {
    ErfWriter writer;
    for (auto &resource : resources) {
        writer.add(std::move(resource));
    }
    return writer.toBytes(type);
}

struct ReadArchive {
    std::string signature;
    std::vector<ResourceId> orderedIds;
    std::map<ResourceId, ByteBuffer> members;
};

ReadArchive readArchive(ByteBuffer archive) {
    MemoryInputStream stream(archive);
    ErfReader reader(stream);
    reader.load();

    ReadArchive result;
    result.signature = reader.signature();
    for (size_t i = 0; i < reader.keys().size(); ++i) {
        const auto &id = reader.keys()[i].resId;
        const auto &entry = reader.resources()[i];
        if (static_cast<uint64_t>(entry.offset) + entry.size > archive.size()) {
            throw std::logic_error("writer produced an out-of-bounds ERF member");
        }
        result.orderedIds.push_back(id);
        result.members.emplace(
            id,
            ByteBuffer(
                archive.begin() + entry.offset,
                archive.begin() + entry.offset + entry.size));
    }
    return result;
}

} // namespace

TEST(ErfWriter, round_trips_nested_module_shape_as_mod_v1) {
    auto archive = archiveBytes(ErfWriter::FileType::MOD, {
        {ResourceId("module", ResType::Ifo), bytes("ifo")},
        {ResourceId("area", ResType::Are), bytes("are")},
        {ResourceId("area", ResType::Git), bytes("git")}});

    auto decoded = readArchive(archive);

    EXPECT_EQ("MOD V1.0", decoded.signature);
    EXPECT_EQ(bytes("ifo"), decoded.members.at(ResourceId("module", ResType::Ifo)));
    EXPECT_EQ(bytes("are"), decoded.members.at(ResourceId("area", ResType::Are)));
    EXPECT_EQ(bytes("git"), decoded.members.at(ResourceId("area", ResType::Git)));
    EXPECT_TRUE(std::is_sorted(decoded.orderedIds.begin(), decoded.orderedIds.end()));
}

TEST(ErfWriter, round_trips_outer_like_heterogeneous_members_as_mod_v1) {
    auto archive = archiveBytes(ErfWriter::FileType::MOD, {
        {ResourceId("module", ResType::Sav), bytes("nested module")},
        {ResourceId("inventory", ResType::Res), bytes("inventory")},
        {ResourceId("repute", ResType::Fac), bytes("factions")},
        {ResourceId("pc", ResType::Utc), bytes("player")}});

    auto decoded = readArchive(archive);

    EXPECT_EQ("MOD V1.0", decoded.signature);
    EXPECT_EQ(bytes("nested module"), decoded.members.at(ResourceId("module", ResType::Sav)));
    EXPECT_EQ(bytes("inventory"), decoded.members.at(ResourceId("inventory", ResType::Res)));
    EXPECT_EQ(bytes("factions"), decoded.members.at(ResourceId("repute", ResType::Fac)));
    EXPECT_EQ(bytes("player"), decoded.members.at(ResourceId("pc", ResType::Utc)));
}

TEST(ErfWriter, rejects_duplicate_canonical_resource_identity_before_output) {
    ErfWriter writer;
    writer.add({"Same", ResType::Res, bytes("first")});
    writer.add({"same", ResType::Res, bytes("second")});

    ByteBuffer output {'u', 'n', 't', 'o', 'u', 'c', 'h', 'e', 'd'};
    MemoryOutputStream stream(output);

    EXPECT_THROW(writer.save(ErfWriter::FileType::MOD, stream), ValidationException);
    EXPECT_EQ(bytes("untouched"), output);
}

TEST(ErfWriter, enforces_exact_erf_resref_capacity_without_truncation) {
    std::string maxName(16, 'x');
    auto archive = archiveBytes(ErfWriter::FileType::MOD, {
        {maxName, ResType::Res, bytes("maximum")}});
    auto decoded = readArchive(archive);
    EXPECT_EQ(bytes("maximum"), decoded.members.at(ResourceId(maxName, ResType::Res)));

    ErfWriter overlong;
    overlong.add({std::string(17, 'x'), ResType::Res, bytes("invalid")});
    EXPECT_THROW(overlong.toBytes(ErfWriter::FileType::MOD), ValidationException);

    ErfWriter wouldCollideAfterTruncation;
    wouldCollideAfterTruncation.add({std::string(16, 'x') + "a", ResType::Res, bytes("a")});
    wouldCollideAfterTruncation.add({std::string(16, 'x') + "b", ResType::Res, bytes("b")});
    EXPECT_THROW(wouldCollideAfterTruncation.toBytes(ErfWriter::FileType::MOD), ValidationException);
}

TEST(ErfWriter, output_is_deterministic_across_insertion_order) {
    ErfWriter first;
    first.add({"ZETA", ResType::Utc, bytes("z")});
    first.add({"Alpha", ResType::Res, bytes("a")});
    first.add({"ALPHA", ResType::Are, bytes("area")});

    ErfWriter second;
    second.add({"alpha", ResType::Are, bytes("area")});
    second.add({"alpha", ResType::Res, bytes("a")});
    second.add({"zeta", ResType::Utc, bytes("z")});

    EXPECT_EQ(first.toBytes(ErfWriter::FileType::MOD),
              second.toBytes(ErfWriter::FileType::MOD));
}

TEST(ErfWriter, packages_owned_and_lazy_payloads_once_into_independent_bytes) {
    ByteBuffer owned = bytes("owned original");
    int lazyReads = 0;
    ErfWriter writer;
    writer.add({ResourceId("owned", ResType::Res), owned});
    writer.add(ErfWriter::Resource::lazy(
        ResourceId("borrowed", ResType::Res),
        [&lazyReads]() {
            ++lazyReads;
            return bytes("lazy payload");
        }));
    owned.assign({'m', 'u', 't', 'a', 't', 'e', 'd'});

    ByteBuffer archive = writer.toBytes(ErfWriter::FileType::MOD);
    EXPECT_EQ(1, lazyReads);

    writer = ErfWriter();
    auto decoded = readArchive(archive);
    EXPECT_EQ(bytes("owned original"), decoded.members.at(ResourceId("owned", ResType::Res)));
    EXPECT_EQ(bytes("lazy payload"), decoded.members.at(ResourceId("borrowed", ResType::Res)));
}

TEST(ErfWriter, validates_invalid_entries_and_archive_types) {
    ErfWriter emptyName;
    emptyName.add({"", ResType::Res, {}});
    EXPECT_THROW(emptyName.toBytes(ErfWriter::FileType::MOD), ValidationException);

    ErfWriter invalidType;
    invalidType.add({"entry", ResType::Invalid, {}});
    EXPECT_THROW(invalidType.toBytes(ErfWriter::FileType::MOD), ValidationException);

    ErfWriter writer;
    EXPECT_THROW(writer.toBytes(static_cast<ErfWriter::FileType>(999)), ValidationException);
}
