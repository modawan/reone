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
 * Activated K2 module loading, against real backends.
 *
 * These tests observe returned bytes rather than mount calls: a mount sequence
 * can look right and still resolve wrongly, because a source's priority comes
 * from its bucket and not from when it was mounted. Both backends must agree.
 *
 * The K1 characterization suites in lookup.cpp and extractresources.cpp
 * continue to describe the unactivated path, which this does not change.
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "reone/graphics/options.h"
#include "reone/resource/director.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/extractresources.h"
#include "reone/resource/format/erfwriter.h"
#include "reone/resource/format/rimwriter.h"
#include "reone/resource/resources.h"
#include "reone/system/stream/fileoutput.h"
#include "reone/system/stream/memoryoutput.h"

#include "../fixtures/graphics.h"
#include "../fixtures/resource.h"
#include "../fixtures/script.h"

using namespace reone;
using namespace reone::resource;

using testing::NiceMock;
using testing::Return;

namespace {

ByteBuffer bytes(std::string_view value) {
    return ByteBuffer(value.begin(), value.end());
}

struct NamedRes {
    std::string resRef;
    ResType type;
    std::string data;
};

ByteBuffer erfBytes(ErfWriter::FileType fileType, const std::vector<NamedRes> &resources) {
    ErfWriter writer;
    for (const auto &res : resources) {
        writer.add(ErfWriter::Resource {res.resRef, res.type, bytes(res.data)});
    }
    ByteBuffer buffer;
    MemoryOutputStream stream(buffer);
    writer.save(fileType, stream);
    return buffer;
}

ByteBuffer rimBytes(const std::vector<NamedRes> &resources) {
    RimWriter writer;
    for (const auto &res : resources) {
        writer.add(RimWriter::Resource {res.resRef, res.type, bytes(res.data)});
    }
    ByteBuffer buffer;
    MemoryOutputStream stream(buffer);
    writer.save(stream);
    return buffer;
}

void writeBuffer(const std::filesystem::path &path, const ByteBuffer &buffer) {
    FileOutputStream stream(path);
    if (!buffer.empty()) {
        stream.write(buffer.data(), buffer.size());
    }
}

void writeErf(const std::filesystem::path &path,
              ErfWriter::FileType fileType,
              const std::vector<NamedRes> &resources) {
    writeBuffer(path, erfBytes(fileType, resources));
}

void writeRim(const std::filesystem::path &path, const std::vector<NamedRes> &resources) {
    writeBuffer(path, rimBytes(resources));
}

void writeFile(const std::filesystem::path &path, const std::string &data) {
    FileOutputStream stream(path);
    stream.write(data.data(), data.size());
}

/// An archive as a resource payload, for nesting one inside another.
std::string blob(const ByteBuffer &buffer) {
    return std::string(buffer.begin(), buffer.end());
}

/// A module-save table listing one module and whether it may be saved.
std::shared_ptr<TwoDA> moduleSaveTable(const std::string &moduleRoot, const std::string &includeInSave) {
    return TwoDA::Builder()
        .columns({"modulename", "includeinsave"})
        .row(moduleRoot, {moduleRoot, includeInSave})
        .build();
}

std::string dataOf(const std::optional<Resource> &res) {
    if (!res) {
        return "<not found>";
    }
    return std::string(res->data.begin(), res->data.end());
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

    std::filesystem::path mkdir(const std::string &name) {
        auto dir = path / name;
        std::filesystem::create_directories(dir);
        return dir;
    }
};

/// ResourceDirector::init loads shaderpack.erf from the working directory.
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

std::string backendName(const testing::TestParamInfo<Backend> &info) {
    return info.param == Backend::Legacy ? "Legacy" : "Extract";
}

} // namespace

class K2ModuleLoadingTest : public testing::TestWithParam<Backend> {
protected:
    void SetUp() override {
        _graphics.init();
        _script.init();
        if (GetParam() == Backend::Legacy) {
            _resources = std::make_unique<Resources>();
            _auxResources = std::make_unique<Resources>();
        } else {
            _resources = std::make_unique<ExtractResources>();
            _auxResources = std::make_unique<ExtractResources>();
        }
    }

    /// A minimal installation. Every test needs a modules directory and a
    /// shader pack, because the director mounts both unconditionally.
    void makeInstallation(TmpDir &game, TmpDir &cwd) {
        writeErf(cwd.path / "shaderpack.erf", ErfWriter::FileType::ERF, {});
        game.mkdir("modules");
    }

