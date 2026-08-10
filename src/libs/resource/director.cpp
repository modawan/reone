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

#include "reone/resource/director.h"

#include "reone/graphics/di/services.h"
#include "reone/graphics/options.h"
#include "reone/graphics/types.h"
#include "reone/resource/di/services.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/modulediscovery.h"
#include "reone/resource/modulemount.h"
#include "reone/resource/modulepolicy.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/dialogs.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/provider/lips.h"
#include "reone/resource/provider/paths.h"
#include "reone/resource/provider/scripts.h"
#include "reone/resource/resources.h"
#include "reone/script/di/services.h"
#include "reone/system/fileutil.h"
#include "reone/system/logutil.h"

#include <array>

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

static constexpr char kModulesRootId[] = "modules";
static constexpr char kLipsRootId[] = "lips";
/// Root the staged active module is offered under. reone has no current-game
/// directory; the equivalent is the save archive already in scope.
static constexpr char kStagedRootId[] = "currentgame";

static constexpr char kModuleSaveTableName[] = "modulesave";
static constexpr char kIncludeInSaveColumn[] = "includeinsave";

static const std::vector<std::string> g_globalLipFiles {"global.mod", "localization.mod"};

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
    // Only a primary-eligible archive introduces a module name. Excluding just
    // "_s.rim" would expose _a and _adx as modules of their own, which they
    // never are.
    //
    // Only the module location is enumerated. The lips location is searched for
    // a known module's support archives, but the global archives it also holds
    // are not modules and must not be offered as ones.
    auto roots = discoverModuleRoots({modulesSearchRoot()});
    return std::set<std::string>(roots.begin(), roots.end());
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

bool usesBucketedLookup(GameID game) {
    return game == GameID::TSL;
}

std::optional<ResourceSourceBucket> ResourceDirector::bucketOf(ResourceSourceBucket bucket) const {
    if (!bucketed()) {
        return std::nullopt;
    }
    return bucket;
}

/**
 * Sources that are not Odyssey game data.
 *
 * The shader pack is generated by this project and holds only GLSL; the
 * executable holds only cursors. Neither serves a resource type that any other
 * source serves, so neither has a place in the raw lookup order, and inventing
 * one for them would be wrong in a way no bucket could express. They are kept
 * in a separate insertion-ordered list instead, which also keeps the Odyssey
 * list homogeneous once buckets are in use.
 */
void ResourceDirector::loadAuxiliaryResources() {
    _auxResources.addERF(getFileIgnoreCase(std::filesystem::current_path(), kShaderPackFilename));

    std::optional<std::filesystem::path> exePath;
    if (_gameId == GameID::TSL) {
        exePath = findFileIgnoreCase(_gamePath, kExeFilenameTsl);
    } else {
        exePath = findFileIgnoreCase(_gamePath, kExeFilenameKotor);
    }
    if (exePath) {
        _auxResources.addEXE(*exePath);
    }
}

/**
 * Streamed audio directories.
 *
 * The traced engine reaches these through its path and streaming systems
 * rather than through ordinary raw lookup, so they are not given a bucket. For
 * an activated game they leave the Odyssey list entirely, and which of the two
 * lists a clip is read from becomes the streaming subsystem's decision rather
 * than a question of source priority. For a game that is not activated they
 * stay exactly where they have always been.
 */
void ResourceDirector::loadStreamResources() {
    auto &target = bucketed() ? _auxResources : _resources;

    auto musicPath = findFileIgnoreCase(_gamePath, kMusicDirectoryName);
    if (musicPath) {
        target.addFolder(*musicPath);
    }
    auto soundsPath = findFileIgnoreCase(_gamePath, kSoundsDirectoryName);
    if (soundsPath) {
        target.addFolder(*soundsPath);
    }

    if (_gameId == GameID::TSL) {
        auto voicePath = findFileIgnoreCase(_gamePath, kVoiceDirectoryName);
        if (voicePath) {
            target.addFolder(*voicePath);
        }
    } else {
        auto wavesPath = findFileIgnoreCase(_gamePath, kWavesDirectoryName);
        if (wavesPath) {
            target.addFolder(*wavesPath);
        }
    }
}

