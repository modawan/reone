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

/**
 * The installation and the shared loader answer the same discovery questions.
 *
 * Tooling and the runtime both have to know which modules exist, which files
 * belong to one, and what role each of those files has. These assert that the
 * two derive those answers from the same rules rather than each carrying its
 * own reading of a filename. Whether a running game would mount a discovered
 * archive is a separate question and deliberately not asserted here.
 */

#include <gtest/gtest.h>

#include "reone/extract/installation.h"
#include "reone/resource/modulediscovery.h"

#include "../fixtures/archive.h"

using namespace reone;
using namespace reone::extract;
using namespace reone::resource;
using namespace reone::test;

using ErfType = ErfWriter::FileType;

namespace {

std::vector<std::string> filenamesOf(const std::vector<ModuleArchive> &archives) {
    std::vector<std::string> names;
    names.reserve(archives.size());
    for (const auto &archive : archives) {
        names.push_back(boost::to_lower_copy(archive.path.filename().string()));
    }
    return names;
}

const ModuleArchive *archiveNamed(const std::vector<ModuleArchive> &archives,
                                  const std::string &filename) {
    for (const auto &archive : archives) {
        if (boost::to_lower_copy(archive.path.filename().string()) == filename) {
            return &archive;
        }
    }
    return nullptr;
}

/// Every family and root the shared layer resolves for a module, as the
/// installation must resolve them too.
std::vector<std::pair<std::string, ModuleArchiveFamily>> sharedArchives(
    const std::filesystem::path &root,
    const std::string &module) {
    std::vector<ModuleSearchRoot> roots {
        ModuleSearchRoot {"modules", root / "modules", ModulePrimaryOrigin::Modules, 0, 0}};
    if (std::filesystem::exists(root / "lips")) {
        roots.push_back(ModuleSearchRoot {"lips", root / "lips", ModulePrimaryOrigin::Modules, 0, 0});
    }
    std::vector<std::pair<std::string, ModuleArchiveFamily>> result;
    for (const auto &source : discoverModuleSources(module, roots).sources) {
        result.emplace_back(boost::to_lower_copy(source.filename), source.candidate.family);
    }
    for (const auto &source : discoverRimsModuleAdjuncts(module, root)) {
        result.emplace_back(boost::to_lower_copy(source.filename), source.candidate.family);
    }
    return result;
}

std::vector<std::pair<std::string, ModuleArchiveFamily>> installationArchives(
    Installation &installation) {
    std::vector<std::pair<std::string, ModuleArchiveFamily>> result;
    for (const auto &archive : installation.moduleArchives()) {
        result.emplace_back(boost::to_lower_copy(archive.path.filename().string()), archive.family);
    }
    std::sort(result.begin(), result.end());
    return result;
}

/// A K2 installation holding one of every family the discovery layer knows,
/// plus files that must not become modules of their own.
void writeMixedInstallation(const std::filesystem::path &root) {
    auto modules = root / "modules";
    auto lips = root / "lips";
    auto rims = root / "rims";
    std::filesystem::create_directories(modules);
    std::filesystem::create_directories(lips);
    std::filesystem::create_directories(rims);

    writeRim(modules / "foo.rim", {{"probe", ResType::Txt, "primary rim"}});
    writeRim(modules / "foo_s.rim", {{"probe", ResType::Txt, "static"}});
    writeRim(rims / "foo_a.rim", {{"probe", ResType::Txt, "area"}});
    writeRim(rims / "foo_adx.rim", {{"probe", ResType::Txt, "adx"}});
    writeErf(modules / "foo_dlg.erf", ErfType::ERF, {{"probe", ResType::Txt, "dialogue"}});
    writeErf(modules / "foo_loc.mod", ErfType::MOD, {{"probe", ResType::Txt, "module loc"}});
    writeErf(modules / "bar.mod", ErfType::MOD, {});
    // A support archive with no primary of its own.
    writeRim(modules / "orphan_s.rim", {});
    // Not a family suffix. Read without a requested root it is a module.
    writeRim(modules / "foo_adxx.rim", {});

    writeErf(lips / "foo_loc.mod", ErfType::MOD, {{"probe", ResType::Txt, "lips loc"}});
    writeErf(lips / "global.mod", ErfType::MOD, {});
    writeErf(lips / "localization.mod", ErfType::MOD, {});
}

} // namespace

