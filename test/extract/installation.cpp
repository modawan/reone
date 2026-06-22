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
#include "reone/system/stream/fileoutput.h"

#include "reone/graphics/types.h"

#include "../checkutil.h"
#include "../fixtures/archive.h"

using namespace reone;
using namespace reone::extract;
using namespace reone::resource;
using namespace reone::test;

TEST(FileResourceMetadata, fromPath_exposes_pykotor_style_metadata) {
    auto tmp = std::filesystem::temp_directory_path() / "reone_test_fileresource_metadata";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);
    auto path = tmp / "Sample.TXT";

    {
        FileOutputStream out(path);
        out.write("meta", 4);
        out.close();
    }

    auto res = FileResource::fromPath(path);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(ResourceId("sample", ResType::Txt), res->identifier());
    EXPECT_EQ("sample", res->resName());
    EXPECT_EQ("sample", res->resname());
    EXPECT_EQ(ResRef("sample"), res->resRef());
    EXPECT_EQ(ResRef("sample"), res->resref());
    EXPECT_EQ(ResType::Txt, res->type());
    EXPECT_EQ(ResType::Txt, res->restype());
    EXPECT_EQ("sample.txt", res->filename());
    EXPECT_EQ(path, res->filepath());
    EXPECT_EQ(path, res->source());
    EXPECT_EQ(path, res->pathIdentifier());
    EXPECT_EQ(path, res->pathIdent());
    EXPECT_EQ(0u, res->offset());
    EXPECT_EQ(4u, res->size());
    EXPECT_FALSE(res->insideCapsule());
    EXPECT_FALSE(res->insideBif());
    EXPECT_TRUE(res->exists());
    EXPECT_EQ((ByteBuffer {'m', 'e', 't', 'a'}), res->readData());
    EXPECT_EQ(&*res, &res->asFileResource());

    std::filesystem::remove_all(tmp);
}

TEST(LocationResultMetadata, guards_and_exposes_file_resource_metadata) {
    auto tmp = std::filesystem::temp_directory_path() / "reone_test_location_metadata";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);
    auto path = tmp / "probe.txt";

    {
        FileOutputStream out(path);
        out.write("p", 1);
        out.close();
    }

    auto res = FileResource::fromPath(path);
    ASSERT_TRUE(res.has_value());

    LocationResult loc(path, 0, 1);
    EXPECT_FALSE(loc.hasFileResource());
    EXPECT_THROW(loc.asFileResource(), std::runtime_error);
    EXPECT_THROW(loc.identifier(), std::runtime_error);

    loc.setFileResource(*res);
    EXPECT_TRUE(loc.hasFileResource());
    EXPECT_EQ(ResourceId("probe", ResType::Txt), loc.identifier());
    EXPECT_EQ(ResourceId("probe", ResType::Txt), loc.id());
    EXPECT_EQ(path, loc.asFileResource().filepath());
    EXPECT_THROW(loc.setFileResource(*res), std::runtime_error);

    std::filesystem::remove_all(tmp);
}

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

TEST(InstallationSearchOrder, empty_search_order_returns_no_locations) {
    auto tmp = std::filesystem::temp_directory_path() / "reone_test_installation_empty_order";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "override");

    {
        FileOutputStream out(tmp / "override" / "probe.txt");
        out.write("o", 1);
        out.close();
    }

    Installation installation(GameID::KotOR, tmp);
    SearchScope empty;
    EXPECT_TRUE(installation.locations(ResourceId("probe", ResType::Txt), empty).empty());
    EXPECT_FALSE(installation.resource(ResourceId("probe", ResType::Txt), empty).has_value());

    std::filesystem::remove_all(tmp);
}

TEST(InstallationLocations, collects_matches_across_search_order_in_priority_order) {
    auto tmp = std::filesystem::temp_directory_path() / "reone_test_installation_all_locations";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "override");
    std::filesystem::create_directories(tmp / "modules");

    {
        FileOutputStream out(tmp / "override" / "probe.txt");
        out.write("o", 1);
        out.close();
    }
    writeRim(tmp / "modules" / "danm13.rim", "probe", ResType::Txt, ByteBuffer {'m'});

    Installation installation(GameID::KotOR, tmp);
    installation.setModuleRoot("danm13");

    auto locs = installation.locations(ResourceId("probe", ResType::Txt), canonicalSearchOrder());
    ASSERT_EQ(2u, locs.size());
    EXPECT_EQ('o', locs[0].readData()[0]);
    EXPECT_EQ('m', locs[1].readData()[0]);
    EXPECT_FALSE(locs[0].asFileResource().insideCapsule());
    EXPECT_TRUE(locs[1].asFileResource().insideCapsule());
    EXPECT_EQ(tmp / "modules" / "danm13.rim" / "probe.txt",
              locs[1].asFileResource().pathIdentifier());

    auto selected = installation.resource(ResourceId("probe", ResType::Txt), canonicalSearchOrder());
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ('o', selected->readData()[0]);

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
    EXPECT_EQ("end_m01aa", Installation::getModuleRoot("end_m01aa_loc.mod"));
}