void ResourceDirector::loadGlobalResources() {
    loadAuxiliaryResources();

    auto keyPath = findFileIgnoreCase(_gamePath, kKeyFilename);
    if (keyPath) {
        _resources.addKEY(*keyPath, bucketOf(ResourceSourceBucket::KeyBif));
    }

    auto texPacksPath = findFileIgnoreCase(_gamePath, kTexturePackDirectoryName);
    if (texPacksPath) {
        auto guiPackPath = findFileIgnoreCase(*texPacksPath, kTexturePackFilenameGUI);
        if (guiPackPath) {
            _resources.addERF(*guiPackPath,
                              ContainerKind::Global,
                              bucketOf(ResourceSourceBucket::EncapsulatedClass2));
        }
        auto &texPack = kTexQualityToTexPack.at(_graphicsOpt.textureQuality);
        auto texPackPath = findFileIgnoreCase(*texPacksPath, texPack);
        if (texPackPath) {
            _resources.addERF(*texPackPath,
                              ContainerKind::Global,
                              bucketOf(ResourceSourceBucket::EncapsulatedClass2));
        }
    }

    loadStreamResources();

    auto lipsPath = findFileIgnoreCase(_gamePath, kLipsDirectoryName);
    if (lipsPath) {
        for (auto &filename : g_globalLipFiles) {
            auto globalLipPath = findFileIgnoreCase(*lipsPath, filename);
            if (globalLipPath) {
                // A global LIP archive is a caller-selected encapsulated
                // source. Class 2 keeps it where it has always sat relative to
                // the texture packs it is mounted after.
                _resources.addERF(*globalLipPath,
                                  ContainerKind::Global,
                                  bucketOf(ResourceSourceBucket::EncapsulatedClass2));
            }
        }
    }

    auto patchPath = findFileIgnoreCase(_gamePath, kPatchFilename);
    if (patchPath) {
        _resources.addERF(*patchPath,
                          ContainerKind::Global,
                          bucketOf(ResourceSourceBucket::EncapsulatedClass1));
    }
    auto overridePath = findFileIgnoreCase(_gamePath, kOverrideDirectoryName);
    if (overridePath) {
        _resources.addFolder(*overridePath,
                             ContainerKind::Global,
                             bucketOf(ResourceSourceBucket::LooseDirectory));
    }
}

void ResourceDirector::loadModuleResources(const std::string &name) {
    if (bucketed()) {
        loadModuleResourcesFromPolicy(name);
    } else {
        loadModuleResourcesLegacy(name);
    }
}

/**
 * Module loading for a game that is not yet activated.
 *
 * This is the flat insertion-ordered stack the engine has always used. It is
 * retained unchanged rather than reimplemented, so that activating one game
 * cannot change the other.
 */
void ResourceDirector::loadModuleResourcesLegacy(const std::string &name) {
    std::optional<std::filesystem::path> modulesPath = findFileIgnoreCase(_gamePath, kModulesDirectoryName);
    if (!modulesPath) {
        throw ResourceNotFoundException("Modules directory not found");
    }

    loadRIM(*modulesPath, name, ContainerKind::Local);
    loadRIM(*modulesPath, name + "_s", ContainerKind::Local);
    loadERF(*modulesPath, name, ContainerKind::Local);
    loadERF(*modulesPath, name + "_loc", ContainerKind::Local);

    if (auto lipsPath = findFileIgnoreCase(_gamePath, kLipsDirectoryName)) {
        loadERF(*lipsPath, name + "_loc", ContainerKind::Local);
    }

    if (_gameId == GameID::TSL) {
        loadERF(*modulesPath, name + "_dlg", ContainerKind::Local);
    }
}

