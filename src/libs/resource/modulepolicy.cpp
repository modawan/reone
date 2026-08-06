/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "reone/resource/modulepolicy.h"

#include <algorithm>
#include <limits>
#include <tuple>

namespace reone {
namespace resource {
namespace {

std::optional<ModulePrimaryKind> primaryKind(ModuleArchiveFamily family) {
    switch (family) {
    case ModuleArchiveFamily::SavedResourceImage: return ModulePrimaryKind::SavedResourceImage;
    case ModuleArchiveFamily::SavedArchive: return ModulePrimaryKind::SavedArchive;
    case ModuleArchiveFamily::Nwm: return ModulePrimaryKind::Nwm;
    case ModuleArchiveFamily::PrimaryMod: return ModulePrimaryKind::Mod;
    case ModuleArchiveFamily::PrimaryRim: return ModulePrimaryKind::Rim;
    default: return std::nullopt;
    }
}

bool isNormalModuleRoot(ModulePrimaryOrigin origin) {
    return origin == ModulePrimaryOrigin::Modules ||
           origin == ModulePrimaryOrigin::ConfiguredModuleRoot;
}

/**
 * Source class of a candidate, or -1 when it is not eligible.
 *
 * The two games differ materially here: K1 consults the ordinary module
 * location before numbered packages, K2 consults numbered packages first.
 */
int sourceClass(const ModulePolicyRequest &request, const ModuleSourceCandidate &candidate) {
    switch (candidate.family) {
    case ModuleArchiveFamily::SavedResourceImage:
        if (!request.includeInSave) return -1;
        return candidate.origin == ModulePrimaryOrigin::GameInProgress ? 0 : -1;
    case ModuleArchiveFamily::SavedArchive:
        if (!request.includeInSave) return -1;
        return candidate.origin == ModulePrimaryOrigin::GameInProgress ? 1 : -1;
    case ModuleArchiveFamily::Nwm:
        return candidate.origin == ModulePrimaryOrigin::NwmFiles ? 2 : -1;
    case ModuleArchiveFamily::PrimaryMod:
    case ModuleArchiveFamily::PrimaryRim:
        if (request.game == GameID::KotOR) {
            if (candidate.origin == ModulePrimaryOrigin::Modules) return 3;
            if (candidate.origin == ModulePrimaryOrigin::LivePackage) return 4;
            return -1;
        }
        if (candidate.origin == ModulePrimaryOrigin::LivePackage) return 3;
        if (isNormalModuleRoot(candidate.origin)) return 4;
        return -1;
    default:
        return -1;
    }
}

using SelectionRank = std::tuple<int, std::uint32_t, std::uint32_t, std::uint32_t, std::size_t>;

/**
 * Rank of an eligible candidate. Lower wins.
 *
 * Within a numbered package the engine probes one package at a time and stops
 * at the first hit, so package order outranks archive type. Within the ordinary
 * module location the engine exposes every root at once and tests MOD across
 * all of them before RIM, so archive type outranks root order there. Root order
 * is then consumed in selection-priority order, which runs opposite to the
 * configured order used when mounting, and the base location is the fallback.
 */
SelectionRank selectionRank(int cls, const ModuleSourceCandidate &candidate, std::size_t index) {
    constexpr auto kMaxRoot = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t typeRank = candidate.family == ModuleArchiveFamily::PrimaryMod ? 0 : 1;
    if (candidate.origin == ModulePrimaryOrigin::LivePackage) {
        return {cls, candidate.packageOrder, typeRank, 0, index};
    }
    if (isNormalModuleRoot(candidate.origin)) {
        bool configured = candidate.origin == ModulePrimaryOrigin::ConfiguredModuleRoot;
        return {cls,
                typeRank,
                configured ? 0u : 1u,
                configured ? kMaxRoot - candidate.rootOrder : 0u,
                index};
    }
    return {cls, 0, 0, 0, index};
}

/**
 * Candidates of one family, in configured root order, base location last.
 *
 * Configured module roots are a K2 concept. K1 enumerates no such roots for
 * any family, so it never sees them here either; letting them through would
 * plan a mount K1 cannot reach, and would contradict the selection rules.
 */
std::vector<const ModuleSourceCandidate *> rootsThenBase(
    const std::vector<ModuleSourceCandidate> &inventory,
    ModuleArchiveFamily family,
    bool allowConfiguredRoots) {
    std::vector<const ModuleSourceCandidate *> configured;
    std::vector<const ModuleSourceCandidate *> base;
    for (const auto &candidate : inventory) {
        if (candidate.family != family) continue;
        if (candidate.origin == ModulePrimaryOrigin::ConfiguredModuleRoot) {
            if (allowConfiguredRoots) configured.push_back(&candidate);
        } else if (candidate.origin == ModulePrimaryOrigin::Modules) {
            base.push_back(&candidate);
        }
    }
    std::stable_sort(configured.begin(), configured.end(), [](const auto *a, const auto *b) {
        return a->rootOrder < b->rootOrder;
    });
    configured.insert(configured.end(), base.begin(), base.end());
    return configured;
}

/// Candidates of one family, base location first, then configured root order.
std::vector<const ModuleSourceCandidate *> baseThenRoots(
    const std::vector<ModuleSourceCandidate> &inventory,
    ModuleArchiveFamily family,
    bool allowConfiguredRoots) {
    auto ordered = rootsThenBase(inventory, family, allowConfiguredRoots);
    std::stable_partition(ordered.begin(), ordered.end(), [](const auto *candidate) {
        return candidate->origin == ModulePrimaryOrigin::Modules;
    });
    return ordered;
}

/// The single exact adjunct image of a family, if the inventory offers one.
const ModuleSourceCandidate *exactAdjunct(const std::vector<ModuleSourceCandidate> &inventory,
                                          ModuleArchiveFamily family) {
    auto it = std::find_if(inventory.begin(), inventory.end(), [&](const auto &candidate) {
        return candidate.family == family;
    });
    return it != inventory.end() ? &*it : nullptr;
}

class PlanBuilder {
public:
    explicit PlanBuilder(ModuleLoadPlan &plan) :
        _plan(plan) {
    }

