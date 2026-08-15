/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "reone/resource/director.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/extractresources.h"
#include "reone/resource/format/erfwriter.h"
#include "reone/resource/format/gffwriter.h"
#include "reone/resource/format/rimwriter.h"
#include "reone/resource/gff.h"
#include "reone/resource/resources.h"
#include "reone/system/exception/validation.h"
#include "reone/system/stream/fileoutput.h"
#include "reone/system/stream/memoryoutput.h"

#include "../fixtures/graphics.h"
#include "../fixtures/resource.h"
#include "../fixtures/script.h"

using namespace reone;
using namespace reone::resource;
using testing::NiceMock;

namespace {

ByteBuffer bytes(std::string_view value) {
    return ByteBuffer(value.begin(), value.end());
}

struct NamedRes {
    std::string resRef;
    ResType type;
    std::string data;
};

ByteBuffer erfBytes(ErfWriter::FileType type, const std::vector<NamedRes> &resources) {
    ErfWriter writer;
    for (const auto &resource : resources) {
        writer.add(ErfWriter::Resource {resource.resRef, resource.type, bytes(resource.data)});
    }
    ByteBuffer buffer;
    MemoryOutputStream stream(buffer);
    writer.save(type, stream);
    return buffer;
}

ByteBuffer rimBytes(const std::vector<NamedRes> &resources) {
    RimWriter writer;
    for (const auto &resource : resources) {
        writer.add(RimWriter::Resource {resource.resRef, resource.type, bytes(resource.data)});
    }
    ByteBuffer buffer;
    MemoryOutputStream stream(buffer);
    writer.save(stream);
    return buffer;
}

void writeBuffer(const std::filesystem::path &path, const ByteBuffer &buffer) {
    std::filesystem::create_directories(path.parent_path());
    FileOutputStream stream(path);
    stream.write(buffer.data(), buffer.size());
}

void writeFile(const std::filesystem::path &path, std::string_view data) {
    writeBuffer(path, bytes(data));
}

void writeErf(const std::filesystem::path &path,
              ErfWriter::FileType type,
              const std::vector<NamedRes> &resources) {
    writeBuffer(path, erfBytes(type, resources));
}

void writeRim(const std::filesystem::path &path, const std::vector<NamedRes> &resources) {
    writeBuffer(path, rimBytes(resources));
}

std::string blob(const ByteBuffer &buffer) {
    return std::string(buffer.begin(), buffer.end());
}

std::string dataOf(const std::optional<Resource> &resource) {
    if (!resource) {
        return "<not found>";
    }
    return std::string(resource->data.begin(), resource->data.end());
}

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

    std::filesystem::path mkdir(const std::filesystem::path &name) {
        auto directory = path / name;
        std::filesystem::create_directories(directory);
        return directory;
    }
};

struct CwdGuard {
    std::filesystem::path previous;

    explicit CwdGuard(const std::filesystem::path &path) {
        previous = std::filesystem::current_path();
        std::filesystem::current_path(path);
    }

    ~CwdGuard() {
        std::error_code ec;
        std::filesystem::current_path(previous, ec);
    }
};

enum class Backend {
    Legacy,
    Extract,
};

struct SaveCase {
    Backend backend;
    GameID game;
};

std::string caseName(const testing::TestParamInfo<SaveCase> &info) {
    std::string backend = info.param.backend == Backend::Legacy ? "Legacy" : "Extract";
    std::string game = info.param.game == GameID::KotOR ? "K1" : "K2";
    return backend + game;
}

void writeSlot(TmpDir &game,
               const std::string &name,
               const std::vector<NamedRes> &working) {
    auto slot = game.mkdir(std::filesystem::path("saves") / name);
    writeErf(slot / "savegame.sav", ErfWriter::FileType::MOD, working);
}

std::shared_ptr<SaveWorkingState> makeWorkingState(
    TmpDir &directory,
    const std::vector<NamedRes> &resources) {
    auto path = directory.path / "savegame.sav";
    writeErf(path, ErfWriter::FileType::MOD, resources);
    return std::make_shared<SaveWorkingState>(path);
}

std::string dataOf(const std::optional<SaveResourceView> &view) {
    if (!view) {
        return "<not found>";
    }
    return dataOf(view->read());
}

void writeUint32At(ByteBuffer &buffer, size_t offset, uint32_t value) {
    ASSERT_LE(offset + sizeof(value), buffer.size());
    for (size_t i = 0; i < sizeof(value); ++i) {
        buffer[offset + i] = static_cast<char>((value >> (i * 8)) & 0xff);
    }
}

} // namespace

TEST(SaveWorkingStateBackingTest, source_archive_can_move_and_delete_while_reads_stay_lazy) {
    TmpDir directory("reone_e3f0_detached_archive");
    auto source = directory.path / "SAVEGAME.sav";
    auto moved = directory.path / "old-savegame.sav";
    auto module = erfBytes(
        ErfWriter::FileType::MOD,
        {{"module", ResType::Ifo, "ifo"},
         {"area", ResType::Are, "are"},
         {"area", ResType::Git, "git"}});
    writeErf(
        source,
        ErfWriter::FileType::MOD,
        {{"inventory", ResType::Res, "inventory bytes"},
         {"module_a", ResType::Sav, blob(module)},
         {"pc", ResType::Utc, "player bytes"}});
    auto sourceSize = std::filesystem::file_size(source);

    SaveWorkingState state(source);
    EXPECT_EQ(sourceSize, std::filesystem::file_size(source));
    std::filesystem::rename(source, moved);

    EXPECT_EQ("inventory bytes", dataOf(state.find(ResourceId("inventory", ResType::Res))));
    EXPECT_EQ(blob(module), dataOf(state.find(ResourceId("module_a", ResType::Sav))));
    EXPECT_EQ("player bytes", dataOf(state.find(ResourceId("pc", ResType::Utc))));

    std::filesystem::remove(moved);
    EXPECT_EQ("inventory bytes", dataOf(state.find(ResourceId("inventory", ResType::Res))));
    EXPECT_EQ(blob(module), dataOf(state.find(ResourceId("module_a", ResType::Sav))));
    EXPECT_EQ("player bytes", dataOf(state.find(ResourceId("pc", ResType::Utc))));
}

