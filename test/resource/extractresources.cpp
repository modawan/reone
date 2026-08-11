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
 * The extract-backed IResources implementation must satisfy the same
 * observable contract as the legacy container stack. These tests mirror the
 * characterization suite in test/resource/lookup.cpp, run against
 * ExtractResources - including the behaviors that suite documents as
 * accidental, which a backend swap must not change.
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "reone/graphics/options.h"
#include "reone/resource/director.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/extractresources.h"
#include "reone/system/stream/memoryoutput.h"

#include "../fixtures/archive.h"
#include "../fixtures/graphics.h"
#include "../fixtures/resource.h"
#include "../fixtures/script.h"

using namespace reone;
using namespace reone::resource;
using namespace reone::test;

using testing::NiceMock;

namespace {

ByteBuffer erfBytes(ErfWriter::FileType fileType, const std::vector<ArchiveRes> &resources) {
    ErfWriter writer;
    for (const auto &res : resources) {
        writer.add(ErfWriter::Resource {res.resRef, res.type, toBytes(res.data)});
    }
    ByteBuffer buffer;
    MemoryOutputStream stream(buffer);
    writer.save(fileType, stream);
    return buffer;
}

ByteBuffer rimBytes(const std::vector<ArchiveRes> &resources) {
    RimWriter writer;
    for (const auto &res : resources) {
        writer.add(RimWriter::Resource {res.resRef, res.type, toBytes(res.data)});
    }
    ByteBuffer buffer;
    MemoryOutputStream stream(buffer);
    writer.save(stream);
    return buffer;
}

std::string dataOf(const std::optional<Resource> &res) {
    if (!res) {
        return "<not found>";
    }
    return std::string(res->data.begin(), res->data.end());
}

/// Temporarily change the working directory. ResourceDirector::init loads
/// shaderpack.erf from the working directory.
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

} // namespace

TEST(ExtractResourcesLookup, should_prefer_last_added_source_for_colliding_id) {
    ExtractResources resources;
    resources.addMemERF(erfBytes(ErfWriter::FileType::ERF,
                                 {{"shared", ResType::Txt, "first"},
                                  {"first_only", ResType::Txt, "from first"}}),
                        ResourceOwner::Global);
    resources.addMemERF(erfBytes(ErfWriter::FileType::ERF,
                                 {{"shared", ResType::Txt, "second"}}),
                        ResourceOwner::Global);

    EXPECT_EQ("second", dataOf(resources.find(ResourceId("shared", ResType::Txt))));
    EXPECT_EQ("from first", dataOf(resources.find(ResourceId("first_only", ResType::Txt))));
}

TEST(ExtractResourcesLookup, should_clear_sources_by_kind_only) {
    ExtractResources resources;
    resources.addMemERF(erfBytes(ErfWriter::FileType::ERF,
                                 {{"shared", ResType::Txt, "global"},
                                  {"global_only", ResType::Txt, "global"}}),
                        ResourceOwner::Global);
    resources.addMemERF(erfBytes(ErfWriter::FileType::ERF,
                                 {{"shared", ResType::Txt, "save"},
                                  {"save_only", ResType::Txt, "save"}}),
                        ResourceOwner::SaveSlot);
    resources.addMemERF(erfBytes(ErfWriter::FileType::ERF,
                                 {{"shared", ResType::Txt, "local"},
                                  {"local_only", ResType::Txt, "local"}}),
                        ResourceOwner::ActiveModule);

    EXPECT_EQ("local", dataOf(resources.find(ResourceId("shared", ResType::Txt))));

    resources.clearOwner(ResourceOwner::ActiveModule);
    EXPECT_EQ("save", dataOf(resources.find(ResourceId("shared", ResType::Txt))));
    EXPECT_FALSE(resources.find(ResourceId("local_only", ResType::Txt)));
    EXPECT_TRUE(resources.find(ResourceId("global_only", ResType::Txt)));

    resources.clearOwner(ResourceOwner::SaveSlot);
    EXPECT_EQ("global", dataOf(resources.find(ResourceId("shared", ResType::Txt))));
    EXPECT_FALSE(resources.find(ResourceId("save_only", ResType::Txt)));
    EXPECT_TRUE(resources.find(ResourceId("global_only", ResType::Txt)));
}

