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
 * K1 startup sources, and which of them wins.
 *
 * Several K1 startup sources share a bucket, and within a bucket the source
 * registered later wins. The order the engine registers them in is therefore
 * behaviour, so every collision here uses one resref and type present in more
 * than one source: an assertion that only checked a source was reachable would
 * pass whatever the order.
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "reone/resource/director.h"
#include "reone/resource/extractresources.h"
#include "reone/resource/format/erfwriter.h"
#include "reone/resource/format/rimwriter.h"
#include "reone/resource/2da.h"
#include "reone/resource/format/2dareader.h"
#include "reone/resource/modulediscovery.h"
#include "reone/resource/resources.h"
#include "reone/system/stream/fileoutput.h"
#include "reone/system/stream/memoryinput.h"
#include "reone/system/stream/memoryoutput.h"

#include "../fixtures/archive.h"
#include "../fixtures/graphics.h"
#include "../fixtures/resource.h"
#include "../fixtures/script.h"

using namespace reone;
using namespace reone::resource;

using testing::NiceMock;

namespace {

ByteBuffer bytes(std::string_view v) { return ByteBuffer(v.begin(), v.end()); }

struct NamedRes {
    std::string resRef;
    ResType type;
    std::string data;
};

ByteBuffer erfBytes(ErfWriter::FileType t, const std::vector<NamedRes> &res) {
    ErfWriter w;
    for (const auto &r : res) {
        w.add(ErfWriter::Resource {r.resRef, r.type, bytes(r.data)});
    }
    ByteBuffer b;
    MemoryOutputStream s(b);
    w.save(t, s);
    return b;
}

void writeBuffer(const std::filesystem::path &p, const ByteBuffer &b) {
    std::filesystem::create_directories(p.parent_path());
    FileOutputStream s(p);
    s.write(b.data(), b.size());
}

void writeErf(const std::filesystem::path &p, ErfWriter::FileType t, const std::vector<NamedRes> &res) {
    writeBuffer(p, erfBytes(t, res));
}

/// A MOD-family archive whose four-character type tag is rewritten to HAK.
/// Everything after the tag is the layout both readers already share.
void writeHak(const std::filesystem::path &p, const std::vector<NamedRes> &res) {
    auto b = erfBytes(ErfWriter::FileType::MOD, res);
    const char tag[4] = {'H', 'A', 'K', ' '};
    for (int i = 0; i < 4; ++i) {
        b[i] = tag[i];
    }
    writeBuffer(p, b);
}

void writeRim(const std::filesystem::path &p, const std::vector<NamedRes> &res) {
    RimWriter w;
    for (const auto &r : res) {
        w.add(RimWriter::Resource {r.resRef, r.type, bytes(r.data)});
    }
    ByteBuffer b;
    MemoryOutputStream s(b);
    w.save(s);
    writeBuffer(p, b);
}

void writeFile(const std::filesystem::path &p, const std::string &d) {
    std::filesystem::create_directories(p.parent_path());
    FileOutputStream s(p);
    s.write(d.data(), d.size());
}

std::string dataOf(const std::optional<Resource> &r) {
    return r ? std::string(r->data.begin(), r->data.end()) : "<not found>";
}

struct TmpDir {
    std::filesystem::path path;
    explicit TmpDir(const std::string &n) {
        path = std::filesystem::temp_directory_path() / n;
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TmpDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    std::filesystem::path mkdir(const std::string &n) {
        auto d = path / n;
        std::filesystem::create_directories(d);
        return d;
    }
};

struct CwdGuard {
    std::filesystem::path previous;
    explicit CwdGuard(const std::filesystem::path &p) {
        previous = std::filesystem::current_path();
        std::filesystem::current_path(p);
    }
    ~CwdGuard() {
        std::error_code ec;
        std::filesystem::current_path(previous, ec);
    }
};

enum class Backend { Legacy, Extract };

std::string backendName(const testing::TestParamInfo<Backend> &i) {
    return i.param == Backend::Legacy ? "Legacy" : "Extract";
}

/// The probe resref every collision test uses.
constexpr char kProbe[] = "probe";

} // namespace

class K1StartupTest : public testing::TestWithParam<Backend> {
protected:
    void SetUp() override {
        _graphics.init();
        _script.init();
        if (GetParam() == Backend::Legacy) {
            auto b = std::make_unique<Resources>();
            _count = [r = b.get()]() { return r->containers().size(); };
            _resources = std::move(b);
        } else {
            auto b = std::make_unique<ExtractResources>();
            _count = [r = b.get()]() { return r->sourceCount(); };
            _resources = std::move(b);
        }
        _auxResources = std::make_unique<Resources>();
    }

