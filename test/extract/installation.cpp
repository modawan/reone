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

#include "reone/extract/installation.h"
#include "reone/graphics/types.h"

#include "../fixtures/archive.h"

using namespace reone;
using namespace reone::extract;
using namespace reone::resource;
using namespace reone::test;

using ErfType = ErfWriter::FileType;

static std::string str(const ByteBuffer &buf) {
    return std::string(buf.begin(), buf.end());
}

TEST(InstallationSearchOrder, override_beats_modules_and_chitin) {
    TmpDir tmp("reone_test_installation_order");
    std::filesystem::create_directories(tmp.path / "modules");
    std::filesystem::create_directories(tmp.path / "override");

    writeKeyBif(tmp.path, "data\\sample.bif", {{"probe", ResType::Txt, "chitin"}});
    writeRim(tmp.path / "modules" / "modfoo.rim", {{"probe", ResType::Txt, "module"}});
    detail::writeFile(tmp.path / "override" / "probe.txt", "override");

    Installation installation(GameID::KotOR, tmp.path);
    installation.setModuleRoot("modfoo");

    auto loc = installation.resource(ResourceId("probe", ResType::Txt), canonicalSearchOrder());

    ASSERT_TRUE(loc.has_value());
    EXPECT_EQ("override", str(loc->readData()));
}

TEST(InstallationSearchOrder, empty_search_order_returns_no_locations) {
    TmpDir tmp("reone_test_installation_empty_order");
    std::filesystem::create_directories(tmp.path / "override");
    detail::writeFile(tmp.path / "override" / "probe.txt", "override");

    Installation installation(GameID::KotOR, tmp.path);
    SearchScope empty;

    EXPECT_TRUE(installation.locations(ResourceId("probe", ResType::Txt), empty).empty());
    EXPECT_FALSE(installation.resource(ResourceId("probe", ResType::Txt), empty).has_value());
}

TEST(InstallationSearchOrder, chitin_lookup_falls_back_to_patch_capsule) {
    TmpDir tmp("reone_test_installation_patch");

    writeKeyBif(tmp.path, "sample.bif", {{"in_chitin", ResType::Txt, "chitin"}});
    writeErf(tmp.path / "patch.erf", ErfType::ERF,
             {{"in_chitin", ResType::Txt, "patch shadowed"},
              {"in_patch", ResType::Txt, "patch"}});

    Installation installation(GameID::KotOR, tmp.path);

    auto chitinLoc = installation.resource(ResourceId("in_chitin", ResType::Txt), canonicalSearchOrder());
    ASSERT_TRUE(chitinLoc.has_value());
    EXPECT_EQ("chitin", str(chitinLoc->readData())) << "chitin entry must shadow patch.erf";

    auto patchLoc = installation.resource(ResourceId("in_patch", ResType::Txt), canonicalSearchOrder());
    ASSERT_TRUE(patchLoc.has_value());
    EXPECT_EQ("patch", str(patchLoc->readData())) << "patch.erf must serve entries missing from chitin";
}

TEST(InstallationLocations, collects_matches_across_search_order_in_priority_order) {
    TmpDir tmp("reone_test_installation_all_locations");
    std::filesystem::create_directories(tmp.path / "modules");
    std::filesystem::create_directories(tmp.path / "override");

    detail::writeFile(tmp.path / "override" / "probe.txt", "o");
    writeRim(tmp.path / "modules" / "modfoo.rim", {{"probe", ResType::Txt, "m"}});

    Installation installation(GameID::KotOR, tmp.path);
    installation.setModuleRoot("modfoo");

    auto locs = installation.locations(ResourceId("probe", ResType::Txt), canonicalSearchOrder());

    ASSERT_EQ(2u, locs.size());
    EXPECT_EQ("o", str(locs[0].readData()));
    EXPECT_EQ("m", str(locs[1].readData()));
    EXPECT_FALSE(locs[0].asFileResource().insideCapsule());
    EXPECT_TRUE(locs[1].asFileResource().insideCapsule());
    EXPECT_EQ(tmp.path / "modules" / "modfoo.rim" / "probe.txt",
              locs[1].asFileResource().pathIdentifier());

    auto selected = installation.resource(ResourceId("probe", ResType::Txt), canonicalSearchOrder());
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ("o", str(selected->readData()));
}

