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
 * The executor that turns a mount plan into actual mounts.
 *
 * These tests describe what it mounts and in what order, not which source
 * later wins a lookup: that is decided by the bucket each mount carries, and
 * is covered against real backends elsewhere.
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "reone/resource/format/erfwriter.h"
#include "reone/resource/format/rimwriter.h"
#include "reone/resource/modulemount.h"
#include "reone/resource/resources.h"
#include "../fixtures/resource.h"
#include "reone/system/exception/validation.h"
#include "reone/system/stream/fileoutput.h"
#include "reone/system/stream/memoryoutput.h"

using namespace reone;
using namespace reone::resource;

namespace {

ByteBuffer bytes(std::string_view value) {
    return ByteBuffer(value.begin(), value.end());
}

ByteBuffer erfBytes(const std::string &resRef, const std::string &data) {
    ErfWriter writer;
    writer.add(ErfWriter::Resource {resRef, ResType::Txt, bytes(data)});
    ByteBuffer buffer;
    MemoryOutputStream stream(buffer);
    writer.save(ErfWriter::FileType::MOD, stream);
    return buffer;
}

ByteBuffer rimBytes(const std::string &resRef, const std::string &data) {
    RimWriter writer;
    writer.add(RimWriter::Resource {resRef, ResType::Txt, bytes(data)});
    ByteBuffer buffer;
    MemoryOutputStream stream(buffer);
    writer.save(stream);
    return buffer;
}

/// A directory of archives written to disk, removed on destruction.
struct TmpDir {
    std::filesystem::path path;

    explicit TmpDir(const std::string &name) {
        path = std::filesystem::temp_directory_path() / name;
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }

    ~TmpDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    std::filesystem::path writeRim(const std::string &filename, const std::string &data) {
        return write(filename, rimBytes("shared", data));
    }

    std::filesystem::path writeErf(const std::string &filename, const std::string &data) {
        return write(filename, erfBytes("shared", data));
    }

    std::filesystem::path write(const std::string &filename, const ByteBuffer &buffer) {
        auto full = path / filename;
        FileOutputStream stream(full);
        stream.write(buffer.data(), buffer.size());
        return full;
    }
};

ModuleSourceCandidate candidate(std::string sourceId, ModuleArchiveFamily family) {
    ModuleSourceCandidate result;
    result.sourceId = std::move(sourceId);
    result.rootId = "modules";
    result.family = family;
    return result;
}

ResourceMountAttempt attempt(std::string sourceId,
                             ModuleArchiveFamily family,
                             MountFailureEffect effect = MountFailureEffect::BestEffort) {
    ResourceMountAttempt result;
    result.source = candidate(sourceId, family);
    result.family = family;
    result.phase = ModuleMountPhase::ModuleBranch;
    result.metadata = *mountMetadata(family);
    result.failureEffect = effect;
    return result;
}

ModuleFamilyPlan familyPlan(ModuleArchiveFamily family,
                            MountCardinality cardinality,
                            std::vector<ResourceMountAttempt> attempts) {
    ModuleFamilyPlan plan;
    plan.family = family;
    plan.cardinality = cardinality;
    plan.attempts = std::move(attempts);
    return plan;
}

std::vector<std::string> mountedIds(const ModuleMountReport &report) {
    std::vector<std::string> result;
    for (const auto &outcome : report.outcomes) {
        if (outcome.mounted) {
            result.push_back(outcome.sourceId);
        }
    }
    return result;
}

std::vector<std::string> attemptedIds(const ModuleMountReport &report) {
    std::vector<std::string> result;
    for (const auto &outcome : report.outcomes) {
        result.push_back(outcome.sourceId);
    }
    return result;
}

} // namespace

// Ownership says when a source goes away; it never decides lookup order.

