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

#include "reone/extract/installation.h"
#include "reone/extract/finder.h"
#include "reone/resource/director.h"
#include "reone/system/exception/validation.h"

#include "../fixtures/archive.h"
#include "../fixtures/resource.h"

using namespace reone;
using namespace reone::extract;
using namespace reone::resource;
using namespace reone::test;

TEST(ResourceDirector, onGameLoad_appends_save_scope_and_retains_global_capsules) {
    auto tmp = std::filesystem::temp_directory_path() / "reone_test_director_save_load";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "saves" / "000001");

    writeErf(tmp / "shaderpack.erf", "shader", ResType::Txi, ByteBuffer {'g'});
    writeErf(tmp / "saves" / "000001" / "savegame.sav", "save_res", ResType::Gff, ByteBuffer {'s'});

    Installation installation(GameID::KotOR, tmp);
    installation.setGlobalCustomCapsules({tmp / "shaderpack.erf"});

    MockDialogs dialogs;
    MockGffs gffs;
    MockLips lips;
    MockPaths paths;
    MockScripts scripts;

    ResourceDirector director(tmp, installation, dialogs, gffs, lips, paths, scripts);
    director.onGameLoad("000001");

    ASSERT_EQ(1u, installation.customFolders().size());
    EXPECT_EQ(tmp / "saves" / "000001", installation.customFolders().front());
    ASSERT_EQ(2u, installation.customCapsules().size());
    EXPECT_EQ(tmp / "shaderpack.erf", installation.customCapsules().front());
    EXPECT_EQ(tmp / "saves" / "000001" / "savegame.sav", installation.customCapsules().back());

    auto shader = installation.resource(ResourceId("shader", ResType::Txi), canonicalSearchOrder());
    ASSERT_TRUE(shader.has_value());
    EXPECT_EQ('g', shader->readData()[0]);

    auto saveRes = installation.resource(ResourceId("save_res", ResType::Gff), canonicalSearchOrder());
    ASSERT_TRUE(saveRes.has_value());
    EXPECT_EQ('s', saveRes->readData()[0]);

    std::filesystem::remove_all(tmp);
}

TEST(ResourceDirector, onGameLoad_rejects_invalid_save_slot_name) {
    auto tmp = std::filesystem::temp_directory_path() / "reone_test_director_save_invalid";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "saves");

    Installation installation(GameID::KotOR, tmp);

    MockDialogs dialogs;
    MockGffs gffs;
    MockLips lips;
    MockPaths paths;
    MockScripts scripts;

    ResourceDirector director(tmp, installation, dialogs, gffs, lips, paths, scripts);
    EXPECT_THROW(director.onGameLoad("../evil"), ValidationException);

    std::filesystem::remove_all(tmp);
}

TEST(ResourceDirector, onModuleLoad_rejects_invalid_module_name) {
    auto tmp = std::filesystem::temp_directory_path() / "reone_test_director_module_invalid";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);

    Installation installation(GameID::KotOR, tmp);

    MockDialogs dialogs;
    MockGffs gffs;
    MockLips lips;
    MockPaths paths;
    MockScripts scripts;

    ResourceDirector director(tmp, installation, dialogs, gffs, lips, paths, scripts);
    EXPECT_THROW(director.onModuleLoad("../evil"), ValidationException);

    std::filesystem::remove_all(tmp);
}