TEST(InstallationModuleRoot, filters_module_capsules_by_root) {
    TmpDir tmp("reone_test_installation_module_root");
    std::filesystem::create_directories(tmp.path / "modules");

    writeRim(tmp.path / "modules" / "modfoo.rim", {{"probe", ResType::Txt, "foo"}});
    writeRim(tmp.path / "modules" / "modbar.rim", {{"probe", ResType::Txt, "bar"}});

    Installation installation(GameID::KotOR, tmp.path);
    installation.setModuleRoot("modfoo");

    auto loc = installation.resource(ResourceId("probe", ResType::Txt), canonicalSearchOrder());

    ASSERT_TRUE(loc.has_value());
    EXPECT_EQ("foo", str(loc->readData()));
}

TEST(InstallationModuleRoot, prioritizes_mod_then_static_rim_then_rim) {
    TmpDir tmp("reone_test_installation_module_archive_order");
    std::filesystem::create_directories(tmp.path / "modules");

    writeRim(tmp.path / "modules" / "modfoo.rim", {{"probe", ResType::Txt, "rim"}});
    writeRim(tmp.path / "modules" / "modfoo_s.rim", {{"probe", ResType::Txt, "static rim"}});
    writeErf(tmp.path / "modules" / "modfoo.mod", ErfType::MOD, {{"probe", ResType::Txt, "mod"}});

    Installation installation(GameID::KotOR, tmp.path);
    installation.setModuleRoot("modfoo");

    auto locations = installation.locations(ResourceId("probe", ResType::Txt), SearchScope {SearchLocation::Modules});

    ASSERT_EQ(3u, locations.size());
    EXPECT_EQ("mod", str(locations[0].readData()));
    EXPECT_EQ("static rim", str(locations[1].readData()));
    EXPECT_EQ("rim", str(locations[2].readData()));
}

TEST(InstallationModuleRoot, getModuleRoot_strips_suffixes) {
    EXPECT_EQ("foo", Installation::getModuleRoot("foo_s.rim"));
    EXPECT_EQ("foo", Installation::getModuleRoot("foo_dlg.erf"));
    EXPECT_EQ("modbar", Installation::getModuleRoot("modbar.mod"));
    EXPECT_EQ("modbar", Installation::getModuleRoot("modbar_loc.mod"));
    EXPECT_EQ("modbar", Installation::getModuleRoot("MODBAR.RIM"));
}

TEST(InstallationModuleRoot, loads_lips_loc_capsule_for_module) {
    TmpDir tmp("reone_test_installation_lips_loc");
    std::filesystem::create_directories(tmp.path / "modules");
    std::filesystem::create_directories(tmp.path / "lips");

    writeRim(tmp.path / "modules" / "modfoo.rim", {{"area_res", ResType::Txt, "m"}});
    writeErf(tmp.path / "lips" / "modfoo_loc.mod", ErfType::MOD, {{"lipsync", ResType::Lip, "l"}});

    Installation installation(GameID::KotOR, tmp.path);
    installation.setModuleRoot("modfoo");

    auto loc = installation.resource(ResourceId("lipsync", ResType::Lip), canonicalSearchOrder());

    ASSERT_TRUE(loc.has_value());
    EXPECT_EQ("l", str(loc->readData()));
}

TEST(InstallationModuleRoot, skips_module_index_until_module_root_set) {
    TmpDir tmp("reone_test_installation_module_lazy");
    std::filesystem::create_directories(tmp.path / "modules");

    writeRim(tmp.path / "modules" / "modfoo.rim", {{"probe", ResType::Txt, "foo"}});
    writeRim(tmp.path / "modules" / "modbar.rim", {{"probe", ResType::Txt, "bar"}});

    Installation installation(GameID::KotOR, tmp.path);

    EXPECT_FALSE(installation.resource(ResourceId("probe", ResType::Txt), canonicalSearchOrder()).has_value());

    installation.setModuleRoot("modfoo");
    auto loc = installation.resource(ResourceId("probe", ResType::Txt), canonicalSearchOrder());

    ASSERT_TRUE(loc.has_value());
    EXPECT_EQ("foo", str(loc->readData()));
}