TEST(SaveWorkingStateBackingTest, entire_loaded_slot_can_move_and_delete_while_session_stays_alive) {
    TmpDir directory("reone_e3f0_detached_slot");
    auto target = directory.mkdir("slot");
    auto backup = directory.path / "backup";
    writeFile(target / "GLOBALVARS.res", "globals");
    writeFile(target / "PARTYTABLE.res", "party");
    writeFile(target / "savenfo.res", "nfo");
    writeErf(
        target / "SAVEGAME.sav",
        ErfWriter::FileType::MOD,
        {{"inventory", ResType::Res, "inventory"},
         {"module_a", ResType::Sav, "module"},
         {"availnpc0", ResType::Utc, "npc"}});
    SaveSlotDescriptor descriptor {target, target / "SAVEGAME.sav"};
    SaveSessionState session(descriptor);
    EXPECT_EQ("globals", dataOf(session.findMetadata(ResourceId("globalvars", ResType::Res))));

    std::filesystem::rename(target, backup);
    EXPECT_EQ("inventory", dataOf(session.findWorking(ResourceId("inventory", ResType::Res))));
    EXPECT_EQ("module", dataOf(session.findWorking(ResourceId("module_a", ResType::Sav))));
    EXPECT_EQ("npc", dataOf(session.findWorking(ResourceId("availnpc0", ResType::Utc))));
    EXPECT_EQ(descriptor.directory, session.slot().directory);
    EXPECT_EQ(descriptor.archive, session.slot().archive);

    std::filesystem::remove_all(backup);
    EXPECT_EQ("inventory", dataOf(session.findWorking(ResourceId("inventory", ResType::Res))));
    EXPECT_EQ("module", dataOf(session.findWorking(ResourceId("module_a", ResType::Sav))));
    EXPECT_EQ("npc", dataOf(session.findWorking(ResourceId("availnpc0", ResType::Utc))));
}

TEST(SaveWorkingStateBackingTest, candidate_and_frozen_overlay_share_detached_base_after_slot_deletion) {
    TmpDir directory("reone_e3f0_detached_candidate");
    auto target = directory.mkdir("slot");
    auto backup = directory.path / "backup";
    writeErf(
        target / "SAVEGAME.sav",
        ErfWriter::FileType::MOD,
        {{"unchanged", ResType::Txt, "base"},
         {"replaced", ResType::Txt, "old"},
         {"deleted", ResType::Txt, "delete"}});
    auto base = std::make_shared<SaveWorkingState>(target / "SAVEGAME.sav");
    auto candidate = SaveWorkingStateCandidate::fromCommitted(base);
    auto borrowed = candidate.find(ResourceId("unchanged", ResType::Txt));
    ASSERT_TRUE(borrowed);
    EXPECT_EQ(SaveResourceOrigin::Borrowed, borrowed->origin());
    candidate.put(ResourceId("replaced", ResType::Txt), bytes("new"));
    candidate.erase(ResourceId("deleted", ResType::Txt));
    auto frozen = candidate.freeze();

    std::filesystem::rename(target, backup);
    std::filesystem::remove_all(backup);
    base.reset();

    EXPECT_EQ("base", dataOf(borrowed));
    EXPECT_EQ("base", dataOf(candidate.find(ResourceId("unchanged", ResType::Txt))));
    EXPECT_EQ("new", dataOf(candidate.find(ResourceId("replaced", ResType::Txt))));
    EXPECT_FALSE(candidate.find(ResourceId("deleted", ResType::Txt)));
    EXPECT_EQ("base", dataOf(frozen->find(ResourceId("unchanged", ResType::Txt))));
    EXPECT_EQ("new", dataOf(frozen->find(ResourceId("replaced", ResType::Txt))));
    EXPECT_FALSE(frozen->find(ResourceId("deleted", ResType::Txt)));
}

TEST(SaveWorkingStateBackingTest, malformed_archives_fail_without_retaining_slot_handles) {
    TmpDir directory("reone_e3f0_detached_failure");
    std::vector<ByteBuffer> malformed;
    malformed.push_back(bytes("not an archive"));

    auto impossibleTable = erfBytes(
        ErfWriter::FileType::MOD,
        {{"inventory", ResType::Res, "inventory"}});
    writeUint32At(impossibleTable, 24, 0xfffffff0);
    malformed.push_back(std::move(impossibleTable));

    auto truncated = erfBytes(
        ErfWriter::FileType::MOD,
        {{"inventory", ResType::Res, "inventory"}});
    truncated.pop_back();
    malformed.push_back(std::move(truncated));

    for (size_t i = 0; i < malformed.size(); ++i) {
        auto slot = directory.mkdir("slot" + std::to_string(i));
        auto backup = directory.path / ("backup" + std::to_string(i));
        writeBuffer(slot / "SAVEGAME.sav", malformed[i]);
        EXPECT_THROW(SaveWorkingState(slot / "SAVEGAME.sav"), ValidationException);
        EXPECT_NO_THROW(std::filesystem::rename(slot, backup));
        EXPECT_NO_THROW(std::filesystem::remove_all(backup));
    }
}

TEST(SaveWorkingStateCandidateTest, empty_candidate_over_empty_base) {
    TmpDir directory("reone_e3a_empty");
    auto base = makeWorkingState(directory, {});
    auto candidate = SaveWorkingStateCandidate::fromCommitted(base);

    EXPECT_TRUE(candidate.deterministicResourceIds().empty());
    EXPECT_TRUE(candidate.validate());
}