    std::unique_ptr<ResourceDirector> makeDirector(const std::filesystem::path &gamePath) {
        return std::make_unique<ResourceDirector>(
            GameID::TSL,
            gamePath,
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

    std::string find(const std::string &resRef, ResType type = ResType::Txt) {
        return dataOf(_resources->find(ResourceId(resRef, type)));
    }

    bool has(const std::string &resRef, ResType type = ResType::Txt) {
        return static_cast<bool>(_resources->find(ResourceId(resRef, type)));
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

    std::unique_ptr<IResources> _resources;
    std::unique_ptr<IResources> _auxResources;
};

// Buckets, not mount order, decide the winner.

TEST_P(K2ModuleLoadingTest, resolves_global_and_module_sources_in_raw_lookup_order) {
    TmpDir game("reone_test_k2_order");
    TmpDir cwd("reone_test_k2_order_cwd");
    makeInstallation(game, cwd);

    auto override_ = game.mkdir("override");
    writeFile(override_ / "loose.txt", "override");
    writeErf(game.path / "patch.erf", ErfWriter::FileType::ERF,
             {{"loose", ResType::Txt, "patch"},
              {"class1", ResType::Txt, "patch"}});

    auto modules = game.path / "modules";
    writeRim(modules / "foo.rim",
             {{"loose", ResType::Txt, "module image"},
              {"class1", ResType::Txt, "module image"},
              {"image", ResType::Txt, "module image"}});
    writeErf(modules / "foo_dlg.erf", ErfWriter::FileType::ERF,
             {{"image", ResType::Txt, "module class 2"},
              {"class2", ResType::Txt, "module class 2"}});

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }
    director->onModuleLoad("foo");

    EXPECT_EQ("override", find("loose")) << "a loose directory outranks every archive";
    EXPECT_EQ("patch", find("class1")) << "class 1 outranks a module resource image";
    EXPECT_EQ("module image", find("image")) << "a resource image outranks class 2";
    EXPECT_EQ("module class 2", find("class2"));
}

TEST_P(K2ModuleLoadingTest, prefers_the_adx_image_then_the_area_image_over_the_module_archive) {
    TmpDir game("reone_test_k2_adjunct");
    TmpDir cwd("reone_test_k2_adjunct_cwd");
    makeInstallation(game, cwd);

    auto modules = game.path / "modules";
    writeErf(modules / "foo.mod", ErfWriter::FileType::MOD,
             {{"shared", ResType::Txt, "mod"},
              {"a_vs_mod", ResType::Txt, "mod"}});
    writeRim(modules / "foo_a.rim",
             {{"shared", ResType::Txt, "area image"},
              {"a_vs_mod", ResType::Txt, "area image"}});
    writeRim(modules / "foo_adx.rim", {{"shared", ResType::Txt, "adx image"}});

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }
    director->onModuleLoad("foo");

    EXPECT_EQ("adx image", find("shared")) << "_adx is mounted after _a inside the image bucket";
    EXPECT_EQ("area image", find("a_vs_mod")) << "an adjunct image outranks the class-2 module archive";
}

TEST_P(K2ModuleLoadingTest, suppresses_the_static_and_dialogue_sources_when_a_module_archive_exists) {
    TmpDir game("reone_test_k2_mod_branch");
    TmpDir cwd("reone_test_k2_mod_branch_cwd");
    makeInstallation(game, cwd);

    auto modules = game.path / "modules";
    writeErf(modules / "foo.mod", ErfWriter::FileType::MOD, {{"from_mod", ResType::Txt, "mod"}});
    writeRim(modules / "foo_s.rim", {{"from_static", ResType::Txt, "static"}});
    writeErf(modules / "foo_dlg.erf", ErfWriter::FileType::ERF, {{"from_dlg", ResType::Txt, "dialogue"}});

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }
    director->onModuleLoad("foo");

    EXPECT_EQ("mod", find("from_mod"));
    EXPECT_FALSE(has("from_static")) << "the MOD branch suppresses _s";
    EXPECT_FALSE(has("from_dlg")) << "the MOD branch suppresses _dlg";
}

