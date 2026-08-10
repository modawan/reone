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
 * When a mounted source goes away, and what happens when putting one in place
 * fails partway through.
 *
 * These observe lookups rather than list positions: a source that has been
 * retired must stop answering, and one that has not must keep answering
 * exactly as before. Ownership and lookup order are separate throughout, so
 * clearing an owner is never allowed to change which of the survivors wins.
 *
 * Both backends run everything, because a lifetime rule that holds for only
 * one of them is not a rule.
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "reone/resource/director.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/extractresources.h"
#include "reone/resource/format/erfwriter.h"
#include "reone/resource/format/rimwriter.h"
#include "reone/resource/mounttransaction.h"
#include "reone/resource/provider/gffs.h"
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
    std::filesystem::create_directories(path.parent_path());
    FileOutputStream stream(path);
    stream.write(buffer.data(), buffer.size());
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
    std::filesystem::create_directories(path.parent_path());
    FileOutputStream stream(path);
    stream.write(data.data(), data.size());
}

std::string blob(const ByteBuffer &buffer) {
    return std::string(buffer.begin(), buffer.end());
}

/// A GFF with no fields. Enough to parse, which is all a cache-identity check
/// needs: what is compared is whether the record was parsed again, not what it
/// says.
std::string emptyGff() {
    std::string data("GFF V3.2");
    data.append(48, '\0');
    return data;
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

enum class Backend { Legacy, Extract };

std::string backendName(const testing::TestParamInfo<Backend> &info) {
    return info.param == Backend::Legacy ? "Legacy" : "Extract";
}

} // namespace

/// Owner semantics on a bare backend, with no director deciding anything.
class SourceOwnerTest : public testing::TestWithParam<Backend> {
protected:
    void SetUp() override {
        if (GetParam() == Backend::Legacy) {
            auto backend = std::make_unique<Resources>();
            _count = [raw = backend.get()]() { return raw->containers().size(); };
            _resources = std::move(backend);
        } else {
            auto backend = std::make_unique<ExtractResources>();
            _count = [raw = backend.get()]() { return raw->sourceCount(); };
            _resources = std::move(backend);
        }
    }

    /// One source holding a resource of its own plus the shared probe, so both
    /// "did it go away" and "who wins now" are observable.
    void mount(const std::string &label, ResourceOwner owner) {
        _resources->addMemERF(erfBytes(ErfWriter::FileType::ERF,
                                       {{"shared", ResType::Txt, label},
                                        {label + "_only", ResType::Txt, label}}),
                              owner);
    }

    std::string find(const std::string &resRef) {
        return dataOf(_resources->find(ResourceId(resRef, ResType::Txt)));
    }

    bool has(const std::string &resRef) {
        return static_cast<bool>(_resources->find(ResourceId(resRef, ResType::Txt)));
    }

    std::unique_ptr<IResources> _resources;
    std::function<std::size_t()> _count;
};

TEST_P(SourceOwnerTest, retires_only_the_owner_asked_for) {
    mount("global", ResourceOwner::Global);
    mount("save", ResourceOwner::SaveSlot);
    mount("module", ResourceOwner::ActiveModule);
    mount("state", ResourceOwner::ActiveModuleState);
    mount("temp", ResourceOwner::TemporaryDiscovery);

    EXPECT_TRUE(has("global_only"));
    EXPECT_TRUE(has("save_only"));
    EXPECT_TRUE(has("module_only"));
    EXPECT_TRUE(has("state_only"));
    EXPECT_TRUE(has("temp_only"));

    _resources->clearOwner(ResourceOwner::TemporaryDiscovery);
    EXPECT_FALSE(has("temp_only"));
    EXPECT_TRUE(has("global_only"));
    EXPECT_TRUE(has("save_only"));
    EXPECT_TRUE(has("module_only"));
    EXPECT_TRUE(has("state_only"));

    _resources->clearOwner(ResourceOwner::ActiveModuleState);
    EXPECT_FALSE(has("state_only"));
    EXPECT_TRUE(has("module_only")) << "the module's state is not one of its support sources";
    EXPECT_TRUE(has("save_only"));
    EXPECT_TRUE(has("global_only"));

    _resources->clearOwner(ResourceOwner::ActiveModule);
    EXPECT_FALSE(has("module_only"));
    EXPECT_TRUE(has("save_only")) << "a module transition does not unload the save";
    EXPECT_TRUE(has("global_only"));

    _resources->clearOwner(ResourceOwner::SaveSlot);
    EXPECT_FALSE(has("save_only"));
    EXPECT_TRUE(has("global_only")) << "unloading a save does not rebuild the installation";

    _resources->clearOwner(ResourceOwner::Global);
    EXPECT_FALSE(has("global_only"));
    EXPECT_EQ(0u, _count());
}