TEST(SaveWorkingStateCandidateTest, accepts_exact_generated_gff_bytebuffer) {
    TmpDir directory("reone_e3b_gff_candidate");
    auto base = makeWorkingState(directory, {});
    auto candidate = SaveWorkingStateCandidate::fromCommitted(base);
    ResourceId id("globalvars", ResType::Res);
    Gff root(0, {Gff::Field::newInt("Synthetic", 42)});
    auto generated = GffWriter(GffFileFormat::v32("GVT "), root).toBytes();

    candidate.put(id, generated);

    auto view = candidate.find(id);
    ASSERT_TRUE(view);
    ASSERT_TRUE(view->read());
    EXPECT_EQ(generated, view->read()->data);
    EXPECT_EQ(SaveResourceOrigin::Owned, view->origin());
}

TEST(SaveWorkingStateCandidateTest, accepts_exact_generated_mod_bytebuffer) {
    TmpDir directory("reone_e3b_mod_candidate");
    auto base = makeWorkingState(directory, {});
    auto candidate = SaveWorkingStateCandidate::fromCommitted(base);
    ResourceId id("module", ResType::Sav);
    ErfWriter writer;
    writer.add({"module", ResType::Ifo, bytes("synthetic ifo")});
    auto generated = writer.toBytes(ErfWriter::FileType::MOD);

    candidate.put(id, generated);

    auto view = candidate.find(id);
    ASSERT_TRUE(view);
    ASSERT_TRUE(view->read());
    EXPECT_EQ(generated, view->read()->data);
    EXPECT_EQ(SaveResourceOrigin::Owned, view->origin());
}

TEST(SaveWorkingStateCandidateTest, writer_failure_does_not_mutate_base_or_candidate) {
    TmpDir directory("reone_e3b_failure_candidate");
    ResourceId existing("globalvars", ResType::Res);
    auto base = makeWorkingState(directory, {{"globalvars", ResType::Res, "committed"}});
    auto candidate = SaveWorkingStateCandidate::fromCommitted(base);

    ErfWriter invalid;
    invalid.add({"same", ResType::Res, bytes("first")});
    invalid.add({"SAME", ResType::Res, bytes("second")});
    EXPECT_THROW(invalid.toBytes(ErfWriter::FileType::MOD), ValidationException);

    auto view = candidate.find(existing);
    ASSERT_TRUE(view);
    EXPECT_EQ(SaveResourceOrigin::Borrowed, view->origin());
    EXPECT_EQ("committed", dataOf(view));
    EXPECT_EQ("committed", dataOf(base->find(existing)));
    EXPECT_EQ(std::vector<ResourceId> {existing}, candidate.deterministicResourceIds());
}

TEST(SaveWorkingStateCandidateTest, erf_writer_consumes_a_borrowed_candidate_view_once) {
    TmpDir directory("reone_e3b_lazy_candidate");
    ResourceId sourceId("globalvars", ResType::Res);
    auto base = makeWorkingState(directory, {{"globalvars", ResType::Res, "borrowed bytes"}});
    auto candidate = SaveWorkingStateCandidate::fromCommitted(base);
    auto source = candidate.find(sourceId);
    ASSERT_TRUE(source);
    ASSERT_EQ(SaveResourceOrigin::Borrowed, source->origin());
    int reads = 0;
    ErfWriter writer;
    writer.add(ErfWriter::Resource::lazy(
        sourceId,
        [source = *source, &reads]() {
            ++reads;
            auto resource = source.read();
            if (!resource) {
                throw std::logic_error("candidate view disappeared");
            }
            return resource->data;
        }));

    auto archive = writer.toBytes(ErfWriter::FileType::MOD);

    EXPECT_EQ(1, reads);
    ErfResourceContainer container(Storage(std::move(archive)));
    container.init();
    auto packaged = container.findResourceData(sourceId);
    ASSERT_TRUE(packaged);
    EXPECT_EQ(bytes("borrowed bytes"), *packaged);
}

TEST(SaveWorkingStateCandidateTest, base_resource_lookup_is_borrowed_and_lazy) {
    TmpDir directory("reone_e3a_base_lookup");
    ResourceId id("base", ResType::Txt);
    auto base = makeWorkingState(directory, {{"base", ResType::Txt, "original"}});
    auto candidate = SaveWorkingStateCandidate::fromCommitted(base);

    auto view = candidate.find(id);
    ASSERT_TRUE(view);
    EXPECT_EQ(SaveResourceOrigin::Borrowed, view->origin());
    EXPECT_EQ("original", dataOf(view));
}

TEST(SaveWorkingStateCandidateTest, owned_replacement_wins_without_mutating_base) {
    TmpDir directory("reone_e3a_replace");
    ResourceId id("state", ResType::Res);
    auto base = makeWorkingState(directory, {{"state", ResType::Res, "old"}});
    auto candidate = SaveWorkingStateCandidate::fromCommitted(base);

    candidate.put(id, bytes("new"));

    auto view = candidate.find(id);
    ASSERT_TRUE(view);
    EXPECT_EQ(SaveResourceOrigin::Owned, view->origin());
    EXPECT_EQ("new", dataOf(view));
    EXPECT_EQ("old", dataOf(base->find(id)));
}

TEST(SaveWorkingStateCandidateTest, exact_deletion_tombstone_hides_only_candidate_entry) {
    TmpDir directory("reone_e3a_tombstone");
    ResourceId deleted("same", ResType::Txt);
    ResourceId retained("same", ResType::Res);
    auto base = makeWorkingState(
        directory,
        {{"same", ResType::Txt, "delete"}, {"same", ResType::Res, "retain"}});
    auto candidate = SaveWorkingStateCandidate::fromCommitted(base);

    candidate.erase(deleted);

    EXPECT_FALSE(candidate.find(deleted));
    EXPECT_FALSE(candidate.contains(deleted));
    EXPECT_EQ("retain", dataOf(candidate.find(retained)));
    EXPECT_EQ("delete", dataOf(base->find(deleted)));
    auto ids = candidate.deterministicResourceIds();
    EXPECT_EQ(ids.end(), std::find(ids.begin(), ids.end(), deleted));
}