TEST_P(K2ModuleLoadingTest, mounts_the_static_and_dialogue_sources_in_the_split_branch) {
    TmpDir game("reone_test_k2_split");
    TmpDir cwd("reone_test_k2_split_cwd");
    makeInstallation(game, cwd);

    auto modules = game.path / "modules";
    writeRim(modules / "foo.rim", {{"from_rim", ResType::Txt, "rim"}});
    writeRim(modules / "foo_s.rim",
             {{"from_static", ResType::Txt, "static"},
              {"shared", ResType::Txt, "static"}});
    writeErf(modules / "foo_dlg.erf", ErfWriter::FileType::ERF,
             {{"from_dlg", ResType::Txt, "dialogue"},
              {"shared", ResType::Txt, "dialogue"}});

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }
    director->onModuleLoad("foo");

    EXPECT_EQ("rim", find("from_rim"));
    EXPECT_EQ("static", find("from_static"));
    EXPECT_EQ("dialogue", find("from_dlg"));
    EXPECT_EQ("static", find("shared")) << "every image source outranks the dialogue archive";
}

TEST_P(K2ModuleLoadingTest, mounts_localization_from_the_module_and_lips_locations) {
    TmpDir game("reone_test_k2_loc");
    TmpDir cwd("reone_test_k2_loc_cwd");
    makeInstallation(game, cwd);

    auto modules = game.path / "modules";
    writeRim(modules / "foo.rim", {{"anything", ResType::Txt, "rim"}});
    writeErf(modules / "foo_loc.mod", ErfWriter::FileType::MOD,
             {{"from_module_loc", ResType::Txt, "module loc"},
              {"shared_loc", ResType::Txt, "module loc"}});
    auto lips = game.mkdir("lips");
    writeErf(lips / "foo_loc.mod", ErfWriter::FileType::MOD,
             {{"from_lips_loc", ResType::Txt, "lips loc"},
              {"shared_loc", ResType::Txt, "lips loc"}});

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }
    director->onModuleLoad("foo");

    EXPECT_EQ("module loc", find("from_module_loc"));
    EXPECT_EQ("lips loc", find("from_lips_loc"));
    EXPECT_EQ("lips loc", find("shared_loc")) << "the lips location is consulted after the module location";
}

// Saved state staged inside an archive already in scope.

TEST_P(K2ModuleLoadingTest, prefers_the_staged_image_over_the_staged_archive) {
    TmpDir game("reone_test_k2_staged");
    TmpDir cwd("reone_test_k2_staged_cwd");
    makeInstallation(game, cwd);

    auto modules = game.path / "modules";
    writeRim(modules / "foo.rim", {{"state", ResType::Txt, "disk rim"}});

    auto slot = game.mkdir("saves/000001");
    writeErf(slot / "savegame.sav", ErfWriter::FileType::ERF,
             {{"foo", ResType::Rsv, blob(rimBytes({{"state", ResType::Txt, "staged image"}}))},
              {"foo", ResType::Sav, blob(erfBytes(ErfWriter::FileType::MOD, {{"state", ResType::Txt, "staged archive"}}))}});

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }
    director->onGameLoad("000001");
    director->onModuleLoad("foo");

    // The image wins selection, so the active table takes its image form. The
    // archive existing alongside it must not turn the active table into class 2.
    EXPECT_EQ("staged image", find("state"));
}

TEST_P(K2ModuleLoadingTest, mounts_the_staged_archive_when_no_staged_image_exists) {
    TmpDir game("reone_test_k2_staged_archive");
    TmpDir cwd("reone_test_k2_staged_archive_cwd");
    makeInstallation(game, cwd);

    auto modules = game.path / "modules";
    writeErf(modules / "foo.mod", ErfWriter::FileType::MOD, {{"state", ResType::Txt, "disk mod"}});

    auto nested = erfBytes(ErfWriter::FileType::MOD, {{"state", ResType::Txt, "staged archive"}});
    auto slot = game.mkdir("saves/000001");
    writeErf(slot / "savegame.sav", ErfWriter::FileType::ERF,
             {{"foo", ResType::Sav, blob(nested)}});

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }
    director->onGameLoad("000001");
    director->onModuleLoad("foo");

    EXPECT_EQ("staged archive", find("state"))
        << "the active table is mounted last, so it wins its bucket against the disk archive";
}

