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
 * When a backend decides an archive is unusable.
 *
 * A source that is absent and a source that is unreadable are different
 * answers, and the difference has to be the same on both backends: absence is
 * best effort, and an unreadable container fails the operation that tried to
 * mount it. A backend that accepts a corrupt archive and only fails later, at
 * whichever lookup happens to touch it first, turns a failed load into a module
 * that half works.
 *
 * What is validated is the container: its signature, and the index of what it
 * holds. Payloads stay unread until something asks for one.
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "reone/resource/director.h"
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

void writeBuffer(const std::filesystem::path &path, const ByteBuffer &buffer) {
    std::filesystem::create_directories(path.parent_path());
    FileOutputStream stream(path);
    stream.write(buffer.data(), buffer.size());
}

void writeErf(const std::filesystem::path &path,
              ErfWriter::FileType fileType,
              const std::vector<NamedRes> &resources) {
    ErfWriter writer;
    for (const auto &res : resources) {
        writer.add(ErfWriter::Resource {res.resRef, res.type, bytes(res.data)});
    }
    ByteBuffer buffer;
    MemoryOutputStream stream(buffer);
    writer.save(fileType, stream);
    writeBuffer(path, buffer);
}

void writeRim(const std::filesystem::path &path, const std::vector<NamedRes> &resources) {
    RimWriter writer;
    for (const auto &res : resources) {
        writer.add(RimWriter::Resource {res.resRef, res.type, bytes(res.data)});
    }
    ByteBuffer buffer;
    MemoryOutputStream stream(buffer);
    writer.save(stream);
    writeBuffer(path, buffer);
}