TEST(SaveWorkingStateCandidateTest, replacement_after_tombstone_restores_resource) {
    TmpDir directory("reone_e3a_replace_after_tombstone");
    ResourceId id("state", ResType::Txt);
    auto base = makeWorkingState(directory, {{"state", ResType::Txt, "old"}});
    auto candidate = SaveWorkingStateCandidate::fromCommitted(base);

    candidate.erase(id);
    candidate.put(id, bytes("new"));

    EXPECT_TRUE(candidate.contains(id));
    EXPECT_EQ("new", dataOf(candidate.find(id)));
}

TEST(SaveWorkingStateCandidateTest, tombstone_after_replacement_removes_resource) {
    TmpDir directory("reone_e3a_tombstone_after_replace");
    ResourceId id("state", ResType::Txt);
    auto base = makeWorkingState(directory, {{"state", ResType::Txt, "old"}});
    auto candidate = SaveWorkingStateCandidate::fromCommitted(base);

    candidate.put(id, bytes("new"));
    candidate.erase(id);

    EXPECT_FALSE(candidate.contains(id));
    EXPECT_FALSE(candidate.find(id));
    EXPECT_EQ("old", dataOf(base->find(id)));
}

TEST(SaveWorkingStateCandidateTest, deterministic_enumeration_ignores_mutation_order) {
    TmpDir directory("reone_e3a_deterministic");
    auto base = makeWorkingState(directory, {{"middle", ResType::Txt, "m"}});
    auto first = SaveWorkingStateCandidate::fromCommitted(base);
    auto second = SaveWorkingStateCandidate::fromCommitted(base);

    first.put(ResourceId("zulu", ResType::Res), bytes("z"));
    first.put(ResourceId("alpha", ResType::Txt), bytes("a"));
    second.put(ResourceId("alpha", ResType::Txt), bytes("a"));
    second.put(ResourceId("zulu", ResType::Res), bytes("z"));

    auto firstIds = first.deterministicResourceIds();
    auto secondIds = second.deterministicResourceIds();
    EXPECT_EQ(firstIds, secondIds);
    EXPECT_TRUE(std::is_sorted(firstIds.begin(), firstIds.end()));
}

TEST(SaveWorkingStateCandidateTest, repeated_put_is_last_write_wins_without_duplicate_id) {
    TmpDir directory("reone_e3a_repeated_put");
    ResourceId id("state", ResType::Txt);
    auto base = makeWorkingState(directory, {});
    auto candidate = SaveWorkingStateCandidate::fromCommitted(base);

    candidate.put(id, bytes("first"));
    candidate.put(id, bytes("second"));

    EXPECT_EQ("second", dataOf(candidate.find(id)));
    EXPECT_EQ(1u, candidate.deterministicResourceIds().size());
    EXPECT_TRUE(candidate.validate());
}

TEST(SaveWorkingStateCandidateTest, owned_replacement_outlives_input_and_candidate) {
    TmpDir directory("reone_e3a_owned_lifetime");
    ResourceId id("state", ResType::Txt);
    auto base = makeWorkingState(directory, {});
    std::optional<SaveResourceView> retainedView;
    {
        auto candidate = SaveWorkingStateCandidate::fromCommitted(base);
        auto payload = bytes("owned");
        candidate.put(id, std::move(payload));
        retainedView = candidate.find(id);
    }

    ASSERT_TRUE(retainedView);
    EXPECT_EQ(SaveResourceOrigin::Owned, retainedView->origin());
    EXPECT_EQ("owned", dataOf(retainedView));
}

TEST(SaveWorkingStateCandidateTest, candidate_retains_detached_base_lifetime) {
    TmpDir directory("reone_e3a_base_lifetime");
    ResourceId id("state", ResType::Txt);
    auto base = makeWorkingState(directory, {{"state", ResType::Txt, "lazy"}});
    std::weak_ptr<const SaveWorkingState> lifetime = base;
    {
        auto candidate = SaveWorkingStateCandidate::fromCommitted(base);
        base.reset();

        EXPECT_FALSE(lifetime.expired());
        auto view = candidate.find(id);
        ASSERT_TRUE(view);
        EXPECT_EQ(SaveResourceOrigin::Borrowed, view->origin());
        EXPECT_EQ("lazy", dataOf(view));
    }
    EXPECT_TRUE(lifetime.expired());
}

TEST(SaveWorkingStateCandidateTest, borrowed_view_retains_backing_after_candidate_destruction) {
    TmpDir directory("reone_e3a_borrowed_view_lifetime");
    ResourceId id("state", ResType::Txt);
    auto base = makeWorkingState(directory, {{"state", ResType::Txt, "lazy"}});
    std::weak_ptr<const SaveWorkingState> lifetime = base;
    std::optional<SaveResourceView> retainedView;
    {
        auto candidate = SaveWorkingStateCandidate::fromCommitted(base);
        retainedView = candidate.find(id);
        base.reset();
    }

    EXPECT_FALSE(lifetime.expired());
    ASSERT_TRUE(retainedView);
    EXPECT_EQ(SaveResourceOrigin::Borrowed, retainedView->origin());
    EXPECT_EQ("lazy", dataOf(retainedView));
    retainedView.reset();
    EXPECT_TRUE(lifetime.expired());
}

TEST(SaveWorkingStateCandidateTest, failed_discarded_candidate_needs_no_base_rollback) {
    TmpDir directory("reone_e3a_failed_candidate");
    ResourceId id("state", ResType::Txt);
    auto base = makeWorkingState(directory, {{"state", ResType::Txt, "old"}});
    {
        auto candidate = SaveWorkingStateCandidate::fromCommitted(base);
        candidate.put(id, bytes("uncommitted"));
        auto validation = candidate.validate([](
                                                 const SaveWorkingStateCandidate &,
                                                 SaveWorkingStateCandidateValidation &result) {
            result.addError("forced later-stage validation failure");
        });
        EXPECT_FALSE(validation);
    }

    EXPECT_EQ("old", dataOf(base->find(id)));
    EXPECT_TRUE(base->contains(id));
}