    std::unique_ptr<ResourceDirector> makeDirector(const std::filesystem::path &p,
                                                   GameID game = GameID::KotOR,
                                                   OdysseyResourceRoots roots = {}) {
        _gamePath = p;
        return std::make_unique<ResourceDirector>(
            game, _gamePath, _graphicsOpt, _graphics.services(), _script.services(),
            _dialogs, _gffs, _lips, _paths, *_resources, *_auxResources, _scripts, _twoDas, std::move(roots));
    }

    /// An installation with a modules directory and the shader pack the
    /// director always mounts.
    void makeInstallation(TmpDir &game, TmpDir &cwd) {
        writeErf(cwd.path / "shaderpack.erf", ErfWriter::FileType::ERF, {});
        game.mkdir("modules");
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

// Within the loose bucket, registration order decides.

TEST_P(K1StartupTest, loose_sources_rank_by_the_order_k1_registers_them) {
    TmpDir game("reone_test_k1_loose");
    TmpDir cwd("reone_test_k1_loose_cwd");
    makeInstallation(game, cwd);

    // K1 registers override early and the streaming directories late, so a
    // streamed asset outranks an override file of the same id.
    writeFile(game.mkdir("override") / "probe.txt", "override");
    writeFile(game.mkdir("rims") / "probe.txt", "rims");
    writeFile(game.mkdir("streamwaves") / "probe.txt", "streamwaves");
    writeFile(game.mkdir("streammusic") / "probe.txt", "streammusic");

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    EXPECT_EQ("streammusic", find(kProbe)) << "streammusic is registered last of the loose sources";
}

TEST_P(K1StartupTest, loose_order_holds_when_the_newest_sources_are_absent) {
    TmpDir game("reone_test_k1_loose_partial");
    TmpDir cwd("reone_test_k1_loose_partial_cwd");
    makeInstallation(game, cwd);

    writeFile(game.mkdir("override") / "probe.txt", "override");
    writeFile(game.mkdir("rims") / "probe.txt", "rims");
    writeFile(game.mkdir("streamwaves") / "probe.txt", "streamwaves");

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    EXPECT_EQ("streamwaves", find(kProbe)) << "streamwaves outranks rims and override";
}

TEST_P(K1StartupTest, the_rims_directory_outranks_override) {
    TmpDir game("reone_test_k1_rims_override");
    TmpDir cwd("reone_test_k1_rims_override_cwd");
    makeInstallation(game, cwd);

    writeFile(game.mkdir("override") / "probe.txt", "override");
    writeFile(game.mkdir("rims") / "probe.txt", "rims");

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    EXPECT_EQ("rims", find(kProbe)) << "rims is registered after override";
}

TEST_P(K1StartupTest, streamsounds_stays_out_of_raw_lookup) {
    TmpDir game("reone_test_k1_streamsounds");
    TmpDir cwd("reone_test_k1_streamsounds_cwd");
    makeInstallation(game, cwd);

    writeFile(game.mkdir("override") / "probe.txt", "override");
    writeFile(game.mkdir("streamsounds") / "probe.txt", "streamsounds");

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    // K1 has no bare STREAMSOUNDS alias and never registers the directory; it
    // is reached by constructed path only. So it cannot answer here at all,
    // even though it would have been the newest loose source if it did.
    EXPECT_EQ("override", find(kProbe)) << "the sounds directory is not a raw lookup source";
}

// Class 1.

TEST_P(K1StartupTest, patch_outranks_the_override_texture_archive) {
    TmpDir game("reone_test_k1_class1");
    TmpDir cwd("reone_test_k1_class1_cwd");
    makeInstallation(game, cwd);

    // Both are class 1; K1 registers the override archive first and patch
    // second, so patch wins.
    writeErf(game.mkdir("override") / "textures.erf", ErfWriter::FileType::ERF,
             {{kProbe, ResType::Txt, "override textures"}});
    writeErf(game.path / "patch.erf", ErfWriter::FileType::ERF,
             {{kProbe, ResType::Txt, "patch"}});

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    EXPECT_EQ("patch", find(kProbe)) << "patch is registered after the override texture archive";
}

TEST_P(K1StartupTest, the_override_texture_archive_is_class_one_not_class_two) {
    TmpDir game("reone_test_k1_textures_class");
    TmpDir cwd("reone_test_k1_textures_class_cwd");
    makeInstallation(game, cwd);

    writeErf(game.mkdir("override") / "textures.erf", ErfWriter::FileType::ERF,
             {{kProbe, ResType::Txt, "override textures"}});
    // A class-2 source and a resource image, both of which class 1 outranks.
    writeErf(game.mkdir("texturepacks") / "swpc_tex_gui.erf", ErfWriter::FileType::ERF,
             {{kProbe, ResType::Txt, "texture pack"}});
    writeRim(game.mkdir("rims") / "global.rim", {{kProbe, ResType::Txt, "global rim"}});

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    EXPECT_EQ("override textures", find(kProbe))
        << "class 1 outranks both the resource image and the class-2 pack";
}

TEST_P(K1StartupTest, a_loose_file_outranks_every_encapsulated_startup_source) {
    TmpDir game("reone_test_k1_loose_over_class1");
    TmpDir cwd("reone_test_k1_loose_over_class1_cwd");
    makeInstallation(game, cwd);

    writeFile(game.mkdir("override") / "probe.txt", "override");
    writeErf(game.path / "patch.erf", ErfWriter::FileType::ERF,
             {{kProbe, ResType::Txt, "patch"}});

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    EXPECT_EQ("override", find(kProbe)) << "the loose bucket is searched before class 1";
}

// The global image.

TEST_P(K1StartupTest, the_global_image_outranks_class_two_and_loses_to_class_one) {
    TmpDir game("reone_test_k1_global_rim");
    TmpDir cwd("reone_test_k1_global_rim_cwd");
    makeInstallation(game, cwd);

    writeRim(game.mkdir("rims") / "global.rim", {{kProbe, ResType::Txt, "global rim"}});
    writeErf(game.mkdir("texturepacks") / "swpc_tex_gui.erf", ErfWriter::FileType::ERF,
             {{kProbe, ResType::Txt, "texture pack"}});

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    EXPECT_EQ("global rim", find(kProbe)) << "the image bucket is searched before class 2";
}

TEST_P(K1StartupTest, only_the_named_global_image_is_mounted_from_the_rims_location) {
    TmpDir game("reone_test_k1_rims_only_global");
    TmpDir cwd("reone_test_k1_rims_only_global_cwd");
    makeInstallation(game, cwd);

    auto rims = game.mkdir("rims");
    writeRim(rims / "global.rim", {{"from_global", ResType::Txt, "global"}});
    writeRim(rims / "mainmenu.rim", {{"from_mainmenu", ResType::Txt, "mainmenu"}});

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    EXPECT_EQ("global", find("from_global"));
    // The directory is mounted as a loose location, so the other images are
    // present as files but their contents are not indexed as resources.
    EXPECT_FALSE(has("from_mainmenu")) << "startup names only the global image";
}

// Class 2, around the player archive.

TEST_P(K1StartupTest, the_player_archive_outranks_the_texture_pack_and_loses_to_module_support) {
    TmpDir game("reone_test_k1_players_order");
    TmpDir cwd("reone_test_k1_players_order_cwd");
    makeInstallation(game, cwd);

    // Every class-2 source K1 inserts around the player archive, all colliding.
    writeErf(game.mkdir("texturepacks") / "swpc_tex_gui.erf", ErfWriter::FileType::ERF,
             {{kProbe, ResType::Txt, "texture pack"}});
    writeErf(game.path / "players.erf", ErfWriter::FileType::ERF,
             {{kProbe, ResType::Txt, "players"}});
    auto lips = game.mkdir("lips");
    writeErf(lips / "foo_loc.mod", ErfWriter::FileType::MOD, {{kProbe, ResType::Txt, "module loc"}});
    auto modules = game.path / "modules";
    writeErf(modules / "foo.mod", ErfWriter::FileType::MOD, {{kProbe, ResType::Txt, "module mod"}});

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    // Before any module: the player archive is newer than the texture pack.
    EXPECT_EQ("players", find(kProbe)) << "the player archive is registered after the texture pack";

    director->onModuleLoad("foo");

    // The module's own sources are newer than both.
    EXPECT_EQ("module mod", find(kProbe)) << "the module archive is the newest class-2 source";
}

TEST_P(K1StartupTest, the_player_archive_is_not_duplicated_or_repositioned_by_transitions) {
    TmpDir game("reone_test_k1_players_lifetime");
    TmpDir cwd("reone_test_k1_players_lifetime_cwd");
    makeInstallation(game, cwd);

    writeErf(game.path / "players.erf", ErfWriter::FileType::ERF,
             {{"from_players", ResType::Txt, "players"}});
    auto modules = game.path / "modules";
    writeRim(modules / "foo.rim", {{"probe_a", ResType::Txt, "foo"}});
    writeRim(modules / "bar.rim", {{"probe_b", ResType::Txt, "bar"}});

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }
    auto afterInit = _count();
    EXPECT_TRUE(has("from_players"));

    director->onModuleLoad("foo");
    auto afterFirst = _count();

    director->onModuleLoad("bar");
    director->onModuleLoad("foo");

    // The engine adds this source once and never removes it; re-adding the same
    // exact file rebuilds the existing table rather than inserting another. A
    // transition must therefore neither drop it nor stack a second copy.
    EXPECT_TRUE(has("from_players")) << "the player archive survives module transitions";
    EXPECT_EQ(afterFirst, _count()) << "transitions must not accumulate sources";
    EXPECT_LT(afterInit, afterFirst);
}

TEST_P(K1StartupTest, the_player_archive_is_found_by_basename_not_by_extension) {
    TmpDir game("reone_test_k1_players_basename");
    TmpDir cwd("reone_test_k1_players_basename_cwd");
    makeInstallation(game, cwd);

    // The engine mounts an exact basename, so a supported non-ERF container
    // has to be found just the same.
    writeErf(game.path / "players.mod", ErfWriter::FileType::MOD,
             {{"from_players", ResType::Txt, "players mod"}});

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    EXPECT_EQ("players mod", find("from_players"));
}

TEST_P(K1StartupTest, an_absent_player_archive_is_not_a_failure) {
    TmpDir game("reone_test_k1_players_absent");
    TmpDir cwd("reone_test_k1_players_absent_cwd");
    makeInstallation(game, cwd);
    writeRim(game.path / "modules" / "foo.rim", {{"probe_a", ResType::Txt, "foo"}});

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        EXPECT_NO_THROW(director->init());
    }
    EXPECT_NO_THROW(director->onModuleLoad("foo"));
    EXPECT_EQ("foo", find("probe_a"));
}

// Game discrimination.

TEST_P(K1StartupTest, k2_mounts_rims_and_global_image_but_not_k1_support_archives) {
    TmpDir game("reone_test_k2_global_images");
    TmpDir cwd("reone_test_k2_global_images_cwd");
    makeInstallation(game, cwd);

    writeErf(game.path / "players.erf", ErfWriter::FileType::ERF,
             {{"from_players", ResType::Txt, "players"}});
    auto rims = game.mkdir("rims");
    writeFile(rims / "loose.txt", "rims loose");
    writeRim(rims / "global.rim",
             {{"from_global", ResType::Txt, "global"},
              {kProbe, ResType::Txt, "global rim"}});
    writeErf(game.mkdir("override") / "textures.erf", ErfWriter::FileType::ERF,
             {{"from_override_textures", ResType::Txt, "textures"}});
    reone::test::writeKeyBif(game.path, "data/sample.bif",
                             {{kProbe, ResType::Txt, "key bif"}});

    auto director = makeDirector(game.path, GameID::TSL);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    EXPECT_FALSE(has("from_players")) << "the player archive is a K1 source";
    EXPECT_FALSE(has("from_override_textures")) << "the override texture archive is a K1 source";
    EXPECT_EQ("rims loose", find("loose")) << "RIMS is an active loose resource directory";
    EXPECT_EQ("global", find("from_global")) << "global.rim is an active resource image";
    EXPECT_EQ("global rim", find(kProbe)) << "the image bucket outranks KEY/BIF";

    EXPECT_NO_THROW(director->onModuleLoad("missing"));
    EXPECT_EQ("rims loose", find("loose")) << "the RIMS directory has Global lifetime";
    EXPECT_EQ("global", find("from_global")) << "global.rim has Global lifetime";
}

TEST_P(K1StartupTest, k2_keeps_its_streams_out_of_raw_lookup) {
    TmpDir game("reone_test_k2_streams");
    TmpDir cwd("reone_test_k2_streams_cwd");
    makeInstallation(game, cwd);

    writeFile(game.mkdir("override") / "probe.txt", "override");
    writeFile(game.mkdir("streammusic") / "probe.txt", "streammusic");

    auto director = makeDirector(game.path, GameID::TSL);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    EXPECT_EQ("override", find(kProbe)) << "K2 streams are not raw lookup sources";
}

TEST_P(K1StartupTest, k2_rims_loose_directory_outranks_base_override) {
    TmpDir game("reone_test_k2_rims_override");
    TmpDir cwd("reone_test_k2_rims_override_cwd");
    makeInstallation(game, cwd);
    writeFile(game.mkdir("override") / "probe.txt", "base override");
    writeFile(game.mkdir("rims") / "probe.txt", "rims");

    auto director = makeDirector(game.path, GameID::TSL);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }
    EXPECT_EQ("rims", find(kProbe));
}

