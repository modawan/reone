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
#include "odysseyroots.h"
#include "modulemount.h"
#include "resources.h"
#include "saveworkingstate.h"
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
 * shared bucketed stack established by its global startup
 * precedence is established.
 */
bool usesBucketedLookup(GameID game);

class IResourceDirector {
public:
    virtual ~IResourceDirector() = default;

    virtual void init() = 0;
    virtual void onModuleLoad(const std::string &name) = 0;
    virtual void onNewGame() = 0;
    /**
     * Mount a save resolved by name below the game path.
     *
     * Convenience for callers that only hold a name. It re-resolves the
     * directory, so it does not carry durable slot identity: code that already
     * discovered a slot must pass the descriptor overload instead.
     */
    virtual void onGameLoad(std::string_view name) = 0;

    /** Mount the exact slot discovered during indexing. */
    virtual void onGameLoad(const SaveSlotDescriptor &slot) = 0;

    /**
     * Build an unpublished candidate over the exact slot discovered during
     * indexing.
     *
     * Throws if the slot cannot be opened, and mutates nothing when it does:
     * the committed session stays authoritative until commitGameLoad accepts
     * the candidate, so a load that cannot proceed leaves the running game
     * intact rather than stranding it.
     */
    virtual std::unique_ptr<SaveSessionState> prepareGameLoad(
        const SaveSlotDescriptor &slot) = 0;

    /** Publish a prepared candidate, retiring the previous save mounts. */
    virtual void commitGameLoad(std::unique_ptr<SaveSessionState> candidate) = 0;
    virtual std::optional<Resource> findSaveMetadata(const ResourceId &id) = 0;
    virtual std::optional<Resource> findSaveWorking(const ResourceId &id) = 0;
    virtual std::unordered_set<ResourceId> saveWorkingResourceIds() const = 0;
    virtual std::shared_ptr<const SaveWorkingState> committedSaveWorkingState() const = 0;
    virtual std::optional<SaveSlotDescriptor> saveSlotDescriptor() const = 0;
    virtual void adoptSaveWorkingState(
        std::shared_ptr<const SaveWorkingState> state) = 0;
    virtual void adoptPublishedSave(
        SaveSlotDescriptor descriptor,
        std::shared_ptr<const SaveWorkingState> state) = 0;

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
                     ITwoDAs &twoDas,
                     OdysseyResourceRoots odysseyRoots = {}) :
        _gameId(gameId),
        _gamePath(gamePath),
        _odysseyRoots(std::move(odysseyRoots)),
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
        if (!_odysseyRoots.nwmFiles) {
            _odysseyRoots.nwmFiles = defaultOdysseyResourceRoots(_gamePath).nwmFiles;
        }
    }

    void init() override;
    void onModuleLoad(const std::string &name) override;
    void onNewGame() override;
    void onGameLoad(std::string_view name) override;
    void onGameLoad(const SaveSlotDescriptor &slot) override;
    std::unique_ptr<SaveSessionState> prepareGameLoad(
        const SaveSlotDescriptor &slot) override;
    void commitGameLoad(std::unique_ptr<SaveSessionState> candidate) override;
    std::optional<Resource> findSaveMetadata(const ResourceId &id) override;
    std::optional<Resource> findSaveWorking(const ResourceId &id) override;
    std::unordered_set<ResourceId> saveWorkingResourceIds() const override;
    std::shared_ptr<const SaveWorkingState> committedSaveWorkingState() const override;
    std::optional<SaveSlotDescriptor> saveSlotDescriptor() const override;
    void adoptSaveWorkingState(
        std::shared_ptr<const SaveWorkingState> state) override;
    void adoptPublishedSave(
        SaveSlotDescriptor descriptor,
        std::shared_ptr<const SaveWorkingState> state) override;

    std::set<std::string> moduleNames() override;
    std::set<std::string> saveNames() override;

private:
    GameID _gameId;
    const std::filesystem::path &_gamePath;
    OdysseyResourceRoots _odysseyRoots;
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

    std::unique_ptr<SaveSessionState> _saveSession;

    bool bucketed() const { return usesBucketedLookup(_gameId); }

    /// The given bucket, or nothing when this game is not activated. A list is
    /// homogeneous, so a game either places every source or places none.
    std::optional<ResourceSourceBucket> bucketOf(ResourceSourceBucket bucket) const;

    void loadGlobalResources();
    void loadAuxiliaryResources();
    void loadStreamResources();
    void loadK1StreamResources();
    void loadRimsDirectory();
    void loadGlobalRimResource();
    void loadOverrideTexturesResource();
    void loadTexturePackResources();
    void loadPlayerSupportResource();
    void loadK1GlobalResources();
    void loadLiveResources();
    std::unique_ptr<SaveSessionState> buildSaveSession(std::string_view name);
    std::unique_ptr<SaveSessionState> buildSaveSession(const SaveSlotDescriptor &slot);
    void commitSaveSession(std::unique_ptr<SaveSessionState> candidate);

    void loadModuleResources(const std::string &name);
    void loadModuleResourcesFromPolicy(const std::string &name);

    std::vector<ModuleSearchRoot> moduleSearchRoots();
    void addStagedModuleSources(const std::string &moduleRoot, RuntimeModuleSourceIndex &index);
    bool includeModuleInSave(const std::string &moduleRoot);

};

} // namespace resource

} // namespace reone
