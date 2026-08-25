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
#include "reone/resource/mounttransaction.h"
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
static constexpr char kSavesDirectoryName[] = "saves";
static constexpr char kOverrideDirectoryName[] = "override";
static constexpr char kRimsDirectoryName[] = "rims";
static constexpr char kGlobalRimFilename[] = "global.rim";
static constexpr char kPlayersBasename[] = "players";
static constexpr char kOverrideTexturesBasename[] = "textures";

static constexpr char kTexturePackFilenameGUI[] = "swpc_tex_gui.erf";
static constexpr char kTexturePackFilenameHigh[] = "swpc_tex_tpa.erf";
static constexpr char kTexturePackFilenameMedium[] = "swpc_tex_tpb.erf";
static constexpr char kTexturePackFilenameLow[] = "swpc_tex_tpc.erf";

static constexpr char kExeFilenameKotor[] = "swkotor.exe";
static constexpr char kExeFilenameTsl[] = "swkotor2.exe";

static constexpr char kShaderPackFilename[] = "shaderpack.erf";

/// Root fixed-name installation support archives are offered under.
static constexpr char kRootRootId[] = "hd0";
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

/**
 * Retire the module that was here and put the next one in its place.
 *
 * Both module owners go: the support sources of the old module, and the state
 * of the old module. They are retired separately even though they happen to be
 * retired together here, because they are two lifetimes; collapsing them would
 * make it impossible to replace one without the other later.
 *
 * Global and save-slot sources are untouched. A module transition is no reason
 * to rebuild the installation or to leave the save that is loaded.
 */
void ResourceDirector::onModuleLoad(const std::string &name) {
    _dialogs.clear();
    _paths.clear();
    _scripts.clear();
    _lips.clear();
    _gffs.clear();
    _resources.clearOwner(ResourceOwner::ActiveModule);
    _resources.clearOwner(ResourceOwner::ActiveModuleState);

    loadModuleResources(name);
}

/**
 * Validate and atomically replace the loaded save session.
 *
 * Candidate construction validates the working-state archive first. Committing
 * retires decoded GFF entries from the prior session; a rejected candidate
 * leaves the active session and cache untouched.
 */
void ResourceDirector::onGameLoad(std::string_view name) {
    commitSaveSession(buildSaveSession(name));
}

void ResourceDirector::onGameLoad(const SaveSlotDescriptor &slot) {
    commitSaveSession(buildSaveSession(slot));
}

void ResourceDirector::commitSaveSession(std::unique_ptr<SaveSessionState> candidate) {
    _resources.clearOwner(ResourceOwner::SaveSlot);
    _saveSession = std::move(candidate);
    _gffs.clear();
}

void ResourceDirector::onNewGame() {
    _resources.clearOwner(ResourceOwner::SaveSlot);
    _resources.clearOwner(ResourceOwner::ActiveModuleState);
    _saveSession = std::make_unique<SaveSessionState>();
    _gffs.clear();
}

std::optional<Resource> ResourceDirector::findSaveMetadata(const ResourceId &id) {
    if (!_saveSession) {
        return std::nullopt;
    }
    return _saveSession->findMetadata(id);
}

std::optional<Resource> ResourceDirector::findSaveWorking(const ResourceId &id) {
    if (!_saveSession) {
        return std::nullopt;
    }
    return _saveSession->findWorking(id);
}

std::unordered_set<ResourceId> ResourceDirector::saveWorkingResourceIds() const {
    if (!_saveSession) {
        return {};
    }
    return _saveSession->workingState()->resourceIds();
}

std::shared_ptr<const SaveWorkingState>
ResourceDirector::committedSaveWorkingState() const {
    return _saveSession ? _saveSession->workingState() : nullptr;
}

std::optional<SaveSlotDescriptor> ResourceDirector::saveSlotDescriptor() const {
    return _saveSession ? _saveSession->slotDescriptor() : std::nullopt;
}

void ResourceDirector::adoptSaveWorkingState(
    std::shared_ptr<const SaveWorkingState> state) {
    if (!_saveSession) {
        _saveSession = std::make_unique<SaveSessionState>();
    }
    _saveSession->replaceWorkingState(std::move(state));
}

void ResourceDirector::adoptPublishedSave(
    SaveSlotDescriptor descriptor,
    std::shared_ptr<const SaveWorkingState> state) {
    auto candidate = std::make_unique<SaveSessionState>(
        std::move(descriptor), std::move(state));
    _saveSession = std::move(candidate);
}