TEST_P(SourceOwnerTest, retiring_an_owner_does_not_reorder_the_survivors) {
    mount("global", ResourceOwner::Global);
    mount("module", ResourceOwner::ActiveModule);
    mount("save", ResourceOwner::SaveSlot);

    // The save was mounted last, so it wins. Ownership had no part in that and
    // must have no part in what wins after the module is retired either.
    EXPECT_EQ("save", find("shared"));

    _resources->clearOwner(ResourceOwner::ActiveModule);
    EXPECT_EQ("save", find("shared"));

    _resources->clearOwner(ResourceOwner::SaveSlot);
    EXPECT_EQ("global", find("shared"));
}

TEST_P(SourceOwnerTest, retiring_an_owner_with_no_members_changes_nothing) {
    mount("global", ResourceOwner::Global);

    _resources->clearOwner(ResourceOwner::SaveSlot);
    _resources->clearOwner(ResourceOwner::ActiveModuleState);
    _resources->clearOwner(ResourceOwner::TemporaryDiscovery);

    EXPECT_EQ("global", find("shared"));
    EXPECT_EQ(1u, _count());
}

// Undoing an operation.

TEST_P(SourceOwnerTest, rolls_back_exactly_what_was_mounted_after_the_token) {
    mount("global", ResourceOwner::Global);
    auto token = _resources->mountToken();
    mount("module", ResourceOwner::ActiveModule);
    mount("state", ResourceOwner::ActiveModuleState);

    _resources->rollbackTo(token);

    EXPECT_FALSE(has("module_only"));
    EXPECT_FALSE(has("state_only")) << "rollback spans every owner the operation touched";
    EXPECT_EQ("global", find("shared"));
    EXPECT_EQ(1u, _count());
}

TEST_P(SourceOwnerTest, a_scope_that_is_not_committed_undoes_itself) {
    mount("global", ResourceOwner::Global);
    {
        ResourceMountTransaction transaction(*_resources);
        mount("module", ResourceOwner::ActiveModule);
        EXPECT_TRUE(has("module_only"));
    }

    EXPECT_FALSE(has("module_only"));
    EXPECT_EQ("global", find("shared"));
}

TEST_P(SourceOwnerTest, a_committed_scope_keeps_what_it_mounted) {
    mount("global", ResourceOwner::Global);
    {
        ResourceMountTransaction transaction(*_resources);
        mount("module", ResourceOwner::ActiveModule);
        transaction.commit();
    }

    EXPECT_TRUE(has("module_only"));
    EXPECT_EQ("module", find("shared"));
}

TEST_P(SourceOwnerTest, a_scope_left_by_an_exception_undoes_itself) {
    mount("global", ResourceOwner::Global);
    try {
        ResourceMountTransaction transaction(*_resources);
        mount("module", ResourceOwner::ActiveModule);
        throw ValidationException("mount failed");
    } catch (const ValidationException &) {
    }

    EXPECT_FALSE(has("module_only"));
    EXPECT_EQ("global", find("shared"));
    EXPECT_EQ(1u, _count());
}

