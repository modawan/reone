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

#include "reone/resource/modulemount.h"

#include "reone/system/exception/validation.h"
#include "reone/system/logutil.h"

#include <algorithm>

namespace reone {

namespace resource {

bool isResourceImageFamily(ModuleArchiveFamily family) {
    switch (family) {
    case ModuleArchiveFamily::PrimaryRim:
    case ModuleArchiveFamily::StaticRim:
    case ModuleArchiveFamily::AreaRim:
    case ModuleArchiveFamily::AdxRim:
    case ModuleArchiveFamily::SavedResourceImage:
        return true;
    default:
        return false;
    }
}

const RuntimeModuleSource *RuntimeModuleSourceIndex::find(const std::string &sourceId) const {
    auto it = std::find_if(_sources.begin(), _sources.end(), [&sourceId](const auto &source) {
        return source.candidate.sourceId == sourceId;
    });
    return it != _sources.end() ? &*it : nullptr;
}

std::vector<ModuleSourceCandidate> RuntimeModuleSourceIndex::inventory() const {
    std::vector<ModuleSourceCandidate> inventory;
    inventory.reserve(_sources.size());
    for (const auto &source : _sources) {
        inventory.push_back(source.candidate);
    }
    return inventory;
}

bool ModuleMountReport::mounted(const std::string &sourceId) const {
    return std::any_of(outcomes.begin(), outcomes.end(), [&sourceId](const auto &outcome) {
        return outcome.mounted && outcome.sourceId == sourceId;
    });
}

std::size_t ModuleMountReport::mountedCount() const {
    return static_cast<std::size_t>(std::count_if(outcomes.begin(), outcomes.end(), [](const auto &outcome) {
        return outcome.mounted;
    }));
}

bool ModuleMountExecutor::mount(const RuntimeModuleSource &source, const ModuleSourceMetadata &metadata) {
    auto owner = metadata.owner;
    auto image = isResourceImageFamily(source.candidate.family);
    try {
        if (const auto *path = std::get_if<std::filesystem::path>(&source.locator)) {
            if (!std::filesystem::exists(*path)) {
                return false;
            }
            if (image) {
                _resources.addRIM(*path, owner, metadata.bucket);
            } else {
                _resources.addERF(*path, owner, metadata.bucket);
            }
            return true;
        }
        // A module staged inside an archive already in scope. The bytes are read
        // back through one explicit id: the container is never walked into, so
        // nested archives do not become recursively active.
        const auto &id = std::get<ResourceId>(source.locator);
        auto res = _resources.find(id);
        if (!res) {
            return false;
        }
        if (image) {
            _resources.addMemRIM(res->data, owner, metadata.bucket);
        } else {
            _resources.addMemERF(res->data, owner, metadata.bucket);
        }
        return true;
    } catch (const ValidationException &) {
        // Two things raise this, and neither is best effort.
        //
        // The source list raises it when this executor asks for something the
        // list cannot represent, such as placing a source into a list that
        // holds unplaced ones; that is a fault in the plan or the caller, and
        // degrading it to a missed mount would hide it behind a best-effort
        // result. The archive readers raise it for a malformed container, which
        // the unactivated path does not guard against either, so swallowing it
        // would leave an activated game quietly more tolerant of broken data.
        //
        // Best effort covers a source that is not there, which is answered
        // above without opening anything.
        throw;
    } catch (const std::exception &e) {
        warn("Failed mounting module source '" + source.candidate.sourceId + "': " + e.what());
        return false;
    }
}

bool ModuleMountExecutor::mountBySourceId(const std::string &sourceId,
                                          ModuleArchiveFamily family,
                                          ModuleMountPhase phase,
                                          const ModuleSourceMetadata &metadata,
                                          ModuleMountReport &report) {
    const auto *source = _sources.find(sourceId);
    bool mounted = source && mount(*source, metadata);
    report.outcomes.push_back(ModuleMountOutcome {sourceId, family, phase, mounted});
    return mounted;
}

void ModuleMountExecutor::runFamily(const ModuleFamilyPlan &family, ModuleMountReport &report) {
    bool anySucceeded = false;
    for (const auto &attempt : family.attempts) {
        bool mounted = mountBySourceId(attempt.source.sourceId,
                                       attempt.family,
                                       attempt.phase,
                                       attempt.metadata,
                                       report);
        anySucceeded = anySucceeded || mounted;
        // A family limited to one source looks at one candidate and stops,
        // whether or not that candidate mounted: the limit is on what is
        // considered, not on what succeeds. A family that takes the first
        // source to supply it keeps going until one does. The planner happens
        // to give the former a single attempt, but the limit is the family's
        // own and is enforced here rather than assumed.
        if (family.cardinality == MountCardinality::ZeroOrOne) {
            break;
        }
        if (mounted && family.cardinality == MountCardinality::FirstSuccessful) {
            break;
        }
    }
    if (anySucceeded || family.attempts.empty()) {
        return;
    }
    auto effect = family.attempts.front().failureEffect;
    if (effect != MountFailureEffect::BestEffort) {
        report.requiredFailure = true;
    }
}

bool ModuleMountExecutor::runActiveState(const ModuleLoadPlan &plan, ModuleMountReport &report) {
    bool activeStateMounted = false;
    if (plan.primary) {
        const auto &primary = plan.primary->candidate.source;
        // In the MOD branch the selected archive is also a branch attempt and
        // is already mounted. Selecting a source and mounting the module's
        // tables are separate operations, so this is the only place the two can
        // coincide.
        activeStateMounted = report.mounted(primary.sourceId);
        if (!activeStateMounted) {
            auto metadata = mountMetadata(primary.family);
            // NWM has no generic mount metadata: discovery of a packaged module
            // does not make it a permanent class-2 source. Only the selected
            // NWM becomes the active module state, directly and without
            // CURRENTGAME staging.
            if (primary.family == ModuleArchiveFamily::Nwm) {
                metadata = ModuleSourceMetadata {
                    ResourceSourceBucket::EncapsulatedClass2,
                    plan.activeState.owner};
            }
            if (metadata) {
                // What this phase mounts is the module's active state, so it
                // takes the active-state owner whatever family supplied it. The
                // bucket still comes from the family: how long a source lives
                // and where it is searched are answered separately.
                metadata->owner = plan.activeState.owner;
                activeStateMounted = mountBySourceId(primary.sourceId,
                                                     primary.family,
                                                     ModuleMountPhase::ActiveCurrentGame,
                                                     *metadata,
                                                     report);
            }
        }
    }
    if (!report.requiredFailure) {
        return false;
    }

    // A successfully mounted saved archive is the original CURRENTGAME
    // recovery route, including when it was already the selected primary.
    for (const auto &outcome : report.outcomes) {
        if (outcome.mounted &&
            outcome.family == ModuleArchiveFamily::SavedArchive &&
            outcome.phase == ModuleMountPhase::ActiveCurrentGame) {
            return true;
        }
    }

    // Otherwise try the staged class-2 archive once as recovery.
    for (const auto &source : _sources.sources()) {
        if (source.candidate.family != ModuleArchiveFamily::SavedArchive) {
            continue;
        }
        return mountBySourceId(source.candidate.sourceId,
                               ModuleArchiveFamily::SavedArchive,
                               ModuleMountPhase::ActiveCurrentGame,
                               plan.activeState.encapsulatedArchive,
                               report);
    }
    return false;
}
ModuleMountReport ModuleMountExecutor::run(const ModuleLoadPlan &plan) {
    ModuleMountReport report;
    for (const auto &family : plan.families) {
        runFamily(family, report);
    }
    bool recovered = runActiveState(plan, report);
    if (!plan.primary) {
        return report;
    }
    if (report.requiredFailure) {
        report.outcome = recovered
                             ? ModuleLoadOutcome::RecoveredThroughActiveState
                             : ModuleLoadOutcome::Failed;
    } else if (report.mounted(plan.primary->candidate.source.sourceId)) {
        report.outcome = ModuleLoadOutcome::Succeeded;
    }
    return report;
}

} // namespace resource

} // namespace reone
