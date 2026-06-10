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
#include "reone/resource/format/rimwriter.h"
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

void writeRim(const std::filesystem::path &path, const std::string &resRef, ResType type, ByteBuffer data) {
    ByteBuffer bytes;
    MemoryOutputStream stream(bytes);
    RimWriter writer;
    writer.add(RimWriter::Resource {resRef, type, std::move(data)});
    writer.save(stream);
    FileOutputStream out(path);
    out.write(bytes.data(), bytes.size());
    out.close();
}

} // namespace

TEST(InstallationSearchOrder, override_beats_modules_and_chitin) {
    auto tmp = std::filesystem::temp_directory_path() / "reone_test_installation_order";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "override");
    std::filesystem::create_directories(tmp / "modules");

    writeRim(tmp / "modules" / "testmod.rim", "probe", ResType::Txt, ByteBuffer {'m'});
    {
        FileOutputStream out(tmp / "override" / "probe.txt");
        out.write("o", 1);
        out.close();
    }

    Installation installation(GameID::KotOR, tmp);
    auto loc = installation.resource(ResourceId("probe", ResType::Txt), canonicalSearchOrder());
    ASSERT_TRUE(loc.has_value());
    auto data = loc->readData();
    ASSERT_EQ(1u, data.size());
    EXPECT_EQ('o', data[0]);

    std::filesystem::remove_all(tmp);
}

TEST(InstallationModuleRoot, filters_module_capsules_by_root) {
    auto tmp = std::filesystem::temp_directory_path() / "reone_test_installation_module_root";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "modules");

    writeRim(tmp / "modules" / "danm13.rim", "probe", ResType::Txt, ByteBuffer {'a'});
    writeRim(tmp / "modules" / "tar_m02.rim", "probe", ResType::Txt, ByteBuffer {'b'});

    Installation installation(GameID::KotOR, tmp);
    installation.setModuleRoot("danm13");
    auto loc = installation.resource(ResourceId("probe", ResType::Txt), canonicalSearchOrder());
    ASSERT_TRUE(loc.has_value());
    auto data = loc->readData();
    ASSERT_EQ(1u, data.size());
    EXPECT_EQ('a', data[0]);

    std::filesystem::remove_all(tmp);
}

TEST(InstallationModuleRoot, getModuleRoot_strips_suffixes) {
    EXPECT_EQ("danm13", Installation::getModuleRoot("danm13_s.rim"));
    EXPECT_EQ("danm13", Installation::getModuleRoot("danm13_dlg.erf"));
    EXPECT_EQ("end_m01aa", Installation::getModuleRoot("end_m01aa.mod"));
}

TEST(InstallationModuleRoot, skips_module_index_until_module_root_set) {
    auto tmp = std::filesystem::temp_directory_path() / "reone_test_installation_module_lazy";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "modules");

    writeRim(tmp / "modules" / "danm13.rim", "probe", ResType::Txt, ByteBuffer {'a'});
    writeRim(tmp / "modules" / "tar_m02.rim", "probe", ResType::Txt, ByteBuffer {'b'});

    Installation installation(GameID::KotOR, tmp);
    auto loc = installation.resource(ResourceId("probe", ResType::Txt), canonicalSearchOrder());
    EXPECT_FALSE(loc.has_value());

    installation.setModuleRoot("danm13");
    loc = installation.resource(ResourceId("probe", ResType::Txt), canonicalSearchOrder());
    ASSERT_TRUE(loc.has_value());
    auto data = loc->readData();
    ASSERT_EQ(1u, data.size());
    EXPECT_EQ('a', data[0]);

    std::filesystem::remove_all(tmp);
}

TEST(InstallationSearchOrder, soundSearchOrder_finds_streammusic) {
    auto tmp = std::filesystem::temp_directory_path() / "reone_test_installation_streammusic";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "streammusic");

    {
        FileOutputStream out(tmp / "streammusic" / "mus_theme_cult.wav");
        out.write("wav", 3);
        out.close();
    }

    Installation installation(GameID::KotOR, tmp);
    auto loc = installation.resource(ResourceId("mus_theme_cult", ResType::Wav), soundSearchOrder());
    ASSERT_TRUE(loc.has_value());
    auto data = loc->readData();
    ASSERT_EQ(3u, data.size());
    EXPECT_EQ('w', data[0]);

    std::filesystem::remove_all(tmp);
}

TEST(InstallationResolveLooseRelativePath, finds_root_dialog_tlk) {
    auto tmp = std::filesystem::temp_directory_path() / "reone_test_installation_root_files";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "override");

    {
        FileOutputStream out(tmp / "dialog.tlk");
        out.write("root", 4);
        out.close();
    }
    {
        FileOutputStream out(tmp / "override" / "dialog.tlk");
        out.write("override", 8);
        out.close();
    }

    Installation installation(GameID::KotOR, tmp);
    auto path = installation.resolveLooseRelativePath("dialog.tlk", talkTableSearchOrder());

    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(std::filesystem::weakly_canonical(tmp / "dialog.tlk"),
              std::filesystem::weakly_canonical(*path));

    std::filesystem::remove_all(tmp);
}