TEST(SaveWorkingStateCandidateTest, frozen_overlay_isolated_from_later_candidates) {
    TmpDir directory("reone_e3a_freeze_isolation");
    ResourceId id("state", ResType::Txt);
    auto base = makeWorkingState(directory, {{"state", ResType::Txt, "base"}});
    auto first = SaveWorkingStateCandidate::fromCommitted(base);
    first.put(id, bytes("frozen"));
    auto frozen = first.freeze();

    first.put(id, bytes("mutated"));
    auto second = SaveWorkingStateCandidate::fromCommitted(base);
    second.put(id, bytes("other"));
    base.reset();

    EXPECT_EQ("frozen", dataOf(frozen->find(id)));
    EXPECT_EQ("mutated", dataOf(first.find(id)));
    EXPECT_EQ("other", dataOf(second.find(id)));
}

TEST(SaveWorkingStateCandidateTest, frozen_overlay_retains_unchanged_base_backing) {
    TmpDir directory("reone_e3a_frozen_backing");
    ResourceId unchanged("unchanged", ResType::Txt);
    ResourceId added("added", ResType::Res);
    auto base = makeWorkingState(directory, {{"unchanged", ResType::Txt, "lazy"}});
    auto candidate = SaveWorkingStateCandidate::fromCommitted(base);
    candidate.put(added, bytes("owned"));
    auto frozen = candidate.freeze();
    base.reset();

    EXPECT_EQ("lazy", dataOf(frozen->find(unchanged)));
    EXPECT_EQ("owned", dataOf(frozen->find(added)));
}

TEST(SaveWorkingStateCandidateTest, module_replacement_retains_inactive_neighbors_lazily) {
    TmpDir directory("reone_e3a_module_retention");
    ResourceId aSav("module_a", ResType::Sav);
    ResourceId bSav("module_b", ResType::Sav);
    ResourceId bRsv("module_b", ResType::Rsv);
    ResourceId cSav("module_c", ResType::Sav);
    ResourceId otherRsv("other", ResType::Rsv);
    auto base = makeWorkingState(
        directory,
        {{"module_a", ResType::Sav, "A"},
         {"module_b", ResType::Sav, "old B"},
         {"module_b", ResType::Rsv, "stale B image"},
         {"module_c", ResType::Sav, "C"},
         {"other", ResType::Rsv, "other image"}});
    auto candidate = SaveWorkingStateCandidate::fromCommitted(base);

    candidate.replaceModule(bSav, bytes("new B"), bRsv);

    auto a = candidate.find(aSav);
    auto b = candidate.find(bSav);
    auto c = candidate.find(cSav);
    auto other = candidate.find(otherRsv);
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    ASSERT_TRUE(c);
    ASSERT_TRUE(other);
    EXPECT_EQ(SaveResourceOrigin::Borrowed, a->origin());
    EXPECT_EQ(SaveResourceOrigin::Owned, b->origin());
    EXPECT_EQ(SaveResourceOrigin::Borrowed, c->origin());
    EXPECT_EQ(SaveResourceOrigin::Borrowed, other->origin());
    EXPECT_EQ("A", dataOf(a));
    EXPECT_EQ("new B", dataOf(b));
    EXPECT_EQ("C", dataOf(c));
    EXPECT_FALSE(candidate.find(bRsv));
    EXPECT_EQ("other image", dataOf(other));
    EXPECT_EQ("old B", dataOf(base->find(bSav)));
    EXPECT_EQ("stale B image", dataOf(base->find(bRsv)));
    EXPECT_TRUE(candidate.validate());
}

TEST(SaveWorkingStateCandidateTest, module_replacement_requires_explicit_matching_ids) {
    TmpDir directory("reone_e3a_module_identity");
    auto base = makeWorkingState(directory, {});
    auto candidate = SaveWorkingStateCandidate::fromCommitted(base);

    EXPECT_THROW(
        candidate.replaceModule(
            ResourceId("module", ResType::Txt),
            bytes("bad")),
        std::invalid_argument);
    EXPECT_THROW(
        candidate.replaceModule(
            ResourceId("module", ResType::Sav),
            bytes("bad"),
            ResourceId("different", ResType::Rsv)),
        std::invalid_argument);
    EXPECT_TRUE(candidate.deterministicResourceIds().empty());
}

TEST(SaveWorkingStateCandidateTest, validation_detects_broken_recorded_module_replacement) {
    TmpDir directory("reone_e3a_module_validation");
    ResourceId sav("module", ResType::Sav);
    ResourceId rsv("module", ResType::Rsv);
    auto base = makeWorkingState(
        directory,
        {{"module", ResType::Sav, "old"}, {"module", ResType::Rsv, "image"}});
    auto candidate = SaveWorkingStateCandidate::fromCommitted(base);
    candidate.replaceModule(sav, bytes("new"), rsv);
    candidate.erase(sav);

    EXPECT_FALSE(candidate.validate());
    EXPECT_THROW(candidate.freeze(), ValidationException);
}

TEST(SaveWorkingStateCandidateTest, base_and_candidate_support_alternating_synchronous_reads) {
    TmpDir directory("reone_e3a_synchronous_reads");
    ResourceId unchanged("unchanged", ResType::Txt);
    ResourceId replaced("replaced", ResType::Txt);
    auto base = makeWorkingState(
        directory,
        {{"unchanged", ResType::Txt, "base U"},
         {"replaced", ResType::Txt, "base R"}});
    auto candidate = SaveWorkingStateCandidate::fromCommitted(base);
    candidate.put(replaced, bytes("candidate R"));

    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ("base U", dataOf(base->find(unchanged)));
        EXPECT_EQ("candidate R", dataOf(candidate.find(replaced)));
        EXPECT_EQ("base R", dataOf(base->find(replaced)));
        EXPECT_EQ("base U", dataOf(candidate.find(unchanged)));
    }
}

