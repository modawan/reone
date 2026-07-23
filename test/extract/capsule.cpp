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

#include "reone/extract/capsule.h"

#include "../fixtures/archive.h"

using namespace reone;
using namespace reone::extract;
using namespace reone::resource;

static std::string str(const ByteBuffer &buf) {
    return std::string(buf.begin(), buf.end());
}

TEST(LazyCapsule, should_index_erf_and_find_resources_by_id) {
    test::TmpDir tmp("reone_test_extract_capsule_erf");
    auto path = tmp.path / "archive.erf";
    test::writeErf(path, ErfWriter::FileType::ERF,
                   {{"first", ResType::Txt, "first payload"},
                    {"second", ResType::TwoDA, "second payload"}});

    LazyCapsule capsule(path);

    EXPECT_EQ(2ll, capsule.resources().size());

    auto first = capsule.find(ResourceId("first", ResType::Txt));
    ASSERT_TRUE(first);
    EXPECT_TRUE(first->insideCapsule());
    EXPECT_EQ(path, first->filepath());
    EXPECT_EQ("first payload", str(first->readData()));

    auto second = capsule.find(ResourceId("second", ResType::TwoDA));
    ASSERT_TRUE(second);
    EXPECT_EQ("second payload", str(second->readData()));

    EXPECT_FALSE(capsule.find(ResourceId("first", ResType::TwoDA))) << "type is part of the id";
    EXPECT_FALSE(capsule.find(ResourceId("missing", ResType::Txt)));
}

TEST(LazyCapsule, should_index_rim_and_mod_files) {
    test::TmpDir tmp("reone_test_extract_capsule_rim");

    auto rimPath = tmp.path / "archive.rim";
    test::writeRim(rimPath, {{"entry", ResType::Txt, "rim payload"}});
    LazyCapsule rim(rimPath);
    auto rimRes = rim.find(ResourceId("entry", ResType::Txt));
    ASSERT_TRUE(rimRes);
    EXPECT_EQ("rim payload", str(rimRes->readData()));

    auto modPath = tmp.path / "archive.mod";
    test::writeErf(modPath, ErfWriter::FileType::MOD, {{"entry", ResType::Txt, "mod payload"}});
    LazyCapsule mod(modPath);
    auto modRes = mod.find(ResourceId("entry", ResType::Txt));
    ASSERT_TRUE(modRes);
    EXPECT_EQ("mod payload", str(modRes->readData()));
}

TEST(LazyCapsule, should_defer_loading_until_first_access) {
    test::TmpDir tmp("reone_test_extract_capsule_lazy");
    auto path = tmp.path / "archive.erf";

    // The file does not exist yet at construction time.
    LazyCapsule capsule(path);

    test::writeErf(path, ErfWriter::FileType::ERF, {{"entry", ResType::Txt, "late payload"}});

    auto res = capsule.find(ResourceId("entry", ResType::Txt));
    ASSERT_TRUE(res) << "capsule must not index the file before first access";
    EXPECT_EQ("late payload", str(res->readData()));
}

TEST(LazyCapsule, should_be_empty_for_missing_file) {
    test::TmpDir tmp("reone_test_extract_capsule_missing");

    LazyCapsule capsule(tmp.path / "missing.erf");

    EXPECT_TRUE(capsule.resources().empty());
    EXPECT_FALSE(capsule.find(ResourceId("entry", ResType::Txt)));
}

TEST(CapsuleClassification, should_classify_files_by_extension) {
    EXPECT_TRUE(isCapsuleFile("a.erf"));
    EXPECT_TRUE(isCapsuleFile("a.MOD"));
    EXPECT_TRUE(isCapsuleFile("a.rim"));
    EXPECT_TRUE(isCapsuleFile("a.sav"));
    EXPECT_TRUE(isCapsuleFile("a.hak"));
    EXPECT_FALSE(isCapsuleFile("a.txt"));

    EXPECT_TRUE(isModFile("a.mod"));
    EXPECT_FALSE(isModFile("a.erf"));

    EXPECT_TRUE(isErfFile("a.erf"));
    EXPECT_TRUE(isErfFile("a.sav"));
    EXPECT_TRUE(isErfFile("a.hak"));
    EXPECT_FALSE(isErfFile("a.rim"));
    EXPECT_FALSE(isErfFile("a.mod"));
}
