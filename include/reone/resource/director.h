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

#include "resources.h"
#include "types.h"

#include "reone/extract/installation.h"

namespace reone {

namespace resource {

class IDialogs;
class IGffs;
class ILips;
class IPaths;
class IScripts;

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
    ResourceDirector(const std::filesystem::path &gamePath,
                     extract::Installation &installation,
                     IDialogs &dialogs,
                     IGffs &gffs,
                     ILips &lips,
                     IPaths &paths,
                     IScripts &scripts) :
        _gamePath(gamePath),
        _installation(installation),
        _dialogs(dialogs),
        _gffs(gffs),
        _lips(lips),
        _paths(paths),
        _scripts(scripts) {
    }

    void init() override;
    void onModuleLoad(const std::string &name) override;
    void onGameLoad(std::string_view name) override;

    std::set<std::string> moduleNames() override;
    std::set<std::string> saveNames() override;

private:
    const std::filesystem::path &_gamePath;
    extract::Installation &_installation;
    IDialogs &_dialogs;
    IGffs &_gffs;
    ILips &_lips;
    IPaths &_paths;
    IScripts &_scripts;

    void clearProviderCaches();
    void loadGlobalResources();
    void loadModuleResources(const std::string &name);
    void loadSaveGameResources(std::string_view name);
};

} // namespace resource

} // namespace reone