TEST_P(SourceOwnerTest, a_rejected_mount_leaves_the_list_and_the_token_alone) {
    _resources->addMemERF(erfBytes(ErfWriter::FileType::ERF, {{"shared", ResType::Txt, "unplaced"}}),
                          ResourceOwner::Global);
    auto token = _resources->mountToken();

    // A list holding unplaced sources cannot take a placed one. Nothing is
    // mounted, so nothing may be spent either: a token read afterwards has to
    // still name the same point.
    EXPECT_THROW(_resources->addMemERF(erfBytes(ErfWriter::FileType::ERF,
                                                {{"shared", ResType::Txt, "placed"}}),
                                       ResourceOwner::Global,
                                       ResourceSourceBucket::EncapsulatedClass2),
                 ValidationException);

    EXPECT_EQ(token, _resources->mountToken());
    EXPECT_EQ("unplaced", find("shared"));
    EXPECT_EQ(1u, _count());
}

TEST_P(SourceOwnerTest, the_mount_sequence_never_rewinds) {
    mount("first", ResourceOwner::Global);
    auto afterFirst = _resources->mountToken();

    _resources->clear();
    EXPECT_EQ(afterFirst, _resources->mountToken())
        << "emptying the list must not hand out a spent number again";

    mount("rebuilt", ResourceOwner::Global);
    EXPECT_GT(_resources->mountToken(), afterFirst);

    // A scope opened after the clear still covers exactly its own mounts, which
    // is what reusing numbers would break.
    auto token = _resources->mountToken();
    mount("scoped", ResourceOwner::ActiveModule);
    _resources->rollbackTo(token);
    EXPECT_EQ("rebuilt", find("shared"));
    EXPECT_EQ(1u, _count());
}

INSTANTIATE_TEST_SUITE_P(Backends,
                         SourceOwnerTest,
                         testing::Values(Backend::Legacy, Backend::Extract),
                         backendName);

/// Save and module lifetimes as the director drives them.
class SourceLifetimeFixture {
protected:
    void initBackend(Backend backend) {
        _graphics.init();
        _script.init();
        if (backend == Backend::Legacy) {
            auto legacy = std::make_unique<Resources>();
            _count = [raw = legacy.get()]() { return raw->containers().size(); };
            _resources = std::move(legacy);
        } else {
            auto extract = std::make_unique<ExtractResources>();
            _count = [raw = extract.get()]() { return raw->sourceCount(); };
            _resources = std::move(extract);
        }
        _auxResources = std::make_unique<Resources>();
    }

    void makeInstallation(TmpDir &game, TmpDir &cwd) {
        writeErf(cwd.path / "shaderpack.erf", ErfWriter::FileType::ERF, {});
        game.mkdir("modules");
    }

