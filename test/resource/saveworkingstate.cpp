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
#include "reone/resource/format/rimwriter.h"
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

} // namespace

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

INSTANTIATE_TEST_SUITE_P(
    BackendsAndGames,
    SaveWorkingStateDirectorTest,
    testing::Values(
        SaveCase {Backend::Legacy, GameID::KotOR},
        SaveCase {Backend::Extract, GameID::KotOR},
        SaveCase {Backend::Legacy, GameID::TSL},
        SaveCase {Backend::Extract, GameID::TSL}),
    caseName);
