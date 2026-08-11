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
 * Characterization tests for resource lookup precedence and scoping.
 *
 * These tests pin down the behavior of the current container-based resource
 * stack (Resources + ResourceDirector), so that a future lookup implementation
 * can be verified against the same contract. They describe what the engine
 * does today, which is not necessarily what the original game does.
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "reone/graphics/options.h"
#include "reone/resource/director.h"
#include "reone/resource/exception/notfound.h"
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

namespace {

ByteBuffer bytes(std::string_view s) {
    return ByteBuffer(s.begin(), s.end());
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

void writeErf(const std::filesystem::path &path, ErfWriter::FileType fileType, const std::vector<NamedRes> &resources) {
    ErfWriter writer;
    for (const auto &res : resources) {
        writer.add(ErfWriter::Resource {res.resRef, res.type, bytes(res.data)});
    }
    writer.save(fileType, path);
}

void writeRim(const std::filesystem::path &path, const std::vector<NamedRes> &resources) {
    RimWriter writer;
    for (const auto &res : resources) {
        writer.add(RimWriter::Resource {res.resRef, res.type, bytes(res.data)});
    }
    writer.save(path);
}

void appendUint16(std::string &out, uint16_t val) {
    out.push_back(static_cast<char>(val & 0xff));
    out.push_back(static_cast<char>((val >> 8) & 0xff));
}

void appendUint32(std::string &out, uint32_t val) {
    out.push_back(static_cast<char>(val & 0xff));
    out.push_back(static_cast<char>((val >> 8) & 0xff));
    out.push_back(static_cast<char>((val >> 16) & 0xff));
    out.push_back(static_cast<char>((val >> 24) & 0xff));
}

void writeFile(const std::filesystem::path &path, const std::string &data) {
    FileOutputStream stream(path);
    stream.write(data.data(), data.size());
}

/// Write a minimal chitin.key plus a single BIF holding the given resources.
void writeKeyBif(const std::filesystem::path &dir, const std::vector<NamedRes> &resources) {
    static constexpr char kBifFilename[] = "sample.bif";

    auto count = static_cast<uint32_t>(resources.size());

    // BIF: header, variable resource table, payloads
    std::string bif("BIFFV1  ");
    appendUint32(bif, count);
    appendUint32(bif, 0);
    appendUint32(bif, 20);
    uint32_t dataOffset = 20 + 16 * count;
    for (uint32_t i = 0; i < count; ++i) {
        appendUint32(bif, i);
        appendUint32(bif, dataOffset);
        appendUint32(bif, static_cast<uint32_t>(resources[i].data.size()));
        appendUint32(bif, static_cast<uint32_t>(resources[i].type));
        dataOffset += static_cast<uint32_t>(resources[i].data.size());
    }
    for (const auto &res : resources) {
        bif += res.data;
    }
    writeFile(dir / kBifFilename, bif);

    // KEY: header, file table, filenames, key table
    uint32_t filenameLen = static_cast<uint32_t>(std::strlen(kBifFilename));
    uint32_t offFiles = 64;
    uint32_t offFilename = offFiles + 12;
    uint32_t offKeys = offFilename + filenameLen + 1;

    std::string key("KEY V1  ");
    appendUint32(key, 1);
    appendUint32(key, count);
    appendUint32(key, offFiles);
    appendUint32(key, offKeys);
    appendUint32(key, 0);
    appendUint32(key, 0);
    key.append(32, '\0');

    appendUint32(key, static_cast<uint32_t>(bif.size()));
    appendUint32(key, offFilename);
    appendUint16(key, static_cast<uint16_t>(filenameLen));
    appendUint16(key, 0);

    key += kBifFilename;
    key.push_back('\0');

    for (uint32_t i = 0; i < count; ++i) {
        std::string resRef(resources[i].resRef);
        resRef.resize(16, '\0');
        key += resRef;
        appendUint16(key, static_cast<uint16_t>(resources[i].type));
        appendUint32(key, i); // bifIdx 0, resIdx i
    }
    writeFile(dir / "chitin.key", key);
}

std::string dataOf(const std::optional<Resource> &res) {
    if (!res) {
        return "<not found>";
    }
    return std::string(res->data.begin(), res->data.end());
}

/// Temporary directory tree, removed on destruction.
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

// Resources facade: precedence is purely positional. Containers are pushed to
// the front of the list and lookup returns the first hit, so the container
// added last wins.

TEST(ResourcesLookup, should_prefer_last_added_container_for_colliding_id) {
    Resources resources;
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

TEST(ResourcesLookup, should_clear_containers_by_kind_only) {
    Resources resources;
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

TEST(ResourcesLookup, should_report_missing_resources) {
    Resources resources;

    EXPECT_FALSE(resources.find(ResourceId("missing", ResType::Txt)));
    EXPECT_THROW(resources.get(ResourceId("missing", ResType::Txt)), ResourceNotFoundException);
}

// ResourceDirector: the effective precedence of game data locations follows
// from the order in which the director mounts them.

class ResourceDirectorLookup : public testing::Test {
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

    Resources _resources;
    Resources _auxResources;
};

TEST_F(ResourceDirectorLookup, should_mount_global_locations_in_precedence_order) {
    TmpDir game("reone_test_lookup_global");

    // Working directory, from which the director loads shaderpack.erf.
    TmpDir cwd("reone_test_lookup_global_cwd");
    writeErf(cwd.path / "shaderpack.erf", ErfWriter::FileType::ERF,
             {{"a", ResType::Txt, "shaderpack"}});

    // Pairwise collisions between adjacent locations in mount order.
    writeKeyBif(game.path,
                {{"a", ResType::Txt, "chitin"},
                 {"b", ResType::Txt, "chitin"}});
    auto texPacks = game.mkdir("texturepacks");
    writeErf(texPacks / "swpc_tex_gui.erf", ErfWriter::FileType::ERF,
             {{"b", ResType::Txt, "gui pack"},
              {"c", ResType::Txt, "gui pack"}});
    writeErf(texPacks / "swpc_tex_tpa.erf", ErfWriter::FileType::ERF,
             {{"c", ResType::Txt, "texture pack"},
              {"d", ResType::Txt, "texture pack"}});
    writeErf(game.path / "patch.erf", ErfWriter::FileType::ERF,
             {{"d", ResType::Txt, "patch"},
              {"e", ResType::Txt, "patch"}});
    auto override_ = game.mkdir("override");
    writeFile(override_ / "e.txt", "override");

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

TEST_F(ResourceDirectorLookup, should_mount_module_locations_over_global_and_clear_on_transition) {
    TmpDir game("reone_test_lookup_module");
    TmpDir cwd("reone_test_lookup_module_cwd");
    writeErf(cwd.path / "shaderpack.erf", ErfWriter::FileType::ERF, {});

    auto override_ = game.mkdir("override");
    writeFile(override_ / "m.txt", "override");

    auto modules = game.mkdir("modules");
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

    // K1 is activated, so the buckets decide. A loose override file outranks
    // every module source, and the MOD branch suppresses the static image and
    // leaves the base image unmounted.
    EXPECT_EQ("override", find("m")) << "a loose override file outranks every module archive";
    EXPECT_FALSE(_resources.find(ResourceId("rim_only", ResType::Txt)))
        << "the MOD branch does not mount the base image";
    EXPECT_FALSE(_resources.find(ResourceId("rims_only", ResType::Txt)))
        << "the MOD branch suppresses the static image";

    director->onModuleLoad("bar");

    EXPECT_EQ("override", find("m")) << "previous module containers must be dropped on transition";
    EXPECT_FALSE(_resources.find(ResourceId("rim_only", ResType::Txt)));
}

TEST_F(ResourceDirectorLookup, should_keep_save_session_explicit_and_load_modules_from_working_state) {
    TmpDir game("reone_test_lookup_save");
    TmpDir cwd("reone_test_lookup_save_cwd");
    writeErf(cwd.path / "shaderpack.erf", ErfWriter::FileType::ERF, {});

    auto modules = game.mkdir("modules");
    writeRim(modules / "bar.rim",
             {{"bar_res", ResType::Txt, "disk rim"}});
    writeErf(modules / "bar.mod", ErfWriter::FileType::MOD,
             {{"bar_res", ResType::Txt, "disk mod"}});

    auto nestedModule = erfBytes(ErfWriter::FileType::MOD,
                                 {{"bar_res", ResType::Txt, "save module"}});

    auto slot = game.mkdir("saves/000001");
    writeFile(slot / "loose.txt", "save folder");
    writeErf(slot / "savegame.sav", ErfWriter::FileType::ERF,
             {{"loose", ResType::Txt, "save archive"},
              {"first_only", ResType::Txt, "first save"},
              {"bar", ResType::Sav, std::string(nestedModule.begin(), nestedModule.end())}});

    auto secondSlot = game.mkdir("saves/000002");
    writeFile(secondSlot / "loose.txt", "second folder");
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

    // Replacing the session retires resources held only by its predecessor.
    EXPECT_EQ("<not found>", dataOf(director->findSaveWorking(ResourceId("first_only", ResType::Txt))))
        << "the previous working state must not survive loading another";
}

TEST_F(ResourceDirectorLookup, should_throw_when_save_slot_is_missing) {
    TmpDir game("reone_test_lookup_missing_save");
    game.mkdir("saves");

    auto director = makeDirector(game.path);

    EXPECT_THROW(director->onGameLoad("nosuchslot"), ResourceNotFoundException);
}
