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

#include "reone/extract/finder.h"
#include "reone/extract/installation.h"
#include "reone/resource/resources.h"
#include "reone/system/stream/fileoutput.h"

#include "../checkutil.h"
#include "../fixtures/archive.h"

using namespace reone;
using namespace reone::extract;
using namespace reone::resource;
using namespace reone::test;

TEST(Resources, resolves_override_via_installation) {
    auto tmp = std::filesystem::temp_directory_path() / "reone_test_resources";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "override");

    {
        FileOutputStream out(tmp / "override" / "sample.txt");
        out.write("Hello, world!", 13);
        out.close();
    }

    Installation installation(GameID::KotOR, tmp);
    Resources resources;
    resources.useInstallation(&installation);

    auto expectedResData = ByteBuffer {'H', 'e', 'l', 'l', 'o', ',', ' ', 'w', 'o', 'r', 'l', 'd', '!'};
    auto actualRes = resources.find(ResourceId("sample", ResType::Txt));
    ASSERT_TRUE(static_cast<bool>(actualRes));
    EXPECT_EQ(expectedResData, actualRes->data) << notEqualMessage(expectedResData, actualRes->data);

    Resources bare;
    EXPECT_FALSE(bare.find(ResourceId("sample", ResType::Txt)).has_value());

    std::filesystem::remove_all(tmp);
}

TEST(Resources, findResult_preserves_resource_data_and_location_metadata) {
    auto tmp = std::filesystem::temp_directory_path() / "reone_test_resource_result";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "override");
    auto path = tmp / "override" / "sample.txt";

    {
        FileOutputStream out(path);
        out.write("result", 6);
        out.close();
    }

    Installation installation(GameID::KotOR, tmp);
    Resources resources;
    resources.useInstallation(&installation);

    auto result = resources.findResult(ResourceId("sample", ResType::Txt));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(ResourceId("sample", ResType::Txt), result->id);
    EXPECT_EQ("sample", result->resName());
    EXPECT_EQ("sample", result->resname());
    EXPECT_EQ(ResRef("sample"), result->resRef());
    EXPECT_EQ(ResRef("sample"), result->resref());
    EXPECT_EQ(ResType::Txt, result->type());
    EXPECT_EQ(ResType::Txt, result->restype());
    EXPECT_EQ((ByteBuffer {'r', 'e', 's', 'u', 'l', 't'}), result->data);
    EXPECT_EQ((ByteBuffer {'r', 'e', 's', 'u', 'l', 't'}), result->resource().data);
    EXPECT_EQ(path, result->filepath());
    EXPECT_EQ(0u, result->offset());
    EXPECT_EQ(6u, result->size());
    EXPECT_EQ(ResourceId("sample", ResType::Txt), result->location.identifier());
    EXPECT_EQ(path, result->asFileResource().filepath());
    EXPECT_FALSE(result->asFileResource().insideCapsule());

    auto selected = resources.getResult(ResourceId("sample", ResType::Txt));
    EXPECT_EQ(result->data, selected.data);

    Resources bare;
    EXPECT_FALSE(bare.findResult(ResourceId("sample", ResType::Txt)).has_value());

    std::filesystem::remove_all(tmp);
}