TEST_P(K1StartupTest, k2_loose_startup_preserves_natural_configured_order_below_rims) {
    TmpDir game("reone_test_k2_loose_order");
    TmpDir cwd("reone_test_k2_loose_order_cwd");
    TmpDir configured0("reone_test_k2_loose_order_0");
    TmpDir configured1("reone_test_k2_loose_order_1");
    makeInstallation(game, cwd);
    writeFile(game.mkdir("override") / "probe.txt", "base override");
    writeFile(configured0.mkdir("override") / "probe.txt", "configured 0");
    writeFile(configured1.mkdir("override") / "probe.txt", "configured 1");
    auto rims = game.mkdir("rims");
    writeFile(rims / "probe.txt", "rims");

    OdysseyResourceRoots roots;
    roots.k2OverrideRoots = {configured0.path, configured1.path};
    auto director = makeDirector(game.path, GameID::TSL, roots);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }
    EXPECT_EQ("rims", find(kProbe));

    std::filesystem::remove(rims / "probe.txt");
    _resources->clear();
    _auxResources->clear();
    director = makeDirector(game.path, GameID::TSL, roots);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }
    EXPECT_EQ("configured 1", find(kProbe));

    std::filesystem::remove(configured1.path / "override" / "probe.txt");
    _resources->clear();
    _auxResources->clear();
    director = makeDirector(game.path, GameID::TSL, roots);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }
    EXPECT_EQ("configured 0", find(kProbe));
}