class SaveWorkingStateDirectorTest : public testing::TestWithParam<SaveCase> {
protected:
    void SetUp() override {
        _graphics.init();
        _script.init();
        if (GetParam().backend == Backend::Legacy) {
            auto resources = std::make_unique<Resources>();
            _sourceCount = [raw = resources.get()]() { return raw->containers().size(); };
            _resources = std::move(resources);
            _auxResources = std::make_unique<Resources>();
        } else {
            auto resources = std::make_unique<ExtractResources>();
            _sourceCount = [raw = resources.get()]() { return raw->sourceCount(); };
            _resources = std::move(resources);
            _auxResources = std::make_unique<ExtractResources>();
        }
    }

    void makeInstallation(TmpDir &game, TmpDir &cwd) {
        writeErf(cwd.path / "shaderpack.erf", ErfWriter::FileType::ERF, {});
        game.mkdir("modules");
    }

    std::unique_ptr<ResourceDirector> makeDirector(const std::filesystem::path &gamePath) {
        _gamePath = gamePath;
        return std::make_unique<ResourceDirector>(
            GetParam().game,
            _gamePath,
            _graphicsOpt,
            _graphics.services(),
            _script.services(),
            _dialogs,
            _gffs,
            _lips,
            _paths,
            *_resources,
            *_auxResources,
            _scripts,
            _twoDas);
    }

    void init(ResourceDirector &director, const TmpDir &cwd) {
        CwdGuard guard(cwd.path);
        director.init();
    }

    std::string metadata(ResourceDirector &director,
                         const std::string &resRef,
                         ResType type = ResType::Txt) {
        return dataOf(director.findSaveMetadata(ResourceId(resRef, type)));
    }

    std::string working(ResourceDirector &director,
                        const std::string &resRef,
                        ResType type = ResType::Txt) {
        return dataOf(director.findSaveWorking(ResourceId(resRef, type)));
    }

    std::string raw(const std::string &resRef, ResType type = ResType::Txt) {
        return dataOf(_resources->find(ResourceId(resRef, type)));
    }

    graphics::GraphicsOptions _graphicsOpt;
    graphics::TestGraphicsModule _graphics;
    script::TestScriptModule _script;
    NiceMock<MockDialogs> _dialogs;
    NiceMock<MockGffs> _gffs;
    NiceMock<MockLips> _lips;
    NiceMock<MockPaths> _paths;
    NiceMock<MockScripts> _scripts;
    NiceMock<MockTwoDAs> _twoDas;
    std::filesystem::path _gamePath;
    std::unique_ptr<IResources> _resources;
    std::unique_ptr<IResources> _auxResources;
    std::function<std::size_t()> _sourceCount;
};

TEST_P(SaveWorkingStateDirectorTest, replaces_complete_working_state_and_round_trips_a_b_a) {
    TmpDir game("reone_e1_replace");
    TmpDir cwd("reone_e1_replace_cwd");
    makeInstallation(game, cwd);
    writeSlot(game, "slot_a",
              {{"common", ResType::Txt, "a"}, {"a_only", ResType::Txt, "a"}});
    writeSlot(game, "slot_b",
              {{"common", ResType::Txt, "b"}, {"b_only", ResType::Txt, "b"}});

    auto director = makeDirector(game.path);
    init(*director, cwd);

    director->onGameLoad("slot_a");
    auto firstA = director->saveWorkingResourceIds();
    EXPECT_EQ("a", working(*director, "common"));
    EXPECT_EQ("a", working(*director, "a_only"));

    director->onGameLoad("slot_b");
    EXPECT_EQ("b", working(*director, "common"));
    EXPECT_EQ("<not found>", working(*director, "a_only"));
    EXPECT_EQ("b", working(*director, "b_only"));

    director->onGameLoad("slot_a");
    EXPECT_EQ(firstA, director->saveWorkingResourceIds());
    EXPECT_EQ("a", working(*director, "common"));
    EXPECT_EQ("a", working(*director, "a_only"));
    EXPECT_EQ("<not found>", working(*director, "b_only"));
}

TEST_P(SaveWorkingStateDirectorTest, missing_archive_before_commit_leaves_a_active) {
    TmpDir game("reone_e1_missing");
    TmpDir cwd("reone_e1_missing_cwd");
    makeInstallation(game, cwd);
    writeSlot(game, "slot_a", {{"a_only", ResType::Txt, "a"}});
    auto broken = game.mkdir("saves/slot_b");
    writeFile(broken / "b_only.txt", "b");

    auto director = makeDirector(game.path);
    init(*director, cwd);
    director->onGameLoad("slot_a");

    EXPECT_THROW(director->onGameLoad("slot_b"), ResourceNotFoundException);
    EXPECT_EQ("a", working(*director, "a_only"));
    EXPECT_EQ("<not found>", metadata(*director, "b_only"));
}

TEST_P(SaveWorkingStateDirectorTest, malformed_archive_before_commit_leaves_a_active) {
    TmpDir game("reone_e1_malformed");
    TmpDir cwd("reone_e1_malformed_cwd");
    makeInstallation(game, cwd);
    writeSlot(game, "slot_a", {{"a_only", ResType::Txt, "a"}});
    auto broken = game.mkdir("saves/slot_b");
    writeFile(broken / "b_only.txt", "b");
    writeFile(broken / "savegame.sav", "not an archive");

    auto director = makeDirector(game.path);
    init(*director, cwd);
    director->onGameLoad("slot_a");

    EXPECT_THROW(director->onGameLoad("slot_b"), ValidationException);
    EXPECT_EQ("a", working(*director, "a_only"));
    EXPECT_EQ("<not found>", metadata(*director, "b_only"));
}