    std::unique_ptr<ResourceDirector> makeDirector(const std::filesystem::path &gamePath,
                                                   GameID game = GameID::TSL,
                                                   IGffs *gffs = nullptr) {
        _gamePath = gamePath;
        return std::make_unique<ResourceDirector>(
            game, _gamePath, _graphicsOpt, _graphics.services(), _script.services(),
            _dialogs, gffs ? *gffs : static_cast<IGffs &>(_gffs), _lips, _paths,
            *_resources, *_auxResources, _scripts, _twoDas);
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

    std::filesystem::path _gamePath;
    std::unique_ptr<IResources> _resources;
    std::unique_ptr<IResources> _auxResources;
    std::function<std::size_t()> _count;
};

/// Everything a lifetime rule says must hold on both backends.
class SourceLifetimeDirectorTest : public SourceLifetimeFixture,
                                   public testing::TestWithParam<Backend> {
protected:
    void SetUp() override { initBackend(GetParam()); }
};

// Save slots.

namespace {

/// Two slots holding a resource under the same id and one of their own.
void writeSaveSlots(TmpDir &game) {
    auto a = game.mkdir("saves/slot_a");
    writeFile(a / "loose_a.txt", "loose from a");
    writeErf(a / "savegame.sav", ErfWriter::FileType::ERF,
             {{"common", ResType::Txt, "from a"},
              {"a_only", ResType::Txt, "from a"}});

    auto b = game.mkdir("saves/slot_b");
    writeFile(b / "loose_b.txt", "loose from b");
    writeErf(b / "savegame.sav", ErfWriter::FileType::ERF,
             {{"common", ResType::Txt, "from b"},
              {"b_only", ResType::Txt, "from b"}});
}

} // namespace

TEST_P(SourceLifetimeDirectorTest, a_save_is_unloaded_when_another_is_loaded) {
    TmpDir game("reone_test_lifetime_saves");
    TmpDir cwd("reone_test_lifetime_saves_cwd");
    makeInstallation(game, cwd);
    writeSaveSlots(game);

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    director->onGameLoad("slot_a");
    EXPECT_EQ("from a", find("common"));
    EXPECT_TRUE(has("a_only"));
    EXPECT_TRUE(has("loose_a"));

    director->onGameLoad("slot_b");
    EXPECT_EQ("from b", find("common"));
    EXPECT_TRUE(has("b_only"));
    EXPECT_FALSE(has("a_only")) << "a resource only the previous save held must stop resolving";
    EXPECT_FALSE(has("loose_a")) << "the previous save's loose directory goes with it";

    director->onGameLoad("slot_a");
    EXPECT_EQ("from a", find("common"));
    EXPECT_TRUE(has("a_only"));
    EXPECT_FALSE(has("b_only"));
    EXPECT_FALSE(has("loose_b"));
}

/**
 * Retiring a source does not reach a record already parsed out of it.
 *
 * The save's own records are read between loading a save and loading its
 * module, so the module load's cache clear comes too late for them. They are
 * keyed by resref and type alone, which is the same in every slot, so without
 * clearing here the previous save's parsed copy answers for the new one however
 * the sources are mounted. This is the one cache the save path clears, and this
 * is why.
 */
TEST_P(SourceLifetimeDirectorTest, a_parsed_record_of_the_previous_save_is_not_served_for_the_next) {
    TmpDir game("reone_test_lifetime_save_cache");
    TmpDir cwd("reone_test_lifetime_save_cache_cwd");
    makeInstallation(game, cwd);

    auto a = game.mkdir("saves/slot_a");
    writeErf(a / "savegame.sav", ErfWriter::FileType::ERF,
             {{"savenfo", ResType::Res, emptyGff()}});
    auto b = game.mkdir("saves/slot_b");
    writeErf(b / "savegame.sav", ErfWriter::FileType::ERF,
             {{"savenfo", ResType::Res, emptyGff()}});

    Gffs gffs(*_resources);
    auto director = makeDirector(game.path, GameID::TSL, &gffs);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    director->onGameLoad("slot_a");
    auto first = gffs.get("savenfo", ResType::Res);
    ASSERT_TRUE(first);
    EXPECT_EQ(first, gffs.get("savenfo", ResType::Res)) << "the provider really does cache";

    director->onGameLoad("slot_b");
    auto afterSwitch = gffs.get("savenfo", ResType::Res);
    ASSERT_TRUE(afterSwitch);
    EXPECT_NE(first, afterSwitch)
        << "the record must be read again from the save that is now loaded";
}

TEST_P(SourceLifetimeDirectorTest, loading_the_same_save_twice_accumulates_nothing) {
    TmpDir game("reone_test_lifetime_same_save");
    TmpDir cwd("reone_test_lifetime_same_save_cwd");
    makeInstallation(game, cwd);
    writeSaveSlots(game);

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    director->onGameLoad("slot_a");
    auto after = _count();
    auto common = find("common");

    director->onGameLoad("slot_a");
    EXPECT_EQ(after, _count());
    EXPECT_EQ(common, find("common"));

    director->onGameLoad("slot_a");
    EXPECT_EQ(after, _count());
    EXPECT_EQ(common, find("common"));
}

TEST_P(SourceLifetimeDirectorTest, a_save_that_cannot_be_loaded_leaves_none_of_itself_behind) {
    TmpDir game("reone_test_lifetime_save_failure");
    TmpDir cwd("reone_test_lifetime_save_failure_cwd");
    makeInstallation(game, cwd);
    writeSaveSlots(game);

    // A slot with its loose directory but no archive. The directory is mounted
    // before the archive is looked for, so this is the partial case.
    auto broken = game.mkdir("saves/slot_broken");
    writeFile(broken / "loose_broken.txt", "loose from broken");

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }
    director->onGameLoad("slot_a");
    auto globals = _count();

