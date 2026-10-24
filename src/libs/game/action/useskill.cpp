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

#include "reone/game/action/useskill.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iterator>

#include "reone/game/animations.h"
#include "reone/game/di/services.h"
#include "reone/game/effect/heal.h"
#include "reone/game/effect/visual.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/object/trap.h"
#include "reone/game/object/trigger.h"
#include "reone/game/party.h"
#include "reone/game/script/runner.h"
#include "reone/resource/di/services.h"
#include "reone/resource/strings.h"
#include "reone/system/logutil.h"
#include "reone/system/randomutil.h"

#include "commonactions.h"

namespace reone {

namespace game {

// Demolitions

static constexpr float kMineWorkTime = 4.5f;
static constexpr float kSetMineWorkTime = 2.0f;
static constexpr float kMineUseRangeExtra = 0.25f;
static constexpr float kSetMineUseRangeExtra = 0.5f;
static constexpr int kDisarmActionStrRef = 1529;
static constexpr int kSetMineActionStrRef = 1530;
static constexpr int kRecoverActionStrRef = 1531;
static constexpr int kExamineActionStrRef = 1532;
static constexpr int kSkillSuccessStrRef = 1392;
static constexpr int kSkillFailureStrRef = 1393;
static constexpr int kSkillReportStrRef = 1408;
static constexpr int kDemolitionsNameStrRef = 324;
// A disarm against a DC above this always fails.
static constexpr int kHighestDisarmableDC = 35;
static constexpr int kHighestDisarmableDCTSL = 65;
// TSL's skill experience: the award per level of the player, and the higher
// one for a DC at least this far above that level.
static constexpr int kSkillXPPerLevel = 10;
static constexpr int kHardSkillXPPerLevel = 15;
static constexpr int kHardSkillDCMargin = 20;
static constexpr int kTreatInjuryNameStrRef = 330;
// Client animation rows for the mine work.
static constexpr int kGetFromGroundAnimation = 40;
static constexpr int kGetFromTableAnimation = 41;
static constexpr int kUnlockDoorAnimation = 47;
static constexpr int kUnlockContainerAnimation = 48;
static constexpr int kDisableMineAnimation = 51;
static constexpr int kSetMineAnimation = 52;
// Set-mine blast bonus by trap type, starting at type 4.
static constexpr int kMineBlastBonus[] = {3, 9, 0, 3, 9, 0, 0, 0, 0, 0, 0, 3, 9, 0, 0, 9, 12, 9, 12, 0, 0, 9, 12};

enum class MineWork {
    Disarm,
    Recover,
    Flag,
    Examine
};

enum SkillResult {
    SkillFailure = 0,
    SkillSuccess = 1,
    SkillCriticalFailure = 2,
    SkillTake20Failure = 3,
    SkillOwnMine = 4
};

static MineWork mineWork(int subSkill) {
    switch (subSkill) {
    case static_cast<int>(SubSkill::RecoverTrap):
        return MineWork::Recover;
    case static_cast<int>(SubSkill::FlagTrap):
        return MineWork::Flag;
    case static_cast<int>(SubSkill::ExamineTrap):
        return MineWork::Examine;
    default:
        return MineWork::Disarm;
    }
}

static std::optional<std::reference_wrapper<const Item::PropertyEntry>> firstActiveProperty(const Item &item) {
    for (const auto &property : item.properties()) {
        if (item.isPropertyActive(property)) {
            return std::cref(property);
        }
    }
    return std::nullopt;
}

static bool isTrapKit(const Item &item) {
    auto property = firstActiveProperty(item);
    return property && property->get().type() == ItemProperty::Trap;
}

static bool canUseDemolitions(const Creature &creature) {
    // Demolitions cannot be used untrained.
    return creature.attributes().hasSkill(SkillType::Demolitions);
}

// Mines set by the actor, or by a party member or PC while the actor is one, are its own.
static bool isOwnMine(Game &game, const Creature &actor, const Trigger &trap, bool creatorOnly) {
    auto creator = trap.trapCreator();
    if (!creator) {
        return false;
    }
    if (creator.get() == &actor) {
        return true;
    }
    if (creatorOnly) {
        return false;
    }
    auto &party = game.party();
    return (party.isMember(*creator) || creator->isPC()) && (party.isMember(actor) || actor.isPC());
}

bool UseSkillAction::setsMine() const {
    return _itemUsed && isTrapKit(*_itemUsed);
}

uint32_t UseSkillAction::serializedActionId() const {
    auto type = SavedActionType::Invalid;
    switch (_skill) {
    case SkillType::Security:
        type = SavedActionType::OpenLock;
        break;
    case SkillType::TreatInjury:
        type = SavedActionType::Heal;
        break;
    case SkillType::Demolitions:
        if (setsMine()) {
            type = SavedActionType::SetMine;
            break;
        }
        switch (mineWork(_subSkill)) {
        case MineWork::Recover:
            type = SavedActionType::RecoverMine;
            break;
        case MineWork::Flag:
            type = SavedActionType::FlagMine;
            break;
        case MineWork::Examine:
            type = SavedActionType::ExamineMine;
            break;
        case MineWork::Disarm:
            type = SavedActionType::DisarmMine;
            break;
        }
        break;
    default:
        return Action::serializedActionId();
    }
    return static_cast<uint32_t>(type);
}

void UseSkillAction::executeDemolitions(Creature &actor, float dt) {
    auto area = _game.module()->area();
    const bool setMine = setsMine();
    if ((!setMine && !_target) || !canUseDemolitions(actor)) {
        complete();
        return;
    }
    if (setMine) {
        // The kit must still be carried, there must be somewhere to set it, and the area limit holds for everyone.
        if (!_itemUsed->isHeld() || !_target || !area->playerCanSetMines()) {
            complete();
            return;
        }
    } else {
        const MineWork work = mineWork(_subSkill);
        if (work == MineWork::Disarm || work == MineWork::Recover) {
            bool refused = false;
            if (auto *trigger = dyn_cast<Trigger>(_target.get())) {
                refused = trigger->isTrap() && !trigger->trapDisarmable();
            }
            if (auto *door = dyn_cast<Door>(_target.get())) {
                refused = door->isTrapped() && !door->trapDisarmable();
            }
            if (auto *placeable = dyn_cast<Placeable>(_target.get())) {
                refused = placeable->isTrapped() && !placeable->trapDisarmable();
            }
            if (refused) {
                complete();
                return;
            }
        }
    }

    const bool onSelf = !_target || _target.get() == &actor;
    if (_phase == 0) {
        // Approach to use range.
        if (!onSelf) {
            float extra = 0.0f;
            if (setMine) {
                extra = kSetMineUseRangeExtra;
            } else if (mineWork(_subSkill) == MineWork::Disarm || mineWork(_subSkill) == MineWork::Recover) {
                extra = kMineUseRangeExtra;
            }
            if (!actor.navigateToUse(*_target, extra, dt, true, false, false)) {
                return;
            }
            actor.turnToward(*_target);
        }
        // Work at it for a while.
        int animation = kSetMineAnimation;
        if (!setMine) {
            const MineWork work = mineWork(_subSkill);
            const bool trigger = _target->type() == ObjectType::Trigger;
            const bool door = _target->type() == ObjectType::Door;
            switch (work) {
            case MineWork::Disarm:
                animation = trigger ? kDisableMineAnimation : door ? kUnlockDoorAnimation
                                                                   : kUnlockContainerAnimation;
                break;
            case MineWork::Recover:
                animation = trigger ? kDisableMineAnimation : kGetFromTableAnimation;
                break;
            default:
                animation = trigger ? kGetFromGroundAnimation : kGetFromTableAnimation;
                break;
            }
        }
        actor.playAnimation(
            _services.game.animations.getNameById(animation),
            scene::AnimationProperties::fromFlags(scene::AnimationFlags::loop));
        _workTime = setMine ? kSetMineWorkTime : kMineWorkTime;
        _phase = 1;
        return;
    }
    _workTime -= dt;
    if (_workTime > 0.0f) {
        return;
    }
    actor.resumeStateDrivenAnimation();
    if (setMine) {
        resolveSetMine(actor);
    } else {
        resolveMine(actor);
    }
    complete();
}

void UseSkillAction::reportSkill(Creature &actor, int actionStrRef, int roll, int rank, int dc, int result) const {
    // Told only to the controlling player.
    if (_game.party().getLeader().get() != &actor) {
        return;
    }
    auto &strings = _services.resource.strings;
    const bool success = result == SkillSuccess || result == SkillOwnMine;
    _game.setCustomToken(0, actor.name());
    _game.setCustomToken(1, _game.getFeedbackText(success ? kSkillSuccessStrRef : kSkillFailureStrRef));
    _game.setCustomToken(2, actionStrRef ? strings.getText(actionStrRef) : "");
    _game.setCustomToken(3, std::to_string(roll + rank));
    _game.setCustomToken(4, std::to_string(roll));
    _game.setCustomToken(5, rank < 0 ? "-" : "+");
    _game.setCustomToken(6, strings.getText(_skill == SkillType::TreatInjury ? kTreatInjuryNameStrRef : kDemolitionsNameStrRef));
    _game.setCustomToken(7, std::to_string(std::abs(rank)));
    _game.setCustomToken(8, std::to_string(dc));
    _game.messageLog().add(
        MessageLog::kFeedbackMessageType,
        MessageLog::Style::Normal,
        _game.getFeedbackText(kSkillReportStrRef));
}

void UseSkillAction::resolveMine(Creature &actor) {
    const MineWork work = mineWork(_subSkill);
    const int rank = actor.getUnopposedSkillRank(SkillType::Demolitions);
    const bool take20 = !actor.isInCombat();
    const int roll = take20 ? 20 : randomInt(1, 20);
    const int total = roll + rank;
    auto area = _game.module()->area();
    auto actorPtr = std::dynamic_pointer_cast<Creature>(_game.getObjectById(actor.id()));
    auto *trigger = dyn_cast<Trigger>(_target.get());
    auto *door = dyn_cast<Door>(_target.get());
    auto *placeable = dyn_cast<Placeable>(_target.get());
    if (!trigger && !door && !placeable) {
        return;
    }

    const int disarmDC = trigger ? trigger->trapDisarmDC() : door ? door->trapDisarmDC()
                                                                  : placeable->trapDisarmDC();
    const int trapType = trigger ? trigger->trapBaseType() : door ? door->trapBaseType()
                                                                  : placeable->trapBaseType();
    TrapDetection &detection = trigger ? trigger->trapDetection() : door ? door->trapDetection()
                                                                         : placeable->trapDetection();
    const int autoFailDC = _game.isTSL() ? kHighestDisarmableDCTSL : kHighestDisarmableDC;
    auto awardXP = [&](int dc) {
        if (!_game.isTSL() || !_game.party().isMember(actor)) {
            return;
        }
        const int level = _game.party().player()->attributes().getAggregateLevel();
        _game.party().awardXP(level * (dc >= level + kHardSkillDCMargin ? kHardSkillXPPerLevel : kSkillXPPerLevel), XPSource::Skill);
    };
    auto recoverItem = [&]() {
        // A trap type with no kit recovers nothing.
        const std::string &resRef = _game.trapType(trapType).kitResRef;
        if (resRef.empty()) {
            return;
        }
        if (auto receiver = _game.party().sharedInventoryReceiver(actorPtr)) {
            receiver->addItem(resRef);
        }
    };

    switch (work) {
    case MineWork::Disarm: {
        if (trigger && isOwnMine(_game, actor, *trigger, false)) {
            trigger->disarmTrap(actor);
            area->destroyObject(*trigger);
            // The award compares against an unset DC; it takes the lower rate.
            awardXP(0);
            reportSkill(actor, kDisarmActionStrRef, roll, rank, disarmDC, SkillOwnMine);
            return;
        }
        // A disarm DC is at least 1.
        const int dc = std::max(1, disarmDC);
        const bool success = dc <= autoFailDC && total >= dc;
        int result = success ? SkillSuccess : take20        ? SkillTake20Failure
                                          : total < dc - 10 ? SkillCriticalFailure
                                                            : SkillFailure;
        if (success) {
            if (trigger) {
                trigger->disarmTrap(actor);
                area->destroyObject(*trigger);
            } else if (door) {
                door->disarmTrap(actor);
            } else {
                placeable->disarmTrap(actor);
            }
            awardXP(dc);
        }
        if (!success) {
            actor.playSound(resource::SoundSetEntry::UnlockSuccess);
        }
        if (!success && total < dc - 10) {
            if (trigger) {
                trigger->enterMine(actorPtr, false);
            } else if (door) {
                door->triggerTrap(actorPtr, false);
            } else {
                placeable->triggerTrap(actorPtr, false);
            }
        }
        reportSkill(actor, kDisarmActionStrRef, roll, rank, dc, result);
        return;
    }
    case MineWork::Recover: {
        if (trigger && isOwnMine(_game, actor, *trigger, false)) {
            area->destroyObject(*trigger);
            recoverItem();
            reportSkill(actor, kRecoverActionStrRef, roll, rank, disarmDC + 10, SkillOwnMine);
            return;
        }
        const int dc = std::max(1, disarmDC + 10);
        const bool success = total >= dc;
        int result = success ? SkillSuccess : take20        ? SkillTake20Failure
                                          : total < dc - 10 ? SkillCriticalFailure
                                                            : SkillFailure;
        if (success) {
            if (trigger) {
                area->destroyObject(*trigger);
            } else if (door) {
                door->disarmTrap(actor);
            } else {
                placeable->disarmTrap(actor);
            }
            recoverItem();
            awardXP(dc);
        } else if (total < dc - 5) {
            if (trigger) {
                trigger->enterMine(actorPtr, false);
            } else if (door) {
                door->triggerTrap(actorPtr, false);
            } else {
                placeable->triggerTrap(actorPtr, false);
            }
        }
        reportSkill(actor, kRecoverActionStrRef, roll, rank, dc, result);
        return;
    }
    case MineWork::Flag: {
        if (trigger && isOwnMine(_game, actor, *trigger, true)) {
            detection.flagged = true;
            reportSkill(actor, 0, roll, rank, disarmDC, SkillOwnMine);
            return;
        }
        const int dc = std::max(1, disarmDC - 5);
        const bool success = total >= dc;
        if (success) {
            detection.flagged = true;
        }
        reportSkill(actor, 0, roll, rank, dc,
                    success ? SkillSuccess : take20        ? SkillTake20Failure
                                         : total < dc - 10 ? SkillCriticalFailure
                                                           : SkillFailure);
        return;
    }
    case MineWork::Examine: {
        const int dc = std::max(1, disarmDC - 7);
        const bool own = trigger && isOwnMine(_game, actor, *trigger, true);
        const bool success = own || total >= dc;
        reportSkill(actor, kExamineActionStrRef, roll, rank, dc,
                    own ? SkillOwnMine : success       ? SkillSuccess
                                     : take20          ? SkillTake20Failure
                                     : total < dc - 10 ? SkillCriticalFailure
                                                       : SkillFailure);
        return;
    }
    }
}

void UseSkillAction::resolveSetMine(Creature &actor) {
    auto area = _game.module()->area();
    auto actorPtr = std::dynamic_pointer_cast<Creature>(_game.getObjectById(actor.id()));
    auto property = firstActiveProperty(*_itemUsed);
    const int trapType = property->get().subtype;
    const int blastBonus = trapType >= 4 && trapType - 4 < static_cast<int>(std::size(kMineBlastBonus))
                               ? kMineBlastBonus[trapType - 4]
                               : 0;
    // A blank cell counts as 0. The DC, shifted by the difficulty in TSL, is
    // at least 1.
    const auto &row = _game.trapType(trapType);
    int setDC = row.setDC.value_or(0);
    if (_game.isTSL()) {
        setDC += _game.trapDifficultyModifier();
    }
    setDC = std::max(1, setDC);

    int rank = actor.getUnopposedSkillRank(SkillType::Demolitions);
    // KotOR rewards five trained ranks with two more.
    if (!_game.isTSL() && actor.getUnopposedSkillRank(SkillType::Demolitions, true) >= 5) {
        rank += 2;
    }
    const bool onObject = _target && (_target->type() == ObjectType::Door || _target->type() == ObjectType::Placeable);
    const bool hasTarget = static_cast<bool>(_target);
    const bool take20 = _game.isTSL() ? !(actor.isInCombat() && !hasTarget) : !actor.isInCombat();
    const int roll = take20 ? 20 : randomInt(1, 20);
    const int total = roll + rank;
    int result;
    if (total >= setDC) {
        result = SkillSuccess;
    } else if (take20) {
        result = SkillTake20Failure;
    } else if (total >= setDC - 10) {
        result = SkillFailure;
    } else {
        result = SkillCriticalFailure;
    }
    const bool planted = result == SkillSuccess || result == SkillCriticalFailure;
    const int detectDC = row.detectDCMod.value_or(0) + total;
    const int disarmDC = row.disarmDCMod.value_or(0) + total;

    if (planted) {
        std::shared_ptr<Trigger> groundMine;
        if (!onObject) {
            groundMine = area->spawnMine(trapType, actor.position(), actorPtr, actor.faction(), detectDC, disarmDC, rank);
        } else {
            auto *door = dyn_cast<Door>(_target.get());
            auto *placeable = dyn_cast<Placeable>(_target.get());
            const bool trapped = door ? door->isTrapped() : placeable->isTrapped();
            // TSL consumes the kit and reports success on an already trapped object.
            if (!(trapped && _game.isTSL())) {
                std::shared_ptr<Trigger> linked;
                if (_game.isTSL()) {
                    linked = area->spawnMine(trapType, _target->position(), actorPtr, actor.faction(), detectDC, disarmDC, rank);
                    linked->hideLinkedMine();
                }
                const bool blast = _game.isTSL() && result != SkillCriticalFailure;
                if (door) {
                    door->armMine(trapType, detectDC, disarmDC, rank, linked, actorPtr, blastBonus, blast);
                } else {
                    placeable->armMine(trapType, detectDC, disarmDC, rank, linked, actorPtr, blastBonus, blast);
                }
            }
        }
        // One kit is spent, and TSL tells the module the setter lost it.
        auto kit = _itemUsed;
        if (_game.isTSL()) {
            _game.queueScriptEvent(*_game.module(), actor, *_game.newEvent(static_cast<int>(SavedSignalEventType::ItemLost), std::vector<int32_t> {}, std::vector<float> {}, std::vector<std::string> {}, std::vector<std::shared_ptr<Object>> {kit}));
        }
        if (auto holder = _game.getObjectById(kit->owner())) {
            bool last = false;
            holder->removeItem(kit, last);
            if (last) {
                _game.destroyRuntimeObjectGraph(kit);
            }
        }
        // A critical failure sets the mine off on its setter.
        if (result == SkillCriticalFailure) {
            if (groundMine) {
                groundMine->enterMine(actorPtr, true);
            } else if (auto *door = dyn_cast<Door>(_target.get())) {
                door->triggerTrap(actorPtr, true);
            } else if (auto *placeable = dyn_cast<Placeable>(_target.get())) {
                placeable->triggerTrap(actorPtr, true);
            }
        }
    }
    actor.playSound(result == SkillSuccess ? resource::SoundSetEntry::DisarmMine : resource::SoundSetEntry::UnlockSuccess);
    actor.interruptActivities(true);
    actor.removeCombatInvisibilityEffects();
    reportSkill(actor, kSetMineActionStrRef, roll, rank, setDC, result);
}

// END Demolitions

// Treat Injury

static constexpr float kTreatInjuryWorkTime = 2.0f;
// Client animation row of the work: the looping cast.
static constexpr int kCastLoopAnimation = 63;
static constexpr int kHealingVisual = 1001;

enum TreatInjuryPhase {
    TreatInjuryStart = 0,
    TreatInjuryApproach = 1,
    TreatInjuryReady = 2,
    TreatInjuryWork = 3
};

// The healer runs to the patient's use point and faces it, unless already
// within use range, then works on it for two seconds (only character models
// show the work) and heals it. A downed healer fails; without a creature to
// treat or an item to treat it with, the work is forgotten.
void UseSkillAction::executeTreatInjury(Creature &actor, float dt) {
    if (actor.isTemporarilyDead()) {
        complete();
        return;
    }
    auto *target = dyn_cast<Creature>(_target.get());
    if (!target || !_itemUsed) {
        actor.setTreatInjuryWorkDone(false);
        complete();
        return;
    }
    if (_phase == TreatInjuryStart) {
        _phase = actor.isInUseRange(*target, 0.0f) ? TreatInjuryReady : TreatInjuryApproach;
    }
    if (_phase == TreatInjuryApproach) {
        if (!actor.navigateToUse(*target, 0.0f, dt)) {
            return;
        }
        actor.turnToward(*target);
        _phase = TreatInjuryReady;
    }
    if (_phase == TreatInjuryReady) {
        if (!actor.treatInjuryWorkDone()) {
            actor.setTreatInjuryWorkDone(true);
            if (actor.modelType() != Creature::ModelType::Creature) {
                actor.playAnimation(
                    _services.game.animations.getNameById(kCastLoopAnimation),
                    scene::AnimationProperties::fromFlags(scene::AnimationFlags::loop));
            }
            _workTime = kTreatInjuryWorkTime;
            _phase = TreatInjuryWork;
            return;
        }
        resolveHeal(actor, *target);
        return;
    }
    _workTime -= dt;
    if (_workTime > 0.0f) {
        return;
    }
    actor.resumeStateDrivenAnimation();
    resolveHeal(actor, *target);
}

// Treat Injury, taking 20 out of combat, against the patient's first poison:
// treating a poison always meets difficulty 1, and a treated poison is
// removed. A patient with no poison and no injury is not treated and keeps the
// healer's work for its next heal. Otherwise the healer reports the roll, uses
// up one of the item when it holds it (a party member in the party
// inventory), and heals the roll plus its rank.
void UseSkillAction::resolveHeal(Creature &actor, Creature &target) {
    const int rank = actor.getSkillRankVersus(SkillType::TreatInjury, target);
    const bool take20 = !actor.isInCombat();
    const int roll = take20 ? 20 : randomInt(1, 20);
    const int total = roll + rank;
    const auto &effects = target.effects();
    auto poison = std::find_if(effects.begin(), effects.end(), [](const EffectInstance &effect) {
        return effect.type() == EffectType::Poison;
    });
    int dc = 0;
    if (poison != effects.end()) {
        dc = 1;
        if (total >= dc) {
            target.removeEffectsById(poison->id);
        }
    } else if (target.currentHitPoints() >= target.maxHitPoints()) {
        complete();
        return;
    }
    reportSkill(actor, kTreatInjuryNameStrRef, roll, rank, dc,
                total >= dc ? SkillSuccess : take20 ? SkillTake20Failure
                                                    : SkillFailure);

    auto repository = _game.getObjectById(actor.itemRepositoryOwner().id());
    auto item = _itemUsed;
    bool last = false;
    if (repository->removeItem(item, last) && last) {
        _game.destroyRuntimeObjectGraph(item);
    }

    auto actorPtr = _game.getObjectById(actor.id());
    auto heal = _game.newEffect<HealEffect>(total);
    heal->setSaveFacingCreator(actorPtr);
    target.applyEffect(heal, DurationType::Instant);
    auto visual = _game.newEffect<VisualEffect>(kHealingVisual, false, _services);
    visual->setSaveFacingCreator(actorPtr);
    target.applyEffect(visual, DurationType::Instant);
    actor.setTreatInjuryWorkDone(false);
    complete();
}

void UseSkillAction::skipApproach() {
    assert(_skill == SkillType::TreatInjury);
    _phase = TreatInjuryReady;
}

// END Treat Injury

static SavedActionParameter savedObject(const std::shared_ptr<Object> &object) {
    return {kSavedObjectParameter, SavedObjectReference::fromRuntimeId(object ? object->id() : script::kObjectInvalid)};
}

// A mine is known by the target alone, the kind of work being the action
// itself; setting one keeps the kit, the target and where the mine goes; an
// unlock keeps the target and the item used on the lock; a heal keeps the
// patient, the item, an unused integer, and whether the healer may still walk
// up to the patient.
std::optional<SavedActionRecord> UseSkillAction::saveFacingState() const {
    std::vector<SavedActionParameter> parameters;
    switch (_skill) {
    case SkillType::Demolitions:
        if (setsMine()) {
            const glm::vec3 point = _target ? _target->position() : glm::vec3(0.0f);
            parameters = {savedObject(_itemUsed), savedObject(_target), {kSavedFloatParameter, point.x}, {kSavedFloatParameter, point.y}, {kSavedFloatParameter, point.z}};
        } else {
            parameters = {savedObject(_target)};
        }
        break;
    case SkillType::Security:
        parameters = {savedObject(_target), savedObject(_itemUsed), {kSavedIntegerParameter, int32_t {0}}};
        break;
    case SkillType::TreatInjury: {
        const bool mayApproach = _phase != TreatInjuryReady && _phase != TreatInjuryWork;
        parameters = {savedObject(_target), savedObject(_itemUsed), {kSavedIntegerParameter, int32_t {0}}, {kSavedIntegerParameter, int32_t {mayApproach ? 1 : 0}}};
        break;
    }
    default:
        return std::nullopt;
    }
    return SavedActionRecord::command(static_cast<SavedActionType>(serializedActionId()), std::move(parameters));
}

std::shared_ptr<Action> UseSkillAction::restore(const SavedActionRecord &record, Game &game) {
    const auto object = [&record](size_t index) { return record.objectParameter(index).boundObject(); };
    switch (record.type()) {
    case SavedActionType::DisarmMine:
        return game.newAction<UseSkillAction>(SkillType::Demolitions, object(0));
    case SavedActionType::RecoverMine:
        return game.newAction<UseSkillAction>(SkillType::Demolitions, object(0), static_cast<int>(SubSkill::RecoverTrap));
    case SavedActionType::FlagMine:
        return game.newAction<UseSkillAction>(SkillType::Demolitions, object(0), static_cast<int>(SubSkill::FlagTrap));
    case SavedActionType::ExamineMine:
        return game.newAction<UseSkillAction>(SkillType::Demolitions, object(0), static_cast<int>(SubSkill::ExamineTrap));
    case SavedActionType::SetMine: {
        // Without its kit the action is no longer a mine set.
        auto kit = std::dynamic_pointer_cast<Item>(object(0));
        if (!kit) {
            return nullptr;
        }
        return game.newAction<UseSkillAction>(SkillType::Demolitions, object(1), 0, std::move(kit));
    }
    case SavedActionType::OpenLock:
        return game.newAction<UseSkillAction>(SkillType::Security, object(0), 0,
                                              std::dynamic_pointer_cast<Item>(object(1)));
    default: {
        assert(record.type() == SavedActionType::Heal);
        auto action = game.newAction<UseSkillAction>(SkillType::TreatInjury, object(0), 0,
                                                     std::dynamic_pointer_cast<Item>(object(1)));
        if (record.integerParameter(3) == 0) {
            action->skipApproach();
        }
        return action;
    }
    }
}

// Clearing a mine's work or the work at a lock gives the pose back to the
// creature's state; a one-shot still running plays on.
bool UseSkillAction::cancel(std::shared_ptr<Action> self, Object &actor) {
    if (_skill == SkillType::Security) {
        resumePoseAfterLockWork(actor, _workingAtLock);
    }
    if (_skill == SkillType::Demolitions && _phase == 1) {
        if (auto *creature = dyn_cast<Creature>(&actor)) {
            creature->refreshPauseAnimation();
        }
        _phase = 0;
    }
    // Clearing a heal returns the healer to its ordinary animation and forgets its work.
    if (_skill == SkillType::TreatInjury) {
        if (auto *creature = dyn_cast<Creature>(&actor)) {
            if (_phase == TreatInjuryWork) {
                creature->resumeStateDrivenAnimation();
            }
            creature->setTreatInjuryWorkDone(false);
        }
        _phase = TreatInjuryStart;
    }
    return true;
}

void UseSkillAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    switch (_skill) {
    case SkillType::Security: {
        if (!_target) {
            warn("ActionExecutor: unsupported Security target: null");
            complete();
            return;
        }
        if (_target->type() == ObjectType::Door) {
            if (!workAtLock(*this, *_target, actor, _workingAtLock, dt)) {
                return;
            }
            unlockDoor(static_cast<Door &>(*_target), actor);
            complete();
            return;
        }
        if (_target->type() == ObjectType::Placeable) {
            if (!workAtLock(*this, *_target, actor, _workingAtLock, dt)) {
                return;
            }
            auto &placeable = static_cast<Placeable &>(*_target);
            unlockPlaceable(placeable, actor);
            if (placeable.hasInventory() && !placeable.isLocked()) {
                placeable.openInventory(actor);
            }
            complete();
            return;
        }

        warn("ActionExecutor: unsupported Security target: " + std::to_string(_target->id()));
        complete();
        return;
    }
    case SkillType::Demolitions: {
        auto *creature = dyn_cast<Creature>(&actor);
        if (!creature) {
            complete();
            return;
        }
        executeDemolitions(*creature, dt);
        return;
    }
    case SkillType::TreatInjury: {
        auto *creature = dyn_cast<Creature>(&actor);
        if (!creature) {
            complete();
            return;
        }
        executeTreatInjury(*creature, dt);
        return;
    }
    default:
        warn("ActionExecutor: unsupported UseSkillAction");
        complete();
    }
}

} // namespace game

} // namespace reone