TEST_P(K2ModuleLoadingTest, resolves_the_save_folder_above_the_outer_save_archive) {
    TmpDir game("reone_test_k2_save_scope");
    TmpDir cwd("reone_test_k2_save_scope_cwd");
    makeInstallation(game, cwd);

    auto slot = game.mkdir("saves/000001");
    writeFile(slot / "loose.txt", "save folder");
    writeErf(slot / "savegame.sav", ErfWriter::FileType::ERF,
             {{"loose", ResType::Txt, "save archive"},
              {"archive_only", ResType::Txt, "save archive"}});

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }
    director->onGameLoad("000001");

    // The save folder is a loose directory and the outer archive is not. This
    // reverses the unactivated order, where whichever was mounted last won.
    EXPECT_EQ("save folder", find("loose"));
    EXPECT_EQ("save archive", find("archive_only"))
        << "the outer archive must still supply what nothing else holds";
}

// Saved-state eligibility.

TEST_P(K2ModuleLoadingTest, ignores_staged_state_for_a_module_the_save_table_excludes) {
    TmpDir game("reone_test_k2_excluded");
    TmpDir cwd("reone_test_k2_excluded_cwd");
    makeInstallation(game, cwd);

    writeRim(game.path / "modules" / "foo.rim", {{"state", ResType::Txt, "disk rim"}});

    auto nested = erfBytes(ErfWriter::FileType::MOD, {{"state", ResType::Txt, "staged archive"}});
    auto slot = game.mkdir("saves/000001");
    writeErf(slot / "savegame.sav", ErfWriter::FileType::ERF,
             {{"foo", ResType::Sav, blob(nested)}});

    EXPECT_CALL(_twoDas, get("modulesave")).WillRepeatedly(Return(moduleSaveTable("foo", "0")));

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }
    director->onGameLoad("000001");
    director->onModuleLoad("foo");

    EXPECT_EQ("disk rim", find("state")) << "an excluded module may not be entered from saved state";
}

TEST_P(K2ModuleLoadingTest, includes_a_module_the_save_table_does_not_mention) {
    TmpDir game("reone_test_k2_absent_row");
    TmpDir cwd("reone_test_k2_absent_row_cwd");
    makeInstallation(game, cwd);

    writeRim(game.path / "modules" / "foo.rim", {{"state", ResType::Txt, "disk rim"}});

    auto nested = erfBytes(ErfWriter::FileType::MOD, {{"state", ResType::Txt, "staged archive"}});
    auto slot = game.mkdir("saves/000001");
    writeErf(slot / "savegame.sav", ErfWriter::FileType::ERF,
             {{"foo", ResType::Sav, blob(nested)}});

    // A table that lists other modules supplies no exclusion for this one.
    EXPECT_CALL(_twoDas, get("modulesave")).WillRepeatedly(Return(moduleSaveTable("bar", "0")));

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }
    director->onGameLoad("000001");
    director->onModuleLoad("foo");

    EXPECT_EQ("staged archive", find("state"));
}

// Module names.

TEST_P(K2ModuleLoadingTest, never_reports_a_sidecar_as_a_module) {
    TmpDir game("reone_test_k2_names");
    TmpDir cwd("reone_test_k2_names_cwd");
    makeInstallation(game, cwd);

    auto modules = game.path / "modules";
    writeRim(modules / "foo.rim", {});
    writeRim(modules / "foo_s.rim", {});
    writeRim(modules / "foo_a.rim", {});
    writeRim(modules / "foo_adx.rim", {});
    writeErf(modules / "foo_dlg.erf", ErfWriter::FileType::ERF, {});
    writeErf(modules / "bar.mod", ErfWriter::FileType::MOD, {});

    // The lips location holds global archives that are not modules, and is
    // searched for a known module's support archives only.
    auto lips = game.mkdir("lips");
    writeErf(lips / "global.mod", ErfWriter::FileType::MOD, {});
    writeErf(lips / "localization.mod", ErfWriter::FileType::MOD, {});
    writeErf(lips / "foo_loc.mod", ErfWriter::FileType::MOD, {});

    auto director = makeDirector(game.path);

    EXPECT_EQ((std::set<std::string> {"bar", "foo"}), director->moduleNames());
}

// Transitions.

TEST_P(K2ModuleLoadingTest, drops_the_previous_module_sources_on_a_transition) {
    TmpDir game("reone_test_k2_transition");
    TmpDir cwd("reone_test_k2_transition_cwd");
    makeInstallation(game, cwd);

    auto modules = game.path / "modules";
    writeRim(modules / "foo.rim", {{"shared", ResType::Txt, "foo"}, {"foo_only", ResType::Txt, "foo"}});
    writeRim(modules / "bar.rim", {{"shared", ResType::Txt, "bar"}});

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    director->onModuleLoad("foo");
    EXPECT_EQ("foo", find("shared"));

    director->onModuleLoad("bar");
    EXPECT_EQ("bar", find("shared"));
    EXPECT_FALSE(has("foo_only")) << "sources of the previous module must not survive a transition";

    // A module already visited must load identically the second time.
    director->onModuleLoad("foo");
    EXPECT_EQ("foo", find("shared"));
    EXPECT_TRUE(has("foo_only"));
}