ModuleSearchRoot ResourceDirector::modulesSearchRoot() {
    auto modulesPath = findFileIgnoreCase(_gamePath, kModulesDirectoryName);
    if (!modulesPath) {
        throw ResourceNotFoundException("Modules directory not found");
    }
    return ModuleSearchRoot {kModulesRootId, *modulesPath, ModulePrimaryOrigin::Modules, 0, 0};
}

std::vector<ModuleSearchRoot> ResourceDirector::moduleSearchRoots() {
    std::vector<ModuleSearchRoot> roots;
    roots.push_back(modulesSearchRoot());
    // The lips location supplies a module's own support archives. It is
    // searched after the module location, which is the order their mounts are
    // attempted in.
    if (auto lipsPath = findFileIgnoreCase(_gamePath, kLipsDirectoryName)) {
        roots.push_back(ModuleSearchRoot {kLipsRootId, *lipsPath, ModulePrimaryOrigin::Modules, 0, 0});
    }
    return roots;
}

/**
 * Offer the module's staged active state, when a save archive in scope holds
 * it.
 *
 * This is the explicit nested-source route, not discovery: exactly two ids are
 * probed, and the container is never walked into. The saved image is checked
 * before the saved archive because the policy ranks it first.
 */
void ResourceDirector::addStagedModuleSources(const std::string &moduleRoot,
                                              RuntimeModuleSourceIndex &index) {
    struct Probe {
        ResType type;
        ModuleArchiveFamily family;
        const char *extension;
    };
    static const std::array<Probe, 2> kProbes {{
        {ResType::Rsv, ModuleArchiveFamily::SavedResourceImage, ".rsv"},
        {ResType::Sav, ModuleArchiveFamily::SavedArchive, ".sav"},
    }};

    for (const auto &probe : kProbes) {
        auto id = ResourceId(moduleRoot, probe.type);
        if (!_resources.find(id)) {
            continue;
        }
        ModuleSourceCandidate candidate;
        candidate.sourceId = std::string(kStagedRootId) + ":" + moduleRoot + probe.extension;
        candidate.rootId = kStagedRootId;
        candidate.origin = ModulePrimaryOrigin::GameInProgress;
        candidate.family = probe.family;
        index.add(RuntimeModuleSource {std::move(candidate), id});
    }
}

/**
 * Whether saved state for this module may be selected at all.
 *
 * The table is keyed by row label, which is the module root. Every way of
 * failing to find an exclusion means include: no table, no row, or no cell.
 */
bool ResourceDirector::includeModuleInSave(const std::string &moduleRoot) {
    auto table = _twoDas.get(kModuleSaveTableName);
    if (!table) {
        return true;
    }
    int row = table->indexByLabel(moduleRoot);
    if (row == -1) {
        return true;
    }
    auto include = table->getIntOpt(row, kIncludeInSaveColumn);
    if (!include) {
        return true;
    }
    return *include != 0;
}