    void add(ModuleArchiveFamily family,
             MountCardinality cardinality,
             ModuleMountPhase phase,
             MountFailureEffect failureEffect,
             const std::vector<const ModuleSourceCandidate *> &sources) {
        auto metadata = mountMetadata(family);
        if (sources.empty() || !metadata) return;
        ModuleFamilyPlan familyPlan;
        familyPlan.family = family;
        familyPlan.cardinality = cardinality;
        // A family that admits at most one mount is given at most one attempt,
        // so that cardinality and attempt count cannot describe different plans.
        auto count = cardinality == MountCardinality::ZeroOrOne ? 1u : sources.size();
        for (std::size_t i = 0; i < std::min<std::size_t>(count, sources.size()); ++i) {
            familyPlan.attempts.push_back(ResourceMountAttempt {
                *sources[i], family, phase, *metadata, failureEffect, _order++});
        }
        _plan.families.push_back(std::move(familyPlan));
    }

private:
    ModuleLoadPlan &_plan;
    std::uint32_t _order {0};
};

} // namespace

std::optional<ModuleSourceMetadata> mountMetadata(ModuleArchiveFamily family) {
    switch (family) {
    // Module archives, saved module state and the dialogue archive are all
    // encapsulated class 2. Class 1 is evidence-backed and belongs to the
    // global patch archive and package texture archives, none of which is a
    // module source, so no module family may claim it.
    case ModuleArchiveFamily::PrimaryMod:
    case ModuleArchiveFamily::Dialogue:
    case ModuleArchiveFamily::Localization:
    case ModuleArchiveFamily::PlayerSupport:
        return ModuleSourceMetadata {ResourceSourceBucket::EncapsulatedClass2,
                                     ResourceOwner::ActiveModule};
    case ModuleArchiveFamily::SavedArchive:
        return ModuleSourceMetadata {ResourceSourceBucket::EncapsulatedClass2,
                                     ResourceOwner::ActiveModuleState};
    case ModuleArchiveFamily::SavedResourceImage:
        return ModuleSourceMetadata {ResourceSourceBucket::ResourceImage,
                                     ResourceOwner::ActiveModuleState};
    // A packaged module reaches its final form through staging, so its mount
    // route belongs to the caller. Assigning it a bucket here would invent one.
    case ModuleArchiveFamily::Nwm:
        return std::nullopt;
    default:
        return ModuleSourceMetadata {ResourceSourceBucket::ResourceImage,
                                     ResourceOwner::ActiveModule};
    }
}

std::optional<ModulePrimarySelection> selectModulePrimary(
    const ModulePolicyRequest &request,
    const std::vector<ModuleSourceCandidate> &inventory) {
    const ModuleSourceCandidate *selected = nullptr;
    ModulePrimaryKind selectedKind {ModulePrimaryKind::Rim};
    SelectionRank selectedRank {std::numeric_limits<int>::max(),
                                std::numeric_limits<std::uint32_t>::max(),
                                std::numeric_limits<std::uint32_t>::max(),
                                std::numeric_limits<std::uint32_t>::max(),
                                std::numeric_limits<std::size_t>::max()};
    for (std::size_t i = 0; i < inventory.size(); ++i) {
        const auto &candidate = inventory[i];
        auto kind = primaryKind(candidate.family);
        auto cls = sourceClass(request, candidate);
        if (!kind || cls < 0) continue;
        auto rank = selectionRank(cls, candidate, i);
        if (rank < selectedRank) {
            selected = &candidate;
            selectedKind = *kind;
            selectedRank = rank;
        }
    }
    if (!selected) return std::nullopt;

    ModulePrimarySelection selection;
    selection.candidate = ModulePrimaryCandidate {*selected, selectedKind};
    selection.canonicalModuleName = request.moduleName;
    selection.fromSavedState = selectedKind == ModulePrimaryKind::SavedResourceImage ||
                               selectedKind == ModulePrimaryKind::SavedArchive;
    selection.isNwm = selectedKind == ModulePrimaryKind::Nwm;
    return selection;
}

ModuleLoadPlan planModuleLoad(const ModulePolicyRequest &request,
                              const std::vector<ModuleSourceCandidate> &inventory) {
    ModuleLoadPlan plan;
    plan.primary = selectModulePrimary(request, inventory);

    plan.activeState.canonicalModuleName = request.moduleName;
    plan.activeState.encapsulatedArchive = *mountMetadata(ModuleArchiveFamily::SavedArchive);
    plan.activeState.resourceImage = *mountMetadata(ModuleArchiveFamily::SavedResourceImage);
    plan.activeState.savedModeRequested = request.savedMode;

    const bool k1 = request.game == GameID::KotOR;
    // Configured module roots are enumerated by K2 only, for every family.
    const bool roots = !k1;
    PlanBuilder builder(plan);

    // Support archives. K1 mounts a player archive here; neither game lets a
    // support failure fail the module load.
    if (k1) {
        builder.add(ModuleArchiveFamily::PlayerSupport,
                    MountCardinality::ZeroOrOne,
                    ModuleMountPhase::Support,
                    MountFailureEffect::BestEffort,
                    baseThenRoots(inventory, ModuleArchiveFamily::PlayerSupport, roots));
    }
    builder.add(ModuleArchiveFamily::Localization,
                MountCardinality::AllSuccessful,
                ModuleMountPhase::Support,
                MountFailureEffect::BestEffort,
                baseThenRoots(inventory, ModuleArchiveFamily::Localization, roots));

    // Adjunct images, exact and singular, tried before the module's own
    // packaging is resolved. An absent adjunct is not a failure.
    for (auto family : {ModuleArchiveFamily::AreaRim, ModuleArchiveFamily::AdxRim}) {
        if (const auto *adjunct = exactAdjunct(inventory, family)) {
            builder.add(family,
                        MountCardinality::ZeroOrOne,
                        ModuleMountPhase::AdjunctImages,
                        MountFailureEffect::BestEffort,
                        {adjunct});
        }
    }

    // The branch turns on a module archive being visible in the module
    // locations, not on which source was selected as the primary.
    auto moduleArchives = baseThenRoots(inventory, ModuleArchiveFamily::PrimaryMod, roots);
    if (!moduleArchives.empty()) {
        builder.add(ModuleArchiveFamily::PrimaryMod,
                    k1 ? MountCardinality::ZeroOrOne : MountCardinality::AllSuccessful,
                    ModuleMountPhase::ModuleBranch,
                    MountFailureEffect::FailOrCurrentGameFallback,
                    moduleArchives);
        return plan;
    }

    // Split branch. The static image stops at the first root that supplies it
    // and falls back to the base location, so its attempts run configured roots
    // first. Dialogue attempts every location and never fails the load, and is
    // absent from the K1 path entirely.
    builder.add(ModuleArchiveFamily::StaticRim,
                MountCardinality::FirstSuccessful,
                ModuleMountPhase::ModuleBranch,
                MountFailureEffect::FailOrCurrentGameFallback,
                rootsThenBase(inventory, ModuleArchiveFamily::StaticRim, roots));
    if (!k1) {
        builder.add(ModuleArchiveFamily::Dialogue,
                    MountCardinality::AllSuccessful,
                    ModuleMountPhase::ModuleBranch,
                    MountFailureEffect::BestEffort,
                    baseThenRoots(inventory, ModuleArchiveFamily::Dialogue, roots));
    }
    return plan;
}

} // namespace resource
} // namespace reone
