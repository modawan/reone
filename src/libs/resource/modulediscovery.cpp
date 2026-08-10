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

#include "reone/resource/modulediscovery.h"

#include "reone/system/fileutil.h"

#include <algorithm>
#include <array>
#include <set>

#include <boost/algorithm/string.hpp>

namespace reone {
namespace resource {
namespace {

struct SuffixRule {
    std::string_view suffix;
    std::string_view extension;
    ModuleArchiveFamily family;
};

/**
 * Supported sidecar and support suffixes, longest first so that exact
 * longest-suffix matching falls out of the search order.
 *
 * The table is the whole of what is recognized. A suffix outside it does not
 * resolve to the entry it most resembles, and no catch-all entry stands in
 * for one.
 */
constexpr std::array<SuffixRule, 7> kSuffixRules {{
    {"_adx", ".rim", ModuleArchiveFamily::AdxRim},
    {"_dlg", ".erf", ModuleArchiveFamily::Dialogue},
    {"_dlg", ".mod", ModuleArchiveFamily::Dialogue},
    {"_loc", ".mod", ModuleArchiveFamily::Localization},
    {"_loc", ".erf", ModuleArchiveFamily::Localization},
    {"_s", ".rim", ModuleArchiveFamily::StaticRim},
    {"_a", ".rim", ModuleArchiveFamily::AreaRim},
}};

struct PrimaryRule {
    std::string_view extension;
    ModuleArchiveFamily family;
};

/// Families a file named exactly after its module root may belong to.
constexpr std::array<PrimaryRule, 5> kPrimaryRules {{
    {".mod", ModuleArchiveFamily::PrimaryMod},
    {".rim", ModuleArchiveFamily::PrimaryRim},
    {".nwm", ModuleArchiveFamily::Nwm},
    {".rsv", ModuleArchiveFamily::SavedResourceImage},
    {".sav", ModuleArchiveFamily::SavedArchive},
}};

bool endsWith(const std::string &value, std::string_view suffix) {
    return value.size() > suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

struct SplitName {
    std::string stem;
    std::string extension;
};

SplitName splitFilename(std::string_view filename) {
    auto path = std::filesystem::path(filename);
    return {boost::to_lower_copy(path.stem().string()),
            boost::to_lower_copy(path.extension().string())};
}

/// The recognized suffix a stem ends with, longest first, or nothing.
std::optional<std::string_view> recognizedSuffix(const std::string &stem) {
    for (const auto &rule : kSuffixRules) {
        if (endsWith(stem, rule.suffix)) return rule.suffix;
    }
    return std::nullopt;
}

/// Regular files of a location, ordered by lowercased name for reproducibility.
std::vector<std::filesystem::path> orderedFiles(const std::filesystem::path &root) {
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) return {};
    std::vector<std::filesystem::path> paths;
    for (auto &entry : std::filesystem::directory_iterator(root)) {
        if (entry.is_regular_file()) paths.push_back(entry.path());
    }
    std::sort(paths.begin(), paths.end(), [](const auto &a, const auto &b) {
        return boost::to_lower_copy(a.filename().string()) <
               boost::to_lower_copy(b.filename().string());
    });
    return paths;
}

} // namespace

bool isPrimaryEligible(ModuleArchiveFamily family) {
    switch (family) {
    case ModuleArchiveFamily::PrimaryMod:
    case ModuleArchiveFamily::PrimaryRim:
    case ModuleArchiveFamily::Nwm:
    case ModuleArchiveFamily::SavedResourceImage:
    case ModuleArchiveFamily::SavedArchive:
        return true;
    default:
        return false;
    }
}

bool isActiveSavedState(ModuleArchiveFamily family) {
    return family == ModuleArchiveFamily::SavedResourceImage ||
           family == ModuleArchiveFamily::SavedArchive;
}

ModuleArchiveClassification classifyModuleArchive(std::string_view filename) {
    auto trimmed = boost::trim_copy(std::string(filename));
    if (trimmed.empty()) {
        return {std::nullopt, ModuleNameRejection::Empty};
    }
    auto [stem, extension] = splitFilename(trimmed);
    if (stem.empty() || extension.empty()) {
        return {std::nullopt, ModuleNameRejection::UnsupportedExtension};
    }

    // A recognized suffix resolves the file to the module it supports, so a
    // sidecar never introduces a module name of its own.
    if (auto suffix = recognizedSuffix(stem)) {
        for (const auto &rule : kSuffixRules) {
            if (rule.suffix != *suffix || rule.extension != extension) continue;
            return {ClassifiedModuleArchive {stem.substr(0, stem.size() - suffix->size()),
                                             rule.family},
                    std::nullopt};
        }
        return {std::nullopt, ModuleNameRejection::SuffixExtensionMismatch};
    }

    for (const auto &rule : kPrimaryRules) {
        if (extension == rule.extension) {
            return {ClassifiedModuleArchive {stem, rule.family}, std::nullopt};
        }
    }
    return {std::nullopt, ModuleNameRejection::UnsupportedExtension};
}

ModuleArchiveClassification classifyForModuleRoot(std::string_view filename,
                                                  std::string_view moduleRoot) {
    auto [stem, extension] = splitFilename(boost::trim_copy(std::string(filename)));
    std::string root(moduleRoot);
    // Anything outside this module's name space is another module's file:
    // not ours, and not an error.
    if (stem != root && !boost::starts_with(stem, root + "_")) {
        return {std::nullopt, std::nullopt};
    }
    if (extension.empty()) {
        return {std::nullopt, ModuleNameRejection::UnsupportedExtension};
    }

    if (stem == root) {
        for (const auto &rule : kPrimaryRules) {
            if (extension == rule.extension) {
                return {ClassifiedModuleArchive {root, rule.family}, std::nullopt};
            }
        }
        return {std::nullopt, ModuleNameRejection::UnsupportedExtension};
    }

    auto suffix = stem.substr(root.size());
    bool suffixSupported = false;
    for (const auto &rule : kSuffixRules) {
        if (suffix != rule.suffix) continue;
        if (extension == rule.extension) {
            return {ClassifiedModuleArchive {root, rule.family}, std::nullopt};
        }
        suffixSupported = true;
    }
    return {std::nullopt,
            suffixSupported ? ModuleNameRejection::SuffixExtensionMismatch
                            : ModuleNameRejection::UnsupportedSuffix};
}

ModuleNameNormalization normalizeModuleName(std::string_view requested) {
    auto trimmed = boost::trim_copy(std::string(requested));
    if (trimmed.empty()) {
        return {std::nullopt, ModuleNameRejection::Empty};
    }
    auto [stem, extension] = splitFilename(trimmed);
    if (stem.empty()) {
        return {std::nullopt, ModuleNameRejection::Empty};
    }
    if (extension.empty()) {
        return {stem, std::nullopt};
    }
    // Only the extension is removed. A trailing family suffix stays part of
    // the root: the caller has already said this is the module it wants, and
    // reading the suffix here would discard that.
    for (const auto &rule : kPrimaryRules) {
        if (extension == rule.extension) {
            return {stem, std::nullopt};
        }
    }
    for (const auto &rule : kSuffixRules) {
        if (extension == rule.extension) {
            return {stem, std::nullopt};
        }
    }
    return {std::nullopt, ModuleNameRejection::UnsupportedExtension};
}

std::vector<std::string> discoverModuleRoots(const std::vector<ModuleSearchRoot> &roots) {
    std::set<std::string> names;
    for (const auto &root : roots) {
        for (const auto &path : orderedFiles(root.path)) {
            auto classified = classifyModuleArchive(path.filename().string());
            if (!classified.archive) continue;
            // Only a primary-eligible archive introduces a module name.
            if (!isPrimaryEligible(classified.archive->family)) continue;
            names.insert(classified.archive->moduleRoot);
        }
    }
    return {names.begin(), names.end()};
}

ModuleDiscoveryResult discoverModuleSources(std::string_view requestedModule,
                                            const std::vector<ModuleSearchRoot> &roots) {
    ModuleDiscoveryResult result;
    auto normalized = normalizeModuleName(requestedModule);
    if (!normalized.root) {
        result.nameRejection = normalized.rejection;
        return result;
    }
    result.moduleRoot = *normalized.root;

    for (const auto &root : roots) {
        for (const auto &path : orderedFiles(root.path)) {
            auto filename = path.filename().string();
            // The requested root is known here, so ownership is resolved
            // against it rather than inferred from the name alone.
            auto classified = classifyForModuleRoot(filename, result.moduleRoot);
            if (!classified.archive) {
                // A rejection means the file is in this module's name space
                // but belongs to no supported family; no rejection at all
                // means it is simply another module's file.
                if (classified.rejection) {
                    result.rejected.push_back(RejectedModuleFile {
                        root.rootId, path, filename, *classified.rejection});
                }
                continue;
            }

            DiscoveredModuleSource source;
            source.candidate = ModuleSourceCandidate {
                root.rootId + ":" + boost::to_lower_copy(filename),
                root.rootId,
                root.origin,
                classified.archive->family,
                root.packageOrder,
                root.rootOrder};
            source.path = path;
            source.filename = filename;
            source.moduleRoot = result.moduleRoot;
            result.sources.push_back(std::move(source));
        }
    }
    return result;
}

const std::array<std::string_view, 5> kEncapsulatedProbeOrder {
    ".nwm", ".mod", ".sav", ".erf", ".hak"};

std::optional<std::filesystem::path> findEncapsulatedByBasename(
    const std::filesystem::path &directory,
    std::string_view basename) {
    if (basename.empty()) {
        return std::nullopt;
    }
    for (auto extension : kEncapsulatedProbeOrder) {
        auto filename = std::string(basename) + std::string(extension);
        if (auto path = findFileIgnoreCase(directory, filename)) {
            return path;
        }
    }
    return std::nullopt;
}

std::vector<ModuleSourceCandidate> plannerInventory(const ModuleDiscoveryResult &discovered) {
    std::vector<ModuleSourceCandidate> inventory;
    inventory.reserve(discovered.sources.size());
    for (const auto &source : discovered.sources) {
        inventory.push_back(source.candidate);
    }
    return inventory;
}

} // namespace resource
} // namespace reone
