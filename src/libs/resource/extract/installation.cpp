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

#include "reone/extract/installation.h"

#include "reone/extract/chitin.h"
#include "reone/resource/format/pereader.h"
#include "reone/resource/modulemount.h"
#include "reone/system/fileutil.h"
#include "reone/system/stream/gameinput.h"

#include <algorithm>

namespace reone {

namespace extract {

static constexpr char kKeyFilename[] = "chitin.key";
static constexpr char kPatchFilename[] = "patch.erf";
static constexpr char kTexturePackDirectoryName[] = "texturepacks";
static constexpr char kMusicDirectoryName[] = "streammusic";
static constexpr char kSoundsDirectoryName[] = "streamsounds";
static constexpr char kWavesDirectoryName[] = "streamwaves";
static constexpr char kVoiceDirectoryName[] = "streamvoice";
static constexpr char kRimsDirectoryName[] = "rims";
static constexpr char kMoviesDirectoryName[] = "movies";
static constexpr char kExeFilenameKotor[] = "swkotor.exe";
static constexpr char kExeFilenameTsl[] = "swkotor2.exe";

static constexpr char kTexturePackTpa[] = "swpc_tex_tpa.erf";
static constexpr char kTexturePackTpb[] = "swpc_tex_tpb.erf";
static constexpr char kTexturePackTpc[] = "swpc_tex_tpc.erf";
static constexpr char kTexturePackGui[] = "swpc_tex_gui.erf";

static const std::unordered_map<resource::PEResType, resource::ResType> kPEResTypeToResType {
    {resource::PEResType::Cursor, resource::ResType::Cursor},
    {resource::PEResType::CursorGroup, resource::ResType::CursorGroup},
};

static void appendLocation(const FileResource &file, std::vector<LocationResult> &out) {
    LocationResult loc(file.filepath(), file.offset(), file.size());
    loc.setFileResource(file);
    out.push_back(std::move(loc));
}

static std::string pathCacheKey(const std::filesystem::path &path) {
    auto key = path.lexically_normal().string();
    boost::replace_all(key, "\\", "/");
    boost::to_lower(key);
    return key;
}

static void sortResources(std::vector<FileResource> &resources, size_t first = 0) {
    std::sort(resources.begin() + first, resources.end(), [](const auto &lhs, const auto &rhs) {
        return pathCacheKey(lhs.filepath()) < pathCacheKey(rhs.filepath());
    });
}

template <typename T>
static std::vector<std::string> sortedKeys(const std::unordered_map<std::string, T> &dict) {
    std::vector<std::string> keys;
    keys.reserve(dict.size());
    for (const auto &[key, _] : dict) {
        keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

Installation::Installation(resource::GameID game,
                           std::filesystem::path root,
                           resource::OdysseyResourceRoots odysseyRoots) :
    _game(game),
    _root(std::move(root)),
    _odysseyRoots(std::move(odysseyRoots)) {
    if (!_odysseyRoots.nwmFiles) {
        _odysseyRoots.nwmFiles = resource::defaultOdysseyResourceRoots(_root).nwmFiles;
    }
}

void Installation::clearLocationCaches() {
    _listCache.clear();
}

void Installation::loadChitin() {
    if (_chitinLoaded) {
        return;
    }
    auto keyPath = findFileIgnoreCase(_root, kKeyFilename);
    if (keyPath) {
        Chitin chitin(*keyPath);
        _chitin = chitin.resources();
    }
    _chitinLoaded = true;
    clearLocationCaches();
}

std::vector<resource::ModuleSearchRoot> Installation::moduleSearchRoots() const {
    return resource::moduleSearchRoots(_game, _root, _odysseyRoots);
}

/// Position of a bucket in the raw lookup order. A family the policy assigns no
/// bucket has no position in that order, and is ranked after every source that
/// does rather than being given an invented one.
static std::size_t bucketRank(const std::optional<resource::ResourceSourceBucket> &bucket) {
    if (!bucket) {
        return resource::kRawResourceLookupOrder.size();
    }
    for (std::size_t i = 0; i < resource::kRawResourceLookupOrder.size(); ++i) {
        if (resource::kRawResourceLookupOrder[i] == *bucket) {
            return i;
        }
    }
    return resource::kRawResourceLookupOrder.size();
}

/**
 * Index the current module root's archives, in canonical raw lookup order.
 *
 * The set comes from shared discovery, so which files belong to the module and
 * what family each one has are answered exactly as the runtime answers them.
 * The order comes from the raw lookup contract: bucket first, and within a
 * bucket the source a running game would have mounted latest. Mount timing
 * therefore orders sources inside a bucket and never across buckets, which is
 * what stops the old ".mod beats _s.rim beats .rim" ladder from reappearing.
 *
 * Nothing is suppressed. A game whose policy would not mount a family still
 * has the file on disk, and answering where a resource lives is not the same
 * question as which source a running game would read it from; an unmounted
 * archive simply ranks below every mounted one in its bucket.
 */
void Installation::loadModules() {
    if (_modulesLoaded) {
        return;
    }
    _modulesLoaded = true;
    _moduleArchives.clear();
    clearLocationCaches();
    if (!_moduleRoot) {
        return;
    }

    auto discovered = resource::discoverModuleSources(*_moduleRoot, moduleSearchRoots());
    if (discovered.nameRejection) {
        return;
    }
    auto rimsAdjuncts = resource::discoverRimsModuleAdjuncts(discovered.moduleRoot, _root);
    discovered.sources.insert(discovered.sources.end(),
                              rimsAdjuncts.begin(),
                              rimsAdjuncts.end());
    auto inventory = resource::plannerInventory(discovered);

    resource::ModulePolicyRequest request;
    request.game = _game;
    request.moduleName = discovered.moduleRoot;
    // No module-save table is consulted. The gate excludes saved candidates
    // only, and an installation's module locations offer none for it to
    // exclude, so reading the table could not change the plan.
    request.includeInSave = true;
    auto selection = resource::selectModulePrimary(request, inventory);
    request.savedMode = selection &&
                        selection->candidate.kind == resource::ModulePrimaryKind::SavedArchive;
    auto plan = resource::planModuleLoad(request, inventory);

    std::unordered_map<std::string, std::uint32_t> mountSequence;
    std::uint32_t nextSequence = 0;
    for (const auto &family : plan.families) {
        for (const auto &attempt : family.attempts) {
            mountSequence[attempt.source.sourceId] = attempt.attemptOrder;
            nextSequence = std::max(nextSequence, attempt.attemptOrder + 1);
        }
    }
    // The selected primary is staged and mounted as the active table after
    // every other phase, so it is the newest source of its bucket.
    if (plan.primary) {
        mountSequence[plan.primary->candidate.source.sourceId] = nextSequence;
    }

    struct Ranked {
        ModuleArchive archive;
        std::size_t bucket {0};
        int unmounted {0};
        std::uint32_t sequence {0};
        std::size_t discovered {0};
    };
    std::vector<Ranked> ranked;
    ranked.reserve(discovered.sources.size());
    for (std::size_t i = 0; i < discovered.sources.size(); ++i) {
        const auto &source = discovered.sources[i];
        Ranked entry;
        entry.archive.moduleRoot = source.moduleRoot;
        entry.archive.family = source.candidate.family;
        entry.archive.rootId = source.candidate.rootId;
        entry.archive.path = source.path;
        if (auto metadata = resource::mountMetadata(source.candidate.family)) {
            entry.archive.bucket = metadata->bucket;
        }
        entry.archive.resourceImage = resource::isResourceImageFamily(source.candidate.family);
        auto mounted = mountSequence.find(source.candidate.sourceId);
        entry.bucket = bucketRank(entry.archive.bucket);
        entry.unmounted = mounted == mountSequence.end() ? 1 : 0;
        entry.sequence = mounted == mountSequence.end() ? 0 : mounted->second;
        entry.discovered = i;
        ranked.push_back(std::move(entry));
    }
    std::sort(ranked.begin(), ranked.end(), [](const Ranked &lhs, const Ranked &rhs) {
        if (lhs.bucket != rhs.bucket) {
            return lhs.bucket < rhs.bucket;
        }
        if (lhs.unmounted != rhs.unmounted) {
            return lhs.unmounted < rhs.unmounted;
        }
        if (lhs.sequence != rhs.sequence) {
            return lhs.sequence > rhs.sequence;
        }
        return lhs.discovered < rhs.discovered;
    });

    _moduleArchives.reserve(ranked.size());
    for (auto &entry : ranked) {
        _moduleArchives.push_back(std::move(entry.archive));
    }
}

std::vector<std::string> Installation::moduleNames() {
    return resource::discoverModuleRoots(
        resource::primaryModuleSearchRoots(_game, _root, _odysseyRoots));
}

const std::vector<ModuleArchive> &Installation::moduleArchives() {
    loadModules();
    return _moduleArchives;
}

const std::vector<FileResource> &Installation::chitinResources() {
    loadChitin();
    return _chitin;
}

void Installation::indexLooseFiles(const std::filesystem::path &dir, std::vector<FileResource> &out) {
    if (!std::filesystem::exists(dir)) {
        return;
    }
    auto first = out.size();
    for (auto &entry : std::filesystem::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        auto id = resourceIdFromPath(entry.path());
        if (!id) {
            continue;
        }
        auto size = static_cast<uint32_t>(entry.file_size());
        out.emplace_back(id->resRef.value(), id->type, size, 0, entry.path());
    }
    sortResources(out, first);
}

void Installation::indexCapsuleDict(const std::filesystem::path &dir,
                                    bool (*filter)(const std::filesystem::path &),
                                    std::unordered_map<std::string, std::vector<FileResource>> &out) {
    if (!std::filesystem::exists(dir)) {
        return;
    }
    for (auto &entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (filter && !filter(entry.path())) {
            continue;
        }
        auto filename = boost::to_lower_copy(entry.path().filename().string());
        LazyCapsule capsule(entry.path());
        out[filename] = capsule.resources();
    }
}

void Installation::loadOverride() {
    if (_overrideLoaded) {
        return;
    }
    _overrideIndex.clear();
    _override.clear();
    auto roots = resource::looseOverrideRoots(_game, _root, _odysseyRoots);
    for (std::size_t i = 0; i < roots.size(); ++i) {
        std::vector<FileResource> files;
        indexLooseFiles(roots[i], files);
        auto key = "root" + std::to_string(i);
        _override[key] = files;
        std::unordered_map<resource::ResourceId, FileResource> rootIndex;
        for (const auto &file : files) {
            // Files are path-sorted, so first insertion preserves the existing
            // deterministic winner among duplicate subdirectories in one root.
            rootIndex.emplace(file.id(), file);
        }
        // Natural registration order is oldest to newest, so assignment makes
        // the later configured root the lookup winner just as runtime does.
        for (const auto &[id, file] : rootIndex) {
            _overrideIndex.insert_or_assign(id, file);
        }
    }
    _overrideLoaded = true;
    clearLocationCaches();
}

void Installation::loadTexturePacks() {
    if (_texturePacksLoaded) {
        return;
    }
    auto texPacksPath = findFileIgnoreCase(_root, kTexturePackDirectoryName);
    if (!texPacksPath) {
        _texturePacksLoaded = true;
        return;
    }
    for (auto name : {kTexturePackTpa, kTexturePackTpb, kTexturePackTpc, kTexturePackGui}) {
        auto packPath = findFileIgnoreCase(*texPacksPath, name);
        if (!packPath) {
            continue;
        }
        LazyCapsule capsule(*packPath);
        auto resources = capsule.resources();
        _texturePacks[name] = resources;
        auto &index = _texturePackIndex[name];
        index.clear();
        for (auto &res : resources) {
            index.emplace(res.id(), res);
        }
    }
    _texturePacksLoaded = true;
    clearLocationCaches();
}

void Installation::loadStreams() {
    if (_streamsLoaded) {
        return;
    }
    if (auto musicPath = findFileIgnoreCase(_root, kMusicDirectoryName)) {
        indexLooseFiles(*musicPath, _streamMusic);
    }
    if (auto soundsPath = findFileIgnoreCase(_root, kSoundsDirectoryName)) {
        indexLooseFiles(*soundsPath, _streamSounds);
    }
    if (_game == resource::GameID::TSL) {
        if (auto voicePath = findFileIgnoreCase(_root, kVoiceDirectoryName)) {
            indexLooseFiles(*voicePath, _streamVoice);
        }
    } else if (auto wavesPath = findFileIgnoreCase(_root, kWavesDirectoryName)) {
        indexLooseFiles(*wavesPath, _streamVoice);
    }
    _streamsLoaded = true;
    clearLocationCaches();
}

void Installation::loadLips() {
    if (_lipsLoaded) {
        return;
    }
    auto roots = resource::lipsRoots(_game, _root, _odysseyRoots);
    for (std::size_t i = 0; i < roots.size(); ++i) {
        std::unordered_map<std::string, std::vector<FileResource>> indexed;
        indexCapsuleDict(roots[i], isCapsuleFile, indexed);
        for (auto &[name, files] : indexed) {
            _lips["root" + std::to_string(i) + ":" + name] = std::move(files);
        }
    }
    _lipsLoaded = true;
    clearLocationCaches();
}

void Installation::loadRims() {
    if (_rimsLoaded) {
        return;
    }
    auto rimsPath = findFileIgnoreCase(_root, kRimsDirectoryName);
    if (rimsPath) {
        indexCapsuleDict(*rimsPath, isCapsuleFile, _rims);
    }
    _rimsLoaded = true;
    clearLocationCaches();
}

void Installation::loadRoot() {
    if (_rootLoaded) {
        return;
    }
    if (!std::filesystem::exists(_root)) {
        _rootLoaded = true;
        return;
    }
    for (auto &entry : std::filesystem::directory_iterator(_root)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        auto id = resourceIdFromPath(entry.path());
        if (!id) {
            continue;
        }
        _rootLoose.emplace_back(
            id->resRef.value(),
            id->type,
            static_cast<uint32_t>(entry.file_size()),
            0,
            entry.path());
    }
    sortResources(_rootLoose);
    _rootLoaded = true;
    clearLocationCaches();
}

void Installation::loadMovies() {
    if (_moviesLoaded) {
        return;
    }
    auto moviesPath = findFileIgnoreCase(_root, kMoviesDirectoryName);
    if (!moviesPath || !std::filesystem::exists(*moviesPath)) {
        _moviesLoaded = true;
        return;
    }
    for (auto &entry : std::filesystem::directory_iterator(*moviesPath)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        auto ext = boost::to_lower_copy(entry.path().extension().string());
        if (ext != ".bik") {
            continue;
        }
        auto name = boost::to_lower_copy(entry.path().stem().string());
        _movies[name] = entry.path();
    }
    _moviesLoaded = true;
}

std::optional<std::filesystem::path> Installation::moviePath(std::string_view name) {
    auto stem = boost::to_lower_copy(std::string(name));
    auto rel = std::string("movies/") + stem + ".bik";
    if (auto path = resolveLooseRelativePath(rel, movieSearchOrder())) {
        return path;
    }
    loadMovies();
    auto it = _movies.find(stem);
    if (it != _movies.end()) {
        return it->second;
    }
    return std::nullopt;
}

void Installation::loadExecutable() {
    if (_executableLoaded) {
        return;
    }
    auto exeName = _game == resource::GameID::TSL ? kExeFilenameTsl : kExeFilenameKotor;
    auto exePath = findFileIgnoreCase(_root, exeName);
    if (!exePath) {
        _executableLoaded = true;
        return;
    }
    try {
        auto stream = openGameInputStream(*exePath);
        resource::PeReader reader(*stream);
        reader.load();
        for (auto &peRes : reader.resources()) {
            auto resType = kPEResTypeToResType.find(peRes.type);
            if (resType == kPEResTypeToResType.end()) {
                continue;
            }
            _executable.emplace_back(
                std::to_string(peRes.name),
                resType->second,
                peRes.size,
                peRes.offset,
                *exePath);
        }
    } catch (const std::exception &) {
        _executable.clear();
    }
    _executableLoaded = true;
    clearLocationCaches();
}

/**
 * Scope module lookups to one module.
 *
 * The name is normalized by the shared rule, which removes a supported archive
 * extension and nothing else. A root that ends in something resembling a family
 * suffix stays whole: the caller has said which module it wants, so "foo_adxx"
 * is that module and "foo_adxx.rim" is its own primary. Only enumeration, which
 * has to infer ownership from a name alone, reads a trailing suffix at all.
 *
 * A name no supported archive could carry leaves the scope unset rather than
 * being reduced to something that is.
 */
void Installation::setModuleRoot(std::optional<std::string> root) {
    _moduleRoot.reset();
    if (root) {
        _moduleRoot = resource::normalizeModuleName(*root).root;
    }
    _modulesLoaded = false;
    _moduleArchives.clear();
    clearLocationCaches();
}

void Installation::setCustomFolders(std::vector<std::filesystem::path> folders) {
    _customFolders = std::move(folders);
    clearLocationCaches();
}

void Installation::setGlobalCustomFolders(std::vector<std::filesystem::path> folders) {
    _globalCustomFolders = std::move(folders);
    _customFolders = _globalCustomFolders;
    clearLocationCaches();
}

void Installation::setGlobalCustomCapsules(std::vector<std::filesystem::path> capsules) {
    _globalCustomCapsules = std::move(capsules);
    _customCapsules = _globalCustomCapsules;
    clearLocationCaches();
}

void Installation::setCustomCapsules(std::vector<std::filesystem::path> capsules) {
    _customCapsules = std::move(capsules);
    clearLocationCaches();
}

void Installation::appendSaveScope(std::filesystem::path saveDir, std::filesystem::path savegameSav) {
    auto folders = _customFolders;
    folders.push_back(std::move(saveDir));
    auto capsules = _customCapsules;
    capsules.push_back(std::move(savegameSav));
    setCustomFolders(std::move(folders));
    setCustomCapsules(std::move(capsules));
}

void Installation::clearModuleScope() {
    _moduleRoot.reset();
    _modulesLoaded = false;
    _moduleArchives.clear();
    clearLocationCaches();
}

void Installation::clearSaveScope() {
    _customFolders = _globalCustomFolders;
    _customCapsules = _globalCustomCapsules;
    _capsuleCache.clear();
    clearLocationCaches();
}

static std::string normalizeRelativePath(std::string_view relativePath) {
    auto path = std::filesystem::path(relativePath).lexically_normal().string();
    boost::replace_all(path, "\\", "/");
    boost::to_lower(path);
    return path;
}

static std::optional<std::filesystem::path> findInFolders(const std::vector<std::filesystem::path> &folders,
                                                          std::string_view relativePath) {
    auto target = normalizeRelativePath(relativePath);
    for (auto &folder : folders) {
        if (!std::filesystem::exists(folder)) {
            continue;
        }
        for (auto &entry : std::filesystem::recursive_directory_iterator(folder)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            auto candidate = entry.path().lexically_relative(folder).string();
            boost::replace_all(candidate, "\\", "/");
            boost::to_lower(candidate);
            if (candidate == target) {
                return entry.path();
            }
        }
    }
    return std::nullopt;
}

static std::optional<std::filesystem::path> findInRoot(const std::filesystem::path &root,
                                                       std::string_view relativePath) {
    auto normalized = normalizeRelativePath(relativePath);
    if (auto path = findFileIgnoreCase(root, normalized)) {
        return path;
    }
    auto direct = root / std::filesystem::path(relativePath);
    if (std::filesystem::exists(direct)) {
        return direct;
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> Installation::resolveLooseRelativePath(std::string_view relativePath,
                                                                            const SearchScope &order) {
    auto target = normalizeRelativePath(relativePath);

    for (auto location : order) {
        switch (location) {
        case SearchLocation::CustomFolders: {
            if (auto path = findInFolders(_customFolders, target)) {
                return path;
            }
            break;
        }
        case SearchLocation::Root:
        case SearchLocation::Movies: {
            if (auto path = findInRoot(_root, target)) {
                return path;
            }
            break;
        }
        default:
            break;
        }
    }
    return std::nullopt;
}

void Installation::checkList(const std::string &cacheKey,
                             const std::vector<FileResource> &list,
                             const resource::ResourceId &id,
                             std::vector<LocationResult> &out) {
    auto &cache = _listCache[cacheKey];
    if (cache.empty()) {
        for (auto &res : list) {
            cache.emplace(res.id(), res);
        }
    }
    auto it = cache.find(id);
    if (it != cache.end()) {
        appendLocation(it->second, out);
    }
}

void Installation::checkDict(const std::string &cachePrefix,
                             const std::unordered_map<std::string, std::vector<FileResource>> &dict,
                             const resource::ResourceId &id,
                             std::vector<LocationResult> &out) {
    for (const auto &name : sortedKeys(dict)) {
        const auto &list = dict.at(name);
        checkList(cachePrefix + "/" + name, list, id, out);
    }
}

void Installation::checkFolders(const std::vector<std::filesystem::path> &folders,
                                const resource::ResourceId &id,
                                std::vector<LocationResult> &out) {
    for (auto &folder : folders) {
        if (!std::filesystem::exists(folder)) {
            continue;
        }
        std::vector<FileResource> matches;
        for (auto &entry : std::filesystem::recursive_directory_iterator(folder)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            auto fileId = resourceIdFromPath(entry.path());
            if (fileId && *fileId == id) {
                FileResource res(
                    fileId->resRef.value(),
                    fileId->type,
                    static_cast<uint32_t>(entry.file_size()),
                    0,
                    entry.path());
                matches.push_back(std::move(res));
            }
        }
        sortResources(matches);
        for (const auto &res : matches) {
            appendLocation(res, out);
        }
    }
}

void Installation::checkCapsules(const std::vector<std::filesystem::path> &capsules,
                                 const resource::ResourceId &id,
                                 std::vector<LocationResult> &out) {
    for (auto &path : capsules) {
        if (auto res = cachedCapsule(path).find(id)) {
            appendLocation(*res, out);
        }
    }
}

const LazyCapsule &Installation::cachedCapsule(const std::filesystem::path &path) const {
    auto key = pathCacheKey(path);
    auto it = _capsuleCache.find(key);
    if (it != _capsuleCache.end()) {
        return *it->second;
    }
    auto inserted = _capsuleCache.emplace(key, std::make_shared<LazyCapsule>(path));
    return *inserted.first->second;
}

void Installation::checkOverride(const resource::ResourceId &id, std::vector<LocationResult> &out) {
    loadOverride();
    auto it = _overrideIndex.find(id);
    if (it != _overrideIndex.end()) {
        appendLocation(it->second, out);
    }
}

void Installation::checkModules(const resource::ResourceId &id, std::vector<LocationResult> &out) {
    if (!_moduleRoot) {
        return;
    }
    loadModules();
    for (const auto &archive : _moduleArchives) {
        if (auto res = cachedCapsule(archive.path).find(id)) {
            appendLocation(*res, out);
        }
    }
}

void Installation::checkTexturePack(const char *packName,
                                    const resource::ResourceId &id,
                                    std::vector<LocationResult> &out) {
    loadTexturePacks();
    auto packIt = _texturePackIndex.find(packName);
    if (packIt == _texturePackIndex.end()) {
        return;
    }
    auto resIt = packIt->second.find(id);
    if (resIt != packIt->second.end()) {
        appendLocation(resIt->second, out);
    }
}

std::vector<LocationResult> Installation::locations(const resource::ResourceId &id,
                                                    const SearchScope &order,
                                                    const ResourceLookupContext &ctx) {
    std::vector<LocationResult> results;

    std::vector<std::filesystem::path> mergedFolders;
    const std::vector<std::filesystem::path> *folderView = &_customFolders;
    if (!ctx.customFolders.empty()) {
        mergedFolders = _customFolders;
        mergedFolders.insert(mergedFolders.end(), ctx.customFolders.begin(), ctx.customFolders.end());
        folderView = &mergedFolders;
    }

    std::vector<std::filesystem::path> mergedCapsules;
    const std::vector<std::filesystem::path> *capsuleView = &_customCapsules;
    if (!ctx.customCapsules.empty()) {
        mergedCapsules = _customCapsules;
        mergedCapsules.insert(mergedCapsules.end(), ctx.customCapsules.begin(), ctx.customCapsules.end());
        capsuleView = &mergedCapsules;
    }

    for (auto location : order) {
        switch (location) {
        case SearchLocation::CustomFolders:
            checkFolders(*folderView, id, results);
            break;
        case SearchLocation::Override:
            checkOverride(id, results);
            break;
        case SearchLocation::Root:
            loadRoot();
            checkList("root", _rootLoose, id, results);
            break;
        case SearchLocation::CustomModules:
            checkCapsules(*capsuleView, id, results);
            break;
        case SearchLocation::Modules:
            checkModules(id, results);
            break;
        case SearchLocation::Chitin: {
            loadChitin();
            checkList("chitin", _chitin, id, results);
            if (results.empty()) {
                if (auto patchPath = findFileIgnoreCase(_root, kPatchFilename)) {
                    if (auto res = cachedCapsule(*patchPath).find(id)) {
                        appendLocation(*res, results);
                    }
                }
            }
            break;
        }
        case SearchLocation::TexturesTpa:
            checkTexturePack(kTexturePackTpa, id, results);
            break;
        case SearchLocation::TexturesTpb:
            checkTexturePack(kTexturePackTpb, id, results);
            break;
        case SearchLocation::TexturesTpc:
            checkTexturePack(kTexturePackTpc, id, results);
            break;
        case SearchLocation::TexturesGui:
            checkTexturePack(kTexturePackGui, id, results);
            break;
        case SearchLocation::Music:
            loadStreams();
            checkList("music", _streamMusic, id, results);
            break;
        case SearchLocation::Sound:
            loadStreams();
            checkList("sound", _streamSounds, id, results);
            break;
        case SearchLocation::Voice:
            loadStreams();
            checkList("voice", _streamVoice, id, results);
            break;
        case SearchLocation::Lips:
            loadLips();
            checkDict("lips", _lips, id, results);
            break;
        case SearchLocation::Rims:
            loadRims();
            checkDict("rims", _rims, id, results);
            break;
        case SearchLocation::Executable:
            loadExecutable();
            checkList("exe", _executable, id, results);
            break;
        default:
            break;
        }
    }

    return results;
}

std::optional<LocationResult> Installation::resource(const resource::ResourceId &id,
                                                     const SearchScope &order,
                                                     const ResourceLookupContext &ctx) {
    auto locs = locations(id, order, ctx);
    if (locs.empty()) {
        return std::nullopt;
    }
    return locs.front();
}

std::unordered_map<resource::ResourceId, std::vector<LocationResult>> Installation::locations(
    const std::vector<resource::ResourceId> &ids,
    const SearchScope &order,
    const ResourceLookupContext &ctx) {
    std::unordered_map<resource::ResourceId, std::vector<LocationResult>> result;
    for (auto &id : ids) {
        result.emplace(id, locations(id, order, ctx));
    }
    return result;
}

} // namespace extract

} // namespace reone