TEST(ExtractResourcesLookup, should_serve_memory_rim_archives) {
    ExtractResources resources;
    resources.addMemRIM(rimBytes({{"entry", ResType::Txt, "rim payload"}}), ResourceOwner::ActiveModule);

    EXPECT_EQ("rim payload", dataOf(resources.find(ResourceId("entry", ResType::Txt))));
    EXPECT_FALSE(resources.find(ResourceId("missing", ResType::Txt)));
}

TEST(ExtractResourcesLookup, should_report_missing_resources) {
    ExtractResources resources;

    EXPECT_FALSE(resources.find(ResourceId("missing", ResType::Txt)));
    EXPECT_THROW(resources.get(ResourceId("missing", ResType::Txt)), ResourceNotFoundException);
}

// The production director drives the extract-backed implementation exactly
// like the legacy one; observable precedence must be identical.

class ExtractResourcesDirectorLookup : public testing::Test {
protected:
    void SetUp() override {
        _graphics.init();
        _script.init();
    }

    std::unique_ptr<ResourceDirector> makeDirector(const std::filesystem::path &gamePath,
                                                   GameID gameId = GameID::KotOR) {
        return std::make_unique<ResourceDirector>(
            gameId,
            gamePath,
            _graphicsOpt,
            _graphics.services(),
            _script.services(),
            _dialogs,
            _gffs,
            _lips,
            _paths,
            _resources,
            _auxResources,
            _scripts,
            _twoDas);
    }