void ResourceDirector::loadModuleResourcesFromPolicy(const std::string &name) {
    auto discovered = discoverModuleSources(name, moduleSearchRoots());
    if (discovered.nameRejection) {
        throw ResourceNotFoundException("Not a supported module name: " + name);
    }

    RuntimeModuleSourceIndex index;
    for (const auto &source : discovered.sources) {
        index.add(RuntimeModuleSource {source.candidate, source.path});
    }
    addStagedModuleSources(discovered.moduleRoot, index);

    ModulePolicyRequest request;
    request.game = _gameId;
    request.moduleName = discovered.moduleRoot;
    request.includeInSave = includeModuleInSave(discovered.moduleRoot);

    auto inventory = index.inventory();
    // Saved mode is a property of the form the active table takes, not of the
    // mere presence of saved state.
    //
    // The original sequence is: wipe the current-game location, select exactly
    // one primary with the image checked before the archive, stage that one
    // artifact, then ask whether the current-game location now holds an
    // archive. Because the wipe leaves nothing behind and exactly one artifact
    // is staged, that question can only answer yes when the selected primary
    // was the archive. Deriving it from the selection is therefore the same
    // answer, not an approximation, and it is why the presence of an archive
    // alongside a winning image must not set saved mode.
    //
    // Selection does not read savedMode, so asking for it first cannot change
    // the answer.
    auto selection = selectModulePrimary(request, inventory);
    request.savedMode = selection &&
                        selection->candidate.kind == ModulePrimaryKind::SavedArchive;

    auto plan = planModuleLoad(request, inventory);

    ModuleMountExecutor executor(_resources, index);
    auto report = executor.run(plan);

    if (!plan.primary) {
        warn("No primary source found for module '" + discovered.moduleRoot + "'");
    }
    if (report.requiredFailure) {
        warn("Module '" + discovered.moduleRoot + "' had a required source fail to mount");
    }
    for (const auto &file : discovered.rejected) {
        debug("Ignoring unsupported module file: " + file.filename);
    }
    // Which source supplied a resource is not recoverable from the payload, so
    // record the sequence that was actually mounted.
    for (const auto &outcome : report.outcomes) {
        debug(str(boost::format("Module source %s: %s") %
                  (outcome.mounted ? "mounted" : "skipped") % outcome.sourceId));
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
    _resources.addFolder(*savePath,
                         ContainerKind::Global,
                         bucketOf(ResourceSourceBucket::LooseDirectory));

    _savegamePath = findFileIgnoreCase(*savePath, "savegame.sav");
    if (!_savegamePath) {
        throw ResourceNotFoundException("savegame.sav not found");
    }

    // Add savegame resource archive.
    //
    // The archive itself is genuine: a save slot really does hold a MOD V1.0
    // container packed from the game-in-progress directory. What differs is
    // what is done with it. The original engine unpacks the container back out
    // into the game-in-progress directory and reads the loose results, so the
    // container is never a source in its own right; reone instead mounts it
    // directly and reads through it.
    //
    // Class 2 is therefore a compatibility placement for that direct mount
    // rather than an evidenced bucket for the container: it is the least
    // privileged position that still lets the save supply resources held
    // nowhere else, and it cannot shadow a live module source. Replacing the
    // direct mount with an unpack step belongs to the save architecture work.
    //
    // The ownership here is known to be wrong: these are save-slot sources
    // mounted as global, so clearSave does not retire them. That is left
    // untouched, exactly as it is today.
    _resources.addERF(*_savegamePath,
                      ContainerKind::Global,
                      bucketOf(ResourceSourceBucket::EncapsulatedClass2));
}

void ResourceDirector::loadRIM(const std::filesystem::path &path, const std::string &name, ContainerKind kind) {
    // Try to find a module with the same name in already loaded resources.
    // Same idea as in loadERF.
    std::optional<Resource> res = _resources.find(ResourceId(name, ResType::Res));
    if (res) {
        _resources.addMemRIM(res->data, kind);
        return;
    }

    if (auto rimPath = findFileIgnoreCase(path, name + ".rim")) {
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
    // Type 0x0809 is the saved module archive, which is what a nested module
    // in savegame.sav is stored as. This probe is unchanged; only the name it
    // is spelled with was wrong before.
    std::optional<Resource> res = _resources.find(ResourceId(name, ResType::Sav));
    if (res) {
        _resources.addMemERF(res->data, kind);
        return;
    }

    if (auto modPath = findFileIgnoreCase(path, name + ".mod")) {
        _resources.addERF(*modPath, kind);
    }

    if (auto erfPath = findFileIgnoreCase(path, name + ".erf")) {
        _resources.addERF(*erfPath, kind);
    }
}

} // namespace resource

} // namespace reone