TEST_P(SaveWorkingStateDirectorTest, truncated_payload_before_commit_leaves_a_active) {
    TmpDir game("reone_e1_truncated");
    TmpDir cwd("reone_e1_truncated_cwd");
    makeInstallation(game, cwd);
    writeSlot(game, "slot_a", {{"a_only", ResType::Txt, "a"}});
    writeSlot(game, "slot_b", {{"b_only", ResType::Txt, "payload"}});
    auto broken = game.path / "saves" / "slot_b" / "savegame.sav";
    std::filesystem::resize_file(broken, std::filesystem::file_size(broken) - 1);

    auto director = makeDirector(game.path);
    init(*director, cwd);
    director->onGameLoad("slot_a");

    EXPECT_THROW(director->onGameLoad("slot_b"), ValidationException);
    EXPECT_EQ("a", working(*director, "a_only"));
    EXPECT_EQ("<not found>", working(*director, "b_only"));
}

TEST_P(SaveWorkingStateDirectorTest, loose_metadata_is_authoritative_over_outer_collisions) {
    TmpDir game("reone_e1_metadata");
    TmpDir cwd("reone_e1_metadata_cwd");
    makeInstallation(game, cwd);
    auto slot = game.mkdir("saves/slot_a");
    writeFile(slot / "savenfo.res", "loose nfo");
    writeFile(slot / "globalvars.res", "loose globals");
    writeFile(slot / "partytable.res", "loose party");
    writeErf(slot / "savegame.sav", ErfWriter::FileType::MOD,
             {{"savenfo", ResType::Res, "outer nfo"},
              {"globalvars", ResType::Res, "outer globals"},
              {"partytable", ResType::Res, "outer party"}});

    auto director = makeDirector(game.path);
    init(*director, cwd);
    director->onGameLoad("slot_a");

    EXPECT_EQ("loose nfo", metadata(*director, "savenfo", ResType::Res));
    EXPECT_EQ("loose globals", metadata(*director, "globalvars", ResType::Res));
    EXPECT_EQ("loose party", metadata(*director, "partytable", ResType::Res));
    EXPECT_EQ("outer nfo", working(*director, "savenfo", ResType::Res));
}

TEST_P(SaveWorkingStateDirectorTest, inventory_and_available_npc_use_working_state) {
    TmpDir game("reone_e1_consumers");
    TmpDir cwd("reone_e1_consumers_cwd");
    makeInstallation(game, cwd);
    writeSlot(game, "slot_a",
              {{"inventory", ResType::Res, "inventory"},
               {"availnpc5", ResType::Utc, "npc"}});

    auto director = makeDirector(game.path);
    init(*director, cwd);
    director->onGameLoad("slot_a");

    EXPECT_EQ("inventory", working(*director, "inventory", ResType::Res));
    EXPECT_EQ("npc", working(*director, "availnpc5", ResType::Utc));
    EXPECT_EQ("<not found>", metadata(*director, "inventory", ResType::Res));
}

TEST_P(SaveWorkingStateDirectorTest, current_commit_not_raw_source_recency_decides_working_reads) {
    TmpDir game("reone_e1_recency");
    TmpDir cwd("reone_e1_recency_cwd");
    makeInstallation(game, cwd);
    writeSlot(game, "slot_a", {{"common", ResType::Txt, "a"}});
    writeSlot(game, "slot_b", {{"common", ResType::Txt, "b"}});

    auto director = makeDirector(game.path);
    init(*director, cwd);
    director->onGameLoad("slot_a");
    director->onGameLoad("slot_b");

    _resources->addMemERF(
        erfBytes(ErfWriter::FileType::ERF, {{"common", ResType::Txt, "stale raw"}}),
        ResourceOwner::Global,
        ResourceSourceBucket::EncapsulatedClass2);

    EXPECT_EQ("stale raw", raw("common"));
    EXPECT_EQ("b", working(*director, "common"));
}

TEST_P(SaveWorkingStateDirectorTest, durable_outer_archive_is_not_a_raw_compatibility_source) {
    TmpDir game("reone_e1_not_mounted");
    TmpDir cwd("reone_e1_not_mounted_cwd");
    makeInstallation(game, cwd);
    writeSlot(game, "slot_a", {{"working_only", ResType::Txt, "state"}});

    auto director = makeDirector(game.path);
    init(*director, cwd);
    auto before = _sourceCount();
    director->onGameLoad("slot_a");

    EXPECT_EQ(before, _sourceCount());
    EXPECT_EQ("<not found>", raw("working_only"));
    EXPECT_EQ("state", working(*director, "working_only"));
}

TEST_P(SaveWorkingStateDirectorTest, saved_resource_image_wins_saved_archive) {
    TmpDir game("reone_e1_rsv");
    TmpDir cwd("reone_e1_rsv_cwd");
    makeInstallation(game, cwd);
    writeRim(game.path / "modules" / "foo.rim", {{"state", ResType::Txt, "disk"}});
    writeSlot(game, "slot_a",
              {{"foo", ResType::Rsv, blob(rimBytes({{"state", ResType::Txt, "image"}}))},
               {"foo", ResType::Sav,
                blob(erfBytes(ErfWriter::FileType::MOD,
                              {{"state", ResType::Txt, "archive"}}))}});

    auto director = makeDirector(game.path);
    init(*director, cwd);
    director->onGameLoad("slot_a");
    director->onModuleLoad("foo");

    EXPECT_EQ("image", raw("state"));
}

TEST_P(SaveWorkingStateDirectorTest, saved_archive_works_without_saved_resource_image) {
    TmpDir game("reone_e1_sav");
    TmpDir cwd("reone_e1_sav_cwd");
    makeInstallation(game, cwd);
    writeRim(game.path / "modules" / "foo.rim", {{"state", ResType::Txt, "disk"}});
    writeSlot(game, "slot_a",
              {{"foo", ResType::Sav,
                blob(erfBytes(ErfWriter::FileType::MOD,
                              {{"state", ResType::Txt, "archive"}}))}});

    auto director = makeDirector(game.path);
    init(*director, cwd);
    director->onGameLoad("slot_a");
    director->onModuleLoad("foo");

    EXPECT_EQ("archive", raw("state"));
}

