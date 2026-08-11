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

#include "reone/resource/modulediscovery.h"

#include "../fixtures/archive.h"

using namespace reone;
using namespace reone::resource;
using namespace reone::test;

namespace {

void touch(const std::filesystem::path &path) {
    detail::writeFile(path, "synthetic");
}

ModuleSearchRoot root(std::string id,
                      std::filesystem::path path,
                      ModulePrimaryOrigin origin = ModulePrimaryOrigin::Modules,
                      std::uint32_t packageOrder = 0,
                      std::uint32_t rootOrder = 0) {
    return {std::move(id), std::move(path), origin, packageOrder, rootOrder};
}

std::vector<std::string> discoveredNames(const ModuleDiscoveryResult &result) {
    std::vector<std::string> names;
    for (const auto &source : result.sources) names.push_back(source.filename);
    return names;
}

std::vector<std::string> rejectedNames(const ModuleDiscoveryResult &result) {
    std::vector<std::string> names;
    for (const auto &file : result.rejected) names.push_back(file.filename);
    return names;
}

std::optional<ModuleArchiveFamily> familyOf(const ModuleDiscoveryResult &result,
                                            const std::string &filename) {
    for (const auto &source : result.sources) {
        if (source.filename == filename) return source.candidate.family;
    }
    return std::nullopt;
}

// 15.1 Root normalization

TEST(ModuleDiscovery, resolves_primary_archives_to_their_own_root) {
    for (const auto &filename : {"foo.mod", "foo.rim"}) {
        auto classified = classifyModuleArchive(filename);
        ASSERT_TRUE(classified.archive) << filename;
        EXPECT_EQ("foo", classified.archive->moduleRoot) << filename;
        EXPECT_TRUE(isPrimaryEligible(classified.archive->family)) << filename;
    }
    EXPECT_EQ(ModuleArchiveFamily::PrimaryMod, classifyModuleArchive("foo.mod").archive->family);
    EXPECT_EQ(ModuleArchiveFamily::PrimaryRim, classifyModuleArchive("foo.rim").archive->family);
}

TEST(ModuleDiscovery, resolves_sidecars_to_the_module_they_support_and_never_to_a_primary) {
    const std::vector<std::pair<std::string, ModuleArchiveFamily>> cases {
        {"foo_s.rim", ModuleArchiveFamily::StaticRim},
        {"foo_a.rim", ModuleArchiveFamily::AreaRim},
        {"foo_adx.rim", ModuleArchiveFamily::AdxRim},
        {"foo_dlg.erf", ModuleArchiveFamily::Dialogue},
        {"foo_loc.mod", ModuleArchiveFamily::Localization},
        {"foo_loc.erf", ModuleArchiveFamily::Localization},
    };
    for (const auto &entry : cases) {
        auto classified = classifyModuleArchive(entry.first);
        ASSERT_TRUE(classified.archive) << entry.first;
        EXPECT_EQ("foo", classified.archive->moduleRoot) << entry.first;
        EXPECT_EQ(entry.second, classified.archive->family) << entry.first;
        EXPECT_FALSE(isPrimaryEligible(classified.archive->family)) << entry.first;
    }
}

TEST(ModuleDiscovery, does_not_resolve_an_unrecognized_suffix_to_a_known_family) {
    // Inference matches the recognized suffixes exactly. A name that merely
    // resembles one is not resolved to it, so it never becomes a sidecar of
    // the module it would then have belonged to.
    auto isSidecarOf = [](const ModuleArchiveClassification &classified,
                          const std::string &moduleRoot) {
        return classified.archive && classified.archive->moduleRoot == moduleRoot &&
               !isPrimaryEligible(classified.archive->family);
    };
    for (const auto &filename : {"foo_adrx.rim", "foo_ss.rim", "foo_aa.rim",
                                 "foo_dlgx.erf", "foo_locale.erf"}) {
        EXPECT_FALSE(isSidecarOf(classifyModuleArchive(filename), "foo")) << filename;
    }
}

TEST(ModuleDiscovery, keeps_underscores_that_belong_to_a_module_root) {
    auto primary = classifyModuleArchive("ebo_m12aa.rim");
    ASSERT_TRUE(primary.archive);
    EXPECT_EQ("ebo_m12aa", primary.archive->moduleRoot);

    auto sidecar = classifyModuleArchive("ebo_m12aa_s.rim");
    ASSERT_TRUE(sidecar.archive);
    EXPECT_EQ("ebo_m12aa", sidecar.archive->moduleRoot);
    EXPECT_EQ(ModuleArchiveFamily::StaticRim, sidecar.archive->family);
}

TEST(ModuleDiscovery, keeps_a_literal_requested_root_that_looks_like_a_sidecar) {
    // Inference is only needed when no root was requested. A caller naming
    // "foo_s" has already said which module it means.
    for (const auto &requested : {"foo_s", "foo_a", "foo_adx", "foo_dlg", "foo_loc"}) {
        auto normalized = normalizeModuleName(requested);
        ASSERT_TRUE(normalized.root) << requested;
        EXPECT_EQ(requested, *normalized.root) << requested;
        EXPECT_FALSE(normalized.rejection) << requested;
    }
}

TEST(ModuleDiscovery, normalizes_a_request_by_case_and_extension_only) {
    auto bare = normalizeModuleName("  FooBar  ");
    ASSERT_TRUE(bare.root);
    EXPECT_EQ("foobar", *bare.root);

    auto archive = normalizeModuleName("FOO_S.RIM");
    ASSERT_TRUE(archive.root);
    EXPECT_EQ("foo_s", *archive.root);
}

TEST(ModuleDiscovery, rejects_empty_and_unsupported_requests) {
    EXPECT_EQ(ModuleNameRejection::Empty, normalizeModuleName("").rejection);
    EXPECT_EQ(ModuleNameRejection::Empty, normalizeModuleName("   ").rejection);
    EXPECT_EQ(ModuleNameRejection::UnsupportedExtension, normalizeModuleName("foo.txt").rejection);
}

TEST(ModuleDiscovery, resolves_candidates_against_a_root_that_looks_like_a_sidecar) {
    TmpDir tmp("reone_test_discovery_suffixed_root");
    auto modules = tmp.path / "modules";
    std::filesystem::create_directories(modules);
    touch(modules / "foo_s.rim");
    touch(modules / "foo_s_s.rim");
    touch(modules / "foo_s_a.rim");
    touch(modules / "foo_s_adx.rim");

    auto result = discoverModuleSources("foo_s", {root("modules", modules)});

    EXPECT_EQ("foo_s", result.moduleRoot);
    EXPECT_TRUE(result.rejected.empty());
    EXPECT_EQ(ModuleArchiveFamily::PrimaryRim, familyOf(result, "foo_s.rim"));
    EXPECT_EQ(ModuleArchiveFamily::StaticRim, familyOf(result, "foo_s_s.rim"));
    EXPECT_FALSE(familyOf(result, "foo_s_a.rim"));
    EXPECT_FALSE(familyOf(result, "foo_s_adx.rim"));
}

TEST(ModuleDiscovery, resolves_the_same_files_differently_with_and_without_a_root) {
    // Without a root, ownership must be inferred and foo_s.rim belongs to foo.
    auto inferred = classifyModuleArchive("foo_s.rim");
    ASSERT_TRUE(inferred.archive);
    EXPECT_EQ("foo", inferred.archive->moduleRoot);
    EXPECT_EQ(ModuleArchiveFamily::StaticRim, inferred.archive->family);

    // With the root known, the same name is that module's own primary.
    auto resolved = classifyForModuleRoot("foo_s.rim", "foo_s");
    ASSERT_TRUE(resolved.archive);
    EXPECT_EQ("foo_s", resolved.archive->moduleRoot);
    EXPECT_EQ(ModuleArchiveFamily::PrimaryRim, resolved.archive->family);

    // And relative to foo it remains the static sidecar.
    auto sidecar = classifyForModuleRoot("foo_s.rim", "foo");
    ASSERT_TRUE(sidecar.archive);
    EXPECT_EQ(ModuleArchiveFamily::StaticRim, sidecar.archive->family);
}

TEST(ModuleDiscovery, reports_a_suffix_carried_by_the_wrong_extension) {
    TmpDir tmp("reone_test_discovery_mismatch");
    auto modules = tmp.path / "modules";
    std::filesystem::create_directories(modules);
    touch(modules / "foo.rim");
    touch(modules / "foo_s.mod");

    auto result = discoverModuleSources("foo", {root("modules", modules)});

    EXPECT_EQ((std::vector<std::string> {"foo.rim"}), discoveredNames(result));
    ASSERT_EQ(1, result.rejected.size());
    EXPECT_EQ(ModuleNameRejection::SuffixExtensionMismatch, result.rejected[0].reason);
}

// 15.2 Discovery combinations

TEST(ModuleDiscovery, sidecars_alone_do_not_create_a_module) {
    TmpDir tmp("reone_test_discovery_no_pseudo_modules");
    auto modules = tmp.path / "modules";
    std::filesystem::create_directories(modules);
    touch(modules / "orphan_s.rim");
    touch(modules / "orphan_a.rim");
    touch(modules / "orphan_adx.rim");
    touch(modules / "orphan_dlg.erf");
    touch(modules / "orphan_loc.erf");
    touch(modules / "real.rim");

    EXPECT_EQ((std::vector<std::string> {"real"}),
              discoverModuleRoots({root("modules", modules)}));
}

TEST(ModuleDiscovery, discovers_module_roots_from_every_primary_family) {
    TmpDir tmp("reone_test_discovery_roots");
    auto modules = tmp.path / "modules";
    std::filesystem::create_directories(modules);
    touch(modules / "packed.mod");
    touch(modules / "split.rim");
    touch(modules / "zeta.nwm");

    EXPECT_EQ((std::vector<std::string> {"packed", "split", "zeta"}),
              discoverModuleRoots({root("modules", modules)}));
}

TEST(ModuleDiscovery, discovers_a_packed_module_with_its_adjuncts) {
    TmpDir tmp("reone_test_discovery_packed");
    auto modules = tmp.path / "modules";
    std::filesystem::create_directories(modules);
    touch(modules / "foo.mod");
    touch(modules / "foo_a.rim");
    touch(modules / "foo_adx.rim");
    touch(modules / "foo_s.rim");
    touch(modules / "foo_dlg.erf");

    auto result = discoverModuleSources("foo", {root("modules", modules)});

    // Discovery reports everything present; suppressing _s and _dlg under a
    // module archive is the policy's decision, not discovery's.
    EXPECT_EQ(ModuleArchiveFamily::PrimaryMod, familyOf(result, "foo.mod"));
    EXPECT_FALSE(familyOf(result, "foo_a.rim"));
    EXPECT_FALSE(familyOf(result, "foo_adx.rim"));
    EXPECT_EQ(ModuleArchiveFamily::StaticRim, familyOf(result, "foo_s.rim"));
    EXPECT_EQ(ModuleArchiveFamily::Dialogue, familyOf(result, "foo_dlg.erf"));
    EXPECT_TRUE(result.rejected.empty());
}

TEST(ModuleDiscovery, reports_an_unsupported_sidecar_rather_than_absorbing_it) {
    TmpDir tmp("reone_test_discovery_unsupported");
    auto modules = tmp.path / "modules";
    std::filesystem::create_directories(modules);
    touch(modules / "foo.rim");
    touch(modules / "foo_adx.rim");
    touch(modules / "foo_adrx.rim");
    touch(modules / "foo_bar.erf");

    auto result = discoverModuleSources("foo", {root("modules", modules)});

    EXPECT_EQ((std::vector<std::string> {"foo.rim"}), discoveredNames(result));
    EXPECT_EQ((std::vector<std::string> {"foo_adrx.rim", "foo_bar.erf"}), rejectedNames(result));
    ASSERT_EQ(2, result.rejected.size());
    // Relative to a known root, both carry a suffix no supported family
    // claims, so both are reported instead of being resolved to a family
    // they resemble.
    EXPECT_EQ(ModuleNameRejection::UnsupportedSuffix, result.rejected[0].reason);
    EXPECT_EQ(ModuleNameRejection::UnsupportedSuffix, result.rejected[1].reason);
}

TEST(ModuleDiscovery, ignores_files_belonging_to_other_modules) {
    TmpDir tmp("reone_test_discovery_other_modules");
    auto modules = tmp.path / "modules";
    std::filesystem::create_directories(modules);
    touch(modules / "foo.rim");
    touch(modules / "foobar.rim");
    touch(modules / "other.rim");
    touch(modules / "other_s.rim");

    auto result = discoverModuleSources("foo", {root("modules", modules)});

    EXPECT_EQ((std::vector<std::string> {"foo.rim"}), discoveredNames(result));
    EXPECT_TRUE(result.rejected.empty());
}

TEST(ModuleDiscovery, preserves_configured_root_order_and_provenance) {
    TmpDir tmp("reone_test_discovery_roots_order");
    auto base = tmp.path / "modules";
    auto first = tmp.path / "root0";
    auto second = tmp.path / "root1";
    for (const auto &dir : {base, first, second}) {
        std::filesystem::create_directories(dir);
        touch(dir / "foo.mod");
    }

    auto result = discoverModuleSources(
        "foo",
        {root("base", base, ModulePrimaryOrigin::Modules),
         root("root0", first, ModulePrimaryOrigin::ConfiguredModuleRoot, 0, 0),
         root("root1", second, ModulePrimaryOrigin::ConfiguredModuleRoot, 0, 1)});

    ASSERT_EQ(3, result.sources.size());
    EXPECT_EQ("base", result.sources[0].candidate.rootId);
    EXPECT_EQ(ModulePrimaryOrigin::Modules, result.sources[0].candidate.origin);
    EXPECT_EQ(0u, result.sources[1].candidate.rootOrder);
    EXPECT_EQ(1u, result.sources[2].candidate.rootOrder);
    EXPECT_EQ(ModulePrimaryOrigin::ConfiguredModuleRoot, result.sources[2].candidate.origin);
}

TEST(ModuleDiscovery, carries_numbered_package_order) {
    TmpDir tmp("reone_test_discovery_packages");
    auto live2 = tmp.path / "live2";
    std::filesystem::create_directories(live2);
    touch(live2 / "foo.mod");

    auto result = discoverModuleSources(
        "foo", {root("live2", live2, ModulePrimaryOrigin::LivePackage, 2, 0)});

    ASSERT_EQ(1, result.sources.size());
    EXPECT_EQ(ModulePrimaryOrigin::LivePackage, result.sources[0].candidate.origin);
    EXPECT_EQ(2u, result.sources[0].candidate.packageOrder);
}

TEST(ModuleDiscovery, output_does_not_depend_on_directory_creation_order) {
    auto build = [](const std::filesystem::path &dir, bool reverse) {
        std::filesystem::create_directories(dir);
        std::vector<std::string> names {"foo.rim", "foo_a.rim", "foo_adx.rim", "foo_s.rim"};
        if (reverse) std::reverse(names.begin(), names.end());
        for (const auto &name : names) touch(dir / name);
    };
    TmpDir forwardDir("reone_test_discovery_order_forward");
    TmpDir reverseDir("reone_test_discovery_order_reverse");
    build(forwardDir.path / "modules", false);
    build(reverseDir.path / "modules", true);

    auto forward = discoverModuleSources("foo", {root("modules", forwardDir.path / "modules")});
    auto reverse = discoverModuleSources("foo", {root("modules", reverseDir.path / "modules")});

    EXPECT_EQ((std::vector<std::string> {"foo.rim", "foo_s.rim"}),
              discoveredNames(forward));
    EXPECT_EQ(discoveredNames(forward), discoveredNames(reverse));
}

TEST(ModuleDiscovery, rejects_the_request_without_scanning_roots) {
    TmpDir tmp("reone_test_discovery_rejected_request");
    auto modules = tmp.path / "modules";
    std::filesystem::create_directories(modules);
    touch(modules / "foo_s.rim");

    auto result = discoverModuleSources("foo.txt", {root("modules", modules)});

    EXPECT_EQ(ModuleNameRejection::UnsupportedExtension, result.nameRejection);
    EXPECT_TRUE(result.moduleRoot.empty());
    EXPECT_TRUE(result.sources.empty());
    EXPECT_TRUE(result.rejected.empty());
}

TEST(ModuleDiscovery, skips_locations_that_do_not_exist) {
    TmpDir tmp("reone_test_discovery_missing_root");
    auto modules = tmp.path / "modules";
    std::filesystem::create_directories(modules);
    touch(modules / "foo.rim");

    auto result = discoverModuleSources(
        "foo", {root("absent", tmp.path / "absent"), root("modules", modules)});

    ASSERT_EQ(1, result.sources.size());
    EXPECT_EQ("modules", result.sources[0].candidate.rootId);
}

// 15.3 Save and package candidates

TEST(ModuleDiscovery, treats_saved_image_and_saved_archive_as_distinct_families) {
    TmpDir tmp("reone_test_discovery_saved");
    auto save = tmp.path / "gameinprogress";
    std::filesystem::create_directories(save);
    touch(save / "foo.rsv");
    touch(save / "foo.sav");

    auto result = discoverModuleSources(
        "foo", {root("save", save, ModulePrimaryOrigin::GameInProgress)});

    EXPECT_EQ(ModuleArchiveFamily::SavedResourceImage, familyOf(result, "foo.rsv"));
    EXPECT_EQ(ModuleArchiveFamily::SavedArchive, familyOf(result, "foo.sav"));
    for (const auto &source : result.sources) {
        EXPECT_EQ(ModulePrimaryOrigin::GameInProgress, source.candidate.origin);
        EXPECT_TRUE(isActiveSavedState(source.candidate.family));
        EXPECT_TRUE(isPrimaryEligible(source.candidate.family));
    }
}

TEST(ModuleDiscovery, treats_a_packaged_module_as_its_own_primary_family) {
    TmpDir tmp("reone_test_discovery_nwm");
    auto nwm = tmp.path / "nwm";
    std::filesystem::create_directories(nwm);
    touch(nwm / "foo.nwm");

    auto result = discoverModuleSources("foo", {root("nwm", nwm, ModulePrimaryOrigin::NwmFiles)});

    ASSERT_EQ(1, result.sources.size());
    EXPECT_EQ(ModuleArchiveFamily::Nwm, result.sources[0].candidate.family);
    EXPECT_TRUE(isPrimaryEligible(ModuleArchiveFamily::Nwm));
    EXPECT_FALSE(isActiveSavedState(ModuleArchiveFamily::Nwm));
}

TEST(ModuleDiscovery, does_not_look_inside_archives) {
    // A container is a source, never a directory to walk into. Discovery reads
    // directory entries only, so a saved archive contributes exactly itself.
    TmpDir tmp("reone_test_discovery_no_recursion");
    auto save = tmp.path / "save";
    std::filesystem::create_directories(save);
    writeErf(save / "foo.sav", ErfWriter::FileType::MOD,
             {{"inner", ResType::Sav, "nested module blob"}});

    auto result = discoverModuleSources("foo", {root("save", save, ModulePrimaryOrigin::GameInProgress)});

    EXPECT_EQ((std::vector<std::string> {"foo.sav"}), discoveredNames(result));
}

TEST(ModuleDiscovery, leaves_internal_module_identity_unset) {
    TmpDir tmp("reone_test_discovery_identity");
    auto modules = tmp.path / "modules";
    std::filesystem::create_directories(modules);
    touch(modules / "foo.rim");

    auto result = discoverModuleSources("foo", {root("modules", modules)});

    ASSERT_EQ(1, result.sources.size());
    EXPECT_FALSE(result.sources[0].internalModuleId);
    EXPECT_EQ("foo", result.sources[0].moduleRoot);
    EXPECT_EQ(modules / "foo.rim", result.sources[0].path);
}

// Hand-off to the policy

TEST(ModuleDiscovery, feeds_the_policy_with_a_consumable_inventory) {
    TmpDir tmp("reone_test_discovery_policy");
    auto modules = tmp.path / "modules";
    std::filesystem::create_directories(modules);
    touch(modules / "foo.rim");
    touch(modules / "foo_s.rim");
    touch(modules / "foo_a.rim");
    touch(modules / "foo_adx.rim");
    touch(modules / "foo_dlg.erf");

    auto result = discoverModuleSources("foo", {root("modules", modules)});
    auto inventory = plannerInventory(result);
    ASSERT_EQ(result.sources.size(), inventory.size());

    ModulePolicyRequest request;
    request.game = GameID::TSL;
    request.moduleName = result.moduleRoot;
    auto plan = planModuleLoad(request, inventory);

    ASSERT_TRUE(plan.primary);
    EXPECT_EQ("modules:foo.rim", plan.primary->candidate.source.sourceId);
    EXPECT_EQ(ModulePrimaryKind::Rim, plan.primary->candidate.kind);

    std::vector<ModuleArchiveFamily> families;
    for (const auto &family : plan.families) families.push_back(family.family);
    EXPECT_EQ((std::vector<ModuleArchiveFamily> {ModuleArchiveFamily::StaticRim,
                                                 ModuleArchiveFamily::Dialogue}),
              families);
}

TEST(ModuleDiscovery, constructs_only_exact_adjunct_names_from_rims) {
    TmpDir tmp("reone_test_discovery_exact_rims");
    auto rims = tmp.path / "RiMs";
    auto modules = tmp.path / "modules";
    std::filesystem::create_directories(rims);
    std::filesystem::create_directories(modules);
    touch(rims / "FoO_A.RiM");
    touch(rims / "foo_adx.rim");
    touch(rims / "bar_a.rim");
    touch(rims / "foo_extra.rim");
    std::filesystem::create_directories(rims / "nested");
    touch(rims / "nested" / "foo_a.rim");
    touch(modules / "foo_a.rim");

    auto exact = discoverRimsModuleAdjuncts("FOO.MOD", tmp.path);
    ASSERT_EQ(2u, exact.size());
    EXPECT_EQ(ModuleArchiveFamily::AreaRim, exact[0].candidate.family);
    EXPECT_EQ(ModuleArchiveFamily::AdxRim, exact[1].candidate.family);
    EXPECT_EQ("rims:foo_a.rim", exact[0].candidate.sourceId);
    EXPECT_EQ("rims:foo_adx.rim", exact[1].candidate.sourceId);
    EXPECT_EQ(rims / "FoO_A.RiM", exact[0].path);
    EXPECT_EQ(rims / "foo_adx.rim", exact[1].path);

    auto generic = discoverModuleSources("foo", {root("modules", modules)});
    EXPECT_TRUE(generic.sources.empty());
}
TEST(ModuleDiscovery, saved_candidates_reach_the_policy_gate) {
    TmpDir tmp("reone_test_discovery_save_gate");
    auto save = tmp.path / "save";


    auto modules = tmp.path / "modules";
    std::filesystem::create_directories(save);
    std::filesystem::create_directories(modules);
    touch(save / "foo.sav");
    touch(modules / "foo.rim");

    auto result = discoverModuleSources(
        "foo",
        {root("save", save, ModulePrimaryOrigin::GameInProgress), root("modules", modules)});
    auto inventory = plannerInventory(result);

    ModulePolicyRequest request;
    request.game = GameID::TSL;
    request.moduleName = result.moduleRoot;

    request.includeInSave = true;
    auto included = selectModulePrimary(request, inventory);
    ASSERT_TRUE(included);
    EXPECT_EQ("save:foo.sav", included->candidate.source.sourceId);

    request.includeInSave = false;
    auto excluded = selectModulePrimary(request, inventory);
    ASSERT_TRUE(excluded);
    EXPECT_EQ("modules:foo.rim", excluded->candidate.source.sourceId);
}

} // namespace