    EXPECT_THROW(director->onGameLoad("slot_broken"), ResourceNotFoundException);
    EXPECT_FALSE(has("loose_broken")) << "the half-loaded save must leave nothing mounted";
    EXPECT_FALSE(has("a_only")) << "the previous save was already unloaded before the attempt";

    // Repeating the failed load starts from the same state and ends in it.
    auto afterFailure = _count();
    EXPECT_THROW(director->onGameLoad("slot_broken"), ResourceNotFoundException);
    EXPECT_EQ(afterFailure, _count());
    EXPECT_FALSE(has("loose_broken"));

    // And a good slot still loads afterwards.
    director->onGameLoad("slot_b");
    EXPECT_EQ("from b", find("common"));
    EXPECT_LT(globals - globals, _count());
}

// Module transitions.

namespace {

/// Two split modules and one MOD-layout module, each with its own support
/// families, all serving the same probe id.
void writeTransitionModules(TmpDir &game) {
    auto modules = game.path / "modules";
    auto lips = game.mkdir("lips");

    writeRim(modules / "a.rim", {{"probe", ResType::Txt, "a base"}, {"a_base", ResType::Txt, "x"}});
    writeRim(modules / "a_s.rim", {{"a_static", ResType::Txt, "x"}});
    writeRim(modules / "a_a.rim", {{"a_area", ResType::Txt, "x"}});
    writeRim(modules / "a_adx.rim", {{"a_adx", ResType::Txt, "x"}});
    writeErf(modules / "a_dlg.erf", ErfWriter::FileType::ERF, {{"a_dlg", ResType::Txt, "x"}});
    writeErf(lips / "a_loc.mod", ErfWriter::FileType::MOD, {{"a_loc", ResType::Txt, "x"}});

    writeRim(modules / "b.rim", {{"probe", ResType::Txt, "b base"}, {"b_base", ResType::Txt, "x"}});
    writeRim(modules / "b_s.rim", {{"b_static", ResType::Txt, "x"}});
    writeErf(modules / "b_dlg.erf", ErfWriter::FileType::ERF, {{"b_dlg", ResType::Txt, "x"}});

    // MOD layout. The static image and dialogue archive exist but the branch
    // suppresses them, so they must not become reachable through a transition
    // out of a module that did mount its own.
    writeErf(modules / "c.mod", ErfWriter::FileType::MOD,
             {{"probe", ResType::Txt, "c mod"}, {"c_mod", ResType::Txt, "x"}});
    writeRim(modules / "c_s.rim", {{"c_static", ResType::Txt, "x"}});
    writeErf(modules / "c_dlg.erf", ErfWriter::FileType::ERF, {{"c_dlg", ResType::Txt, "x"}});
    writeRim(modules / "c_a.rim", {{"c_area", ResType::Txt, "x"}});
}

} // namespace

