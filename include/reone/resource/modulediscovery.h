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

#pragma once

#include "reone/resource/modulepolicy.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace reone {
namespace resource {

/**
 * Why a name was not accepted. Unsupported and ambiguous names are reported
 * rather than reduced onto a supported family.
 */
enum class ModuleNameRejection {
    Empty,
    UnsupportedExtension,
    UnsupportedSuffix,
    SuffixExtensionMismatch,
};

/**
 * A filename resolved against the archive-family contract.
 *
 * moduleRoot is the module the file belongs to, never the file's own stem: a
 * sidecar resolves to the module it supports, so a sidecar can never introduce
 * a module name of its own.
 */
struct ClassifiedModuleArchive {
    std::string moduleRoot;
    ModuleArchiveFamily family {ModuleArchiveFamily::PrimaryRim};
};

/**
 * Outcome of resolving a filename.
 *
 * At most one field is ever set. Both being empty is a third outcome, and only
 * classifyForModuleRoot produces it: the file belongs to a different module,
 * which is neither a result nor an error. Resolving without a root always
 * yields an archive or a rejection.
 */
struct ModuleArchiveClassification {
    std::optional<ClassifiedModuleArchive> archive;
    std::optional<ModuleNameRejection> rejection;
};

/// Outcome of normalizing a requested module name. Exactly one field is set.
struct ModuleNameNormalization {
    std::optional<std::string> root;
    std::optional<ModuleNameRejection> rejection;
};

/**
 * A location searched for module archives.
 *
 * origin says what kind of location it is. packageOrder orders numbered
 * packages and rootOrder orders configured roots in their configured
 * enumeration order; the policy consumes the two differently, so discovery
 * carries both rather than collapsing them into list position.
 */
struct ModuleSearchRoot {
    std::string rootId;
    std::filesystem::path path;
    ModulePrimaryOrigin origin {ModulePrimaryOrigin::Modules};
    std::uint32_t packageOrder {0};
    std::uint32_t rootOrder {0};
};

/**
 * One discovered archive.
 *
 * candidate is exactly what the policy consumes. The remaining fields are the
 * physical facts the policy must not see: where the file is, what it was
 * called, and which module it resolved to.
 */
struct DiscoveredModuleSource {
    ModuleSourceCandidate candidate;
    std::filesystem::path path;
    std::string filename;
    std::string moduleRoot;

    /**
     * Internal ARE/GIT module identity, where the architecture can supply it
     * naturally. Discovery does not open archives to obtain it, so it is unset
     * here. The field exists so callers that do know the identity are not
     * forced to conflate it with the outer filename.
     */
    std::optional<std::string> internalModuleId;
};

/// A file in a module's name space that belongs to no supported family.
struct RejectedModuleFile {
    std::string rootId;
    std::filesystem::path path;
    std::string filename;
    ModuleNameRejection reason {ModuleNameRejection::UnsupportedSuffix};
};

struct ModuleDiscoveryResult {
    /// Empty when the request itself was rejected.
    std::string moduleRoot;
    std::optional<ModuleNameRejection> nameRejection;
    std::vector<DiscoveredModuleSource> sources;
    std::vector<RejectedModuleFile> rejected;
};

/**
 * Whether a family can be the source a module is entered or staged from.
 * Sidecar and support families never can, which is what stops them from
 * introducing module names.
 */
bool isPrimaryEligible(ModuleArchiveFamily family);

/**
 * Whether a family is active saved state, and so selectable only when the
 * module-save policy admits it. The gate itself is an input to the policy;
 * discovery only reports which candidates it applies to.
 */
bool isActiveSavedState(ModuleArchiveFamily family);

/**
 * Resolve a filename to the module it belongs to, with no requested root for
 * context. Used when enumerating an installation, where the module a file
 * belongs to has to be inferred from the name alone.
 *
 * Suffixes are matched exactly, longest first, so a sidecar resolves to the
 * module it supports. A suffix outside the recognized set is not resolved to
 * the one it most resembles.
 */
ModuleArchiveClassification classifyModuleArchive(std::string_view filename);

/**
 * Resolve a filename against a module root that is already known.
 *
 * Knowing the root removes the ambiguity that inference cannot: relative to
 * the root "foo_s", "foo_s.rim" is that module's own primary and
 * "foo_s_s.rim" is its static sidecar. Returns neither an archive nor a
 * rejection for a file belonging to some other module.
 */
ModuleArchiveClassification classifyForModuleRoot(std::string_view filename,
                                                  std::string_view moduleRoot);

/**
 * Normalize a requested module name to its canonical root.
 *
 * The caller has already identified the request as a module, so the name is
 * preserved apart from case and a supported archive extension. A root that
 * happens to end in something resembling a family suffix is kept: only
 * enumeration, which must infer ownership, treats such a name as a sidecar.
 */
ModuleNameNormalization normalizeModuleName(std::string_view requested);

/**
 * Module names that are actually playable across the search roots, sorted and
 * independent of directory enumeration order. Only a primary-eligible archive
 * introduces a name, so a location holding nothing but sidecars yields none.
 */
std::vector<std::string> discoverModuleRoots(const std::vector<ModuleSearchRoot> &roots);

/**
 * Discover every source belonging to the requested module, in root order.
 *
 * Classification derives nothing from a filename but family and module root.
 * Which family applies to a game, in which order sources are consulted, how
 * many may mount and how long they live are all policy.
 */
ModuleDiscoveryResult discoverModuleSources(std::string_view requestedModule,
                                            const std::vector<ModuleSearchRoot> &roots);

/**
 * Construct the two exact module adjunct images from the installation RIMS
 * location. These names are probed directly and RIMS is never enumerated.
 */
std::vector<DiscoveredModuleSource> discoverRimsModuleAdjuncts(
    std::string_view moduleRoot,
    const std::filesystem::path &gamePath);

/**
 * Extensions the generic encapsulated opener probes for an exact basename, in
 * the order it probes them.
 *
 * A caller that mounts a basename rather than a filename gets whichever of
 * these exists first. This is the opener's own order, not a preference of the
 * caller's, so every such mount shares it.
 */
extern const std::array<std::string_view, 5> kEncapsulatedProbeOrder;

/**
 * The first encapsulated container matching an exact basename in a location,
 * or nothing when the location holds none of them.
 *
 * Nothing is opened or validated here; this only answers which file the
 * basename resolves to. An absent basename is a normal answer, not an error.
 */
std::optional<std::filesystem::path> findEncapsulatedByBasename(
    const std::filesystem::path &directory,
    std::string_view basename);

/// Candidate inventory for the policy, in discovery order.
std::vector<ModuleSourceCandidate> plannerInventory(const ModuleDiscoveryResult &discovered);

} // namespace resource
} // namespace reone