TEST(InstallationModuleRoot, loads_lips_loc_capsule_for_module) {
    auto tmp = std::filesystem::temp_directory_path() / "reone_test_installation_lips_loc";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "modules");
    std::filesystem::create_directories(tmp / "lips");

    writeRim(tmp / "modules" / "end_m01aa.rim", "area_git", ResType::Txt, ByteBuffer {'m'});
    writeErf(tmp / "lips" / "end_m01aa_loc.mod", "lipsync", ResType::Lip, ByteBuffer {'l'});

    Installation installation(GameID::KotOR, tmp);
    installation.setModuleRoot("end_m01aa");
    auto loc = installation.resource(ResourceId("lipsync", ResType::Lip), canonicalSearchOrder());
    ASSERT_TRUE(loc.has_value());
    auto data = loc->readData();
    ASSERT_EQ(1u, data.size());
    EXPECT_EQ('l', data[0]);

    std::filesystem::remove_all(tmp);
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

TEST(InstallationLookupContext, searches_extra_folders_and_capsules_without_mutating_installation_scope) {
    auto tmp = std::filesystem::temp_directory_path() / "reone_test_installation_lookup_context";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "ctx_folder");

    {
        FileOutputStream out(tmp / "ctx_folder" / "folder_probe.txt");
        out.write("f", 1);
        out.close();
    }
    writeErf(tmp / "ctx.erf", "capsule_probe", ResType::Txt, ByteBuffer {'c'});

    Installation installation(GameID::KotOR, tmp);
    ResourceLookupContext ctx;
    ctx.customFolders = {tmp / "ctx_folder"};
    ctx.customCapsules = {tmp / "ctx.erf"};

    auto folderLocs = installation.locations(
        ResourceId("folder_probe", ResType::Txt),
        SearchScope {SearchLocation::CustomFolders},
        ctx);
    ASSERT_EQ(1u, folderLocs.size());
    EXPECT_EQ('f', folderLocs[0].readData()[0]);

    auto capsuleLocs = installation.locations(
        ResourceId("capsule_probe", ResType::Txt),
        SearchScope {SearchLocation::CustomModules},
        ctx);
    ASSERT_EQ(1u, capsuleLocs.size());
    EXPECT_EQ('c', capsuleLocs[0].readData()[0]);

    EXPECT_TRUE(installation.customFolders().empty());
    EXPECT_TRUE(installation.customCapsules().empty());

    std::filesystem::remove_all(tmp);
}

TEST(InstallationSaveScope, clearSaveScope_resets_folders_to_global_baseline) {
    auto tmp = std::filesystem::temp_directory_path() / "reone_test_installation_save_scope";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "global");
    std::filesystem::create_directories(tmp / "save_a");

    writeErf(tmp / "global.erf", "shader", ResType::Txi, ByteBuffer {'g'});
    writeErf(tmp / "savegame.sav", "save_res", ResType::Gff, ByteBuffer {'s'});

    Installation installation(GameID::KotOR, tmp);
    installation.setGlobalCustomFolders({tmp / "global"});
    installation.setGlobalCustomCapsules({tmp / "global.erf"});
    installation.setCustomFolders({tmp / "global", tmp / "save_a"});
    installation.setCustomCapsules({tmp / "global.erf", tmp / "savegame.sav"});

    installation.clearSaveScope();

    ASSERT_EQ(1u, installation.customFolders().size());
    EXPECT_EQ(tmp / "global", installation.customFolders().front());
    ASSERT_EQ(1u, installation.customCapsules().size());
    EXPECT_EQ(tmp / "global.erf", installation.customCapsules().front());

    std::filesystem::remove_all(tmp);
}

TEST(InstallationSaveScope, save_load_merges_save_capsule_with_global_baseline) {
    auto tmp = std::filesystem::temp_directory_path() / "reone_test_installation_save_capsules";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "saves" / "slot_a");

    writeErf(tmp / "shaderpack.erf", "shader", ResType::Txi, ByteBuffer {'g'});
    writeErf(tmp / "saves" / "slot_a" / "savegame.sav", "save_res", ResType::Gff, ByteBuffer {'s'});

    Installation installation(GameID::KotOR, tmp);
    installation.setGlobalCustomCapsules({tmp / "shaderpack.erf"});

    installation.clearSaveScope();
    installation.appendSaveScope(tmp / "saves" / "slot_a", tmp / "saves" / "slot_a" / "savegame.sav");

    ASSERT_EQ(2u, installation.customCapsules().size());
    EXPECT_EQ(tmp / "shaderpack.erf", installation.customCapsules().front());
    EXPECT_EQ(tmp / "saves" / "slot_a" / "savegame.sav", installation.customCapsules().back());

    auto shader = installation.resource(ResourceId("shader", ResType::Txi), canonicalSearchOrder());
    ASSERT_TRUE(shader.has_value());
    EXPECT_EQ('g', shader->readData()[0]);

    auto saveRes = installation.resource(ResourceId("save_res", ResType::Gff), canonicalSearchOrder());
    ASSERT_TRUE(saveRes.has_value());
    EXPECT_EQ('s', saveRes->readData()[0]);

    std::filesystem::remove_all(tmp);
}