TEST_P(SourceLifetimeDirectorTest, a_transition_leaves_nothing_of_the_previous_module) {
    TmpDir game("reone_test_lifetime_transition");
    TmpDir cwd("reone_test_lifetime_transition_cwd");
    makeInstallation(game, cwd);
    writeTransitionModules(game);

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    auto expectOnlyA = [&]() {
        EXPECT_EQ("a base", find("probe"));
        for (const auto *id : {"a_base", "a_static", "a_area", "a_adx", "a_dlg", "a_loc"}) {
            EXPECT_TRUE(has(id)) << id;
        }
        for (const auto *id : {"b_base", "b_static", "b_dlg", "c_mod", "c_area", "c_static", "c_dlg"}) {
            EXPECT_FALSE(has(id)) << id;
        }
    };
    auto expectOnlyB = [&]() {
        EXPECT_EQ("b base", find("probe"));
        for (const auto *id : {"b_base", "b_static", "b_dlg"}) {
            EXPECT_TRUE(has(id)) << id;
        }
        for (const auto *id : {"a_base", "a_static", "a_area", "a_adx", "a_dlg", "a_loc", "c_mod"}) {
            EXPECT_FALSE(has(id)) << id;
        }
    };
    auto expectOnlyC = [&]() {
        EXPECT_EQ("c mod", find("probe"));
        EXPECT_TRUE(has("c_mod"));
        EXPECT_TRUE(has("c_area")) << "adjunct images coexist with the module archive";
        EXPECT_FALSE(has("c_static")) << "the MOD branch suppresses the static image";
        EXPECT_FALSE(has("c_dlg")) << "the MOD branch suppresses dialogue";
        for (const auto *id : {"a_base", "a_static", "a_dlg", "b_base", "b_static", "b_dlg"}) {
            EXPECT_FALSE(has(id)) << id;
        }
    };

    director->onModuleLoad("a");
    expectOnlyA();
    auto sourcesForA = _count();

    director->onModuleLoad("b");
    expectOnlyB();

    director->onModuleLoad("a");
    expectOnlyA();
    EXPECT_EQ(sourcesForA, _count()) << "re-entering a module must not accumulate sources";

    // split -> MOD -> split.
    director->onModuleLoad("c");
    expectOnlyC();

    director->onModuleLoad("a");
    expectOnlyA();
    EXPECT_EQ(sourcesForA, _count());

    // MOD -> split -> MOD.
    director->onModuleLoad("c");
    auto sourcesForC = _count();
    director->onModuleLoad("b");
    expectOnlyB();
    director->onModuleLoad("c");
    expectOnlyC();
    EXPECT_EQ(sourcesForC, _count());
}

TEST_P(SourceLifetimeDirectorTest, loading_the_same_module_twice_accumulates_nothing) {
    TmpDir game("reone_test_lifetime_same_module");
    TmpDir cwd("reone_test_lifetime_same_module_cwd");
    makeInstallation(game, cwd);
    writeTransitionModules(game);

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    director->onModuleLoad("a");
    auto after = _count();

    director->onModuleLoad("a");
    EXPECT_EQ(after, _count());
    EXPECT_EQ("a base", find("probe"));

    director->onModuleLoad("a");
    EXPECT_EQ(after, _count());
    EXPECT_EQ("a base", find("probe"));
}

TEST_P(SourceLifetimeDirectorTest, a_module_transition_does_not_unload_the_save) {
    TmpDir game("reone_test_lifetime_save_survives");
    TmpDir cwd("reone_test_lifetime_save_survives_cwd");
    makeInstallation(game, cwd);
    writeTransitionModules(game);
    writeSaveSlots(game);

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    director->onGameLoad("slot_a");
    director->onModuleLoad("a");
    EXPECT_TRUE(has("a_only")) << "the save is still loaded";

    director->onModuleLoad("b");
    EXPECT_TRUE(has("a_only")) << "walking to another module does not leave the save";
    EXPECT_EQ("from a", find("common"));
}

TEST_P(SourceLifetimeDirectorTest, replacing_the_save_does_not_disturb_the_active_module) {
    TmpDir game("reone_test_lifetime_module_survives");
    TmpDir cwd("reone_test_lifetime_module_survives_cwd");
    makeInstallation(game, cwd);
    writeTransitionModules(game);
    writeSaveSlots(game);

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    director->onGameLoad("slot_a");
    director->onModuleLoad("a");

    director->onGameLoad("slot_b");
    EXPECT_TRUE(has("a_static")) << "the module's sources are not the save's to retire";
    EXPECT_TRUE(has("a_area"));
    EXPECT_EQ("a base", find("probe"));
    EXPECT_FALSE(has("a_only"));
}

// The module's own state, separately from its support sources.

