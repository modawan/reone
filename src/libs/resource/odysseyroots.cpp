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

#include "reone/resource/odysseyroots.h"

#include "reone/resource/strings.h"
#include "reone/system/fileutil.h"

namespace reone::resource {
namespace {

std::string pathIdentity(const std::filesystem::path &path) {
    auto identity = path.lexically_normal().string();
    boost::replace_all(identity, "\\", "/");
    return identity;
}

void appendDistinct(std::vector<std::filesystem::path> &paths,
                    const std::filesystem::path &path) {
    auto identity = pathIdentity(path);
    for (const auto &existing : paths) {
        if (pathIdentity(existing) == identity) {
            return;
        }
    }
    paths.push_back(path);
}

std::optional<std::filesystem::path> child(const std::filesystem::path &root,
                                           std::string_view name) {
    return findFileIgnoreCase(root, name);
}

void appendConfiguredModuleRoots(std::vector<ModuleSearchRoot> &result,
                                 const OdysseyResourceRoots &roots) {
    for (std::size_t i = 0; i < roots.k2OverrideRoots.size(); ++i) {
        auto modules = child(roots.k2OverrideRoots[i], "modules");
        if (!modules) {
            continue;
        }
        result.push_back(ModuleSearchRoot {
            "configured" + std::to_string(i) + ":modules",
            *modules,
            ModulePrimaryOrigin::ConfiguredModuleRoot,
            0,
            static_cast<std::uint32_t>(i)});
    }
}

} // namespace

OdysseyResourceRoots defaultOdysseyResourceRoots(const std::filesystem::path &gamePath) {
    OdysseyResourceRoots roots;
    roots.nwmFiles = gamePath / "nwm";
    return roots;
}

void loadLiveTalkTables(Strings &strings, const OdysseyResourceRoots &roots) {
    for (std::size_t i = 0; i < roots.livePackages.size(); ++i) {
        if (!roots.livePackages[i]) {
            continue;
        }
        auto slot = i + 1;
        auto filename = "live" + std::to_string(slot) + ".tlk";
        strings.loadTalkTable(slot, *roots.livePackages[i] / filename);
    }
}

std::vector<std::filesystem::path> looseOverrideRoots(
    GameID game,
    const std::filesystem::path &gamePath,
    const OdysseyResourceRoots &roots) {
    std::vector<std::filesystem::path> result;
    if (auto base = child(gamePath, "override")) {
        appendDistinct(result, *base);
    }
    if (game == GameID::TSL) {
        for (const auto &configured : roots.k2OverrideRoots) {
            if (auto path = child(configured, "override")) {
                appendDistinct(result, *path);
            }
        }
    }
    return result;
}

std::vector<std::filesystem::path> lipsRoots(
    GameID game,
    const std::filesystem::path &gamePath,
    const OdysseyResourceRoots &roots) {
    std::vector<std::filesystem::path> result;
    if (auto base = child(gamePath, "lips")) {
        appendDistinct(result, *base);
    }
    if (game == GameID::TSL) {
        for (const auto &configured : roots.k2OverrideRoots) {
            if (auto path = child(configured, "lips")) {
                appendDistinct(result, *path);
            }
        }
    }
    return result;
}

std::vector<ModuleSearchRoot> primaryModuleSearchRoots(
    GameID game,
    const std::filesystem::path &gamePath,
    const OdysseyResourceRoots &roots) {
    std::vector<ModuleSearchRoot> result;
    if (roots.nwmFiles) {
        if (auto nwm = child(roots.nwmFiles->parent_path(),
                             roots.nwmFiles->filename().string())) {
            result.push_back(ModuleSearchRoot {
                "nwm", *nwm, ModulePrimaryOrigin::NwmFiles, 0, 0});
        }
    }
    if (auto base = child(gamePath, "modules")) {
        result.push_back(ModuleSearchRoot {
            "modules", *base, ModulePrimaryOrigin::Modules, 0, 0});
    }
    for (std::size_t i = 0; i < roots.livePackages.size(); ++i) {
        if (!roots.livePackages[i]) {
            continue;
        }
        auto modules = child(*roots.livePackages[i], "modules");
        if (!modules) {
            continue;
        }
        result.push_back(ModuleSearchRoot {
            "live" + std::to_string(i + 1),
            *modules,
            ModulePrimaryOrigin::LivePackage,
            static_cast<std::uint32_t>(i + 1),
            0});
    }
    if (game == GameID::TSL) {
        appendConfiguredModuleRoots(result, roots);
    }
    return result;
}

std::vector<ModuleSearchRoot> moduleSearchRoots(
    GameID game,
    const std::filesystem::path &gamePath,
    const OdysseyResourceRoots &roots) {
    auto result = primaryModuleSearchRoots(game, gamePath, roots);
    std::vector<std::filesystem::path> added;
    if (auto base = child(gamePath, "lips")) {
        appendDistinct(added, *base);
        result.push_back(ModuleSearchRoot {
            "lips", *base, ModulePrimaryOrigin::Modules, 0, 0});
    }
    if (game == GameID::TSL) {
        for (std::size_t i = 0; i < roots.k2OverrideRoots.size(); ++i) {
            auto configured = child(roots.k2OverrideRoots[i], "lips");
            if (!configured) {
                continue;
            }
            auto before = added.size();
            appendDistinct(added, *configured);
            if (added.size() == before) {
                continue;
            }
            result.push_back(ModuleSearchRoot {
                "configured" + std::to_string(i) + ":lips",
                *configured,
                ModulePrimaryOrigin::ConfiguredModuleRoot,
                0,
                static_cast<std::uint32_t>(i)});
        }
    }
    return result;
}

} // namespace reone::resource