INSTANTIATE_TEST_SUITE_P(Backends,
                         K1StartupTest,
                         testing::Values(Backend::Legacy, Backend::Extract),
                         backendName);

// The encapsulated basename probe itself.

TEST(EncapsulatedBasenameProbe, probes_the_engine_extension_order) {
    EXPECT_EQ((std::array<std::string_view, 5> {".nwm", ".mod", ".sav", ".erf", ".hak"}),
              kEncapsulatedProbeOrder);
}

TEST(EncapsulatedBasenameProbe, takes_the_first_extension_that_exists) {
    TmpDir tmp("reone_test_probe_order");

    // Every candidate holds the same id, so which one is returned is the only
    // thing the assertions can be reading.
    writeHak(tmp.path / "thing.hak", {{kProbe, ResType::Txt, "hak"}});
    auto path = findEncapsulatedByBasename(tmp.path, "thing");
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ("thing.hak", path->filename().string()) << "hak answers when nothing earlier exists";

    writeErf(tmp.path / "thing.erf", ErfWriter::FileType::ERF, {{kProbe, ResType::Txt, "erf"}});
    EXPECT_EQ("thing.erf", findEncapsulatedByBasename(tmp.path, "thing")->filename().string())
        << "erf is probed before hak";

    writeErf(tmp.path / "thing.sav", ErfWriter::FileType::MOD, {{kProbe, ResType::Txt, "sav"}});
    EXPECT_EQ("thing.sav", findEncapsulatedByBasename(tmp.path, "thing")->filename().string())
        << "sav is probed before erf";

    writeErf(tmp.path / "thing.mod", ErfWriter::FileType::MOD, {{kProbe, ResType::Txt, "mod"}});
    EXPECT_EQ("thing.mod", findEncapsulatedByBasename(tmp.path, "thing")->filename().string())
        << "mod is probed before sav";

    writeErf(tmp.path / "thing.nwm", ErfWriter::FileType::MOD, {{kProbe, ResType::Txt, "nwm"}});
    EXPECT_EQ("thing.nwm", findEncapsulatedByBasename(tmp.path, "thing")->filename().string())
        << "nwm is probed first";
}