TEST(InstallationSaveScope, module_transition_preserves_save_scope_after_game_load) {
    auto tmp = std::filesystem::temp_directory_path() / "reone_test_installation_save_module";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "modules");
    std::filesystem::create_directories(tmp / "saves" / "slot_a");

    writeRim(tmp / "modules" / "end_m01aa.rim", "mod_res", ResType::Txt, ByteBuffer {'m'});
    writeErf(tmp / "shaderpack.erf", "shader", ResType::Txi, ByteBuffer {'g'});
    writeErf(tmp / "saves" / "slot_a" / "savegame.sav", "save_res", ResType::Gff, ByteBuffer {'s'});

    Installation installation(GameID::KotOR, tmp);
    installation.setGlobalCustomCapsules({tmp / "shaderpack.erf"});

    installation.clearSaveScope();
    installation.appendSaveScope(tmp / "saves" / "slot_a", tmp / "saves" / "slot_a" / "savegame.sav");

    installation.setModuleRoot("end_m01aa");

    ASSERT_EQ(1u, installation.customFolders().size());
    EXPECT_EQ(tmp / "saves" / "slot_a", installation.customFolders().front());
    ASSERT_EQ(2u, installation.customCapsules().size());

    auto saveRes = installation.resource(ResourceId("save_res", ResType::Gff), canonicalSearchOrder());
    ASSERT_TRUE(saveRes.has_value());
    EXPECT_EQ('s', saveRes->readData()[0]);

    auto modRes = installation.resource(ResourceId("mod_res", ResType::Txt), canonicalSearchOrder());
    ASSERT_TRUE(modRes.has_value());
    EXPECT_EQ('m', modRes->readData()[0]);

    std::filesystem::remove_all(tmp);
}

TEST(InstallationSaveScope, save_switch_replaces_prior_save_folder) {
    auto tmp = std::filesystem::temp_directory_path() / "reone_test_installation_save_switch";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "saves" / "slot_a");
    std::filesystem::create_directories(tmp / "saves" / "slot_b");

    writeErf(tmp / "saves" / "slot_a" / "savegame.sav", "save_res", ResType::Gff, ByteBuffer {'a'});
    writeErf(tmp / "saves" / "slot_b" / "savegame.sav", "save_res", ResType::Gff, ByteBuffer {'b'});

    Installation installation(GameID::KotOR, tmp);

    installation.clearSaveScope();
    installation.appendSaveScope(tmp / "saves" / "slot_a", tmp / "saves" / "slot_a" / "savegame.sav");

    installation.clearSaveScope();
    installation.appendSaveScope(tmp / "saves" / "slot_b", tmp / "saves" / "slot_b" / "savegame.sav");

    ASSERT_EQ(1u, installation.customFolders().size());
    EXPECT_EQ(tmp / "saves" / "slot_b", installation.customFolders().front());
    ASSERT_EQ(1u, installation.customCapsules().size());

    auto loc = installation.resource(ResourceId("save_res", ResType::Gff), canonicalSearchOrder());
    ASSERT_TRUE(loc.has_value());
    EXPECT_EQ('b', loc->readData()[0]);

    std::filesystem::remove_all(tmp);
}

TEST(InstallationTextureQuality, medium_prefers_tpb_over_tpa) {
    auto tmp = std::filesystem::temp_directory_path() / "reone_test_installation_tex_quality";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "texturepacks");

    writeErf(tmp / "texturepacks" / "swpc_tex_tpa.erf", "probe", ResType::Tga, ByteBuffer {'a'});
    writeErf(tmp / "texturepacks" / "swpc_tex_tpb.erf", "probe", ResType::Tga, ByteBuffer {'b'});
    writeErf(tmp / "texturepacks" / "swpc_tex_gui.erf", "gui_probe", ResType::Tga, ByteBuffer {'g'});

    Installation installation(GameID::KotOR, tmp);
    auto loc = installation.resource(
        ResourceId("probe", ResType::Tga),
        textureSearchOrder(graphics::TextureQuality::Medium));
    ASSERT_TRUE(loc.has_value());
    auto data = loc->readData();
    ASSERT_EQ(1u, data.size());
    EXPECT_EQ('b', data[0]);

    std::filesystem::remove_all(tmp);
}

TEST(InstallationSearchOrder, lips_directory_reachable_via_canonical_order) {
    auto tmp = std::filesystem::temp_directory_path() / "reone_test_installation_lips";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "lips");

    writeErf(tmp / "lips" / "global.mod", "lip_anim", ResType::Lip, ByteBuffer {'l'});

    Installation installation(GameID::KotOR, tmp);
    auto loc = installation.resource(ResourceId("lip_anim", ResType::Lip), canonicalSearchOrder());
    ASSERT_TRUE(loc.has_value());
    auto data = loc->readData();
    ASSERT_EQ(1u, data.size());
    EXPECT_EQ('l', data[0]);

    std::filesystem::remove_all(tmp);
}
