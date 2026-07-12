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

#include "reone/extract/chitin.h"

#include "../fixtures/archive.h"

using namespace reone;
using namespace reone::extract;
using namespace reone::resource;

static std::string str(const ByteBuffer &buf) {
    return std::string(buf.begin(), buf.end());
}

TEST(Chitin, should_index_key_bif_resources) {
    test::TmpDir tmp("reone_test_extract_chitin");
    test::writeKeyBif(tmp.path, "sample.bif",
                      {{"first", ResType::Txt, "first payload"},
                       {"second", ResType::TwoDA, "second payload"}});

    Chitin chitin(tmp.path / "chitin.key");

    ASSERT_EQ(2ll, chitin.resources().size());

    const auto &first = chitin.resources()[0];
    EXPECT_EQ("first", first.resRef().value());
    EXPECT_EQ(ResType::Txt, first.type());
    EXPECT_TRUE(first.insideBif());
    EXPECT_FALSE(first.insideCapsule());
    EXPECT_TRUE(first.exists());
    EXPECT_EQ("first payload", str(first.readData()));

    const auto &second = chitin.resources()[1];
    EXPECT_EQ("second payload", str(second.readData()));
}

TEST(Chitin, should_resolve_bif_paths_with_backslash_separators) {
    test::TmpDir tmp("reone_test_extract_chitin_backslash");
    test::writeKeyBif(tmp.path, "data\\sample.bif",
                      {{"entry", ResType::Txt, "nested payload"}});

    Chitin chitin(tmp.path / "chitin.key");

    ASSERT_EQ(1ll, chitin.resources().size());
    EXPECT_EQ("nested payload", str(chitin.resources()[0].readData()));
}

TEST(Chitin, should_be_empty_for_missing_key) {
    test::TmpDir tmp("reone_test_extract_chitin_missing");

    Chitin chitin(tmp.path / "chitin.key");

    EXPECT_TRUE(chitin.resources().empty());
}

TEST(Chitin, should_skip_missing_bifs) {
    test::TmpDir tmp("reone_test_extract_chitin_missing_bif");
    test::writeKeyBif(tmp.path, "sample.bif", {{"entry", ResType::Txt, "payload"}});
    std::filesystem::remove(tmp.path / "sample.bif");

    Chitin chitin(tmp.path / "chitin.key");

    EXPECT_TRUE(chitin.resources().empty());
}