TEST(EncapsulatedBasenameProbe, answers_nothing_for_an_absent_basename) {
    TmpDir tmp("reone_test_probe_absent");
    EXPECT_FALSE(findEncapsulatedByBasename(tmp.path, "missing").has_value());
    EXPECT_FALSE(findEncapsulatedByBasename(tmp.path, "").has_value());
}

/// A HAK container is the ERF/MOD layout under a third type tag, which is why
/// the opener probes it alongside the others and validates only the tag.
class HakContainerTest : public testing::TestWithParam<Backend> {
protected:
    std::unique_ptr<IResources> make() {
        if (GetParam() == Backend::Legacy) {
            return std::make_unique<Resources>();
        }
        return std::make_unique<ExtractResources>();
    }
};

TEST_P(HakContainerTest, reads_a_hak_tagged_container) {
    TmpDir tmp("reone_test_hak");
    writeHak(tmp.path / "thing.hak", {{kProbe, ResType::Txt, "from hak"}});

    auto resources = make();
    ASSERT_NO_THROW(resources->addERF(tmp.path / "thing.hak", ResourceOwner::Global));
    EXPECT_EQ("from hak", dataOf(resources->find(ResourceId(kProbe, ResType::Txt))));
}

TEST_P(HakContainerTest, still_rejects_a_container_with_no_valid_tag) {
    TmpDir tmp("reone_test_hak_invalid");
    auto b = erfBytes(ErfWriter::FileType::MOD, {{kProbe, ResType::Txt, "x"}});
    const char tag[4] = {'B', 'A', 'D', ' '};
    for (int i = 0; i < 4; ++i) {
        b[i] = tag[i];
    }
    writeBuffer(tmp.path / "thing.erf", b);

    auto resources = make();
    EXPECT_THROW(resources->addERF(tmp.path / "thing.erf", ResourceOwner::Global),
                 ValidationException);
}