// Enumeration.

TEST(InstallationModuleNames, matches_shared_discovery_over_the_module_location) {
    TmpDir tmp("reone_test_alignment_names");
    writeMixedInstallation(tmp.path);

    Installation installation(GameID::TSL, tmp.path);

    auto expected = discoverModuleRoots({ModuleSearchRoot {
        "modules", tmp.path / "modules", ModulePrimaryOrigin::Modules, 0, 0}});

    EXPECT_EQ(expected, installation.moduleNames());
    EXPECT_EQ((std::vector<std::string> {"bar", "foo", "foo_adxx"}), installation.moduleNames());
}

TEST(InstallationModuleNames, never_reports_a_support_archive_as_a_module) {
    TmpDir tmp("reone_test_alignment_support_only");
    writeMixedInstallation(tmp.path);

    auto names = Installation(GameID::TSL, tmp.path).moduleNames();

    for (const auto &sidecar : {"foo_s", "foo_a", "foo_adx", "foo_dlg", "foo_loc", "orphan"}) {
        EXPECT_EQ(0, std::count(names.begin(), names.end(), sidecar))
            << sidecar << " is a support archive, not a module";
    }
}

TEST(InstallationModuleNames, never_reports_a_lips_archive_as_a_module) {
    TmpDir tmp("reone_test_alignment_lips");
    writeMixedInstallation(tmp.path);

    auto names = Installation(GameID::TSL, tmp.path).moduleNames();

    for (const auto &global : {"global", "localization"}) {
        EXPECT_EQ(0, std::count(names.begin(), names.end(), global))
            << global << ".mod is a support archive of the lips location";
    }
}

TEST(InstallationModuleNames, is_empty_without_a_module_location) {
    TmpDir tmp("reone_test_alignment_no_modules");

    EXPECT_TRUE(Installation(GameID::TSL, tmp.path).moduleNames().empty());
}

// Classification.

TEST(InstallationModuleArchives, resolves_the_same_families_as_shared_discovery) {
    TmpDir tmp("reone_test_alignment_families");
    writeMixedInstallation(tmp.path);

    Installation installation(GameID::TSL, tmp.path);
    installation.setModuleRoot("foo");

    auto expected = sharedArchives(tmp.path, "foo");
    std::sort(expected.begin(), expected.end());

    EXPECT_EQ(expected, installationArchives(installation));
}

TEST(InstallationModuleArchives, separates_lookup_bucket_from_container_form) {
    TmpDir tmp("reone_test_alignment_metadata");
    writeMixedInstallation(tmp.path);

    Installation installation(GameID::TSL, tmp.path);
    installation.setModuleRoot("foo");
    const auto &archives = installation.moduleArchives();

    struct Expected {
        const char *filename;
        ModuleArchiveFamily family;
        ResourceSourceBucket bucket;
        bool resourceImage;
    };
    const std::vector<Expected> expected {
        {"foo.rim", ModuleArchiveFamily::PrimaryRim, ResourceSourceBucket::ResourceImage, true},
        {"foo_s.rim", ModuleArchiveFamily::StaticRim, ResourceSourceBucket::ResourceImage, true},
        {"foo_a.rim", ModuleArchiveFamily::AreaRim, ResourceSourceBucket::ResourceImage, true},
        {"foo_adx.rim", ModuleArchiveFamily::AdxRim, ResourceSourceBucket::ResourceImage, true},
        // An encapsulated support archive is class 2, below every image, and
        // its extension is what it physically is rather than where it sits.
        {"foo_dlg.erf", ModuleArchiveFamily::Dialogue, ResourceSourceBucket::EncapsulatedClass2, false},
        {"foo_loc.mod", ModuleArchiveFamily::Localization, ResourceSourceBucket::EncapsulatedClass2, false},
    };
    for (const auto &entry : expected) {
        const auto *archive = archiveNamed(archives, entry.filename);
        ASSERT_NE(nullptr, archive) << entry.filename;
        EXPECT_EQ(entry.family, archive->family) << entry.filename;
        ASSERT_TRUE(archive->bucket.has_value()) << entry.filename;
        EXPECT_EQ(entry.bucket, *archive->bucket) << entry.filename;
        EXPECT_EQ(entry.resourceImage, archive->resourceImage) << entry.filename;
        EXPECT_EQ("foo", archive->moduleRoot) << entry.filename;
    }
}

