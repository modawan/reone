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

#include "reone/extract/fileresource.h"
#include "reone/system/stream/fileoutput.h"

#include "../fixtures/archive.h"

using namespace reone;
using namespace reone::extract;
using namespace reone::resource;

static std::string str(const ByteBuffer &buf) {
    return std::string(buf.begin(), buf.end());
}

TEST(FileResourceId, should_derive_resource_id_from_filename) {
    auto id = resourceIdFromPath("some/dir/SaMpLe.TXT");
    ASSERT_TRUE(id);
    EXPECT_EQ("sample", id->resRef.value());
    EXPECT_EQ(ResType::Txt, id->type);

    EXPECT_FALSE(resourceIdFromPath("noextension"));
    EXPECT_FALSE(resourceIdFromPath(".hidden"));
    EXPECT_FALSE(resourceIdFromPath("bad.notarestype"));
    EXPECT_FALSE(resourceIdFromPath(""));
}

TEST(FileResourceMetadata, should_describe_loose_file) {
    test::TmpDir tmp("reone_test_extract_fileres_loose");
    auto path = tmp.path / "sample.txt";
    test::detail::writeFile(path, "Hello, world!");

    auto res = FileResource::fromPath(path);

    ASSERT_TRUE(res);
    EXPECT_EQ("sample", res->resRef().value());
    EXPECT_EQ(ResType::Txt, res->type());
    EXPECT_EQ("sample.txt", res->filename());
    EXPECT_EQ(path, res->filepath());
    EXPECT_EQ(path, res->pathIdentifier());
    EXPECT_EQ(0u, res->offset());
    EXPECT_EQ(13u, res->size());
    EXPECT_FALSE(res->insideCapsule());
    EXPECT_FALSE(res->insideBif());
    EXPECT_TRUE(res->exists());
    EXPECT_EQ("Hello, world!", str(res->readData()));

    EXPECT_FALSE(FileResource::fromPath(tmp.path / "missing.txt"));
}

TEST(FileResourceMetadata, should_describe_capsule_entry_and_check_bounds) {
    test::TmpDir tmp("reone_test_extract_fileres_capsule");
    auto path = tmp.path / "archive.erf";
    test::detail::writeFile(path, "0123456789");

    FileResource inRange("entry", ResType::Txt, 4, 2, path);
    EXPECT_TRUE(inRange.insideCapsule());
    EXPECT_FALSE(inRange.insideBif());
    EXPECT_EQ(path / "entry.txt", inRange.pathIdentifier());
    EXPECT_TRUE(inRange.exists());
    EXPECT_EQ("2345", str(inRange.readData()));

    FileResource outOfRange("entry", ResType::Txt, 20, 2, path);
    EXPECT_FALSE(outOfRange.exists());

    FileResource missingFile("entry", ResType::Txt, 4, 0, tmp.path / "missing.erf");
    EXPECT_FALSE(missingFile.exists());
}

TEST(LocationResultMetadata, should_read_raw_range_and_guard_metadata) {
    test::TmpDir tmp("reone_test_extract_locresult");
    auto path = tmp.path / "sample.txt";
    test::detail::writeFile(path, "0123456789");

    LocationResult location(path, 3, 4);
    EXPECT_EQ(path, location.filepath());
    EXPECT_EQ(3u, location.offset());
    EXPECT_EQ(4u, location.size());
    EXPECT_EQ("3456", str(location.readData()));

    EXPECT_FALSE(location.hasFileResource());
    EXPECT_THROW(location.asFileResource(), std::logic_error);
    EXPECT_THROW(location.id(), std::logic_error);

    location.setFileResource(FileResource("sample", ResType::Txt, 4, 3, path));
    EXPECT_TRUE(location.hasFileResource());
    EXPECT_EQ("sample", location.id().resRef.value());
    EXPECT_EQ("3456", str(location.readData()));

    EXPECT_THROW(location.setFileResource(FileResource("other", ResType::Txt, 1, 0, path)),
                 std::logic_error);
}
