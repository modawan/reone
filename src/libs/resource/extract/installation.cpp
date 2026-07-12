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
static constexpr char kLipsDirectoryName[] = "lips";
static constexpr char kOverrideDirectoryName[] = "override";
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

Installation::Installation(resource::GameID game, std::filesystem::path root) :
    _game(game),
    _root(std::move(root)) {
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

void Installation::loadModules() {
    if (_modulesLoaded) {
        return;
    }
    _modulesLoaded = true;
    _moduleCapsulePaths.clear();
    if (!_moduleRoot) {
        return;
    }
    for (auto &rel : moduleArchiveRelPaths(*_moduleRoot)) {
        if (auto path = findFileIgnoreCase(_root, rel)) {
            auto filename = boost::to_lower_copy(path->filename().string());
            _moduleCapsulePaths.emplace(filename, *path);
        }
    }
    clearLocationCaches();
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
    auto overridePath = findFileIgnoreCase(_root, kOverrideDirectoryName);
    if (!overridePath) {
        _overrideIndex.clear();
        _overrideLoaded = true;
        return;
    }

    std::vector<FileResource> loose;
    for (auto &entry : std::filesystem::directory_iterator(*overridePath)) {
        if (entry.is_regular_file()) {
            auto id = resourceIdFromPath(entry.path());
            if (id) {
                loose.emplace_back(
                    id->resRef.value(),
                    id->type,
                    static_cast<uint32_t>(entry.file_size()),
                    0,
                    entry.path());
            }
            continue;
        }
        if (!entry.is_directory()) {
            continue;
        }
        auto relKey = entry.path().filename().string();
        std::vector<FileResource> subdir;
        indexLooseFiles(entry.path(), subdir);
        if (!subdir.empty()) {
            _override[relKey] = std::move(subdir);
        }
    }
    if (!loose.empty()) {
        sortResources(loose);
        _override["."] = std::move(loose);
    }
    _overrideIndex.clear();
    for (const auto &key : sortedKeys(_override)) {
        auto &list = _override.at(key);
        for (auto &res : list) {
            _overrideIndex.emplace(res.id(), res);
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
    auto lipsPath = findFileIgnoreCase(_root, kLipsDirectoryName);
    if (lipsPath) {
        indexCapsuleDict(*lipsPath, isCapsuleFile, _lips);
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

void Installation::setModuleRoot(std::optional<std::string> root) {
    if (root) {
        boost::to_lower(*root);
    }
    _moduleRoot = std::move(root);
    _modulesLoaded = false;
    _moduleCapsulePaths.clear();
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
    _moduleCapsulePaths.clear();
    clearLocationCaches();
}

void Installation::clearSaveScope() {
    _customFolders = _globalCustomFolders;
    _customCapsules = _globalCustomCapsules;
    _capsuleCache.clear();
    clearLocationCaches();
}

std::string Installation::getModuleRoot(std::string_view capsuleFilename) {
    auto stem = std::filesystem::path(capsuleFilename).stem().string();
    boost::to_lower(stem);
    if (boost::ends_with(stem, "_s")) {
        stem.resize(stem.size() - 2);
    }
    if (boost::ends_with(stem, "_dlg")) {
        stem.resize(stem.size() - 4);
    }
    if (boost::ends_with(stem, "_loc")) {
        stem.resize(stem.size() - 4);
    }
    return stem;
}

std::vector<std::string> Installation::moduleArchiveRelPaths(std::string_view moduleRoot) {
    auto low = boost::to_lower_copy(std::string(moduleRoot));
    return {
        "modules/" + low + ".mod",
        "modules/" + low + "_s.rim",
        "modules/" + low + ".rim",
        "modules/" + low + ".erf",
        "modules/" + low + "_loc.mod",
        "modules/" + low + "_loc.erf",
        "modules/" + low + "_dlg.mod",
        "modules/" + low + "_dlg.erf",
        "lips/" + low + "_loc.mod",
        "lips/" + low + "_loc.erf",
    };
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
    for (const auto &rel : moduleArchiveRelPaths(*_moduleRoot)) {
        auto filename = boost::to_lower_copy(std::filesystem::path(rel).filename().string());
        auto it = _moduleCapsulePaths.find(filename);
        if (it != _moduleCapsulePaths.end()) {
            auto res = cachedCapsule(it->second).find(id);
            if (res) {
                appendLocation(*res, out);
            }
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