TEST(InstallationModuleArchives, keeps_a_support_archive_out_of_another_modules_scope) {
    TmpDir tmp("reone_test_alignment_scope");
    writeMixedInstallation(tmp.path);

    Installation installation(GameID::TSL, tmp.path);
    installation.setModuleRoot("foo");

    auto names = filenamesOf(installation.moduleArchives());
    EXPECT_EQ(0, std::count(names.begin(), names.end(), "bar.mod"));
    EXPECT_EQ(0, std::count(names.begin(), names.end(), "orphan_s.rim"));
    EXPECT_EQ(0, std::count(names.begin(), names.end(), "global.mod"));
    EXPECT_EQ(0, std::count(names.begin(), names.end(), "localization.mod"));
}

// Context-free enumeration versus an explicitly requested root.

TEST(InstallationModuleArchives, an_unrecognized_suffix_is_a_module_of_its_own) {
    TmpDir tmp("reone_test_alignment_adxx_free");
    writeMixedInstallation(tmp.path);

    auto names = Installation(GameID::TSL, tmp.path).moduleNames();

    // Read without a requested root, "_adxx" is not a family suffix and is not
    // reduced to the one it resembles, so the file is its own primary.
    EXPECT_EQ(1, std::count(names.begin(), names.end(), "foo_adxx"));
}

TEST(InstallationModuleArchives, an_unrecognized_suffix_belongs_to_no_other_module) {
    TmpDir tmp("reone_test_alignment_adxx_explicit");
    writeMixedInstallation(tmp.path);

    Installation foo(GameID::TSL, tmp.path);
    foo.setModuleRoot("foo");
    auto fooNames = filenamesOf(foo.moduleArchives());
    EXPECT_EQ(0, std::count(fooNames.begin(), fooNames.end(), "foo_adxx.rim"))
        << "_adxx is unsupported for foo and must not be normalized to _adx";

    Installation adxx(GameID::TSL, tmp.path);
    adxx.setModuleRoot("foo_adxx");
    const auto &adxxArchives = adxx.moduleArchives();
    ASSERT_EQ(1u, adxxArchives.size());
    EXPECT_EQ("foo_adxx.rim", boost::to_lower_copy(adxxArchives[0].path.filename().string()));
    EXPECT_EQ(ModuleArchiveFamily::PrimaryRim, adxxArchives[0].family)
        << "the requested root resolves the ambiguity that inference cannot";
    EXPECT_EQ("foo_adxx", adxxArchives[0].moduleRoot);
}

TEST(InstallationModuleArchives, a_requested_name_may_carry_a_supported_extension) {
    TmpDir tmp("reone_test_alignment_requested_extension");
    writeMixedInstallation(tmp.path);

    Installation installation(GameID::TSL, tmp.path);
    installation.setModuleRoot("FOO.RIM");

    ASSERT_TRUE(installation.moduleRoot().has_value());
    EXPECT_EQ("foo", *installation.moduleRoot());
    EXPECT_FALSE(installation.moduleArchives().empty());
}

TEST(InstallationModuleArchives, an_unusable_name_leaves_the_scope_unset) {
    TmpDir tmp("reone_test_alignment_bad_name");
    writeMixedInstallation(tmp.path);

    Installation installation(GameID::TSL, tmp.path);
    installation.setModuleRoot("foo.bogus");

    EXPECT_FALSE(installation.moduleRoot().has_value());
    EXPECT_TRUE(installation.moduleArchives().empty());
}