INSTANTIATE_TEST_SUITE_P(Backends,
                         HakContainerTest,
                         testing::Values(Backend::Legacy, Backend::Extract),
                         backendName);

namespace {

/**
 * A minimal binary 2DA holding one column and one row, with a chosen label
 * separator. Retail ships both: the BIF copies of these tables use tabs and the
 * copies inside K1's global.rim use NULs, and are otherwise the same layout.
 */
std::string twoDaBytes(const std::string &value, char separator) {
    std::string b("2DA V2.b\n");
    b += "src";
    b += separator;              // column label
    b += '\0';                   // end of column labels
    b += std::string("\x01\x00\x00\x00", 4);  // row count
    b += "0";
    b += separator;              // row label
    b += std::string("\x00\x00", 2);          // one cell offset
    auto size = static_cast<uint16_t>(value.size() + 1);
    b += std::string(reinterpret_cast<const char *>(&size), 2);
    b += value;
    b += '\0';
    return b;
}

} // namespace

/**
 * The loader, the winner and the parser, end to end.
 *
 * K1 registers global.rim as a resource image and chitin.key as the fixed key
 * table, and the image bucket is searched first, so a table present in both
 * must be served from global.rim. The copy that wins is the NUL-delimited one,
 * so this also proves the winner is the copy that actually has to parse.
 *
 * It fails if global.rim is not registered, is given the wrong bucket, loses to
 * the key table, or cannot be read once it wins.
 */
TEST_P(K1StartupTest, the_global_image_supplies_the_two_da_that_the_key_table_also_holds) {
    TmpDir game("reone_test_k1_global_2da");
    TmpDir cwd("reone_test_k1_global_2da_cwd");
    makeInstallation(game, cwd);

    // The key table carries the ordinary tab-delimited copy...
    reone::test::writeKeyBif(game.path, "data/sample.bif",
                             {{"probe", ResType::TwoDA, twoDaBytes("keybif", '\t')}});
    // ...and the global image carries the NUL-delimited one, as retail does.
    writeRim(game.mkdir("rims") / "global.rim",
             {{"probe", ResType::TwoDA, twoDaBytes("globalrim", '\0')}});

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    auto res = _resources->find(ResourceId("probe", ResType::TwoDA));
    ASSERT_TRUE(res) << "the table must resolve from one of the two sources";

    auto stream = MemoryInputStream(res->data);
    auto reader = TwoDAReader(stream);
    ASSERT_NO_THROW(reader.load()) << "the winning copy must parse";
    auto twoDa = reader.twoDA();
    ASSERT_TRUE(twoDa);

    EXPECT_EQ(1, twoDa->getRowCount());
    EXPECT_EQ((std::vector<std::string> {"src"}), twoDa->columns());
    EXPECT_EQ("globalrim", twoDa->getString(0, "src"))
        << "the resource image outranks the key table";
}
