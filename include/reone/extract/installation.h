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

#pragma once

#include "finder.h"
#include "fileresource.h"
#include "lookupcontext.h"

#include "reone/resource/types.h"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace reone {

namespace extract {

/// Game installation indexer and resolver (PyKotor Installation).
class Installation {
public:
    Installation(resource::GameID game, std::filesystem::path root);

    resource::GameID game() const { return _game; }
    const std::filesystem::path &root() const { return _root; }

    void loadChitin();
    void loadPatchErf();
    void loadModules();
    void loadOverride();
    void loadTexturePacks();
    void loadStreams();
    void loadLips();
    void loadRims();
    void loadRoot();
    void loadMovies();
    void loadExecutable();

    std::vector<LocationResult> locations(const resource::ResourceId &id,
                                          const SearchScope &order,
                                          const ResourceLookupContext &ctx = {});

    std::optional<LocationResult> resource(const resource::ResourceId &id,
                                           const SearchScope &order = canonicalSearchOrder(),
                                           const ResourceLookupContext &ctx = {});

    void setModuleRoot(std::optional<std::string> root);
    void setCustomFolders(std::vector<std::filesystem::path> folders);
    void setCustomCapsules(std::vector<std::filesystem::path> capsules);
    void clearModuleScope();
    void clearSaveScope();

    const std::optional<std::string> &moduleRoot() const { return _moduleRoot; }
    const std::vector<std::filesystem::path> &customFolders() const { return _customFolders; }
    const std::vector<std::filesystem::path> &customCapsules() const { return _customCapsules; }

    static std::string getModuleRoot(std::string_view capsuleFilename);

    /// Relative paths under the game root for module archives (mirrors tools/web/module_mirror.json).
    static std::vector<std::string> moduleArchiveRelPaths(std::string_view moduleRoot);

    std::optional<std::filesystem::path> moviePath(std::string_view name);

    /// Indexed chitin resources (requires loadChitin()).
    const std::vector<FileResource> &chitinResources() const { return _chitin; }

    /// Indexed module archives keyed by relative path (requires loadModules()).
    const std::unordered_map<std::string, std::vector<FileResource>> &moduleArchives() const {
        return _modules;
    }

    /// Indexed override tree (requires loadOverride()).
    const std::unordered_map<std::string, std::vector<FileResource>> &overrideResources() const {
        return _override;
    }

    /// Resolve a loose file relative to the game root (e.g. dialog.tlk, movies/foo.bik).
    std::optional<std::filesystem::path> resolveLooseRelativePath(
        std::string_view relativePath,
        const SearchScope &order = canonicalSearchOrder());

private:
    resource::GameID _game;
    std::filesystem::path _root;

    bool _chitinLoaded {false};
    bool _patchErfLoaded {false};
    bool _modulesLoaded {false};
    bool _overrideLoaded {false};
    bool _texturePacksLoaded {false};
    bool _streamsLoaded {false};
    bool _lipsLoaded {false};
    bool _rimsLoaded {false};
    bool _rootLoaded {false};
    bool _moviesLoaded {false};
    bool _executableLoaded {false};

    std::vector<FileResource> _chitin;
    std::vector<FileResource> _patchErf;
    std::unordered_map<std::string, std::vector<FileResource>> _modules;
    std::unordered_map<std::string, std::vector<FileResource>> _override;
    std::unordered_map<std::string, std::vector<FileResource>> _texturePacks;
    std::vector<FileResource> _streamMusic;
    std::vector<FileResource> _streamSounds;
    std::vector<FileResource> _streamVoice;
    std::unordered_map<std::string, std::vector<FileResource>> _lips;
    std::unordered_map<std::string, std::vector<FileResource>> _rims;
    std::vector<FileResource> _rootLoose;
    std::unordered_map<std::string, std::filesystem::path> _movies;
    std::vector<FileResource> _executable;

    std::optional<std::string> _moduleRoot;
    std::vector<std::filesystem::path> _customFolders;
    std::vector<std::filesystem::path> _customCapsules;

    mutable std::unordered_map<uintptr_t, std::unordered_map<resource::ResourceId, FileResource>> _listCache;
    mutable std::optional<std::string> _filteredModulesRoot;
    mutable std::unordered_map<std::string, std::vector<FileResource>> _filteredModulesCache;

    void clearLocationCaches();

    void indexLooseFiles(const std::filesystem::path &dir, std::vector<FileResource> &out);
    void indexCapsuleDict(const std::filesystem::path &dir,
                          bool (*filter)(const std::filesystem::path &),
                          std::unordered_map<std::string, std::vector<FileResource>> &out);

    void checkList(const std::vector<FileResource> &list,
                   const resource::ResourceId &id,
                   std::vector<LocationResult> &out);

    void checkDict(const std::unordered_map<std::string, std::vector<FileResource>> &dict,
                   const resource::ResourceId &id,
                   std::vector<LocationResult> &out);

    void checkFolders(const std::vector<std::filesystem::path> &folders,
                      const resource::ResourceId &id,
                      std::vector<LocationResult> &out);

    void checkCapsules(const std::vector<std::filesystem::path> &capsules,
                       const resource::ResourceId &id,
                       std::vector<LocationResult> &out);

    void checkModules(const resource::ResourceId &id, std::vector<LocationResult> &out);

    const std::unordered_map<std::string, std::vector<FileResource>> &filteredModules();
};

} // namespace extract

} // namespace reone
