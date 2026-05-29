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

#include "reone/resource/exception/notfound.h"
#include "reone/resource/provider/dialogs.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/provider/lips.h"
#include "reone/resource/provider/paths.h"
#include "reone/resource/provider/scripts.h"
#include "reone/resource/resources.h"
#include "reone/system/fileutil.h"

using namespace reone::resource;

namespace reone {

namespace resource {

static constexpr char kModulesDirectoryName[] = "modules";
static constexpr char kSavesDirectoryName[] = "saves";
static constexpr char kShaderPackFilename[] = "shaderpack.erf";

void ResourceDirector::init() {
    loadGlobalResources();
}

void ResourceDirector::onModuleLoad(const std::string &name) {
    _dialogs.clear();
    _paths.clear();
    _scripts.clear();
    _lips.clear();
    _gffs.clear();

    loadGlobalResources();
    loadModuleResources(name);
}

void ResourceDirector::onGameLoad(std::string_view name) {
    _installation.clearSaveScope();
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
    std::vector<std::filesystem::path> customFolders;
    std::vector<std::filesystem::path> customCapsules;

    auto shaderPackPath = findFileIgnoreCase(std::filesystem::current_path(), kShaderPackFilename);
    if (shaderPackPath) {
        customCapsules.push_back(*shaderPackPath);
    }
#ifdef __EMSCRIPTEN__
    else {
        auto glslPath = findFileIgnoreCase(std::filesystem::current_path(), "glsl");
        if (glslPath) {
            customFolders.push_back(*glslPath);
        } else {
            throw ResourceNotFoundException("shaderpack.erf or /glsl folder not found");
        }
    }
#else
    else {
        throw ResourceNotFoundException(kShaderPackFilename);
    }
#endif

    _installation.clearModuleScope();
    _installation.clearSaveScope();
    _installation.setCustomFolders(std::move(customFolders));
    _installation.setCustomCapsules(std::move(customCapsules));
}

void ResourceDirector::loadModuleResources(const std::string &name) {
#ifdef __EMSCRIPTEN__
    EM_ASM({ console.log("reone web: loadModuleResources begin " + UTF8ToString($0)); }, name.c_str());
#endif
    auto moduleRoot = boost::to_lower_copy(name);
    _installation.setModuleRoot(moduleRoot);
#ifdef __EMSCRIPTEN__
    EM_ASM({ console.log("reone web: loadModuleResources done " + UTF8ToString($0)); }, name.c_str());
#endif
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

    auto savegamePath = findFileIgnoreCase(*savePath, "savegame.sav");
    if (!savegamePath) {
        throw ResourceNotFoundException("savegame.sav not found");
    }

    auto customFolders = _installation.customFolders();
    customFolders.push_back(*savePath);
    _installation.setCustomFolders(std::move(customFolders));
    _installation.setCustomCapsules({*savegamePath});
}

} // namespace resource

} // namespace reone
