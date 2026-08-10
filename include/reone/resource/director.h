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

#include "modulediscovery.h"
#include "modulemount.h"
#include "resources.h"
#include "types.h"

namespace reone {

namespace graphics {

struct GraphicsOptions;
struct GraphicsServices;

} // namespace graphics

namespace script {

struct ScriptServices;

}

namespace resource {

class IDialogs;
class IGffs;
class ILips;
class IPaths;
class IResources;
class IScripts;
class ITwoDAs;

/**
 * Whether a game's Odyssey sources are placed in the raw lookup order.
 *
 * A source list is homogeneous, so anything mounting into the game's resource
 * list has to agree with the director about this. K2 is activated; K1 keeps the
 * insertion-ordered stack it has always used until its own global startup
 * precedence is established.
 */
bool usesBucketedLookup(GameID game);

class IResourceDirector {
public:
    virtual ~IResourceDirector() = default;

    virtual void init() = 0;
    virtual void onModuleLoad(const std::string &name) = 0;
    virtual void onGameLoad(std::string_view name) = 0;

    virtual std::set<std::string> moduleNames() = 0;
    virtual std::set<std::string> saveNames() = 0;
};

class ResourceDirector : public IResourceDirector, boost::noncopyable {
public:
    ResourceDirector(GameID gameId,
                     const std::filesystem::path &gamePath,
                     const graphics::GraphicsOptions &graphicsOpt,
                     graphics::GraphicsServices &graphicsSvc,
                     script::ScriptServices &scriptSvc,
                     IDialogs &dialogs,
                     IGffs &gffs,
                     ILips &lips,
                     IPaths &paths,
                     IResources &resources,
                     IResources &auxResources,
                     IScripts &scripts,
                     ITwoDAs &twoDas) :
        _gameId(gameId),
        _gamePath(gamePath),
        _graphicsOpt(graphicsOpt),
        _graphicsSvc(graphicsSvc),
        _scriptSvc(scriptSvc),
        _dialogs(dialogs),
        _gffs(gffs),
        _lips(lips),
        _paths(paths),
        _resources(resources),
        _auxResources(auxResources),
        _scripts(scripts),
        _twoDas(twoDas) {
    }

    void init() override;
    void onModuleLoad(const std::string &name) override;
    void onGameLoad(std::string_view name) override;

    std::set<std::string> moduleNames() override;
    std::set<std::string> saveNames() override;

private:
    GameID _gameId;
    const std::filesystem::path &_gamePath;
    const graphics::GraphicsOptions &_graphicsOpt;
    graphics::GraphicsServices &_graphicsSvc;
    script::ScriptServices &_scriptSvc;
    IDialogs &_dialogs;
    IGffs &_gffs;
    ILips &_lips;
    IPaths &_paths;
    IResources &_resources;
    IResources &_auxResources;
    IScripts &_scripts;
    ITwoDAs &_twoDas;

    // Set when a savegame is loaded.
    std::optional<std::filesystem::path> _savegamePath;

    bool bucketed() const { return usesBucketedLookup(_gameId); }

    /// The given bucket, or nothing when this game is not activated. A list is
    /// homogeneous, so a game either places every source or places none.
    std::optional<ResourceSourceBucket> bucketOf(ResourceSourceBucket bucket) const;

    void loadGlobalResources();
    void loadAuxiliaryResources();
    void loadStreamResources();
    void loadSaveGameResources(std::string_view name);

    void loadModuleResources(const std::string &name);
    void loadModuleResourcesLegacy(const std::string &name);
    void loadModuleResourcesFromPolicy(const std::string &name);

    ModuleSearchRoot modulesSearchRoot();
    std::vector<ModuleSearchRoot> moduleSearchRoots();
    void addStagedModuleSources(const std::string &moduleRoot, RuntimeModuleSourceIndex &index);
    bool includeModuleInSave(const std::string &moduleRoot);

    void loadRIM(const std::filesystem::path &path, const std::string &name, ResourceOwner owner);
    void loadERF(const std::filesystem::path &path, const std::string &name, ResourceOwner owner);
};

} // namespace resource

} // namespace reone