TEST(ModuleMount, gives_the_module_and_its_state_separate_owners) {
    // The state of the module is not one of its support sources. Both are
    // retired on a transition today, but a save can be replaced under a module
    // that is not, so the two lifetimes stay distinct.
    EXPECT_EQ(ResourceOwner::ActiveModule, mountMetadata(ModuleArchiveFamily::StaticRim)->owner);
    EXPECT_EQ(ResourceOwner::ActiveModule, mountMetadata(ModuleArchiveFamily::AdxRim)->owner);
    EXPECT_EQ(ResourceOwner::ActiveModule, mountMetadata(ModuleArchiveFamily::PrimaryMod)->owner);
    EXPECT_EQ(ResourceOwner::ActiveModule, mountMetadata(ModuleArchiveFamily::Dialogue)->owner);

    EXPECT_EQ(ResourceOwner::ActiveModuleState,
              mountMetadata(ModuleArchiveFamily::SavedArchive)->owner);
    EXPECT_EQ(ResourceOwner::ActiveModuleState,
              mountMetadata(ModuleArchiveFamily::SavedResourceImage)->owner);
}

TEST(ModuleMount, derives_container_form_from_the_family_not_the_bucket) {
    EXPECT_TRUE(isResourceImageFamily(ModuleArchiveFamily::PrimaryRim));
    EXPECT_TRUE(isResourceImageFamily(ModuleArchiveFamily::StaticRim));
    EXPECT_TRUE(isResourceImageFamily(ModuleArchiveFamily::AreaRim));
    EXPECT_TRUE(isResourceImageFamily(ModuleArchiveFamily::AdxRim));
    EXPECT_TRUE(isResourceImageFamily(ModuleArchiveFamily::SavedResourceImage));

    EXPECT_FALSE(isResourceImageFamily(ModuleArchiveFamily::PrimaryMod));
    EXPECT_FALSE(isResourceImageFamily(ModuleArchiveFamily::SavedArchive));
    EXPECT_FALSE(isResourceImageFamily(ModuleArchiveFamily::Dialogue));
    EXPECT_FALSE(isResourceImageFamily(ModuleArchiveFamily::Localization));
}

// The index: two suppliers, one inventory, opaque identifiers.

TEST(RuntimeModuleSourceIndex, offers_disk_and_staged_sources_as_one_inventory) {
    RuntimeModuleSourceIndex index;
    index.add(RuntimeModuleSource {candidate("modules:foo.rim", ModuleArchiveFamily::PrimaryRim),
                                   std::filesystem::path("foo.rim")});
    index.add(RuntimeModuleSource {candidate("currentgame:foo.sav", ModuleArchiveFamily::SavedArchive),
                                   ResourceId("foo", ResType::Sav)});

    auto inventory = index.inventory();
    ASSERT_EQ(2u, inventory.size());
    EXPECT_EQ("modules:foo.rim", inventory[0].sourceId);
    EXPECT_EQ("currentgame:foo.sav", inventory[1].sourceId);

    ASSERT_TRUE(index.find("currentgame:foo.sav"));
    EXPECT_TRUE(std::holds_alternative<ResourceId>(index.find("currentgame:foo.sav")->locator));
    EXPECT_TRUE(std::holds_alternative<std::filesystem::path>(index.find("modules:foo.rim")->locator));
    EXPECT_FALSE(index.find("modules:nothing.rim"));
}

// Mounting.

class ModuleMountExecutorTest : public testing::Test {
protected:
    void addDiskSource(const std::string &sourceId,
                       ModuleArchiveFamily family,
                       const std::filesystem::path &path) {
        _index.add(RuntimeModuleSource {candidate(sourceId, family), path});
    }

    void addStagedSource(const std::string &sourceId,
                         ModuleArchiveFamily family,
                         const ResourceId &id) {
        _index.add(RuntimeModuleSource {candidate(sourceId, family), id});
    }

    /// Put a save archive in scope, holding the module blobs a staged source is
    /// read back from. The container is never walked into; only the ids the
    /// director probes for are ever resolved out of it.
    ///
    /// It carries a bucket because the executor's mounts do: a list is
    /// homogeneous, so leaving this one unplaced would have the list reject
    /// every later mount rather than rank it.
    void mountSaveArchive(std::vector<ErfWriter::Resource> entries) {
        ErfWriter writer;
        for (auto &entry : entries) {
            writer.add(std::move(entry));
        }
        ByteBuffer buffer;
        MemoryOutputStream stream(buffer);
        writer.save(ErfWriter::FileType::ERF, stream);
        _resources.addMemERF(std::move(buffer),
                             ResourceOwner::SaveSlot,
                             ResourceSourceBucket::EncapsulatedClass2);
    }

