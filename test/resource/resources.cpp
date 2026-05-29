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
#include "reone/resource/format/erfwriter.h"
#include "reone/resource/resources.h"
#include "reone/system/stream/fileoutput.h"
#include "reone/system/stream/memoryoutput.h"

#include "../checkutil.h"

using namespace reone;
using namespace reone::extract;
using namespace reone::resource;

namespace {

void writeErf(const std::filesystem::path &path, const std::string &resRef, ResType type, ByteBuffer data) {
    ByteBuffer bytes;
    MemoryOutputStream stream(bytes);
    ErfWriter writer;
    writer.add(ErfWriter::Resource {resRef, type, std::move(data)});
    writer.save(ErfWriter::FileType::ERF, stream);
    FileOutputStream out(path);
    out.write(bytes.data(), bytes.size());
    out.close();
}

} // namespace

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
