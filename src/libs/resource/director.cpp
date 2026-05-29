/*
 * Copyright (c) 2020-2023 The reone project contributors
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

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "reone/resource/director.h"

#include <cstring>
#include <string_view>

#include "reone/graphics/di/services.h"
#include "reone/graphics/options.h"
#include "reone/graphics/types.h"
#include "reone/resource/di/services.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/dialogs.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/provider/lips.h"
#include "reone/resource/provider/paths.h"
#include "reone/resource/provider/scripts.h"
#include "reone/resource/resources.h"
#include "reone/script/di/services.h"
#include "reone/system/fileutil.h"

using namespace reone::graphics;
using namespace reone::resource;

namespace reone {

namespace resource {

static constexpr char kKeyFilename[] = "chitin.key";
static constexpr char kPatchFilename[] = "patch.erf";
static constexpr char kTexturePackDirectoryName[] = "texturepacks";
static constexpr char kMusicDirectoryName[] = "streammusic";
static constexpr char kSoundsDirectoryName[] = "streamsounds";
static constexpr char kWavesDirectoryName[] = "streamwaves";
static constexpr char kVoiceDirectoryName[] = "streamvoice";
static constexpr char kModulesDirectoryName[] = "modules";
static constexpr char kSavesDirectoryName[] = "saves";
static constexpr char kLipsDirectoryName[] = "lips";
static constexpr char kOverrideDirectoryName[] = "override";

static constexpr char kTexturePackFilenameGUI[] = "swpc_tex_gui.erf";
static constexpr char kTexturePackFilenameHigh[] = "swpc_tex_tpa.erf";
static constexpr char kTexturePackFilenameMedium[] = "swpc_tex_tpb.erf";
static constexpr char kTexturePackFilenameLow[] = "swpc_tex_tpc.erf";

static constexpr char kExeFilenameKotor[] = "swkotor.exe";
static constexpr char kExeFilenameTsl[] = "swkotor2.exe";

static constexpr char kShaderPackFilename[] = "shaderpack.erf";

static const std::vector<std::string> g_globalLipFiles {"global.mod", "localization.mod"};

namespace {

bool hasRimHeader(const ByteBuffer &data) {
    return data.size() >= 8 && std::memcmp(data.data(), "RIM V1.0", 8) == 0;
}

bool hasErfHeader(const ByteBuffer &data) {
    if (data.size() < 8) {
        return false;
    }
    auto sig = std::string_view(data.data(), 8);
    return sig == "ERF V1.0" || sig == "MOD V1.0";
}

} // namespace

static const std::unordered_map<TextureQuality, std::string> kTexQualityToTexPack {
    {TextureQuality::High, kTexturePackFilenameHigh},
    {TextureQuality::Medium, kTexturePackFilenameMedium},
    {TextureQuality::Low, kTexturePackFilenameLow}};

void ResourceDirector::init() {
    loadGlobalResources();
}

void ResourceDirector::onModuleLoad(const std::string &name) {
    _dialogs.clear();
    _paths.clear();
    _scripts.clear();
    _lips.clear();
    _gffs.clear();
    _resources.clearLocal();

    loadModuleResources(name);
}

void ResourceDirector::onGameLoad(std::string_view name) {
    _resources.clearSave();
    loadSaveGameResources(name);
}

std::set<std::string> ResourceDirector::moduleNames() {
    auto moduleNames = std::set<std::string>();
    auto modulesPath = findFileIgnoreCase(_gamePath, kModulesDirectoryName);
    if (!modulesPath) {
        throw ResourceNotFoundException("Modules directory not found");
    }
    for (auto &entry : std::filesystem::directory_iterator(*modulesPath)) {
        auto filename = boost::to_lower_copy(entry.path().filename().string());
        if (boost::ends_with(filename, ".mod") || (boost::ends_with(filename, ".rim") && !boost::ends_with(filename, "_s.rim"))) {
            auto moduleName = boost::to_lower_copy(filename.substr(0, filename.size() - 4));
            moduleNames.insert(moduleName);
        }
    }
    return moduleNames;
}

std::set<std::string> ResourceDirector::saveNames() {
    auto names = std::set<std::string>();
    auto savesPath = findFileIgnoreCase(_gamePath, kSavesDirectoryName);
    if (!savesPath) {
        return names;
    }
    for (auto &entry : std::filesystem::directory_iterator(*savesPath)) {
        names.insert(boost::to_lower_copy(entry.path().filename().string()));
    }
    return names;
}

void ResourceDirector::loadGlobalResources() {
    auto shaderPackPath = findFileIgnoreCase(std::filesystem::current_path(), kShaderPackFilename);
    if (shaderPackPath) {
        _resources.addERF(*shaderPackPath);
    }
#ifdef __EMSCRIPTEN__
    else {
        auto glslPath = findFileIgnoreCase(std::filesystem::current_path(), "glsl");
        if (glslPath) {
            _resources.addFolder(*glslPath);
        } else {
            throw ResourceNotFoundException("shaderpack.erf or /glsl folder not found");
        }
    }
#else
    else {
        throw ResourceNotFoundException(kShaderPackFilename);
    }
#endif

    auto keyPath = findFileIgnoreCase(_gamePath, kKeyFilename);
    if (keyPath) {
        _resources.addKEY(*keyPath);
    }

    auto texPacksPath = findFileIgnoreCase(_gamePath, kTexturePackDirectoryName);
    if (texPacksPath) {
        auto guiPackPath = findFileIgnoreCase(*texPacksPath, kTexturePackFilenameGUI);
        if (guiPackPath) {
            _resources.addERF(*guiPackPath);
        }
        auto &texPack = kTexQualityToTexPack.at(_graphicsOpt.textureQuality);
        auto texPackPath = findFileIgnoreCase(*texPacksPath, texPack);
        if (texPackPath) {
            _resources.addERF(*texPackPath);
        }
    }

    auto musicPath = findFileIgnoreCase(_gamePath, kMusicDirectoryName);
    if (musicPath) {
        _resources.addFolder(*musicPath);
    }
    auto soundsPath = findFileIgnoreCase(_gamePath, kSoundsDirectoryName);
    if (soundsPath) {
        _resources.addFolder(*soundsPath);
    }

    if (_gameId == GameID::TSL) {
        auto voicePath = findFileIgnoreCase(_gamePath, kVoiceDirectoryName);
        if (voicePath) {
            _resources.addFolder(*voicePath);
        }
    } else {
        auto wavesPath = findFileIgnoreCase(_gamePath, kWavesDirectoryName);
        if (wavesPath) {
            _resources.addFolder(*wavesPath);
        }
    }

    auto lipsPath = findFileIgnoreCase(_gamePath, kLipsDirectoryName);
    if (lipsPath) {
        for (auto &filename : g_globalLipFiles) {
            auto globalLipPath = findFileIgnoreCase(*lipsPath, filename);
            if (globalLipPath) {
                _resources.addERF(*globalLipPath);
            }
        }
    }

    auto patchPath = findFileIgnoreCase(_gamePath, kPatchFilename);
    if (patchPath) {
        _resources.addERF(*patchPath);
    }
    auto overridePath = findFileIgnoreCase(_gamePath, kOverrideDirectoryName);
    if (overridePath) {
        _resources.addFolder(*overridePath);
    }

    std::optional<std::filesystem::path> exePath;
    if (_gameId == GameID::TSL) {
        exePath = findFileIgnoreCase(_gamePath, kExeFilenameTsl);
    } else {
        exePath = findFileIgnoreCase(_gamePath, kExeFilenameKotor);
    }
    if (exePath) {
        _resources.addEXE(*exePath);
    }
}

void ResourceDirector::loadModuleResources(const std::string &name) {
    std::optional<std::filesystem::path> modulesPath = findFileIgnoreCase(_gamePath, kModulesDirectoryName);
    if (!modulesPath) {
        throw ResourceNotFoundException("Modules directory not found");
    }

#ifdef __EMSCRIPTEN__
    EM_ASM({ console.log("reone web: loadModuleResources begin " + UTF8ToString($0)); }, name.c_str());
#endif

    loadRIM(*modulesPath, name, ContainerKind::Local);
#ifdef __EMSCRIPTEN__
    EM_ASM({ console.log("reone web: loadModuleResources rim " + UTF8ToString($0)); }, name.c_str());
#endif
    loadRIM(*modulesPath, name + "_s", ContainerKind::Local);
#ifdef __EMSCRIPTEN__
    EM_ASM({ console.log("reone web: loadModuleResources rim_s done"); });
#endif
    loadERF(*modulesPath, name, ContainerKind::Local);
    loadERF(*modulesPath, name + "_loc", ContainerKind::Local);

    if (auto lipsPath = findFileIgnoreCase(_gamePath, kLipsDirectoryName)) {
#ifdef __EMSCRIPTEN__
        EM_ASM({ console.log("reone web: loadModuleResources lips_loc " + UTF8ToString($0)); }, (name + "_loc").c_str());
#endif
        loadERF(*lipsPath, name + "_loc", ContainerKind::Local);
    }

#ifdef __EMSCRIPTEN__
    EM_ASM({ console.log("reone web: loadModuleResources done " + UTF8ToString($0)); }, name.c_str());
#endif

    if (_gameId == GameID::TSL) {
        loadERF(*modulesPath, name + "_dlg", ContainerKind::Local);
    }
}

void ResourceDirector::loadSaveGameResources(std::string_view name) {
    auto allSavesPath = findFileIgnoreCase(_gamePath, kSavesDirectoryName);
    if (!allSavesPath) {
        throw ResourceNotFoundException("Saves directory not found");
    }

    auto savePath = findFileIgnoreCase(*allSavesPath, name);
    if (!savePath) {
        throw ResourceNotFoundException(str(boost::format("Save directory not found: %s") % name));
    }

    // Add savegame directory itself, so we can load globalvars.res
    // partytable.res and savenfo.res.
    _resources.addFolder(*savePath);

    _savegamePath = findFileIgnoreCase(*savePath, "savegame.sav");
    if (!_savegamePath) {
        throw ResourceNotFoundException("savegame.sav not found");
    }

    // Add savegame resource archive.
    _resources.addERF(*_savegamePath);
}

void ResourceDirector::loadRIM(const std::filesystem::path &path, const std::string &name, ContainerKind kind) {
    // Try to find a module with the same name in already loaded resources.
    // Same idea as in loadERF.
    std::optional<Resource> res = _resources.find(ResourceId(name, ResType::Res));
    if (res && hasRimHeader(res->data)) {
        _resources.addMemRIM(res->data, kind);
        return;
    }

    if (auto rimPath = findFileIgnoreCase(path, name + ".rim")) {
#ifdef __EMSCRIPTEN__
        EM_ASM({ console.log("reone web: addRIM " + UTF8ToString($0)); }, rimPath->generic_string().c_str());
#endif
        _resources.addRIM(*rimPath, kind);
    }
}

void ResourceDirector::loadERF(const std::filesystem::path &path, const std::string &name, ContainerKind kind) {
    // Try to find a module with the same name in already loaded resources.
    //
    // This allows us to support savegame archives: savegame.sav is an ERF
    // archive that contains ERF modules. When loading from a save game we add
    // savegame.sav ERF container. Module lookups resolve to modules from this
    // container, and fall back to filesystem search if the module is not in the
    // save archive.
    std::optional<Resource> res = _resources.find(ResourceId(name, ResType::Mod));
    if (res && hasErfHeader(res->data)) {
        _resources.addMemERF(res->data, kind);
        return;
    }

    if (auto modPath = findFileIgnoreCase(path, name + ".mod")) {
#ifdef __EMSCRIPTEN__
        EM_ASM({ console.log("reone web: addERF mod " + UTF8ToString($0)); }, modPath->generic_string().c_str());
#endif
        _resources.addERF(*modPath, kind);
    }

    if (auto erfPath = findFileIgnoreCase(path, name + ".erf")) {
#ifdef __EMSCRIPTEN__
        EM_ASM({ console.log("reone web: addERF erf " + UTF8ToString($0)); }, erfPath->generic_string().c_str());
#endif
        _resources.addERF(*erfPath, kind);
    }
}

} // namespace resource

} // namespace reone