    ModuleMountReport run(const ModuleLoadPlan &plan) {
        ModuleMountExecutor executor(_resources, _index);
        return executor.run(plan);
    }

    std::string shared() {
        auto res = _resources.find(ResourceId("shared", ResType::Txt));
        return res ? std::string(res->data.begin(), res->data.end()) : "<not found>";
    }

    Resources _resources;
    RuntimeModuleSourceIndex _index;
};

TEST(ModuleMountExecutorNwm, mounts_only_the_selected_nwm_as_class_2_active_state) {
    testing::StrictMock<MockResources> resources;
    RuntimeModuleSourceIndex index;
    TmpDir tmp("reone_test_mount_nwm_metadata");
    auto path = tmp.writeErf("foo.nwm", "nwm");
    index.add(RuntimeModuleSource {
        candidate("nwm:foo.nwm", ModuleArchiveFamily::Nwm), path});

    ModuleLoadPlan plan;
    ModulePrimarySelection selection;
    selection.candidate = ModulePrimaryCandidate {
        candidate("nwm:foo.nwm", ModuleArchiveFamily::Nwm),
        ModulePrimaryKind::Nwm};
    plan.primary = selection;
    plan.activeState.owner = ResourceOwner::ActiveModuleState;

    EXPECT_CALL(resources,
                addERF(path,
                       ResourceOwner::ActiveModuleState,
                       std::optional<ResourceSourceBucket>(
                           ResourceSourceBucket::EncapsulatedClass2)));

    ModuleMountExecutor executor(resources, index);
    auto report = executor.run(plan);
    EXPECT_EQ((std::vector<std::string> {"nwm:foo.nwm"}), mountedIds(report));
}

TEST_F(ModuleMountExecutorTest, mounts_every_attempt_of_an_all_successful_family) {
    TmpDir tmp("reone_test_mount_all");
    addDiskSource("a", ModuleArchiveFamily::Dialogue, tmp.writeErf("a_dlg.erf", "a"));
    addDiskSource("b", ModuleArchiveFamily::Dialogue, tmp.writeErf("b_dlg.erf", "b"));

    ModuleLoadPlan plan;
    plan.families.push_back(familyPlan(ModuleArchiveFamily::Dialogue,
                                       MountCardinality::AllSuccessful,
                                       {attempt("a", ModuleArchiveFamily::Dialogue),
                                        attempt("b", ModuleArchiveFamily::Dialogue)}));

    auto report = run(plan);

    EXPECT_EQ((std::vector<std::string> {"a", "b"}), mountedIds(report));
    EXPECT_FALSE(report.requiredFailure);
}

TEST_F(ModuleMountExecutorTest, stops_a_first_successful_family_at_its_first_success) {
    TmpDir tmp("reone_test_mount_first");
    addDiskSource("missing", ModuleArchiveFamily::StaticRim, tmp.path / "absent_s.rim");
    addDiskSource("root", ModuleArchiveFamily::StaticRim, tmp.writeRim("root_s.rim", "root"));
    addDiskSource("base", ModuleArchiveFamily::StaticRim, tmp.writeRim("base_s.rim", "base"));

    ModuleLoadPlan plan;
    plan.families.push_back(familyPlan(ModuleArchiveFamily::StaticRim,
                                       MountCardinality::FirstSuccessful,
                                       {attempt("missing", ModuleArchiveFamily::StaticRim),
                                        attempt("root", ModuleArchiveFamily::StaticRim),
                                        attempt("base", ModuleArchiveFamily::StaticRim)}));

    auto report = run(plan);

    EXPECT_EQ((std::vector<std::string> {"root"}), mountedIds(report));
    EXPECT_EQ((std::vector<std::string> {"missing", "root"}), attemptedIds(report))
        << "the fallback location must not be attempted once a location supplied the image";
}