// Sources that are not Odyssey game data.

TEST_P(K2ModuleLoadingTest, keeps_streamed_audio_out_of_the_odyssey_resource_list) {
    TmpDir game("reone_test_k2_streams");
    TmpDir cwd("reone_test_k2_streams_cwd");
    makeInstallation(game, cwd);

    auto music = game.mkdir("streammusic");
    writeFile(music / "track.wav", "streamed");
    auto override_ = game.mkdir("override");
    writeFile(override_ / "track.wav", "override");

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    // Streamed audio has no bucket, so it cannot be ranked against Odyssey
    // sources at all and does not appear in the game's resource list. Which of
    // the two a clip is read from is the streaming subsystem's decision, and is
    // covered by the AudioClips tests.
    EXPECT_EQ("override", find("track", ResType::Wav));
    EXPECT_EQ("streamed", dataOf(_auxResources->find(ResourceId("track", ResType::Wav))));
}

TEST_P(K2ModuleLoadingTest, mounts_every_odyssey_source_into_one_placed_order) {
    // A list is homogeneous: it rejects a mount that does not match the mode it
    // is in. A load that completes therefore proves no Odyssey source was left
    // unplaced, which no assertion about individual mount calls could show.
    TmpDir game("reone_test_k2_homogeneous");
    TmpDir cwd("reone_test_k2_homogeneous_cwd");
    makeInstallation(game, cwd);

    writeErf(game.path / "patch.erf", ErfWriter::FileType::ERF, {{"p", ResType::Txt, "patch"}});
    game.mkdir("override");
    game.mkdir("texturepacks");
    game.mkdir("streammusic");
    auto lips = game.mkdir("lips");
    writeErf(lips / "global.mod", ErfWriter::FileType::MOD, {{"g", ResType::Txt, "global lips"}});
    writeRim(game.path / "modules" / "foo.rim", {{"m", ResType::Txt, "module"}});

    auto slot = game.mkdir("saves/000001");
    writeErf(slot / "savegame.sav", ErfWriter::FileType::ERF, {{"s", ResType::Txt, "save"}});

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        EXPECT_NO_THROW(director->init());
    }
    EXPECT_NO_THROW(director->onGameLoad("000001"));
    EXPECT_NO_THROW(director->onModuleLoad("foo"));

    EXPECT_EQ("patch", find("p"));
    EXPECT_EQ("global lips", find("g"));
    EXPECT_EQ("module", find("m"));
    EXPECT_EQ("save", find("s"));
}

TEST_P(K2ModuleLoadingTest, admits_a_later_loose_directory_mounted_by_another_caller) {
    // The toolkit mounts the resources directory itself when there is no key
    // table. That lands in the same list the director just filled, so it has to
    // be placed the same way; leaving it unplaced would be rejected.
    TmpDir game("reone_test_k2_late_folder");
    TmpDir cwd("reone_test_k2_late_folder_cwd");
    makeInstallation(game, cwd);

    auto override_ = game.mkdir("override");
    writeFile(override_ / "shared.txt", "override");

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    auto loose = game.mkdir("loose");
    writeFile(loose / "shared.txt", "late folder");

    std::optional<ResourceSourceBucket> bucket;
    if (usesBucketedLookup(GameID::TSL)) {
        bucket = ResourceSourceBucket::LooseDirectory;
    }
    ASSERT_NO_THROW(_resources->addFolder(loose, ContainerKind::Global, bucket));

    EXPECT_EQ("late folder", find("shared")) << "the source added last wins inside its own bucket";
}

TEST(ResourceDirectorActivation, activates_only_the_game_whose_precedence_is_established) {
    EXPECT_TRUE(usesBucketedLookup(GameID::TSL));
    EXPECT_FALSE(usesBucketedLookup(GameID::KotOR));
}

INSTANTIATE_TEST_SUITE_P(Backends,
                         K2ModuleLoadingTest,
                         testing::Values(Backend::Legacy, Backend::Extract),
                         backendName);
