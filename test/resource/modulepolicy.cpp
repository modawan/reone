/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <gtest/gtest.h>

#include "reone/resource/modulepolicy.h"

using namespace reone;
using namespace reone::resource;

namespace {

ModuleSourceCandidate source(std::string id,
                             ModuleArchiveFamily family,
                             ModulePrimaryOrigin origin = ModulePrimaryOrigin::Modules,
                             std::string root = "modules",
                             std::uint32_t packageOrder = 0,
                             std::uint32_t rootOrder = 0) {
    return {std::move(id), std::move(root), origin, family, packageOrder, rootOrder};
}

ModulePolicyRequest request(GameID game, bool includeInSave = true, bool savedMode = false) {
    ModulePolicyRequest req;
    req.game = game;
    req.moduleName = "module";
    req.includeInSave = includeInSave;
    req.savedMode = savedMode;
    return req;
}

const ModuleFamilyPlan *familyPlan(const ModuleLoadPlan &plan, ModuleArchiveFamily family) {
    for (const auto &entry : plan.families) {
        if (entry.family == family) return &entry;
    }
    return nullptr;
}

std::vector<std::string> attemptIds(const ModuleLoadPlan &plan, ModuleArchiveFamily family) {
    std::vector<std::string> ids;
    if (const auto *entry = familyPlan(plan, family)) {
        for (const auto &attempt : entry->attempts) ids.push_back(attempt.source.sourceId);
    }
    return ids;
}

std::vector<ModuleArchiveFamily> plannedFamilies(const ModuleLoadPlan &plan) {
    std::vector<ModuleArchiveFamily> families;
    for (const auto &entry : plan.families) families.push_back(entry.family);
    return families;
}

// 14.1 Lookup metadata

TEST(ModulePolicy, exposes_the_five_raw_buckets_in_verified_order) {
    const std::array expected {
        ResourceSourceBucket::LooseDirectory,
        ResourceSourceBucket::EncapsulatedClass1,
        ResourceSourceBucket::ResourceImage,
        ResourceSourceBucket::EncapsulatedClass2,
        ResourceSourceBucket::KeyBif,
    };
    EXPECT_EQ(expected, kRawResourceLookupOrder);
}

TEST(ModulePolicy, assigns_module_saved_and_dialogue_archives_to_encapsulated_class_two) {
    for (auto family : {ModuleArchiveFamily::PrimaryMod,
                        ModuleArchiveFamily::SavedArchive,
                        ModuleArchiveFamily::Dialogue}) {
        auto metadata = mountMetadata(family);
        ASSERT_TRUE(metadata);
        EXPECT_EQ(ResourceSourceBucket::EncapsulatedClass2, metadata->bucket);
        EXPECT_EQ(EncapsulatedClass::Class2, encapsulatedClassOf(metadata->bucket));
    }
}

TEST(ModulePolicy, derives_encapsulated_class_from_the_bucket) {
    EXPECT_EQ(EncapsulatedClass::Class1,
              encapsulatedClassOf(ResourceSourceBucket::EncapsulatedClass1));
    EXPECT_EQ(EncapsulatedClass::Class2,
              encapsulatedClassOf(ResourceSourceBucket::EncapsulatedClass2));
    for (auto bucket : {ResourceSourceBucket::LooseDirectory,
                        ResourceSourceBucket::ResourceImage,
                        ResourceSourceBucket::KeyBif}) {
        EXPECT_FALSE(encapsulatedClassOf(bucket));
    }
}

TEST(ModulePolicy, assigns_images_to_the_resource_image_bucket) {
    for (auto family : {ModuleArchiveFamily::PrimaryRim,
                        ModuleArchiveFamily::SavedResourceImage,
                        ModuleArchiveFamily::StaticRim,
                        ModuleArchiveFamily::AreaRim,
                        ModuleArchiveFamily::AdxRim}) {
        auto metadata = mountMetadata(family);
        ASSERT_TRUE(metadata);
        EXPECT_EQ(ResourceSourceBucket::ResourceImage, metadata->bucket);
        EXPECT_FALSE(encapsulatedClassOf(metadata->bucket));
    }
}

TEST(ModulePolicy, assigns_no_bucket_to_a_packaged_nwm_module) {
    // Its final form comes from staging, so the mount route is the caller's.
    EXPECT_FALSE(mountMetadata(ModuleArchiveFamily::Nwm));
}

TEST(ModulePolicy, ownership_does_not_determine_lookup_priority) {
    auto adjunct = mountMetadata(ModuleArchiveFamily::AdxRim);
    auto savedArchive = mountMetadata(ModuleArchiveFamily::SavedArchive);
    ASSERT_TRUE(adjunct);
    ASSERT_TRUE(savedArchive);
    EXPECT_EQ(ResourceOwner::ActiveModule, adjunct->owner);
    EXPECT_EQ(ResourceOwner::ActiveModuleState, savedArchive->owner);

    // Despite the saved archive owning later-cleared state, the adjunct image
    // sits in an earlier bucket and therefore wins an identical raw key.
    auto position = [](ResourceSourceBucket bucket) {
        return std::find(kRawResourceLookupOrder.begin(), kRawResourceLookupOrder.end(), bucket) -
               kRawResourceLookupOrder.begin();
    };
    EXPECT_LT(position(adjunct->bucket), position(savedArchive->bucket));
}

// 14.2 Primary selection

TEST(ModulePolicy, k1_selects_saved_image_then_archive_then_nwm_then_modules_then_live) {
    const std::vector<ModuleSourceCandidate> all {
        source("live-mod", ModuleArchiveFamily::PrimaryMod, ModulePrimaryOrigin::LivePackage, "live1"),
        source("modules-mod", ModuleArchiveFamily::PrimaryMod),
        source("nwm", ModuleArchiveFamily::Nwm, ModulePrimaryOrigin::NwmFiles, "nwm"),
        source("save-sav", ModuleArchiveFamily::SavedArchive, ModulePrimaryOrigin::GameInProgress, "save"),
        source("save-rsv", ModuleArchiveFamily::SavedResourceImage,
               ModulePrimaryOrigin::GameInProgress, "save"),
    };
    const std::vector<std::string> expected {"save-rsv", "save-sav", "nwm", "modules-mod", "live-mod"};
    auto remaining = all;
    for (const auto &next : expected) {
        auto selection = selectModulePrimary(request(GameID::KotOR), remaining);
        ASSERT_TRUE(selection) << next;
        EXPECT_EQ(next, selection->candidate.source.sourceId);
        remaining.erase(std::remove_if(remaining.begin(), remaining.end(),
                                       [&](const auto &c) { return c.sourceId == next; }),
                        remaining.end());
    }
    EXPECT_FALSE(selectModulePrimary(request(GameID::KotOR), remaining));
}

TEST(ModulePolicy, k2_selects_saved_image_then_archive_then_nwm_then_live_then_module_roots) {
    const std::vector<ModuleSourceCandidate> all {
        source("modules-mod", ModuleArchiveFamily::PrimaryMod),
        source("live-mod", ModuleArchiveFamily::PrimaryMod, ModulePrimaryOrigin::LivePackage, "live1"),
        source("nwm", ModuleArchiveFamily::Nwm, ModulePrimaryOrigin::NwmFiles, "nwm"),
        source("save-sav", ModuleArchiveFamily::SavedArchive, ModulePrimaryOrigin::GameInProgress, "save"),
        source("save-rsv", ModuleArchiveFamily::SavedResourceImage,
               ModulePrimaryOrigin::GameInProgress, "save"),
    };
    const std::vector<std::string> expected {"save-rsv", "save-sav", "nwm", "live-mod", "modules-mod"};
    auto remaining = all;
    for (const auto &next : expected) {
        auto selection = selectModulePrimary(request(GameID::TSL), remaining);
        ASSERT_TRUE(selection) << next;
        EXPECT_EQ(next, selection->candidate.source.sourceId);
        remaining.erase(std::remove_if(remaining.begin(), remaining.end(),
                                       [&](const auto &c) { return c.sourceId == next; }),
                        remaining.end());
    }
}

TEST(ModulePolicy, prefers_a_module_archive_to_a_base_image_within_one_root) {
    for (auto game : {GameID::KotOR, GameID::TSL}) {
        auto selection = selectModulePrimary(request(game), {
            source("module.rim", ModuleArchiveFamily::PrimaryRim),
            source("module.mod", ModuleArchiveFamily::PrimaryMod),
        });
        ASSERT_TRUE(selection);
        EXPECT_EQ("module.mod", selection->candidate.source.sourceId);
        EXPECT_EQ(ModulePrimaryKind::Mod, selection->candidate.kind);
    }
}

TEST(ModulePolicy, k2_tests_the_module_archive_across_every_root_before_the_image) {
    // The ordinary class exposes all roots at once, so a MOD anywhere beats a
    // RIM in a root that would otherwise be consulted first.
    auto selection = selectModulePrimary(request(GameID::TSL), {
        source("root-rim", ModuleArchiveFamily::PrimaryRim,
               ModulePrimaryOrigin::ConfiguredModuleRoot, "root", 0, 1),
        source("base-mod", ModuleArchiveFamily::PrimaryMod, ModulePrimaryOrigin::Modules),
    });
    ASSERT_TRUE(selection);
    EXPECT_EQ("base-mod", selection->candidate.source.sourceId);
}

TEST(ModulePolicy, live_packages_are_probed_one_package_at_a_time) {
    // Each package is probed alone and the loop stops on a hit, so the first
    // package wins with an image even when a later package has an archive.
    auto selection = selectModulePrimary(request(GameID::TSL), {
        source("live2-mod", ModuleArchiveFamily::PrimaryMod,
               ModulePrimaryOrigin::LivePackage, "live2", 2),
        source("live1-rim", ModuleArchiveFamily::PrimaryRim,
               ModulePrimaryOrigin::LivePackage, "live1", 1),
    });
    ASSERT_TRUE(selection);
    EXPECT_EQ("live1-rim", selection->candidate.source.sourceId);
}

TEST(ModulePolicy, k2_prefers_configured_roots_in_selection_order_then_the_base_location) {
    // Selection consumes configured roots in the reverse of their configured
    // order, and the base location is the fallback.
    auto selection = selectModulePrimary(request(GameID::TSL), {
        source("base-mod", ModuleArchiveFamily::PrimaryMod, ModulePrimaryOrigin::Modules),
        source("root0-mod", ModuleArchiveFamily::PrimaryMod,
               ModulePrimaryOrigin::ConfiguredModuleRoot, "root0", 0, 0),
        source("root1-mod", ModuleArchiveFamily::PrimaryMod,
               ModulePrimaryOrigin::ConfiguredModuleRoot, "root1", 0, 1),
    });
    ASSERT_TRUE(selection);
    EXPECT_EQ("root1-mod", selection->candidate.source.sourceId);

    auto baseOnly = selectModulePrimary(request(GameID::TSL), {
        source("base-mod", ModuleArchiveFamily::PrimaryMod, ModulePrimaryOrigin::Modules),
    });
    ASSERT_TRUE(baseOnly);
    EXPECT_EQ("base-mod", baseOnly->candidate.source.sourceId);
}

TEST(ModulePolicy, excludes_saved_candidates_when_include_in_save_disallows_them) {
    const std::vector<ModuleSourceCandidate> inventory {
        source("save-rsv", ModuleArchiveFamily::SavedResourceImage,
               ModulePrimaryOrigin::GameInProgress, "save"),
        source("save-sav", ModuleArchiveFamily::SavedArchive,
               ModulePrimaryOrigin::GameInProgress, "save"),
        source("modules-rim", ModuleArchiveFamily::PrimaryRim),
    };
    auto included = selectModulePrimary(request(GameID::TSL, true), inventory);
    ASSERT_TRUE(included);
    EXPECT_EQ("save-rsv", included->candidate.source.sourceId);
    EXPECT_TRUE(included->fromSavedState);

    auto excluded = selectModulePrimary(request(GameID::TSL, false), inventory);
    ASSERT_TRUE(excluded);
    EXPECT_EQ("modules-rim", excluded->candidate.source.sourceId);
    EXPECT_FALSE(excluded->fromSavedState);
}

TEST(ModulePolicy, selects_nothing_from_an_empty_or_sidecar_only_inventory) {
    EXPECT_FALSE(selectModulePrimary(request(GameID::TSL), {}));
    EXPECT_FALSE(selectModulePrimary(request(GameID::TSL), {
                                                               source("module_s.rim", ModuleArchiveFamily::StaticRim),
                                                               source("module_a.rim", ModuleArchiveFamily::AreaRim),
                                                               source("module_dlg.erf", ModuleArchiveFamily::Dialogue),
                                                           }));
}

TEST(ModulePolicy, records_provenance_without_giving_the_selection_a_bucket) {
    auto selection = selectModulePrimary(request(GameID::TSL), {
        source("nwm", ModuleArchiveFamily::Nwm, ModulePrimaryOrigin::NwmFiles, "nwm"),
    });
    ASSERT_TRUE(selection);
    EXPECT_TRUE(selection->isNwm);
    EXPECT_EQ(ModulePrimaryOrigin::NwmFiles, selection->candidate.source.origin);
    EXPECT_EQ("module", selection->canonicalModuleName);
}

TEST(ModulePolicy, k1_ignores_configured_module_roots_when_planning) {
    // Configured roots are a K2 concept. K1 must neither select nor plan one,
    // and a root archive must not take the branch that suppresses the static
    // image.
    const std::vector<ModuleSourceCandidate> inventory {
        source("root.mod", ModuleArchiveFamily::PrimaryMod,
               ModulePrimaryOrigin::ConfiguredModuleRoot, "root", 0, 0),
        source("base_s.rim", ModuleArchiveFamily::StaticRim, ModulePrimaryOrigin::Modules),
        source("root_s.rim", ModuleArchiveFamily::StaticRim,
               ModulePrimaryOrigin::ConfiguredModuleRoot, "root", 0, 0),
        source("base.rim", ModuleArchiveFamily::PrimaryRim, ModulePrimaryOrigin::Modules),
    };
    auto plan = planModuleLoad(request(GameID::KotOR), inventory);

    ASSERT_TRUE(plan.primary);
    EXPECT_EQ("base.rim", plan.primary->candidate.source.sourceId);
    EXPECT_FALSE(familyPlan(plan, ModuleArchiveFamily::PrimaryMod));
    EXPECT_EQ((std::vector<std::string> {"base_s.rim"}),
              attemptIds(plan, ModuleArchiveFamily::StaticRim));

    // K2 reaches both.
    auto k2 = planModuleLoad(request(GameID::TSL), inventory);
    EXPECT_EQ((std::vector<std::string> {"root.mod"}),
              attemptIds(k2, ModuleArchiveFamily::PrimaryMod));
}

TEST(ModulePolicy, a_zero_or_one_family_is_never_given_more_than_one_attempt) {
    auto plan = planModuleLoad(request(GameID::KotOR), {
        source("base.rim", ModuleArchiveFamily::PrimaryRim),
        source("base.mod", ModuleArchiveFamily::PrimaryMod, ModulePrimaryOrigin::Modules),
        source("other.mod", ModuleArchiveFamily::PrimaryMod, ModulePrimaryOrigin::Modules),
    });
    const auto *archives = familyPlan(plan, ModuleArchiveFamily::PrimaryMod);
    ASSERT_TRUE(archives);
    EXPECT_EQ(MountCardinality::ZeroOrOne, archives->cardinality);
    EXPECT_EQ(1u, archives->attempts.size());
}

// 14.3 Phased mount planning

TEST(ModulePolicy, plans_the_area_image_before_the_adx_image_in_both_games) {
    for (auto game : {GameID::KotOR, GameID::TSL}) {
        auto plan = planModuleLoad(request(game), {
            source("module.rim", ModuleArchiveFamily::PrimaryRim),
            source("module_adx.rim", ModuleArchiveFamily::AdxRim),
            source("module_a.rim", ModuleArchiveFamily::AreaRim),
        });
        const auto *area = familyPlan(plan, ModuleArchiveFamily::AreaRim);
        const auto *adx = familyPlan(plan, ModuleArchiveFamily::AdxRim);
        ASSERT_TRUE(area);
        ASSERT_TRUE(adx);
        EXPECT_EQ(MountCardinality::ZeroOrOne, area->cardinality);
        EXPECT_EQ(MountCardinality::ZeroOrOne, adx->cardinality);
        EXPECT_EQ(ModuleMountPhase::AdjunctImages, area->attempts[0].phase);
        EXPECT_LT(area->attempts[0].attemptOrder, adx->attempts[0].attemptOrder);
    }
}

TEST(ModulePolicy, a_module_archive_suppresses_the_static_image_in_both_games) {
    for (auto game : {GameID::KotOR, GameID::TSL}) {
        auto plan = planModuleLoad(request(game), {
            source("module.mod", ModuleArchiveFamily::PrimaryMod),
            source("module_s.rim", ModuleArchiveFamily::StaticRim),
            source("module_a.rim", ModuleArchiveFamily::AreaRim),
        });
        EXPECT_TRUE(familyPlan(plan, ModuleArchiveFamily::PrimaryMod));
        EXPECT_FALSE(familyPlan(plan, ModuleArchiveFamily::StaticRim));
        // The adjunct is unaffected by the module's packaging.
        EXPECT_TRUE(familyPlan(plan, ModuleArchiveFamily::AreaRim));
    }
}

TEST(ModulePolicy, k2_module_archive_suppresses_dialogue) {
    auto plan = planModuleLoad(request(GameID::TSL), {
        source("module.mod", ModuleArchiveFamily::PrimaryMod),
        source("module_dlg.erf", ModuleArchiveFamily::Dialogue),
    });
    EXPECT_FALSE(familyPlan(plan, ModuleArchiveFamily::Dialogue));
}

TEST(ModulePolicy, k1_never_plans_dialogue) {
    auto plan = planModuleLoad(request(GameID::KotOR), {
        source("module.rim", ModuleArchiveFamily::PrimaryRim),
        source("module_s.rim", ModuleArchiveFamily::StaticRim),
        source("module_dlg.erf", ModuleArchiveFamily::Dialogue),
    });
    EXPECT_TRUE(familyPlan(plan, ModuleArchiveFamily::StaticRim));
    EXPECT_FALSE(familyPlan(plan, ModuleArchiveFamily::Dialogue));
}

TEST(ModulePolicy, k1_plans_the_module_archive_as_a_single_base_attempt) {
    auto plan = planModuleLoad(request(GameID::KotOR), {
        source("base.mod", ModuleArchiveFamily::PrimaryMod, ModulePrimaryOrigin::Modules),
        source("root.mod", ModuleArchiveFamily::PrimaryMod,
               ModulePrimaryOrigin::ConfiguredModuleRoot, "root", 0, 0),
    });
    const auto *archives = familyPlan(plan, ModuleArchiveFamily::PrimaryMod);
    ASSERT_TRUE(archives);
    EXPECT_EQ(MountCardinality::ZeroOrOne, archives->cardinality);
    EXPECT_EQ((std::vector<std::string> {"base.mod"}),
              attemptIds(plan, ModuleArchiveFamily::PrimaryMod));
}

TEST(ModulePolicy, k2_plans_the_module_archive_across_base_and_every_configured_root) {
    auto plan = planModuleLoad(request(GameID::TSL), {
        source("root1.mod", ModuleArchiveFamily::PrimaryMod,
               ModulePrimaryOrigin::ConfiguredModuleRoot, "root1", 0, 1),
        source("base.mod", ModuleArchiveFamily::PrimaryMod, ModulePrimaryOrigin::Modules),
        source("root0.mod", ModuleArchiveFamily::PrimaryMod,
               ModulePrimaryOrigin::ConfiguredModuleRoot, "root0", 0, 0),
    });
    const auto *archives = familyPlan(plan, ModuleArchiveFamily::PrimaryMod);
    ASSERT_TRUE(archives);
    EXPECT_EQ(MountCardinality::AllSuccessful, archives->cardinality);
    EXPECT_EQ(MountFailureEffect::FailOrCurrentGameFallback, archives->attempts[0].failureEffect);
    // Base first, then configured roots in configured order regardless of the
    // order the inventory happened to list them in.
    EXPECT_EQ((std::vector<std::string> {"base.mod", "root0.mod", "root1.mod"}),
              attemptIds(plan, ModuleArchiveFamily::PrimaryMod));
}

TEST(ModulePolicy, k2_plans_the_static_image_first_successful_across_roots_then_base) {
    auto plan = planModuleLoad(request(GameID::TSL), {
        source("module.rim", ModuleArchiveFamily::PrimaryRim),
        source("base_s.rim", ModuleArchiveFamily::StaticRim, ModulePrimaryOrigin::Modules),
        source("root1_s.rim", ModuleArchiveFamily::StaticRim,
               ModulePrimaryOrigin::ConfiguredModuleRoot, "root1", 0, 1),
        source("root0_s.rim", ModuleArchiveFamily::StaticRim,
               ModulePrimaryOrigin::ConfiguredModuleRoot, "root0", 0, 0),
    });
    const auto *statics = familyPlan(plan, ModuleArchiveFamily::StaticRim);
    ASSERT_TRUE(statics);
    EXPECT_EQ(MountCardinality::FirstSuccessful, statics->cardinality);
    EXPECT_EQ((std::vector<std::string> {"root0_s.rim", "root1_s.rim", "base_s.rim"}),
              attemptIds(plan, ModuleArchiveFamily::StaticRim));
}

TEST(ModulePolicy, k2_plans_dialogue_across_base_and_every_root_as_best_effort) {
    auto plan = planModuleLoad(request(GameID::TSL), {
        source("module.rim", ModuleArchiveFamily::PrimaryRim),
        source("root0_dlg.erf", ModuleArchiveFamily::Dialogue,
               ModulePrimaryOrigin::ConfiguredModuleRoot, "root0", 0, 0),
        source("base_dlg.erf", ModuleArchiveFamily::Dialogue, ModulePrimaryOrigin::Modules),
    });
    const auto *dialogue = familyPlan(plan, ModuleArchiveFamily::Dialogue);
    ASSERT_TRUE(dialogue);
    EXPECT_EQ(MountCardinality::AllSuccessful, dialogue->cardinality);
    EXPECT_EQ(MountFailureEffect::BestEffort, dialogue->attempts[0].failureEffect);
    EXPECT_EQ((std::vector<std::string> {"base_dlg.erf", "root0_dlg.erf"}),
              attemptIds(plan, ModuleArchiveFamily::Dialogue));
}

TEST(ModulePolicy, plans_support_archives_before_the_adjunct_phase) {
    auto k1 = planModuleLoad(request(GameID::KotOR), {
        source("module.rim", ModuleArchiveFamily::PrimaryRim),
        source("players", ModuleArchiveFamily::PlayerSupport),
        source("lips_loc", ModuleArchiveFamily::Localization),
        source("module_a.rim", ModuleArchiveFamily::AreaRim),
    });
    EXPECT_EQ((std::vector<ModuleArchiveFamily> {ModuleArchiveFamily::PlayerSupport,
                                                 ModuleArchiveFamily::Localization,
                                                 ModuleArchiveFamily::AreaRim}),
              plannedFamilies(k1));

    // The player archive is a K1 support mount and has no K2 counterpart.
    auto k2 = planModuleLoad(request(GameID::TSL), {
        source("module.rim", ModuleArchiveFamily::PrimaryRim),
        source("players", ModuleArchiveFamily::PlayerSupport),
    });
    EXPECT_FALSE(familyPlan(k2, ModuleArchiveFamily::PlayerSupport));
}

TEST(ModulePolicy, k1_keeps_localization_to_the_base_locations) {
    const std::vector<ModuleSourceCandidate> inventory {
        source("module.rim", ModuleArchiveFamily::PrimaryRim),
        source("base_loc", ModuleArchiveFamily::Localization, ModulePrimaryOrigin::Modules),
        source("root_loc", ModuleArchiveFamily::Localization,
               ModulePrimaryOrigin::ConfiguredModuleRoot, "root0", 0, 0),
    };
    EXPECT_EQ((std::vector<std::string> {"base_loc"}),
              attemptIds(planModuleLoad(request(GameID::KotOR), inventory),
                         ModuleArchiveFamily::Localization));
    EXPECT_EQ((std::vector<std::string> {"base_loc", "root_loc"}),
              attemptIds(planModuleLoad(request(GameID::TSL), inventory),
                         ModuleArchiveFamily::Localization));
}

TEST(ModulePolicy, represents_the_active_state_separately_from_primary_selection) {
    auto plan = planModuleLoad(request(GameID::TSL, true, true), {
        source("module.rim", ModuleArchiveFamily::PrimaryRim),
        source("module_s.rim", ModuleArchiveFamily::StaticRim),
    });
    ASSERT_TRUE(plan.primary);
    EXPECT_EQ("module.rim", plan.primary->candidate.source.sourceId);

    // The selected base image is not itself an entry in the mount sequence.
    for (const auto &family : plan.families) {
        for (const auto &attempt : family.attempts) {
            EXPECT_NE("module.rim", attempt.source.sourceId);
        }
    }

    EXPECT_EQ("module", plan.activeState.canonicalModuleName);
    EXPECT_TRUE(plan.activeState.savedModeRequested);
    EXPECT_EQ(ModuleMountPhase::ActiveCurrentGame, plan.activeState.phase);
    EXPECT_EQ(ResourceOwner::ActiveModuleState, plan.activeState.owner);
}

TEST(ModulePolicy, active_state_offers_both_an_archive_and_an_image_form) {
    // The archive form also serves as the recovery route when a required
    // branch mount fails, so both forms are described rather than resolved.
    auto plan = planModuleLoad(request(GameID::TSL), {
        source("module.rim", ModuleArchiveFamily::PrimaryRim),
    });
    EXPECT_FALSE(plan.activeState.savedModeRequested);
    EXPECT_EQ(ResourceSourceBucket::EncapsulatedClass2, plan.activeState.encapsulatedArchive.bucket);
    EXPECT_EQ(EncapsulatedClass::Class2,
              encapsulatedClassOf(plan.activeState.encapsulatedArchive.bucket));
    EXPECT_EQ(ResourceSourceBucket::ResourceImage, plan.activeState.resourceImage.bucket);
    EXPECT_EQ(ResourceOwner::ActiveModuleState, plan.activeState.encapsulatedArchive.owner);
}

TEST(ModulePolicy, attempt_order_is_deterministic_and_independent_of_inventory_order) {
    const std::vector<ModuleSourceCandidate> forward {
        source("module.rim", ModuleArchiveFamily::PrimaryRim),
        source("root0_s.rim", ModuleArchiveFamily::StaticRim,
               ModulePrimaryOrigin::ConfiguredModuleRoot, "root0", 0, 0),
        source("root1_s.rim", ModuleArchiveFamily::StaticRim,
               ModulePrimaryOrigin::ConfiguredModuleRoot, "root1", 0, 1),
    };
    std::vector<ModuleSourceCandidate> reversed(forward.rbegin(), forward.rend());

    auto forwardIds = attemptIds(planModuleLoad(request(GameID::TSL), forward),
                                 ModuleArchiveFamily::StaticRim);
    auto reversedIds = attemptIds(planModuleLoad(request(GameID::TSL), reversed),
                                  ModuleArchiveFamily::StaticRim);
    EXPECT_EQ((std::vector<std::string> {"root0_s.rim", "root1_s.rim"}), forwardIds);
    EXPECT_EQ(forwardIds, reversedIds);

    // Attempt order is strictly increasing across the whole plan.
    auto plan = planModuleLoad(request(GameID::TSL), forward);
    std::uint32_t previous = 0;
    bool first = true;
    for (const auto &family : plan.families) {
        for (const auto &attempt : family.attempts) {
            if (!first) EXPECT_LT(previous, attempt.attemptOrder);
            previous = attempt.attemptOrder;
            first = false;
        }
    }
}

TEST(ModulePolicy, never_synthesizes_a_family_the_inventory_did_not_offer) {
    // Families are matched exactly, so a plan only ever contains the families
    // the inventory actually offered.
    auto plan = planModuleLoad(request(GameID::TSL), {
        source("module.rim", ModuleArchiveFamily::PrimaryRim),
        source("module_adx.rim", ModuleArchiveFamily::AdxRim),
    });
    EXPECT_EQ((std::vector<ModuleArchiveFamily> {ModuleArchiveFamily::AdxRim}),
              plannedFamilies(plan));
}

// 14.4 Evidence restraint

TEST(ModulePolicy, split_layout_asserts_invariants_rather_than_a_total_image_order) {
    auto plan = planModuleLoad(request(GameID::TSL), {
        source("module.rim", ModuleArchiveFamily::PrimaryRim),
        source("module_a.rim", ModuleArchiveFamily::AreaRim),
        source("module_adx.rim", ModuleArchiveFamily::AdxRim),
        source("module_s.rim", ModuleArchiveFamily::StaticRim),
        source("module_dlg.erf", ModuleArchiveFamily::Dialogue),
    });
    const auto *area = familyPlan(plan, ModuleArchiveFamily::AreaRim);
    const auto *adx = familyPlan(plan, ModuleArchiveFamily::AdxRim);
    const auto *statics = familyPlan(plan, ModuleArchiveFamily::StaticRim);
    const auto *dialogue = familyPlan(plan, ModuleArchiveFamily::Dialogue);
    ASSERT_TRUE(area && adx && statics && dialogue);

    // Established: the adx image is attempted after the area image, and every
    // image family sits in a bucket above the class-2 dialogue archive. The
    // total image order also depends on the active current-game mount, so it
    // is deliberately not asserted here.
    EXPECT_LT(area->attempts[0].attemptOrder, adx->attempts[0].attemptOrder);
    for (const auto *images : {area, adx, statics}) {
        EXPECT_EQ(ResourceSourceBucket::ResourceImage, images->attempts[0].metadata.bucket);
    }
    EXPECT_EQ(ResourceSourceBucket::EncapsulatedClass2, dialogue->attempts[0].metadata.bucket);
}

} // namespace