TEST_F(ModuleMountExecutorTest, reports_a_required_family_that_mounted_nothing) {
    TmpDir tmp("reone_test_mount_required");
    addDiskSource("gone", ModuleArchiveFamily::StaticRim, tmp.path / "absent_s.rim");

    ModuleLoadPlan plan;
    plan.families.push_back(familyPlan(
        ModuleArchiveFamily::StaticRim,
        MountCardinality::FirstSuccessful,
        {attempt("gone", ModuleArchiveFamily::StaticRim, MountFailureEffect::FailOrCurrentGameFallback)}));

    auto report = run(plan);
    EXPECT_TRUE(report.requiredFailure);
    EXPECT_EQ(ModuleLoadOutcome::Failed, report.outcome);
}

TEST_F(ModuleMountExecutorTest, keeps_a_best_effort_failure_out_of_the_required_result) {
    TmpDir tmp("reone_test_mount_best_effort");
    addDiskSource("gone", ModuleArchiveFamily::Dialogue, tmp.path / "absent_dlg.erf");

    ModuleLoadPlan plan;
    plan.families.push_back(familyPlan(ModuleArchiveFamily::Dialogue,
                                       MountCardinality::AllSuccessful,
                                       {attempt("gone", ModuleArchiveFamily::Dialogue)}));

    auto report = run(plan);
    EXPECT_FALSE(report.requiredFailure);
    EXPECT_TRUE(mountedIds(report).empty());
}

TEST_F(ModuleMountExecutorTest, mounts_the_selected_primary_as_the_active_table) {
    TmpDir tmp("reone_test_mount_active");
    addDiskSource("modules:foo.rim", ModuleArchiveFamily::PrimaryRim, tmp.writeRim("foo.rim", "primary"));
    addDiskSource("modules:foo_s.rim", ModuleArchiveFamily::StaticRim, tmp.writeRim("foo_s.rim", "static"));

    ModuleLoadPlan plan;
    plan.families.push_back(familyPlan(ModuleArchiveFamily::StaticRim,
                                       MountCardinality::FirstSuccessful,
                                       {attempt("modules:foo_s.rim", ModuleArchiveFamily::StaticRim)}));
    ModulePrimarySelection selection;
    selection.candidate = ModulePrimaryCandidate {
        candidate("modules:foo.rim", ModuleArchiveFamily::PrimaryRim), ModulePrimaryKind::Rim};
    plan.primary = selection;

    auto report = run(plan);

    EXPECT_EQ((std::vector<std::string> {"modules:foo_s.rim", "modules:foo.rim"}), mountedIds(report))
        << "the active table is mounted last, so it wins its bucket";
    EXPECT_EQ("primary", shared());
    EXPECT_EQ(ModuleLoadOutcome::Succeeded, report.outcome);
}

TEST_F(ModuleMountExecutorTest, does_not_mount_the_primary_twice_in_the_mod_branch) {
    TmpDir tmp("reone_test_mount_mod_branch");
    addDiskSource("modules:foo.mod", ModuleArchiveFamily::PrimaryMod, tmp.writeErf("foo.mod", "mod"));

    ModuleLoadPlan plan;
    plan.families.push_back(familyPlan(
        ModuleArchiveFamily::PrimaryMod,
        MountCardinality::AllSuccessful,
        {attempt("modules:foo.mod", ModuleArchiveFamily::PrimaryMod, MountFailureEffect::FailOrCurrentGameFallback)}));
    ModulePrimarySelection selection;
    selection.candidate = ModulePrimaryCandidate {
        candidate("modules:foo.mod", ModuleArchiveFamily::PrimaryMod), ModulePrimaryKind::Mod};
    plan.primary = selection;

    auto report = run(plan);

    EXPECT_EQ((std::vector<std::string> {"modules:foo.mod"}), mountedIds(report));
    EXPECT_EQ(1u, report.mountedCount()) << "selecting a source and mounting it are separate, but not repeated";
}