TEST(InstallationModuleArchives, filenames_are_matched_regardless_of_case) {
    TmpDir tmp("reone_test_alignment_case");
    std::filesystem::create_directories(tmp.path / "Modules");

    writeRim(tmp.path / "Modules" / "MODFOO.RIM", {{"probe", ResType::Txt, "rim"}});
    writeRim(tmp.path / "Modules" / "ModFoo_S.rim", {{"probe", ResType::Txt, "static"}});

    Installation installation(GameID::KotOR, tmp.path);
    EXPECT_EQ((std::vector<std::string> {"modfoo"}), installation.moduleNames());

    installation.setModuleRoot("MODFOO");
    EXPECT_EQ(2u, installation.moduleArchives().size());
}

// Lookup order.

TEST(InstallationModuleArchives, k2_mod_layout_ranks_adx_above_a_above_the_module_archive) {
    TmpDir tmp("reone_test_alignment_k2_mod");
    std::filesystem::create_directories(tmp.path / "modules");

    writeErf(tmp.path / "modules" / "foo.mod", ErfType::MOD, {});
    std::filesystem::create_directories(tmp.path / "rims");
    writeRim(tmp.path / "rims" / "foo_a.rim", {});
    writeRim(tmp.path / "rims" / "foo_adx.rim", {});

    Installation installation(GameID::TSL, tmp.path);
    installation.setModuleRoot("foo");

    EXPECT_EQ((std::vector<std::string> {"foo_adx.rim", "foo_a.rim", "foo.mod"}),
              filenamesOf(installation.moduleArchives()));
}

TEST(InstallationModuleArchives, k2_split_layout_ranks_every_image_above_the_dialogue_archive) {
    TmpDir tmp("reone_test_alignment_k2_split");
    writeMixedInstallation(tmp.path);

    Installation installation(GameID::TSL, tmp.path);
    installation.setModuleRoot("foo");

    EXPECT_EQ((std::vector<std::string> {"foo.rim",
                                         "foo_s.rim",
                                         "foo_adx.rim",
                                         "foo_a.rim",
                                         "foo_dlg.erf",
                                         "foo_loc.mod",
                                         "foo_loc.mod"}),
              filenamesOf(installation.moduleArchives()));
}

TEST(InstallationModuleArchives, k1_lists_a_dialogue_archive_it_would_never_mount) {
    TmpDir tmp("reone_test_alignment_k1_dlg");
    writeMixedInstallation(tmp.path);

    Installation installation(GameID::KotOR, tmp.path);
    installation.setModuleRoot("foo");

    auto names = filenamesOf(installation.moduleArchives());
    // K1 never mounts a dialogue archive, but the file still exists and the
    // installation still reports where the resources in it live. Being
    // unmounted places it last within its class-2 bucket, below the
    // localization archives K1 does mount.
    EXPECT_EQ(1, std::count(names.begin(), names.end(), "foo_dlg.erf"));
    EXPECT_EQ("foo_dlg.erf", names.back());
    EXPECT_EQ((std::vector<std::string> {"foo.rim", "foo_s.rim", "foo_adx.rim", "foo_a.rim"}),
              (std::vector<std::string> {names.begin(), names.begin() + 4}));
}

TEST(InstallationModuleArchives, order_is_independent_of_directory_creation_order) {
    TmpDir first("reone_test_alignment_order_a");
    TmpDir second("reone_test_alignment_order_b");
    std::filesystem::create_directories(first.path / "modules");
    std::filesystem::create_directories(second.path / "modules");

    std::filesystem::create_directories(first.path / "rims");
    std::filesystem::create_directories(second.path / "rims");
    for (const auto &name : {"foo.rim", "foo_s.rim"}) {
        writeRim(first.path / "modules" / name, {});
    }
    writeRim(first.path / "rims" / "foo_a.rim", {});
    writeRim(first.path / "rims" / "foo_adx.rim", {});
    for (const auto &name : {"foo_s.rim", "foo.rim"}) {
        writeRim(second.path / "modules" / name, {});
    }

    writeRim(second.path / "rims" / "foo_adx.rim", {});
    writeRim(second.path / "rims" / "foo_a.rim", {});
    Installation a(GameID::TSL, first.path);
    Installation b(GameID::TSL, second.path);
    a.setModuleRoot("foo");
    b.setModuleRoot("foo");

    EXPECT_EQ(filenamesOf(a.moduleArchives()), filenamesOf(b.moduleArchives()));
}