TEST(InstallationSearchOrder, soundSearchOrder_finds_streammusic) {
    TmpDir tmp("reone_test_installation_streammusic");
    std::filesystem::create_directories(tmp.path / "streammusic");
    detail::writeFile(tmp.path / "streammusic" / "theme.wav", "wav");

    Installation installation(GameID::KotOR, tmp.path);

    auto loc = installation.resource(ResourceId("theme", ResType::Wav), soundSearchOrder());

    ASSERT_TRUE(loc.has_value());
    EXPECT_EQ("wav", str(loc->readData()));
}

TEST(InstallationResolveLooseRelativePath, finds_root_dialog_tlk) {
    TmpDir tmp("reone_test_installation_root_files");
    std::filesystem::create_directories(tmp.path / "override");
    detail::writeFile(tmp.path / "dialog.tlk", "root");
    detail::writeFile(tmp.path / "override" / "dialog.tlk", "override");

    Installation installation(GameID::KotOR, tmp.path);

    auto path = installation.resolveLooseRelativePath("dialog.tlk", talkTableSearchOrder());

    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(std::filesystem::weakly_canonical(tmp.path / "dialog.tlk"),
              std::filesystem::weakly_canonical(*path));
}

TEST(InstallationLookupContext, searches_extra_locations_without_mutating_installation_scope) {
    TmpDir tmp("reone_test_installation_lookup_context");
    std::filesystem::create_directories(tmp.path / "ctx_folder");
    detail::writeFile(tmp.path / "ctx_folder" / "folder_probe.txt", "f");
    writeErf(tmp.path / "ctx.erf", ErfType::ERF, {{"capsule_probe", ResType::Txt, "c"}});

    Installation installation(GameID::KotOR, tmp.path);
    ResourceLookupContext ctx;
    ctx.customFolders = {tmp.path / "ctx_folder"};
    ctx.customCapsules = {tmp.path / "ctx.erf"};

    auto folderLocs = installation.locations(
        ResourceId("folder_probe", ResType::Txt),
        SearchScope {SearchLocation::CustomFolders},
        ctx);
    ASSERT_EQ(1u, folderLocs.size());
    EXPECT_EQ("f", str(folderLocs[0].readData()));

    auto capsuleLocs = installation.locations(
        ResourceId("capsule_probe", ResType::Txt),
        SearchScope {SearchLocation::CustomModules},
        ctx);
    ASSERT_EQ(1u, capsuleLocs.size());
    EXPECT_EQ("c", str(capsuleLocs[0].readData()));

    EXPECT_TRUE(installation.customFolders().empty());
    EXPECT_TRUE(installation.customCapsules().empty());
}

TEST(InstallationLocations, batch_lookup_returns_locations_for_each_resource_id) {
    TmpDir tmp("reone_test_installation_batch");
    std::filesystem::create_directories(tmp.path / "override");
    detail::writeFile(tmp.path / "override" / "one.txt", "1");
    detail::writeFile(tmp.path / "override" / "two.txt", "2");

    Installation installation(GameID::KotOR, tmp.path);

    auto batch = installation.locations(
        std::vector<ResourceId> {
            ResourceId("one", ResType::Txt),
            ResourceId("two", ResType::Txt),
            ResourceId("missing", ResType::Txt),
        },
        canonicalSearchOrder());

    ASSERT_EQ(3u, batch.size());
    ASSERT_EQ(1u, batch.at(ResourceId("one", ResType::Txt)).size());
    ASSERT_EQ(1u, batch.at(ResourceId("two", ResType::Txt)).size());
    EXPECT_TRUE(batch.at(ResourceId("missing", ResType::Txt)).empty());
    EXPECT_EQ("1", str(batch.at(ResourceId("one", ResType::Txt)).front().readData()));
    EXPECT_EQ("2", str(batch.at(ResourceId("two", ResType::Txt)).front().readData()));
}