TEST_F(ModuleMountExecutorTest, mounts_a_staged_image_as_an_image_and_a_staged_archive_as_an_archive) {
    // The saved image and the saved archive are distinct sources with distinct
    // container forms; the probe that supplied them decides which is which.
    mountSaveArchive({{"foo", ResType::Rsv, rimBytes("shared", "staged image")},
                      {"foo", ResType::Sav, erfBytes("shared", "staged archive")}});

    addStagedSource("currentgame:foo.rsv", ModuleArchiveFamily::SavedResourceImage, ResourceId("foo", ResType::Rsv));

    ModuleLoadPlan plan;
    ModulePrimarySelection selection;
    selection.candidate = ModulePrimaryCandidate {
        candidate("currentgame:foo.rsv", ModuleArchiveFamily::SavedResourceImage),
        ModulePrimaryKind::SavedResourceImage};
    plan.primary = selection;

    auto report = run(plan);

    EXPECT_EQ((std::vector<std::string> {"currentgame:foo.rsv"}), mountedIds(report));
    EXPECT_EQ("staged image", shared()) << "a saved image is read back as a resource image, not as an archive";
}

TEST_F(ModuleMountExecutorTest, recovers_through_the_staged_archive_when_a_required_mount_fails) {
    TmpDir tmp("reone_test_mount_recovery");
    mountSaveArchive({{"foo", ResType::Sav, erfBytes("shared", "recovered")}});

    addDiskSource("modules:foo_s.rim", ModuleArchiveFamily::StaticRim, tmp.path / "absent_s.rim");
    addStagedSource("currentgame:foo.sav", ModuleArchiveFamily::SavedArchive, ResourceId("foo", ResType::Sav));

    ModuleLoadPlan plan;
    plan.families.push_back(familyPlan(
        ModuleArchiveFamily::StaticRim,
        MountCardinality::FirstSuccessful,
        {attempt("modules:foo_s.rim", ModuleArchiveFamily::StaticRim, MountFailureEffect::FailOrCurrentGameFallback)}));
    ModulePrimarySelection selection;
    selection.candidate = ModulePrimaryCandidate {
        candidate("currentgame:foo.sav", ModuleArchiveFamily::SavedArchive),
        ModulePrimaryKind::SavedArchive};
    plan.primary = selection;
    plan.activeState.encapsulatedArchive = *mountMetadata(ModuleArchiveFamily::SavedArchive);

    auto report = run(plan);

    EXPECT_TRUE(report.requiredFailure);
    EXPECT_EQ((std::vector<std::string> {"currentgame:foo.sav"}), mountedIds(report));
    EXPECT_EQ("recovered", shared());
    EXPECT_EQ(ModuleLoadOutcome::RecoveredThroughActiveState, report.outcome);
}

TEST_F(ModuleMountExecutorTest, looks_at_one_candidate_only_for_a_zero_or_one_family) {
    // The limit is on what is considered, not on what succeeds. A family
    // allowed a single source must not fall through to a later candidate when
    // the first one turns out not to be there, which is what distinguishes it
    // from a family that takes the first source to supply it.
    TmpDir tmp("reone_test_mount_zero_or_one_first_fails");
    addDiskSource("absent", ModuleArchiveFamily::AreaRim, tmp.path / "foo_a.rim");
    addDiskSource("usable", ModuleArchiveFamily::AreaRim, tmp.writeRim("bar_a.rim", "usable"));

    ModuleLoadPlan plan;
    plan.families.push_back(familyPlan(ModuleArchiveFamily::AreaRim,
                                       MountCardinality::ZeroOrOne,
                                       {attempt("absent", ModuleArchiveFamily::AreaRim),
                                        attempt("usable", ModuleArchiveFamily::AreaRim)}));

    auto report = run(plan);

    EXPECT_EQ((std::vector<std::string> {"absent"}), attemptedIds(report))
        << "the second candidate must not be considered once the one allowed candidate was";
    EXPECT_TRUE(mountedIds(report).empty());
    EXPECT_EQ("<not found>", shared()) << "the second candidate must not be mounted";
}