/// A file of the right name that is not the container its extension claims.
void writeCorrupt(const std::filesystem::path &path) {
    std::filesystem::create_directories(path.parent_path());
    FileOutputStream stream(path);
    std::string data("this is not an archive of any kind");
    stream.write(data.data(), data.size());
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

/// Registration on a bare backend, with no director involved.
class ArchiveRegistrationTest : public testing::TestWithParam<Backend> {
protected:
    void SetUp() override {
        if (GetParam() == Backend::Legacy) {
            auto legacy = std::make_unique<Resources>();
            _count = [raw = legacy.get()]() { return raw->containers().size(); };
            _resources = std::move(legacy);
        } else {
            auto extract = std::make_unique<ExtractResources>();
            _count = [raw = extract.get()]() { return raw->sourceCount(); };
            _resources = std::move(extract);
        }
    }

    std::unique_ptr<IResources> _resources;
    std::function<std::size_t()> _count;
};

TEST_P(ArchiveRegistrationTest, rejects_a_corrupt_resource_image_as_it_is_mounted) {
    TmpDir tmp("reone_test_validation_rim");
    writeCorrupt(tmp.path / "broken.rim");

    EXPECT_THROW(_resources->addRIM(tmp.path / "broken.rim", ResourceOwner::ActiveModule),
                 ValidationException);
    EXPECT_EQ(0u, _count()) << "a container that failed to open must not be left registered";
}

TEST_P(ArchiveRegistrationTest, rejects_a_corrupt_encapsulated_archive_as_it_is_mounted) {
    TmpDir tmp("reone_test_validation_erf");
    writeCorrupt(tmp.path / "broken.mod");

    EXPECT_THROW(_resources->addERF(tmp.path / "broken.mod", ResourceOwner::ActiveModule),
                 ValidationException);
    EXPECT_EQ(0u, _count());
}

TEST_P(ArchiveRegistrationTest, accepts_a_valid_archive_and_reads_it_afterwards) {
    TmpDir tmp("reone_test_validation_valid");
    writeRim(tmp.path / "good.rim", {{"probe", ResType::Txt, "payload"}});
    writeErf(tmp.path / "good.mod", ErfWriter::FileType::MOD, {{"other", ResType::Txt, "payload"}});

    EXPECT_NO_THROW(_resources->addRIM(tmp.path / "good.rim", ResourceOwner::ActiveModule));
    EXPECT_NO_THROW(_resources->addERF(tmp.path / "good.mod", ResourceOwner::ActiveModule));

    EXPECT_EQ(2u, _count());
    EXPECT_EQ("payload", dataOf(_resources->find(ResourceId("probe", ResType::Txt))));
    EXPECT_EQ("payload", dataOf(_resources->find(ResourceId("other", ResType::Txt))));
}

TEST_P(ArchiveRegistrationTest, a_rejected_archive_leaves_earlier_sources_untouched) {
    TmpDir tmp("reone_test_validation_earlier");
    writeRim(tmp.path / "good.rim", {{"probe", ResType::Txt, "payload"}});
    writeCorrupt(tmp.path / "broken.rim");

    _resources->addRIM(tmp.path / "good.rim", ResourceOwner::ActiveModule);
    EXPECT_THROW(_resources->addRIM(tmp.path / "broken.rim", ResourceOwner::ActiveModule),
                 ValidationException);

    EXPECT_EQ(1u, _count());
    EXPECT_EQ("payload", dataOf(_resources->find(ResourceId("probe", ResType::Txt))));
}

INSTANTIATE_TEST_SUITE_P(Backends,
                         ArchiveRegistrationTest,
                         testing::Values(Backend::Legacy, Backend::Extract),
                         backendName);

/// The same contract seen through a module load.
class ArchiveValidationDirectorTest : public testing::TestWithParam<Backend> {
protected:
    void SetUp() override {
        _graphics.init();
        _script.init();
        if (GetParam() == Backend::Legacy) {
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

    std::unique_ptr<ResourceDirector> makeDirector(const std::filesystem::path &gamePath) {
        _gamePath = gamePath;
        return std::make_unique<ResourceDirector>(
            GameID::TSL, _gamePath, _graphicsOpt, _graphics.services(), _script.services(),
            _dialogs, _gffs, _lips, _paths, *_resources, *_auxResources, _scripts, _twoDas);
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

TEST_P(ArchiveValidationDirectorTest, an_absent_optional_archive_is_not_fatal) {
    TmpDir game("reone_test_validation_absent");
    TmpDir cwd("reone_test_validation_absent_cwd");
    makeInstallation(game, cwd);

    // Neither adjunct image, no dialogue archive and no localization exist.
    auto modules = game.path / "modules";
    writeRim(modules / "foo.rim", {{"probe", ResType::Txt, "base"}});
    writeRim(modules / "foo_s.rim", {{"from_static", ResType::Txt, "static"}});

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    EXPECT_NO_THROW(director->onModuleLoad("foo"));
    EXPECT_EQ("base", find("probe"));
    EXPECT_TRUE(has("from_static")) << "an absent optional family is not a failed load";
}

TEST_P(ArchiveValidationDirectorTest, a_corrupt_resource_image_fails_the_load_immediately) {
    TmpDir game("reone_test_validation_module_rim");
    TmpDir cwd("reone_test_validation_module_rim_cwd");
    makeInstallation(game, cwd);

    auto modules = game.path / "modules";
    writeRim(modules / "foo.rim", {{"probe", ResType::Txt, "base"}});
    writeCorrupt(modules / "foo_s.rim");

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    EXPECT_THROW(director->onModuleLoad("foo"), ValidationException)
        << "an unreadable container must fail the load, not a later lookup";
}

TEST_P(ArchiveValidationDirectorTest, a_corrupt_encapsulated_archive_fails_the_load_immediately) {
    TmpDir game("reone_test_validation_module_mod");
    TmpDir cwd("reone_test_validation_module_mod_cwd");
    makeInstallation(game, cwd);

    // The module archive is the required branch source for a MOD layout.
    auto modules = game.path / "modules";
    writeCorrupt(modules / "foo.mod");
    writeRim(modules / "foo.rim", {{"probe", ResType::Txt, "base"}});

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    EXPECT_THROW(director->onModuleLoad("foo"), ValidationException);
}

TEST_P(ArchiveValidationDirectorTest, rolls_back_what_the_failed_load_had_already_mounted) {
    TmpDir game("reone_test_validation_rollback");
    TmpDir cwd("reone_test_validation_rollback_cwd");
    makeInstallation(game, cwd);

    // The adjunct images mount before the static image is reached, so the
    // failure genuinely happens partway through.
    auto modules = game.path / "modules";
    auto rims = game.mkdir("rims");
    writeRim(modules / "foo.rim", {{"probe", ResType::Txt, "base"}});
    writeRim(rims / "foo_a.rim", {{"from_area", ResType::Txt, "area"}});
    writeRim(rims / "foo_adx.rim", {{"from_adx", ResType::Txt, "adx"}});
    writeCorrupt(modules / "foo_s.rim");

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }
    auto globals = _count();

    EXPECT_THROW(director->onModuleLoad("foo"), ValidationException);

    EXPECT_FALSE(has("from_area")) << "sources mounted before the failure must be taken back";
    EXPECT_FALSE(has("from_adx"));
    EXPECT_FALSE(has("probe"));
    EXPECT_EQ(globals, _count()) << "nothing of the failed module may remain";
}

TEST_P(ArchiveValidationDirectorTest, repeating_a_failed_load_leaves_the_same_state) {
    TmpDir game("reone_test_validation_repeat");
    TmpDir cwd("reone_test_validation_repeat_cwd");
    makeInstallation(game, cwd);

    auto modules = game.path / "modules";
    auto rims = game.mkdir("rims");
    writeRim(modules / "foo.rim", {{"probe", ResType::Txt, "base"}});
    writeRim(rims / "foo_a.rim", {{"from_area", ResType::Txt, "area"}});
    writeCorrupt(modules / "foo_s.rim");

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }
    auto globals = _count();

    for (int attempt = 0; attempt < 3; ++attempt) {
        EXPECT_THROW(director->onModuleLoad("foo"), ValidationException) << "attempt " << attempt;
        EXPECT_EQ(globals, _count()) << "attempt " << attempt;
        EXPECT_FALSE(has("from_area")) << "attempt " << attempt;
    }
}

TEST_P(ArchiveValidationDirectorTest, a_valid_module_loads_cleanly_after_a_failure) {
    TmpDir game("reone_test_validation_recover");
    TmpDir cwd("reone_test_validation_recover_cwd");
    makeInstallation(game, cwd);

    auto modules = game.path / "modules";
    auto rims = game.mkdir("rims");
    writeRim(modules / "foo.rim", {{"probe", ResType::Txt, "broken base"}});
    writeRim(rims / "foo_a.rim", {{"from_area", ResType::Txt, "area"}});
    writeCorrupt(modules / "foo_s.rim");

    writeRim(modules / "bar.rim", {{"probe", ResType::Txt, "good base"}});
    writeRim(modules / "bar_s.rim", {{"bar_static", ResType::Txt, "static"}});

    auto director = makeDirector(game.path);
    {
        CwdGuard guard(cwd.path);
        director->init();
    }

    director->onModuleLoad("bar");
    auto sourcesForBar = _count();
    EXPECT_EQ("good base", find("probe"));

    EXPECT_THROW(director->onModuleLoad("foo"), ValidationException);
    EXPECT_FALSE(has("probe")) << "the previous module was retired before the attempt";

    director->onModuleLoad("bar");
    EXPECT_EQ("good base", find("probe"));
    EXPECT_TRUE(has("bar_static"));
    EXPECT_FALSE(has("from_area")) << "nothing of the failed module survived into this one";
    EXPECT_EQ(sourcesForBar, _count());
}

INSTANTIATE_TEST_SUITE_P(Backends,
                         ArchiveValidationDirectorTest,
                         testing::Values(Backend::Legacy, Backend::Extract),
                         backendName);