std::set<std::string> ResourceDirector::moduleNames() {
    // Only a primary-eligible archive introduces a module name. Excluding just
    // "_s.rim" would expose _a and _adx as modules of their own, which they
    // never are.
    //
    // Enumerate shared primary roots only. LIPS roots remain support-only.
    auto roots = discoverModuleRoots(
        primaryModuleSearchRoots(_gameId, _gamePath, _odysseyRoots));
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

/**
 * Whether a game's sources are placed in the Odyssey raw lookup order.
 *
 * Both games are, and the answer no longer varies. It stays a named question
 * because a source list is homogeneous: anything mounting into a game's list
 * has to agree with the director about how that list is ordered, and the
 * toolkit asks the same question when it mounts after startup.
 */
bool usesBucketedLookup(GameID game) {
    return game == GameID::KotOR || game == GameID::TSL;
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
    if (_gameId == GameID::KotOR) {
        loadK1StreamResources();
        return;
    }
    // K2 keeps its streamed audio out of the raw lookup order. Its own startup
    // is not traced here, so this is left exactly as it was.
    auto musicPath = findFileIgnoreCase(_gamePath, kMusicDirectoryName);
    if (musicPath) {
        _auxResources.addFolder(*musicPath);
    }
    auto soundsPath = findFileIgnoreCase(_gamePath, kSoundsDirectoryName);
    if (soundsPath) {
        _auxResources.addFolder(*soundsPath);
    }
    auto voicePath = findFileIgnoreCase(_gamePath, kVoiceDirectoryName);
    if (voicePath) {
        _auxResources.addFolder(*voicePath);
    }
}

/**
 * K1 streamed audio, as its startup registers it.
 *
 * K1 is not uniform about this and the difference is evidenced, not a
 * simplification. Startup registers HD0:STREAMMUSIC and HD0:STREAMWAVES as
 * ordinary resource directories, so both sit in the loose bucket and can answer
 * an ordinary lookup. HD0:STREAMSOUNDS has no bare alias and is never
 * registered: the only string for it is the path template HD0:STREAMSOUNDS\%s,
 * so it is reached by constructed path and stays out of raw lookup entirely.
 *
 * The sounds directory therefore goes to the auxiliary list, where the audio
 * provider still finds it and no ordinary lookup can.
 */
void ResourceDirector::loadK1StreamResources() {
    // Waves first, then music: that is the order startup registers them in,
    // and within the loose bucket the later one wins.
    auto wavesPath = findFileIgnoreCase(_gamePath, kWavesDirectoryName);
    if (wavesPath) {
        _resources.addFolder(*wavesPath,
                             ResourceOwner::Global,
                             bucketOf(ResourceSourceBucket::LooseDirectory));
    }
    auto musicPath = findFileIgnoreCase(_gamePath, kMusicDirectoryName);
    if (musicPath) {
        _resources.addFolder(*musicPath,
                             ResourceOwner::Global,
                             bucketOf(ResourceSourceBucket::LooseDirectory));
    }
    auto soundsPath = findFileIgnoreCase(_gamePath, kSoundsDirectoryName);
    if (soundsPath) {
        _auxResources.addFolder(*soundsPath);
    }
}

/**
 * The K1 rims location and its global image.
 *
 * These are two sources with two roles, and K1 startup registers them
 * separately. The directory itself is a resource directory, so a loose file
 * dropped there answers like any other loose file. GLOBAL.rim is registered on
 * its own as a resource image.
 *
 * Only that one image is mounted. The directory holds several other images,
 * and startup names none of them: the rest are reached as module or menu
 * images when something asks for them, so mounting them here because the files
 * exist would invent sources the engine never registers.
 *
 * Both games register this source. Their startup routines use the same
 * RIMS: logical directory even though the other global sources around it
 * differ by game.
 */
void ResourceDirector::loadRimsDirectory() {
    if (auto rimsPath = findFileIgnoreCase(_gamePath, kRimsDirectoryName)) {
        _resources.addFolder(*rimsPath,
                             ResourceOwner::Global,
                             bucketOf(ResourceSourceBucket::LooseDirectory));
    }
}

/**
 * The global resource image, registered last of the startup sources.
 *
 * Only this one image is mounted. The rims location holds several others and
 * startup names none of them: the rest are reached as module or menu images
 * when something asks for them, so mounting them because the files exist would
 * invent sources the engine never registers.
 */
void ResourceDirector::loadGlobalRimResource() {
    auto rimsPath = findFileIgnoreCase(_gamePath, kRimsDirectoryName);
    if (!rimsPath) {
        return;
    }
    if (auto globalRimPath = findFileIgnoreCase(*rimsPath, kGlobalRimFilename)) {
        _resources.addRIM(*globalRimPath,
                          ResourceOwner::Global,
                          bucketOf(ResourceSourceBucket::ResourceImage));
    }
}

/**
 * The K1 override texture archive.
 *
 * Startup mounts the exact basename OVERRIDE:textures as an encapsulated
 * source with source id 1, which is class 1: above every image and class-2
 * archive, below the loose directories. It is a resource-manager source in its
 * own right and has nothing to do with the texture packs, which are class 2 and
 * mounted by the texture subsystem.
 *
 * K1 mounts exactly this one base location. It enumerates no configured roots
 * for it, so neither does this.
 *
 * It is registered immediately before the patch archive because that is the
 * order startup registers them in, and the two share a bucket: within class 1
 * the later mount wins, so the order is the behaviour. Stock installations ship
 * no such archive, and its absence is normal.
 */
void ResourceDirector::loadOverrideTexturesResource() {
    if (_gameId != GameID::KotOR) {
        return;
    }
    auto overridePath = findFileIgnoreCase(_gamePath, kOverrideDirectoryName);
    if (!overridePath) {
        return;
    }
    auto texturesPath = findEncapsulatedByBasename(*overridePath, kOverrideTexturesBasename);
    if (!texturesPath) {
        return;
    }
    _resources.addERF(*texturesPath,
                      ResourceOwner::Global,
                      bucketOf(ResourceSourceBucket::EncapsulatedClass1));
}

/**
 * The GUI and quality-selected texture packs.
 *
 * Both games mount these as encapsulated class-2 sources. K1 does it from its
 * own LoadTexturePack, which builds the path under the TEXTUREPACKS: alias and
 * hands it to AddEncapsulatedResourceFile with source id 2 into a four-slot
 * table; K2 does the same. They are not startup-table sources in either game,
 * but they are ordinary resource-manager sources.
 */
void ResourceDirector::loadTexturePackResources() {
    auto texPacksPath = findFileIgnoreCase(_gamePath, kTexturePackDirectoryName);
    if (!texPacksPath) {
        return;
    }
    if (auto guiPackPath = findFileIgnoreCase(*texPacksPath, kTexturePackFilenameGUI)) {
        _resources.addERF(*guiPackPath,
                          ResourceOwner::Global,
                          bucketOf(ResourceSourceBucket::EncapsulatedClass2));
    }
    auto &texPack = kTexQualityToTexPack.at(_graphicsOpt.textureQuality);
    if (auto texPackPath = findFileIgnoreCase(*texPacksPath, texPack)) {
        _resources.addERF(*texPackPath,
                          ResourceOwner::Global,
                          bucketOf(ResourceSourceBucket::EncapsulatedClass2));
    }
}

/**
 * The K1 player archive.
 *
 * K1 mounts this from its module support phase but never removes it: that path
 * removes only temporary directories, and re-adding the same exact filename
 * rebuilds the existing table in place rather than inserting another. Its
 * observable lifetime is therefore the whole session at a fixed position, which
 * is what mounting it once here reproduces. Module ownership would retire and
 * re-mount it on every transition, moving it to the newest position in its
 * bucket each time, which the original never does.
 *
 * The engine mounts an exact basename, so the container extension is probed
 * rather than assumed.
 */
void ResourceDirector::loadPlayerSupportResource() {
    auto playersPath = findEncapsulatedByBasename(_gamePath, kPlayersBasename);
    if (!playersPath) {
        return;
    }
    _resources.addERF(*playersPath,
                      ResourceOwner::Global,
                      bucketOf(ResourceSourceBucket::EncapsulatedClass2));
}

/**
 * K1 startup sources, in the order K1 registers them.
 *
 * The order is the behaviour. Four of these share the loose bucket and two
 * share class 1, and within a bucket the source registered later wins, so
 * reproducing the chronology is what reproduces the winners. K1 registers the
 * override directory early and the streaming directories late, which means a
 * streamed asset outranks an override file of the same resref and type: the
 * opposite of what mounting override last would give.
 *
 * Only sources reone reads through are mounted. The original also registers
 * TEMPCLIENT:, ERRORTEX:, SERVERVAULT: and PORTRAITS:, which reone has no
 * consumer for, and HD0:MOVIES, which reone plays by direct path.
 */
void ResourceDirector::loadK1GlobalResources() {
    if (auto overridePath = findFileIgnoreCase(_gamePath, kOverrideDirectoryName)) {
        _resources.addFolder(*overridePath,
                             ResourceOwner::Global,
                             bucketOf(ResourceSourceBucket::LooseDirectory));
    }
    if (auto keyPath = findFileIgnoreCase(_gamePath, kKeyFilename)) {
        _resources.addKEY(*keyPath, bucketOf(ResourceSourceBucket::KeyBif));
    }
    loadRimsDirectory();
    loadOverrideTexturesResource();
    if (auto patchPath = findFileIgnoreCase(_gamePath, kPatchFilename)) {
        _resources.addERF(*patchPath,
                          ResourceOwner::Global,
                          bucketOf(ResourceSourceBucket::EncapsulatedClass1));
    }
    loadK1StreamResources();
    loadGlobalRimResource();
    loadTexturePackResources();
    loadPlayerSupportResource();
}

void ResourceDirector::loadLiveResources() {
    for (std::size_t i = 0; i < _odysseyRoots.livePackages.size(); ++i) {
        if (!_odysseyRoots.livePackages[i]) {
            continue;
        }
        const auto &root = *_odysseyRoots.livePackages[i];
        auto basename = "live" + std::to_string(i + 1);
        auto attempt = [](const auto &mount) {
            try {
                mount();
            } catch (const std::exception &) {
                // Every package member is best-effort and independent.
            }
        };

        if (auto key = findFileIgnoreCase(root, basename + ".key")) {
            attempt([&] {
                _resources.addKEY(*key, bucketOf(ResourceSourceBucket::KeyBif));
            });
        }
        if (auto rims = findFileIgnoreCase(root, "rimsxbox")) {
            if (auto baseRim = findFileIgnoreCase(*rims, basename + ".rim")) {
                attempt([&] {
                    _resources.addRIM(*baseRim,
                                      ResourceOwner::Global,
                                      bucketOf(ResourceSourceBucket::ResourceImage));
                });
            }
        }
        if (auto mod = findFileIgnoreCase(root, basename + ".mod")) {
            attempt([&] {
                _resources.addERF(*mod,
                                  ResourceOwner::Global,
                                  bucketOf(ResourceSourceBucket::EncapsulatedClass2));
            });
        }
        if (auto rims = findFileIgnoreCase(root, "rimsxbox")) {
            if (auto dxRim = findFileIgnoreCase(*rims, basename + "dx.rim")) {
                attempt([&] {
                    _resources.addRIM(*dxRim,
                                      ResourceOwner::Global,
                                      bucketOf(ResourceSourceBucket::ResourceImage));
                });
            }
        }
        if (auto overridePath = findFileIgnoreCase(root, "override")) {
            if (auto textures = findEncapsulatedByBasename(*overridePath, "textures")) {
                attempt([&] {
                    _resources.addERF(*textures,
                                      ResourceOwner::Global,
                                      bucketOf(ResourceSourceBucket::EncapsulatedClass1));
                });
            }
        }
    }
}

void ResourceDirector::loadGlobalResources() {
    loadAuxiliaryResources();
    if (_gameId == GameID::KotOR) {
        loadK1GlobalResources();
        loadLiveResources();
        return;
    }

    auto keyPath = findFileIgnoreCase(_gamePath, kKeyFilename);
    if (keyPath) {
        _resources.addKEY(*keyPath, bucketOf(ResourceSourceBucket::KeyBif));
    }

    loadTexturePackResources();
    loadStreamResources();

    for (const auto &lipsPath : lipsRoots(_gameId, _gamePath, _odysseyRoots)) {
        for (auto &filename : g_globalLipFiles) {
            if (auto globalLipPath = findFileIgnoreCase(lipsPath, filename)) {
                // A global LIP archive is a caller-selected encapsulated
                // source. Class 2 keeps it where it has always sat relative to
                // the texture packs it is mounted after.
                _resources.addERF(*globalLipPath,
                                  ResourceOwner::Global,
                                  bucketOf(ResourceSourceBucket::EncapsulatedClass2));
            }
        }
    }

    if (auto patchPath = findFileIgnoreCase(_gamePath, kPatchFilename)) {
        _resources.addERF(*patchPath,
                          ResourceOwner::Global,
                          bucketOf(ResourceSourceBucket::EncapsulatedClass1));
    }
    for (const auto &overridePath : looseOverrideRoots(_gameId, _gamePath, _odysseyRoots)) {
        _resources.addFolder(overridePath,
                             ResourceOwner::Global,
                             bucketOf(ResourceSourceBucket::LooseDirectory));
    }
    loadRimsDirectory();
    loadGlobalRimResource();
    loadLiveResources();
}

void ResourceDirector::loadModuleResources(const std::string &name) {
    loadModuleResourcesFromPolicy(name);
}

std::vector<ModuleSearchRoot> ResourceDirector::moduleSearchRoots() {
    return resource::moduleSearchRoots(_gameId, _gamePath, _odysseyRoots);
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

    if (!_saveSession) {
        return;
    }
    auto workingState = _saveSession->workingState();
    for (const auto &probe : kProbes) {
        auto id = ResourceId(moduleRoot, probe.type);
        if (!workingState->contains(id)) {
            continue;
        }
        ModuleSourceCandidate candidate;
        candidate.sourceId = std::string(kStagedRootId) + ":" + moduleRoot + probe.extension;
        candidate.rootId = kStagedRootId;
        candidate.origin = ModulePrimaryOrigin::GameInProgress;
        candidate.family = probe.family;
        RuntimeModuleResourceReader reader = [workingState, id]() {
            return workingState->find(id);
        };
        index.add(RuntimeModuleSource {std::move(candidate), std::move(reader)});
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
    for (auto &source : discoverRimsModuleAdjuncts(discovered.moduleRoot, _gamePath)) {
        index.add(RuntimeModuleSource {source.candidate, source.path});
    }

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

    // Mounting the module is one operation. Sources are mounted in phases and a
    // later one can throw, which would otherwise leave the earlier phases in
    // place: a module that is neither the old one nor the new one, still
    // answering lookups. Whatever is mounted from here on goes away together if
    // the load does not finish.
    //
    // This does not put the previous module back. Its sources were retired
    // before this ran, and reinstating them would be a new load rather than an
    // undo of this one; whether the caller can still use the module it had is
    // its own question.
    ResourceMountTransaction transaction(_resources);

    ModuleMountExecutor executor(_resources, index);
    auto report = executor.run(plan);

    if (report.outcome != ModuleLoadOutcome::Failed) {
        transaction.commit();
    }

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

/**
 * Build a complete, unpublished save-session candidate.
 *
 * The descriptor, loose metadata view, and indexed working state remain outside
 * the global registry. Construction finishes before publication, so an invalid
 * archive cannot partially replace the active session.
 */
std::unique_ptr<SaveSessionState> ResourceDirector::buildSaveSession(std::string_view name) {
    auto allSavesPath = findFileIgnoreCase(_gamePath, kSavesDirectoryName);
    if (!allSavesPath) {
        throw ResourceNotFoundException("Saves directory not found");
    }

    auto savePath = findFileIgnoreCase(*allSavesPath, name);
    if (!savePath) {
        throw ResourceNotFoundException(str(boost::format("Save directory not found: %s") % name));
    }

    auto savegamePath = findFileIgnoreCase(*savePath, "savegame.sav");
    if (!savegamePath) {
        throw ResourceNotFoundException("savegame.sav not found");
    }

    return buildSaveSession(SaveSlotDescriptor {*savePath, *savegamePath});
}

/**
 * Build a candidate over an already discovered slot.
 *
 * The descriptor is the durable identity: mounting it verbatim keeps the entity
 * the list offered and the entity the loader opens the same one, which a second
 * lookup by name cannot guarantee once the directory has changed underneath or
 * holds entries differing only by case.
 */
std::unique_ptr<SaveSessionState> ResourceDirector::buildSaveSession(const SaveSlotDescriptor &slot) {
    std::error_code ec;
    if (!std::filesystem::is_directory(slot.directory, ec)) {
        throw ResourceNotFoundException(
            str(boost::format("Save directory not found: %s") % slot.directory.string()));
    }
    if (!std::filesystem::is_regular_file(slot.archive, ec)) {
        throw ResourceNotFoundException(
            str(boost::format("savegame.sav not found: %s") % slot.archive.string()));
    }
    return std::make_unique<SaveSessionState>(slot);
}

} // namespace resource

} // namespace reone