TEST_P(SaveWorkingStateDirectorTest, no_saved_primary_falls_through_to_normal_loader) {
    TmpDir game("reone_e1_fallthrough");
    TmpDir cwd("reone_e1_fallthrough_cwd");
    makeInstallation(game, cwd);
    writeRim(game.path / "modules" / "foo.rim", {{"state", ResType::Txt, "disk"}});
    writeSlot(game, "slot_a", {{"unrelated", ResType::Txt, "state"}});

    auto director = makeDirector(game.path);
    init(*director, cwd);
    director->onGameLoad("slot_a");
    director->onModuleLoad("foo");

    EXPECT_EQ("disk", raw("state"));
}

TEST_P(SaveWorkingStateDirectorTest, module_transitions_do_not_destroy_whole_working_state) {
    TmpDir game("reone_e1_module_transition");
    TmpDir cwd("reone_e1_module_transition_cwd");
    makeInstallation(game, cwd);
    writeRim(game.path / "modules" / "a.rim", {{"state", ResType::Txt, "disk a"}});
    writeRim(game.path / "modules" / "b.rim", {{"state", ResType::Txt, "disk b"}});
    writeSlot(game, "slot_a",
              {{"session_only", ResType::Txt, "session"},
               {"a", ResType::Sav,
                blob(erfBytes(ErfWriter::FileType::MOD,
                              {{"state", ResType::Txt, "saved a"}}))}});

    auto director = makeDirector(game.path);
    init(*director, cwd);
    director->onGameLoad("slot_a");
    director->onModuleLoad("a");
    EXPECT_EQ("saved a", raw("state"));

    director->onModuleLoad("b");
    EXPECT_EQ("disk b", raw("state"));
    EXPECT_EQ("session", working(*director, "session_only"));

    director->onModuleLoad("a");
    EXPECT_EQ("saved a", raw("state"));
}

TEST_P(SaveWorkingStateDirectorTest, save_commit_preserves_unrelated_global_and_separate_active_state) {
    TmpDir game("reone_e1_lifetimes");
    TmpDir cwd("reone_e1_lifetimes_cwd");
    makeInstallation(game, cwd);
    auto overridePath = game.mkdir("override");
    writeFile(overridePath / "global_only.txt", "global");
    writeRim(game.path / "modules" / "foo.rim", {{"active_only", ResType::Txt, "active"}});
    writeSlot(game, "slot_a", {{"a_only", ResType::Txt, "a"}});
    writeSlot(game, "slot_b", {{"b_only", ResType::Txt, "b"}});

    auto director = makeDirector(game.path);
    init(*director, cwd);
    director->onGameLoad("slot_a");
    director->onModuleLoad("foo");
    ASSERT_EQ("active", raw("active_only"));

    director->onGameLoad("slot_b");

    EXPECT_EQ("global", raw("global_only"));
    EXPECT_EQ("active", raw("active_only"));
    EXPECT_EQ("<not found>", working(*director, "a_only"));
    EXPECT_EQ("b", working(*director, "b_only"));
}

TEST_P(SaveWorkingStateDirectorTest, enumeration_contains_working_state_not_loose_metadata) {
    TmpDir game("reone_e1_enumeration");
    TmpDir cwd("reone_e1_enumeration_cwd");
    makeInstallation(game, cwd);
    auto slot = game.mkdir("saves/slot_a");
    writeFile(slot / "savenfo.res", "metadata");
    writeErf(slot / "savegame.sav", ErfWriter::FileType::MOD,
             {{"inventory", ResType::Res, "inventory"},
              {"foo", ResType::Sav, "module"}});

    auto director = makeDirector(game.path);
    init(*director, cwd);
    director->onGameLoad("slot_a");
    auto ids = director->saveWorkingResourceIds();

    EXPECT_EQ(2u, ids.size());
    EXPECT_NE(ids.end(), ids.find(ResourceId("inventory", ResType::Res)));
    EXPECT_NE(ids.end(), ids.find(ResourceId("foo", ResType::Sav)));
    EXPECT_EQ(ids.end(), ids.find(ResourceId("savenfo", ResType::Res)));
}

TEST_P(SaveWorkingStateDirectorTest, loaded_session_keeps_no_rename_blocking_slot_handle) {
    TmpDir game("reone_e3f0_director_detached");
    TmpDir cwd("reone_e3f0_director_detached_cwd");
    makeInstallation(game, cwd);
    auto slot = game.mkdir("saves/slot_a");
    auto backup = game.path / "saves" / "slot_a_backup";
    writeFile(slot / "GLOBALVARS.res", "globals");
    writeFile(slot / "PARTYTABLE.res", "party");
    writeFile(slot / "savenfo.res", "nfo");
    writeSlot(
        game,
        "slot_a",
        {{"inventory", ResType::Res, "inventory"},
         {"module_a", ResType::Sav, "module"},
         {"availnpc0", ResType::Utc, "npc"}});
    auto director = makeDirector(game.path);
    init(*director, cwd);
    director->onGameLoad("slot_a");
    EXPECT_EQ("globals", metadata(*director, "globalvars", ResType::Res));

    std::filesystem::rename(slot, backup);
    EXPECT_EQ("inventory", working(*director, "inventory", ResType::Res));
    EXPECT_EQ("module", working(*director, "module_a", ResType::Sav));
    EXPECT_EQ("npc", working(*director, "availnpc0", ResType::Utc));

    std::filesystem::remove_all(backup);
    EXPECT_EQ("inventory", working(*director, "inventory", ResType::Res));
    EXPECT_EQ("module", working(*director, "module_a", ResType::Sav));
    EXPECT_EQ("npc", working(*director, "availnpc0", ResType::Utc));
}

INSTANTIATE_TEST_SUITE_P(
    BackendsAndGames,
    SaveWorkingStateDirectorTest,
    testing::Values(
        SaveCase {Backend::Legacy, GameID::KotOR},
        SaveCase {Backend::Extract, GameID::KotOR},
        SaveCase {Backend::Legacy, GameID::TSL},
        SaveCase {Backend::Extract, GameID::TSL}),
    caseName);
