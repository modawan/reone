/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include "reone/resource/types.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace reone {
namespace resource {

/**
 * Raw lookup bucket. Bucket order, and order within a bucket, decide which
 * source wins an identical resref and resource type. Neither mount phase nor
 * ownership takes part in that decision.
 */
enum class ResourceSourceBucket {
    LooseDirectory,
    EncapsulatedClass1,
    ResourceImage,
    EncapsulatedClass2,
    KeyBif,
};

enum class EncapsulatedClass { Class1, Class2 };

/**
 * What causes a source to be removed. Ownership answers when a source goes
 * away; it never answers where a source is searched.
 *
 * SaveSlot and ActiveModuleState are deliberately distinct: an outer save
 * archive holds state for every module it has visited and survives a module
 * transition, whereas the staged state of the module currently entered does
 * not.
 */
enum class ResourceOwner {
    Global,
    SaveSlot,
    ActiveModule,
    ActiveModuleState,
    TemporaryDiscovery,
};

enum class ModulePrimaryKind { SavedResourceImage, SavedArchive, Nwm, Mod, Rim };

enum class ModulePrimaryOrigin {
    GameInProgress,
    NwmFiles,
    Modules,
    LivePackage,
    ConfiguredModuleRoot,
};

/**
 * Archive families the policy models. "_adrx" is absent on purpose: it has
 * never been observed, is not a supported family, and must never be treated
 * as "_adx".
 *
 * SavedResourceImage, SavedArchive and Nwm describe primary candidates rather
 * than sources the module branch mounts. For the saved pair, mountMetadata
 * describes the staged active table, not the discovery source it was found
 * through.
 */
enum class ModuleArchiveFamily {
    PrimaryMod,
    PrimaryRim,
    StaticRim,
    AreaRim,
    AdxRim,
    Dialogue,
    Localization,
    PlayerSupport,
    SavedResourceImage,
    SavedArchive,
    Nwm,
};

/// Phase of the module load a mount attempt belongs to.
enum class ModuleMountPhase {
    Support,
    AdjunctImages,
    ModuleBranch,
    ActiveCurrentGame,
};

/// How many attempts of one family may end up mounted.
enum class MountCardinality { ZeroOrOne, FirstSuccessful, AllSuccessful };

/// What a failed attempt means for the module load as a whole.
enum class MountFailureEffect { BestEffort, FailOrCurrentGameFallback, Required };

/**
 * One normalized source offered to the policy. Identifiers are opaque: the
 * policy never reads them as filesystem paths and never manufactures one.
 *
 * packageOrder orders numbered packages; rootOrder orders configured module
 * roots in their configured enumeration order. The two orderings are used
 * differently by selection and by mount planning, so both are explicit.
 */
struct ModuleSourceCandidate {
    std::string sourceId;
    std::string rootId;
    ModulePrimaryOrigin origin {ModulePrimaryOrigin::Modules};
    ModuleArchiveFamily family {ModuleArchiveFamily::PrimaryRim};
    std::uint32_t packageOrder {0};
    std::uint32_t rootOrder {0};
};

/**
 * A source eligible to be the primary, with the kind that eligibility gives
 * it. Origin and ordering are not repeated here: they belong to the source and
 * duplicating them would allow the two copies to disagree.
 */
struct ModulePrimaryCandidate {
    ModuleSourceCandidate source;
    ModulePrimaryKind kind {ModulePrimaryKind::Rim};
};

/**
 * The source chosen to enter or stage the module.
 *
 * This deliberately carries no bucket or class: the selected source is copied
 * or staged, and the table eventually mounted for the active module can take a
 * different form. Giving a selection object raw lookup metadata would conflate
 * two separate decisions.
 */
struct ModulePrimarySelection {
    ModulePrimaryCandidate candidate;
    std::string canonicalModuleName;
    bool fromSavedState {false};
    bool isNwm {false};
};

/**
 * Encapsulated class implied by a bucket, or nothing for a bucket that is not
 * encapsulated. Class is derived rather than stored so that it cannot come to
 * disagree with the bucket it describes.
 */
constexpr std::optional<EncapsulatedClass> encapsulatedClassOf(ResourceSourceBucket bucket) {
    switch (bucket) {
    case ResourceSourceBucket::EncapsulatedClass1: return EncapsulatedClass::Class1;
    case ResourceSourceBucket::EncapsulatedClass2: return EncapsulatedClass::Class2;
    default: return std::nullopt;
    }
}

/// Bucket and ownership of a source once it is actually mounted.
struct ModuleSourceMetadata {
    ResourceSourceBucket bucket {ResourceSourceBucket::ResourceImage};
    ResourceOwner owner {ResourceOwner::ActiveModule};
};

struct ResourceMountAttempt {
    ModuleSourceCandidate source;
    ModuleArchiveFamily family {ModuleArchiveFamily::PrimaryRim};
    ModuleMountPhase phase {ModuleMountPhase::ModuleBranch};
    ModuleSourceMetadata metadata;
    MountFailureEffect failureEffect {MountFailureEffect::BestEffort};
    std::uint32_t attemptOrder {0};
};

/**
 * Attempts for one family, in the order the loader tries them, together with
 * how many of them may remain mounted. Cardinality is per family: a single
 * rule covering every family is wrong in both directions.
 */
struct ModuleFamilyPlan {
    ModuleArchiveFamily family {ModuleArchiveFamily::PrimaryRim};
    MountCardinality cardinality {MountCardinality::ZeroOrOne};
    std::vector<ResourceMountAttempt> attempts;
};

/**
 * The final active table for the module, staged under the current-game
 * location. Both forms are described rather than resolved: which one applies
 * depends on saved mode and on whether a required branch mount failed at run
 * time, and neither is known to a pure policy. Performing the staging is not
 * part of this policy.
 *
 * Meaningful only when the plan selected a primary. With no primary there is
 * nothing to stage, and this is inert description rather than an instruction.
 */
struct ModuleActiveStatePlan {
    std::string canonicalModuleName;
    ModuleMountPhase phase {ModuleMountPhase::ActiveCurrentGame};
    ResourceOwner owner {ResourceOwner::ActiveModuleState};
    /// Used when saved mode was requested, or as the recovery route when a
    /// required branch mount fails.
    ModuleSourceMetadata encapsulatedArchive;
    /// Used otherwise, when the module is present there as a resource image.
    ModuleSourceMetadata resourceImage;
    bool savedModeRequested {false};
};

/**
 * Ordered plan for one module load. families holds the phases in loader order;
 * the selected primary is not an entry in it, because selecting a source and
 * mounting the module's tables are separate operations.
 */
struct ModuleLoadPlan {
    std::optional<ModulePrimarySelection> primary;
    std::vector<ModuleFamilyPlan> families;
    ModuleActiveStatePlan activeState;
};

/**
 * Inputs that are not properties of any single source.
 *
 * includeInSave mirrors the module-save table: when it is false, saved
 * candidates are not eligible to be selected at all.
 *
 * savedMode reports that active saved state for this module was already
 * detected under the current-game location, which is what makes the active
 * table take its encapsulated form. It is an input to the policy, not
 * something the policy discovers.
 */
struct ModulePolicyRequest {
    GameID game {GameID::KotOR};
    std::string moduleName;
    bool includeInSave {true};
    bool savedMode {false};
};

/// Raw lookup order. Independent of ownership and of mount phase.
constexpr std::array<ResourceSourceBucket, 5> kRawResourceLookupOrder {
    ResourceSourceBucket::LooseDirectory,
    ResourceSourceBucket::EncapsulatedClass1,
    ResourceSourceBucket::ResourceImage,
    ResourceSourceBucket::EncapsulatedClass2,
    ResourceSourceBucket::KeyBif,
};

/**
 * Bucket, class and owner a family takes once mounted, or nothing when the
 * family has no automatic mount route of its own. A packaged NWM module
 * reaches its final form through staging, so its route is the caller's to
 * choose; the policy assigns it no bucket rather than inventing one.
 */
std::optional<ModuleSourceMetadata> mountMetadata(ModuleArchiveFamily family);

/**
 * Choose the source used to enter or stage the module, or nothing when the
 * inventory offers no eligible candidate. No path is invented on failure.
 */
std::optional<ModulePrimarySelection> selectModulePrimary(
    const ModulePolicyRequest &request,
    const std::vector<ModuleSourceCandidate> &inventory);

/// Build the phased mount plan for the module.
ModuleLoadPlan planModuleLoad(const ModulePolicyRequest &request,
                              const std::vector<ModuleSourceCandidate> &inventory);

} // namespace resource
} // namespace reone