TEST_P(SourceLifetimeDirectorTest, the_staged_module_state_goes_with_the_module) {
    TmpDir game("reone_test_lifetime_staged_state");
    TmpDir cwd("reone_test_lifetime_staged_state_cwd");
    makeInstallation(game, cwd);

    auto modules = game.path / "modules";
    writeRim(modules / "a.rim", {{"probe", ResType::Txt, "disk a"}});
    writeRim(modules / "b.rim", {{"probe", ResType::Txt, "disk b"}});

    // The save holds state for module a only.
    auto staged = erfBytes(ErfWriter::FileType::MOD,
                           {{"probe", ResType::Txt, "staged a"},
                            {"staged_only", ResType::Txt, "staged a"}});
    auto slot = game.mkdir("saves/slot_a");
    writeErf(slot / "savegame.sav", ErfWriter::FileType::ERF,
             {{"a", ResType::Sav, blob(staged)}});

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    director->onGameLoad("slot_a");
    director->onModuleLoad("a");
    EXPECT_EQ("staged a", find("probe"));
    EXPECT_TRUE(has("staged_only"));

    director->onModuleLoad("b");
    EXPECT_EQ("disk b", find("probe"));
    EXPECT_FALSE(has("staged_only")) << "the previous module's state is not the new module's";

    // The outer save is still loaded, so returning restores the same state.
    director->onModuleLoad("a");
    EXPECT_EQ("staged a", find("probe"));
    EXPECT_TRUE(has("staged_only"));
}

// Failure.

/**
 * A module load that throws partway through leaves none of itself mounted.
 *
 * The staged archive is the failure point because both backends read a staged
 * blob eagerly, so this pins the rollback identically on each. It is also the
 * latest phase there is: the adjunct images are already mounted when it runs.
 */
TEST_P(SourceLifetimeDirectorTest, a_module_that_fails_partway_leaves_none_of_itself_behind) {
    TmpDir game("reone_test_lifetime_module_failure");
    TmpDir cwd("reone_test_lifetime_module_failure_cwd");
    makeInstallation(game, cwd);

    auto modules = game.path / "modules";
    writeRim(modules / "good.rim", {{"probe", ResType::Txt, "good"}});

    writeRim(modules / "broken.rim", {{"probe", ResType::Txt, "broken base"}});
    writeRim(modules / "broken_a.rim", {{"broken_area", ResType::Txt, "x"}});
    writeRim(modules / "broken_adx.rim", {{"broken_adx", ResType::Txt, "x"}});

    // Saved state for the module that is not a readable archive. It wins
    // primary selection, so the active-state phase tries to read it.
    auto slot = game.mkdir("saves/slot_a");
    writeErf(slot / "savegame.sav", ErfWriter::FileType::ERF,
             {{"broken", ResType::Sav, "not an archive at all"}});

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }
    director->onGameLoad("slot_a");
    director->onModuleLoad("good");
    auto beforeAttempt = _count();
    EXPECT_TRUE(has("probe"));

    EXPECT_THROW(director->onModuleLoad("broken"), ValidationException);

    EXPECT_FALSE(has("broken_area")) << "sources mounted before the failure must be taken back";
    EXPECT_FALSE(has("broken_adx"));
    EXPECT_FALSE(has("probe")) << "the previous module was retired before the attempt began";

    // Failing again starts from, and ends in, the same state.
    auto afterFailure = _count();
    EXPECT_THROW(director->onModuleLoad("broken"), ValidationException);
    EXPECT_EQ(afterFailure, _count());
    EXPECT_FALSE(has("broken_area"));

    // A good module still loads afterwards, with nothing of the failed one.
    director->onModuleLoad("good");
    EXPECT_EQ("good", find("probe"));
    EXPECT_FALSE(has("broken_area"));
    EXPECT_EQ(beforeAttempt, _count());
}

