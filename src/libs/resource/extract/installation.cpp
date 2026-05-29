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

#include "reone/extract/finder.h"
#include "reone/extract/installation.h"

#include "reone/extract/capsule.h"
#include "reone/extract/chitin.h"
#include "reone/resource/format/pereader.h"
#include "reone/system/fileutil.h"
#include "reone/system/stream/gameinput.h"

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/predicate.hpp>

namespace reone {

namespace extract {

namespace {

constexpr char kKeyFilename[] = "chitin.key";
constexpr char kPatchFilename[] = "patch.erf";
constexpr char kTexturePackDirectoryName[] = "texturepacks";
constexpr char kMusicDirectoryName[] = "streammusic";
constexpr char kSoundsDirectoryName[] = "streamsounds";
constexpr char kWavesDirectoryName[] = "streamwaves";
constexpr char kVoiceDirectoryName[] = "streamvoice";
constexpr char kModulesDirectoryName[] = "modules";
constexpr char kLipsDirectoryName[] = "lips";
constexpr char kOverrideDirectoryName[] = "override";
constexpr char kRimsDirectoryName[] = "rims";
constexpr char kMoviesDirectoryName[] = "movies";
constexpr char kExeFilenameKotor[] = "swkotor.exe";
constexpr char kExeFilenameTsl[] = "swkotor2.exe";

constexpr char kTexturePackTpa[] = "swpc_tex_tpa.erf";
constexpr char kTexturePackTpb[] = "swpc_tex_tpb.erf";
constexpr char kTexturePackTpc[] = "swpc_tex_tpc.erf";
constexpr char kTexturePackGui[] = "swpc_tex_gui.erf";

std::unordered_map<resource::PEResType, resource::ResType> kPEResTypeToResType {
    {resource::PEResType::Cursor, resource::ResType::Cursor},
    {resource::PEResType::CursorGroup, resource::ResType::CursorGroup},
};

void appendLocation(const FileResource &file, std::vector<LocationResult> &out) {
    LocationResult loc(file.filepath(), file.offset(), file.size());
    loc.setFileResource(file);
    out.push_back(std::move(loc));
}

} // namespace

Installation::Installation(resource::GameID game, std::filesystem::path root) :
    _game(game),
    _root(std::move(root)) {
}

void Installation::clearLocationCaches() {
    _listCache.clear();
    _filteredModulesRoot.reset();
    _filteredModulesCache.clear();
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

void Installation::loadPatchErf() {
    if (_patchErfLoaded) {
        return;
    }
    auto patchPath = findFileIgnoreCase(_root, kPatchFilename);
    if (patchPath) {
        LazyCapsule capsule(*patchPath);
        _patchErf = capsule.resources();
    }
    _patchErfLoaded = true;
    clearLocationCaches();
}

void Installation::indexLooseFiles(const std::filesystem::path &dir, std::vector<FileResource> &out) {
    if (!std::filesystem::exists(dir)) {
        return;
    }
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

void Installation::loadModules() {
    if (_modulesLoaded) {
        return;
    }
    _modulesLoaded = true;
    if (!_moduleRoot) {
        return;
    }
    auto modulesPath = findFileIgnoreCase(_root, kModulesDirectoryName);
    if (!modulesPath) {
        return;
    }
    for (auto &entry : std::filesystem::directory_iterator(*modulesPath)) {
        if (!entry.is_regular_file() || !isCapsuleFile(entry.path())) {
            continue;
        }
        auto filename = boost::to_lower_copy(entry.path().filename().string());
        if (getModuleRoot(filename) != *_moduleRoot) {
            continue;
        }
        LazyCapsule capsule(entry.path());
        _modules[filename] = capsule.resources();
    }
    clearLocationCaches();
}

void Installation::loadOverride() {
    if (_overrideLoaded) {
        return;
    }
    auto overridePath = findFileIgnoreCase(_root, kOverrideDirectoryName);
    if (!overridePath) {
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
        _override["."] = std::move(loose);
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
        _texturePacks[name] = capsule.resources();
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
        indexCapsuleDict(*lipsPath, isModFile, _lips);
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
    _modules.clear();
    _filteredModulesRoot.reset();
    _filteredModulesCache.clear();
    clearLocationCaches();
}

void Installation::setCustomFolders(std::vector<std::filesystem::path> folders) {
    _customFolders = std::move(folders);
    clearLocationCaches();
}

void Installation::setCustomCapsules(std::vector<std::filesystem::path> capsules) {
    _customCapsules = std::move(capsules);
    clearLocationCaches();
}

void Installation::clearModuleScope() {
    _moduleRoot.reset();
    _modulesLoaded = false;
    _modules.clear();
    _filteredModulesRoot.reset();
    _filteredModulesCache.clear();
    clearLocationCaches();
}

void Installation::clearSaveScope() {
    _customCapsules.clear();
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
    return stem;
}

std::vector<std::string> Installation::moduleArchiveRelPaths(std::string_view moduleRoot) {
    auto low = boost::to_lower_copy(std::string(moduleRoot));
    return {
        "modules/" + low + ".rim",
        "modules/" + low + "_s.rim",
        "modules/" + low + ".mod",
        "modules/" + low + ".erf",
        "modules/" + low + "_loc.mod",
        "modules/" + low + "_loc.erf",
        "modules/" + low + "_dlg.mod",
        "modules/" + low + "_dlg.erf",
        "lips/" + low + "_loc.mod",
        "lips/" + low + "_loc.erf",
    };
}

namespace {

std::string normalizeRelativePath(std::string_view relativePath) {
    auto path = std::filesystem::path(relativePath).lexically_normal().string();
    boost::replace_all(path, "\\", "/");
    boost::to_lower(path);
    return path;
}

std::optional<std::filesystem::path> findInFolders(const std::vector<std::filesystem::path> &folders,
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

std::optional<std::filesystem::path> findInRoot(const std::filesystem::path &root,
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

} // namespace

std::optional<std::filesystem::path> Installation::resolveLooseRelativePath(std::string_view relativePath,
                                                                            const SearchScope &order) {
    auto target = normalizeRelativePath(relativePath);

    std::vector<std::filesystem::path> customFolders = _customFolders;
    for (auto location : order) {
        switch (location) {
        case SearchLocation::CustomFolders: {
            if (auto path = findInFolders(customFolders, target)) {
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

const std::unordered_map<std::string, std::vector<FileResource>> &Installation::filteredModules() {
    loadModules();
    if (!_moduleRoot) {
        return _modules;
    }
    if (_filteredModulesRoot && *_filteredModulesRoot == *_moduleRoot && !_filteredModulesCache.empty()) {
        return _filteredModulesCache;
    }
    _filteredModulesRoot = _moduleRoot;
    _filteredModulesCache.clear();
    for (auto &[filename, resources] : _modules) {
        if (getModuleRoot(filename) == *_moduleRoot) {
            _filteredModulesCache[filename] = resources;
        }
    }
    return _filteredModulesCache;
}

void Installation::checkList(const std::vector<FileResource> &list,
                             const resource::ResourceId &id,
                             std::vector<LocationResult> &out) {
    auto listId = reinterpret_cast<uintptr_t>(&list);
    auto &cache = _listCache[listId];
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

void Installation::checkDict(const std::unordered_map<std::string, std::vector<FileResource>> &dict,
                             const resource::ResourceId &id,
                             std::vector<LocationResult> &out) {
    for (auto &[_, list] : dict) {
        checkList(list, id, out);
        if (!out.empty()) {
            return;
        }
    }
}

void Installation::checkFolders(const std::vector<std::filesystem::path> &folders,
                              const resource::ResourceId &id,
                              std::vector<LocationResult> &out) {
    for (auto &folder : folders) {
        if (!std::filesystem::exists(folder)) {
            continue;
        }
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
                appendLocation(res, out);
                return;
            }
        }
    }
}

void Installation::checkCapsules(const std::vector<std::filesystem::path> &capsules,
                               const resource::ResourceId &id,
                               std::vector<LocationResult> &out) {
    for (auto &path : capsules) {
        LazyCapsule capsule(path);
        if (auto res = capsule.find(id)) {
            appendLocation(*res, out);
            return;
        }
    }
}

void Installation::checkModules(const resource::ResourceId &id, std::vector<LocationResult> &out) {
    if (!_moduleRoot) {
        return;
    }
    checkDict(filteredModules(), id, out);
}

std::vector<LocationResult> Installation::locations(const resource::ResourceId &id,
                                                    const SearchScope &order,
                                                    const ResourceLookupContext &ctx) {
    std::vector<LocationResult> results;

    std::vector<std::filesystem::path> customFolders = _customFolders;
    customFolders.insert(customFolders.end(), ctx.customFolders.begin(), ctx.customFolders.end());
    std::vector<std::filesystem::path> customCapsules = _customCapsules;
    customCapsules.insert(customCapsules.end(), ctx.customCapsules.begin(), ctx.customCapsules.end());

    for (auto location : order) {
        switch (location) {
        case SearchLocation::CustomFolders:
            checkFolders(customFolders, id, results);
            break;
        case SearchLocation::Override:
            loadOverride();
            checkDict(_override, id, results);
            break;
        case SearchLocation::Root:
            loadRoot();
            checkList(_rootLoose, id, results);
            break;
        case SearchLocation::CustomModules:
            checkCapsules(customCapsules, id, results);
            break;
        case SearchLocation::Modules:
            checkModules(id, results);
            break;
        case SearchLocation::Chitin: {
            loadChitin();
            auto beforeChitin = results.size();
            checkList(_chitin, id, results);
            if (results.size() == beforeChitin) {
                loadPatchErf();
                checkList(_patchErf, id, results);
            }
            break;
        }
        case SearchLocation::TexturesTpa:
            loadTexturePacks();
            if (auto it = _texturePacks.find(kTexturePackTpa); it != _texturePacks.end()) {
                checkList(it->second, id, results);
            }
            break;
        case SearchLocation::TexturesTpb:
            loadTexturePacks();
            if (auto it = _texturePacks.find(kTexturePackTpb); it != _texturePacks.end()) {
                checkList(it->second, id, results);
            }
            break;
        case SearchLocation::TexturesTpc:
            loadTexturePacks();
            if (auto it = _texturePacks.find(kTexturePackTpc); it != _texturePacks.end()) {
                checkList(it->second, id, results);
            }
            break;
        case SearchLocation::TexturesGui:
            loadTexturePacks();
            if (auto it = _texturePacks.find(kTexturePackGui); it != _texturePacks.end()) {
                checkList(it->second, id, results);
            }
            break;
        case SearchLocation::Music:
            loadStreams();
            checkList(_streamMusic, id, results);
            break;
        case SearchLocation::Sound:
            loadStreams();
            checkList(_streamSounds, id, results);
            break;
        case SearchLocation::Voice:
            loadStreams();
            checkList(_streamVoice, id, results);
            break;
        case SearchLocation::Lips:
            loadLips();
            checkDict(_lips, id, results);
            break;
        case SearchLocation::Rims:
            loadRims();
            checkDict(_rims, id, results);
            break;
        case SearchLocation::Executable:
            loadExecutable();
            checkList(_executable, id, results);
            break;
        default:
            break;
        }
        if (!results.empty()) {
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

} // namespace extract

} // namespace reone