TEST(InstallationSaveScope, clearSaveScope_resets_scope_to_global_baseline) {
    TmpDir tmp("reone_test_installation_save_scope");
    std::filesystem::create_directories(tmp.path / "global");
    std::filesystem::create_directories(tmp.path / "save_a");

    writeErf(tmp.path / "global.erf", ErfType::ERF, {{"shader", ResType::Txi, "g"}});
    writeErf(tmp.path / "savegame.sav", ErfType::ERF, {{"save_res", ResType::Gff, "s"}});

    Installation installation(GameID::KotOR, tmp.path);
    installation.setGlobalCustomFolders({tmp.path / "global"});
    installation.setGlobalCustomCapsules({tmp.path / "global.erf"});
    installation.setCustomFolders({tmp.path / "global", tmp.path / "save_a"});
    installation.setCustomCapsules({tmp.path / "global.erf", tmp.path / "savegame.sav"});

    installation.clearSaveScope();

    ASSERT_EQ(1u, installation.customFolders().size());
    EXPECT_EQ(tmp.path / "global", installation.customFolders().front());
    ASSERT_EQ(1u, installation.customCapsules().size());
    EXPECT_EQ(tmp.path / "global.erf", installation.customCapsules().front());
}

TEST(InstallationSaveScope, save_load_merges_save_capsule_with_global_baseline) {
    TmpDir tmp("reone_test_installation_save_capsules");
    std::filesystem::create_directories(tmp.path / "saves" / "slot_a");

    writeErf(tmp.path / "shaderpack.erf", ErfType::ERF, {{"shader", ResType::Txi, "g"}});
    writeErf(tmp.path / "saves" / "slot_a" / "savegame.sav", ErfType::ERF,
             {{"save_res", ResType::Gff, "s"}});

    Installation installation(GameID::KotOR, tmp.path);
    installation.setGlobalCustomCapsules({tmp.path / "shaderpack.erf"});

    installation.clearSaveScope();
    installation.appendSaveScope(tmp.path / "saves" / "slot_a",
                                 tmp.path / "saves" / "slot_a" / "savegame.sav");

    ASSERT_EQ(2u, installation.customCapsules().size());
    EXPECT_EQ(tmp.path / "shaderpack.erf", installation.customCapsules().front());
    EXPECT_EQ(tmp.path / "saves" / "slot_a" / "savegame.sav", installation.customCapsules().back());

    auto shader = installation.resource(ResourceId("shader", ResType::Txi), canonicalSearchOrder());
    ASSERT_TRUE(shader.has_value());
    EXPECT_EQ("g", str(shader->readData()));

    auto saveRes = installation.resource(ResourceId("save_res", ResType::Gff), canonicalSearchOrder());
    ASSERT_TRUE(saveRes.has_value());
    EXPECT_EQ("s", str(saveRes->readData()));
}

TEST(InstallationSaveScope, module_transition_preserves_save_scope_after_game_load) {
    TmpDir tmp("reone_test_installation_save_module");
    std::filesystem::create_directories(tmp.path / "modules");
    std::filesystem::create_directories(tmp.path / "saves" / "slot_a");

    writeRim(tmp.path / "modules" / "modfoo.rim", {{"mod_res", ResType::Txt, "m"}});
    writeErf(tmp.path / "shaderpack.erf", ErfType::ERF, {{"shader", ResType::Txi, "g"}});
    writeErf(tmp.path / "saves" / "slot_a" / "savegame.sav", ErfType::ERF,
             {{"save_res", ResType::Gff, "s"}});

    Installation installation(GameID::KotOR, tmp.path);
    installation.setGlobalCustomCapsules({tmp.path / "shaderpack.erf"});

    installation.clearSaveScope();
    installation.appendSaveScope(tmp.path / "saves" / "slot_a",
                                 tmp.path / "saves" / "slot_a" / "savegame.sav");

    installation.setModuleRoot("modfoo");

    ASSERT_EQ(1u, installation.customFolders().size());
    EXPECT_EQ(tmp.path / "saves" / "slot_a", installation.customFolders().front());
    ASSERT_EQ(2u, installation.customCapsules().size());

    auto saveRes = installation.resource(ResourceId("save_res", ResType::Gff), canonicalSearchOrder());
    ASSERT_TRUE(saveRes.has_value());
    EXPECT_EQ("s", str(saveRes->readData()));

    auto modRes = installation.resource(ResourceId("mod_res", ResType::Txt), canonicalSearchOrder());
    ASSERT_TRUE(modRes.has_value());
    EXPECT_EQ("m", str(modRes->readData()));
}

