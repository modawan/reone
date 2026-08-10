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

#include "id.h"
#include "modulepolicy.h"
#include "resources.h"

#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace reone {

namespace resource {

/**
 * A planning candidate together with where its bytes actually come from.
 *
 * The policy consumes only the candidate; the locator is the physical fact it
 * must not see. A discovered archive carries its path, and a module staged
 * inside an archive already in scope carries the id it is read back by. No
 * filesystem path is invented for the latter.
 */
struct RuntimeModuleSource {
    ModuleSourceCandidate candidate;
    std::variant<std::filesystem::path, ResourceId> locator;
};

/**
 * Every source offered for one module load, whatever supplied it.
 *
 * Sources found on disk and sources staged inside a mounted archive enter one
 * inventory, so the policy ranks them against each other without knowing they
 * were found differently. Lookup is by source id, which stays opaque: it is
 * minted by the supplier and never parsed.
 */
class RuntimeModuleSourceIndex {
public:
    void add(RuntimeModuleSource source) {
        _sources.push_back(std::move(source));
    }

    const RuntimeModuleSource *find(const std::string &sourceId) const;

    /// Candidates in supply order, as the policy consumes them.
    std::vector<ModuleSourceCandidate> inventory() const;

    const std::vector<RuntimeModuleSource> &sources() const { return _sources; }
    bool empty() const { return _sources.empty(); }

private:
    std::vector<RuntimeModuleSource> _sources;
};

enum class ModuleLoadOutcome {
    Succeeded,
    RecoveredThroughActiveState,
    Failed,
};

struct ModuleMountOutcome {
    std::string sourceId;
    ModuleArchiveFamily family {ModuleArchiveFamily::PrimaryRim};
    ModuleMountPhase phase {ModuleMountPhase::ModuleBranch};
    bool mounted {false};
};

/**
 * What the executor did, in the order it did it.
 *
 * requiredFailure reports that a family whose failure is not best-effort
 * produced no successful mount, which is what makes the class-2 recovery route
 * eligible. It is not by itself a failed module load.
 */
struct ModuleMountReport {
    std::vector<ModuleMountOutcome> outcomes;
    bool requiredFailure {false};
    ModuleLoadOutcome outcome {ModuleLoadOutcome::Failed};

    bool mounted(const std::string &sourceId) const;
    std::size_t mountedCount() const;
};

/**
 * Mounts a module load plan.
 *
 * There is one mode. Every mount carries the bucket the policy assigned to it,
 * because a source that has not been placed has no position in the raw lookup
 * order to be ranked at. Phases are mounted in order, so the active table is
 * mounted last and wins its bucket.
 */
class ModuleMountExecutor {
public:
    ModuleMountExecutor(IResources &resources, const RuntimeModuleSourceIndex &sources) :
        _resources(resources),
        _sources(sources) {
    }

    ModuleMountReport run(const ModuleLoadPlan &plan);

private:
    IResources &_resources;
    const RuntimeModuleSourceIndex &_sources;

    bool mount(const RuntimeModuleSource &source, const ModuleSourceMetadata &metadata);
    void runFamily(const ModuleFamilyPlan &family, ModuleMountReport &report);
    bool runActiveState(const ModuleLoadPlan &plan, ModuleMountReport &report);
    bool mountBySourceId(const std::string &sourceId,
                         ModuleArchiveFamily family,
                         ModuleMountPhase phase,
                         const ModuleSourceMetadata &metadata,
                         ModuleMountReport &report);
};

/// Whether a family's mounted table is a resource image rather than an
/// encapsulated archive. Derived from the family, not from the bucket, so that
/// container form and lookup order stay separate decisions.
bool isResourceImageFamily(ModuleArchiveFamily family);

} // namespace resource

} // namespace reone