    std::string find(const std::string &resRef, ResType type = ResType::Txt) {
        return dataOf(_resources.find(ResourceId(resRef, type)));
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

    ExtractResources _resources;
    ExtractResources _auxResources;
};

TEST_F(ExtractResourcesDirectorLookup, should_mount_global_locations_in_precedence_order) {
    TmpDir game("reone_test_xres_global");
    TmpDir cwd("reone_test_xres_global_cwd");
    writeErf(cwd.path / "shaderpack.erf", ErfWriter::FileType::ERF,
             {{"a", ResType::Txt, "shaderpack"}});

    writeKeyBif(game.path, "sample.bif",
                {{"a", ResType::Txt, "chitin"},
                 {"b", ResType::Txt, "chitin"}});
    auto texPacks = game.path / "texturepacks";
    std::filesystem::create_directories(texPacks);
    writeErf(texPacks / "swpc_tex_gui.erf", ErfWriter::FileType::ERF,
             {{"b", ResType::Txt, "gui pack"},
              {"c", ResType::Txt, "gui pack"}});
    writeErf(texPacks / "swpc_tex_tpa.erf", ErfWriter::FileType::ERF,
             {{"c", ResType::Txt, "texture pack"},
              {"d", ResType::Txt, "texture pack"}});
    writeErf(game.path / "patch.erf", ErfWriter::FileType::ERF,
             {{"d", ResType::Txt, "patch"},
              {"e", ResType::Txt, "patch"}});
    auto override_ = game.path / "override";
    std::filesystem::create_directories(override_);
    detail::writeFile(override_ / "e.txt", "override");

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    EXPECT_EQ("gui pack", find("b")) << "GUI texture pack must beat chitin";
    EXPECT_EQ("texture pack", find("c")) << "quality texture pack must beat GUI pack";
    EXPECT_EQ("patch", find("d")) << "patch.erf must beat texture packs";
    EXPECT_EQ("override", find("e")) << "override must beat patch.erf";

    // The shader pack no longer competes with game data at all: it holds only
    // GLSL, so it is kept out of the game's resource list entirely. This used
    // to assert that chitin outranked it.
    EXPECT_EQ("chitin", find("a"));
    EXPECT_EQ("shaderpack", dataOf(_auxResources.find(ResourceId("a", ResType::Txt))));
}

TEST_F(ExtractResourcesDirectorLookup, should_mount_module_locations_over_global_and_clear_on_transition) {
    TmpDir game("reone_test_xres_module");
    TmpDir cwd("reone_test_xres_module_cwd");
    writeErf(cwd.path / "shaderpack.erf", ErfWriter::FileType::ERF, {});

    auto override_ = game.path / "override";
    std::filesystem::create_directories(override_);
    detail::writeFile(override_ / "m.txt", "override");

    auto modules = game.path / "modules";
    std::filesystem::create_directories(modules);
    writeRim(modules / "foo.rim",
             {{"m", ResType::Txt, "main rim"},
              {"rim_only", ResType::Txt, "main rim"}});
    writeRim(modules / "foo_s.rim",
             {{"m", ResType::Txt, "data rim"},
              {"rims_only", ResType::Txt, "data rim"}});
    writeErf(modules / "foo.mod", ErfWriter::FileType::MOD,
             {{"m", ResType::Txt, "mod"}});
    writeRim(modules / "bar.rim",
             {{"m", ResType::Txt, "bar rim"}});

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    director->onModuleLoad("foo");

    // K1 is activated, so buckets decide. A loose override file outranks every
    // module archive, and the MOD branch leaves the base image unmounted and
    // suppresses the static image.
    EXPECT_EQ("override", find("m")) << "a loose override file outranks every module archive";
    EXPECT_FALSE(_resources.find(ResourceId("rim_only", ResType::Txt)))
        << "the MOD branch does not mount the base image";
    EXPECT_FALSE(_resources.find(ResourceId("rims_only", ResType::Txt)))
        << "the MOD branch suppresses the static image";

    director->onModuleLoad("bar");

    EXPECT_EQ("override", find("m")) << "previous module sources must be dropped on transition";
    EXPECT_FALSE(_resources.find(ResourceId("rim_only", ResType::Txt)));
}

TEST_F(ExtractResourcesDirectorLookup, should_keep_save_session_explicit_and_load_modules_from_working_state) {
    TmpDir game("reone_test_xres_save");
    TmpDir cwd("reone_test_xres_save_cwd");
    writeErf(cwd.path / "shaderpack.erf", ErfWriter::FileType::ERF, {});

    auto modules = game.path / "modules";
    std::filesystem::create_directories(modules);
    writeRim(modules / "bar.rim",
             {{"bar_res", ResType::Txt, "disk rim"}});
    writeErf(modules / "bar.mod", ErfWriter::FileType::MOD,
             {{"bar_res", ResType::Txt, "disk mod"}});

    auto nestedModule = erfBytes(ErfWriter::FileType::MOD,
                                 {{"bar_res", ResType::Txt, "save module"}});

    auto slot = game.path / "saves" / "000001";
    std::filesystem::create_directories(slot);
    detail::writeFile(slot / "loose.txt", "save folder");
    writeErf(slot / "savegame.sav", ErfWriter::FileType::ERF,
             {{"loose", ResType::Txt, "save archive"},
              {"first_only", ResType::Txt, "first save"},
              {"bar", ResType::Sav, std::string(nestedModule.begin(), nestedModule.end())}});

    auto secondSlot = game.path / "saves" / "000002";
    std::filesystem::create_directories(secondSlot);
    detail::writeFile(secondSlot / "loose.txt", "second folder");
    writeErf(secondSlot / "savegame.sav", ErfWriter::FileType::ERF,
             {{"loose", ResType::Txt, "second save"}});

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    director->onGameLoad("000001");

    // Loose slot metadata and archive working state have distinct explicit routes.
    EXPECT_EQ("save folder", dataOf(director->findSaveMetadata(ResourceId("loose", ResType::Txt))));

    director->onModuleLoad("bar");

    EXPECT_EQ("save module", find("bar_res")) << "module archive nested in savegame.sav must beat disk modules";

    director->onGameLoad("000002");

    EXPECT_EQ("second folder", dataOf(director->findSaveMetadata(ResourceId("loose", ResType::Txt))));

    EXPECT_EQ("<not found>", dataOf(director->findSaveWorking(ResourceId("first_only", ResType::Txt))))
        << "the previous working state must not survive loading another";
}

TEST_F(ExtractResourcesDirectorLookup, should_throw_when_save_slot_is_missing) {
    TmpDir game("reone_test_xres_missing_save");
    std::filesystem::create_directories(game.path / "saves");

    auto director = makeDirector(game.path);

    EXPECT_THROW(director->onGameLoad("nosuchslot"), ResourceNotFoundException);
}