TEST_F(ModuleMountExecutorTest, stops_a_zero_or_one_family_after_its_first_success) {
    // The planner happens to give such a family a single attempt. The limit is
    // the family's own, so a plan built by hand with two usable attempts must
    // still leave only one mounted.
    TmpDir tmp("reone_test_mount_zero_or_one");
    addDiskSource("first", ModuleArchiveFamily::AreaRim, tmp.writeRim("foo_a.rim", "first"));
    addDiskSource("second", ModuleArchiveFamily::AreaRim, tmp.writeRim("bar_a.rim", "second"));

    ModuleLoadPlan plan;
    plan.families.push_back(familyPlan(ModuleArchiveFamily::AreaRim,
                                       MountCardinality::ZeroOrOne,
                                       {attempt("first", ModuleArchiveFamily::AreaRim),
                                        attempt("second", ModuleArchiveFamily::AreaRim)}));

    auto report = run(plan);

    EXPECT_EQ((std::vector<std::string> {"first"}), mountedIds(report));
    EXPECT_EQ((std::vector<std::string> {"first"}), attemptedIds(report))
        << "the second source must not even be attempted once one is mounted";
    EXPECT_EQ("first", shared());
}

TEST_F(ModuleMountExecutorTest, lets_a_structural_rejection_escape_rather_than_reporting_a_failed_mount) {
    // Mounting a placed source into a list holding unplaced ones is a fault in
    // the caller, not an archive that failed to open. Reporting it as a missed
    // mount would hide it behind a best-effort result.
    TmpDir tmp("reone_test_mount_structural");
    _resources.addMemERF(erfBytes("shared", "unplaced"), ResourceOwner::Global);
    addDiskSource("placed", ModuleArchiveFamily::StaticRim, tmp.writeRim("foo_s.rim", "placed"));

    ModuleLoadPlan plan;
    plan.families.push_back(familyPlan(ModuleArchiveFamily::StaticRim,
                                       MountCardinality::FirstSuccessful,
                                       {attempt("placed", ModuleArchiveFamily::StaticRim)}));

    EXPECT_THROW(run(plan), ValidationException);
}

TEST_F(ModuleMountExecutorTest, treats_a_corrupt_archive_as_a_failed_load_rather_than_a_missing_source) {
    // Best effort means a source that is not there, not one that is there and
    // unreadable. The shared activated path mounts without a guard, so a corrupt
    // archive already aborts the module load; swallowing it here would make an
    // game quietly more tolerant than the other.
    TmpDir tmp("reone_test_mount_corrupt");
    tmp.write("foo_s.rim", ByteBuffer {'n', 'o', 't', ' ', 'a', ' ', 'r', 'i', 'm'});
    addDiskSource("corrupt", ModuleArchiveFamily::StaticRim, tmp.path / "foo_s.rim");

    ModuleLoadPlan plan;
    plan.families.push_back(familyPlan(ModuleArchiveFamily::StaticRim,
                                       MountCardinality::FirstSuccessful,
                                       {attempt("corrupt", ModuleArchiveFamily::StaticRim)}));

    EXPECT_THROW(run(plan), ValidationException);
}

TEST_F(ModuleMountExecutorTest, still_treats_an_absent_source_as_best_effort) {
    // The complement of the two throwing cases: absence is answered before any
    // archive is opened, so it never reaches the exception handling at all.
    TmpDir tmp("reone_test_mount_absent");
    addDiskSource("gone", ModuleArchiveFamily::Dialogue, tmp.path / "absent_dlg.erf");
    addStagedSource("unstaged", ModuleArchiveFamily::SavedArchive, ResourceId("nothing", ResType::Sav));

    ModuleLoadPlan plan;
    plan.families.push_back(familyPlan(ModuleArchiveFamily::Dialogue,
                                       MountCardinality::AllSuccessful,
                                       {attempt("gone", ModuleArchiveFamily::Dialogue),
                                        attempt("unstaged", ModuleArchiveFamily::SavedArchive)}));

    ModuleMountReport report;
    ASSERT_NO_THROW(report = run(plan));
    EXPECT_TRUE(mountedIds(report).empty());
    EXPECT_FALSE(report.requiredFailure);
}

TEST_F(ModuleMountExecutorTest, mounts_nothing_for_a_plan_without_a_primary) {
    ModuleLoadPlan plan;
    auto report = run(plan);
    EXPECT_TRUE(report.outcomes.empty());
    EXPECT_FALSE(report.requiredFailure);
    EXPECT_EQ(ModuleLoadOutcome::Failed, report.outcome);
}