/// The same rollback for an unreadable archive on disk. Both backends decide an
/// archive is unusable as they mount it, so both fail here; archivevalidation
/// covers that contract itself.
TEST_P(SourceLifetimeDirectorTest, a_corrupt_archive_on_disk_rolls_back_the_module) {
    TmpDir game("reone_test_lifetime_corrupt_disk");
    TmpDir cwd("reone_test_lifetime_corrupt_disk_cwd");
    makeInstallation(game, cwd);

    auto modules = game.path / "modules";
    writeRim(modules / "broken.rim", {{"probe", ResType::Txt, "broken base"}});
    writeRim(modules / "broken_a.rim", {{"broken_area", ResType::Txt, "x"}});
    writeRim(modules / "broken_adx.rim", {{"broken_adx", ResType::Txt, "x"}});
    writeFile(modules / "broken_s.rim", "not a rim at all");

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }
    auto globals = _count();

    EXPECT_THROW(director->onModuleLoad("broken"), ValidationException);

    EXPECT_FALSE(has("broken_area"));
    EXPECT_FALSE(has("broken_adx"));
    EXPECT_EQ(globals, _count()) << "nothing of the failed module may remain";
}

TEST_P(SourceLifetimeDirectorTest, an_absent_optional_source_is_not_a_failure) {
    TmpDir game("reone_test_lifetime_optional_absent");
    TmpDir cwd("reone_test_lifetime_optional_absent_cwd");
    makeInstallation(game, cwd);

    // No adjunct images, no dialogue archive, no localization: every optional
    // family of this module is missing.
    auto modules = game.path / "modules";
    writeRim(modules / "lonely.rim", {{"probe", ResType::Txt, "base"}});
    writeRim(modules / "lonely_s.rim", {{"static_only", ResType::Txt, "static"}});

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    EXPECT_NO_THROW(director->onModuleLoad("lonely"));
    EXPECT_EQ("base", find("probe"));
    EXPECT_TRUE(has("static_only")) << "a missing optional family does not roll back a good one";
}

INSTANTIATE_TEST_SUITE_P(Backends,
                         SourceLifetimeDirectorTest,
                         testing::Values(Backend::Legacy, Backend::Extract),
                         backendName);

/// K1 is not activated, so its sources are unplaced. Ownership is generic and
/// applies to it all the same.
class K1SourceLifetimeTest : public SourceLifetimeDirectorTest {};

TEST_P(K1SourceLifetimeTest, retires_module_sources_on_the_unactivated_path) {
    TmpDir game("reone_test_lifetime_k1");
    TmpDir cwd("reone_test_lifetime_k1_cwd");
    makeInstallation(game, cwd);

    auto modules = game.path / "modules";
    writeRim(modules / "a.rim", {{"probe", ResType::Txt, "a base"}, {"a_base", ResType::Txt, "x"}});
    writeRim(modules / "a_s.rim", {{"a_static", ResType::Txt, "x"}});
    writeRim(modules / "b.rim", {{"probe", ResType::Txt, "b base"}, {"b_base", ResType::Txt, "x"}});

    auto director = makeDirector(game.path, GameID::KotOR);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    director->onModuleLoad("a");
    EXPECT_EQ("a base", find("probe"));
    EXPECT_TRUE(has("a_static"));
    auto sourcesForA = _count();

    director->onModuleLoad("b");
    EXPECT_EQ("b base", find("probe"));
    EXPECT_FALSE(has("a_base"));
    EXPECT_FALSE(has("a_static")) << "the unactivated stack is retired by owner too";

    director->onModuleLoad("a");
    EXPECT_EQ("a base", find("probe"));
    EXPECT_EQ(sourcesForA, _count());
}

TEST_P(K1SourceLifetimeTest, unloads_a_save_on_the_unactivated_path) {
    TmpDir game("reone_test_lifetime_k1_saves");
    TmpDir cwd("reone_test_lifetime_k1_saves_cwd");
    makeInstallation(game, cwd);
    writeSaveSlots(game);

    auto director = makeDirector(game.path, GameID::KotOR);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    director->onGameLoad("slot_a");
    EXPECT_TRUE(has("a_only"));

    director->onGameLoad("slot_b");
    EXPECT_EQ("from b", find("common"));
    EXPECT_FALSE(has("a_only"));
}

INSTANTIATE_TEST_SUITE_P(Backends,
                         K1SourceLifetimeTest,
                         testing::Values(Backend::Legacy, Backend::Extract),
                         backendName);