TEST(InstallationSaveScope, save_switch_replaces_prior_save_folder) {
    TmpDir tmp("reone_test_installation_save_switch");
    std::filesystem::create_directories(tmp.path / "saves" / "slot_a");
    std::filesystem::create_directories(tmp.path / "saves" / "slot_b");

    writeErf(tmp.path / "saves" / "slot_a" / "savegame.sav", ErfType::ERF,
             {{"save_res", ResType::Gff, "a"}});
    writeErf(tmp.path / "saves" / "slot_b" / "savegame.sav", ErfType::ERF,
             {{"save_res", ResType::Gff, "b"}});

    Installation installation(GameID::KotOR, tmp.path);

    installation.clearSaveScope();
    installation.appendSaveScope(tmp.path / "saves" / "slot_a",
                                 tmp.path / "saves" / "slot_a" / "savegame.sav");

    installation.clearSaveScope();
    installation.appendSaveScope(tmp.path / "saves" / "slot_b",
                                 tmp.path / "saves" / "slot_b" / "savegame.sav");

    ASSERT_EQ(1u, installation.customFolders().size());
    EXPECT_EQ(tmp.path / "saves" / "slot_b", installation.customFolders().front());
    ASSERT_EQ(1u, installation.customCapsules().size());

    auto loc = installation.resource(ResourceId("save_res", ResType::Gff), canonicalSearchOrder());
    ASSERT_TRUE(loc.has_value());
    EXPECT_EQ("b", str(loc->readData()));
}

TEST(InstallationTextureQuality, medium_prefers_tpb_over_tpa) {
    TmpDir tmp("reone_test_installation_tex_quality");
    std::filesystem::create_directories(tmp.path / "texturepacks");

    writeErf(tmp.path / "texturepacks" / "swpc_tex_tpa.erf", ErfType::ERF,
             {{"probe", ResType::Tga, "a"}});
    writeErf(tmp.path / "texturepacks" / "swpc_tex_tpb.erf", ErfType::ERF,
             {{"probe", ResType::Tga, "b"}});
    writeErf(tmp.path / "texturepacks" / "swpc_tex_gui.erf", ErfType::ERF,
             {{"gui_probe", ResType::Tga, "g"}});

    Installation installation(GameID::KotOR, tmp.path);

    auto loc = installation.resource(
        ResourceId("probe", ResType::Tga),
        textureSearchOrder(graphics::TextureQuality::Medium));

    ASSERT_TRUE(loc.has_value());
    EXPECT_EQ("b", str(loc->readData()));

    auto guiLoc = installation.resource(
        ResourceId("gui_probe", ResType::Tga),
        textureSearchOrder(graphics::TextureQuality::Medium));
    ASSERT_TRUE(guiLoc.has_value());
    EXPECT_EQ("g", str(guiLoc->readData()));
}

TEST(InstallationSearchOrder, lips_directory_reachable_via_canonical_order) {
    TmpDir tmp("reone_test_installation_lips");
    std::filesystem::create_directories(tmp.path / "lips");

    writeErf(tmp.path / "lips" / "global.mod", ErfType::MOD, {{"lip_anim", ResType::Lip, "l"}});

    Installation installation(GameID::KotOR, tmp.path);

    auto loc = installation.resource(ResourceId("lip_anim", ResType::Lip), canonicalSearchOrder());

    ASSERT_TRUE(loc.has_value());
    EXPECT_EQ("l", str(loc->readData()));
}

TEST(InstallationSearchOrder, capsule_dictionary_order_is_stable) {
    TmpDir tmp("reone_test_installation_lips_order");
    std::filesystem::create_directories(tmp.path / "lips");

    writeErf(tmp.path / "lips" / "z_last.mod", ErfType::MOD, {{"lip_anim", ResType::Lip, "z"}});
    writeErf(tmp.path / "lips" / "a_first.mod", ErfType::MOD, {{"lip_anim", ResType::Lip, "a"}});

    Installation installation(GameID::KotOR, tmp.path);

    auto locations = installation.locations(ResourceId("lip_anim", ResType::Lip), SearchScope {SearchLocation::Lips});

    ASSERT_EQ(2u, locations.size());
    EXPECT_EQ("a", str(locations[0].readData()));
    EXPECT_EQ("z", str(locations[1].readData()));
}
