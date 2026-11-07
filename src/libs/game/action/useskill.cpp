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

#include "reone/game/action/playanimation.h"
#include "reone/game/animations.h"
#include "reone/game/animationutil.h"
#include "reone/game/d20/skills.h"
#include "reone/game/di/services.h"
#include "reone/game/effect/heal.h"
#include "reone/game/effect/visual.h"
#include "reone/game/event.h"
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
static constexpr int kExamineActionStrRef = 1532;
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
// Set-mine blast bonus by trap type, starting at type 4.
static constexpr int kMineBlastBonus[] = {3, 9, 0, 3, 9, 0, 0, 0, 0, 0, 0, 3, 9, 0, 0, 9, 12, 9, 12, 0, 0, 9, 12};

// The skill report.
static constexpr int kSkillSuccessStrRef = 1392;
static constexpr int kSkillFailureStrRef = 1393;
static constexpr int kTake20FailureStrRef = 1519;
static constexpr int kHopelessStrRef = 1370;
static constexpr int kSkillReportStrRef = 1408;
static constexpr float kOverheadSeconds = 5.0f;
// A text over a target this long or longer leaves out the action's name.
static constexpr size_t kOverheadNameLimit = 21;
// The log shows the roll as 0-31, the bonus as -128-127 and the DC as 0-127;
// a value past its range wraps around it.
static constexpr int kReportedRollRange = 32;
static constexpr int kReportedBonusRange = 256;
static constexpr int kReportedDCRange = 128;

enum class MineWork {
    Disarm,
    Recover,
    Flag,
    Examine
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
    if ((!setMine && !_target) || !actor.canUseSkill(SkillType::Demolitions)) {
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
                refused = door->trapFlag() == 1 && !door->trapDisarmable();
            }
            if (auto *placeable = dyn_cast<Placeable>(_target.get())) {
                refused = placeable->trapFlag() == 1 && !placeable->trapDisarmable();
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
            // Setting a mine walks toward its object as the walk's goal, to
            // its use range, and takes the extra reach only in the check; the
            // other mine work walks the extra reach too, with no goal.
            if (!actor.navigateToUse(*_target, extra, dt, true, false, false, setMine, setMine ? 0.0f : extra)) {
                return;
            }
            actor.turnToward(*_target);
        }
        // Work at it for a while: the work clip goes before the action.
        const bool trigger = _target && _target->type() == ObjectType::Trigger;
        const bool door = _target && _target->type() == ObjectType::Door;
        const bool onObject = door || (_target && _target->type() == ObjectType::Placeable);
        // KotOR sets a mine on a door or placeable with the get-from-table clip.
        auto clip = !_game.isTSL() && onObject ? WorkAnimation::GetFromTable : WorkAnimation::SetMine;
        if (!setMine) {
            switch (mineWork(_subSkill)) {
            case MineWork::Disarm:
                clip = trigger ? WorkAnimation::DisarmTrigger : door ? WorkAnimation::DisarmDoor
                                                                     : WorkAnimation::DisarmContainer;
                break;
            case MineWork::Recover:
                clip = trigger ? WorkAnimation::RecoverTrigger : WorkAnimation::GetFromTable;
                break;
            default:
                clip = trigger ? WorkAnimation::GetFromGround : WorkAnimation::GetFromTable;
                break;
            }
        }
        actor.addActionBefore(*this, _game.newAction<PlayAnimationAction>(
                                         static_cast<int>(clip), 1.0f, setMine ? kSetMineWorkTime : kMineWorkTime));
        _phase = 1;
        return;
    }
    if (setMine) {
        resolveSetMine(actor);
    } else {
        resolveMine(actor);
    }
    complete();
}

// Which results of which actions show over the target, by the text shown.
static std::optional<int> overheadTextStrRef(bool tsl, int actionStrRef, SkillResult result) {
    const bool mineWork = actionStrRef == kDisarmActionStrRef || actionStrRef == kRecoverActionStrRef;
    const bool setOrUnlock = (tsl && actionStrRef == kSetMineActionStrRef) || actionStrRef == kSecurityStrRef;
    switch (result) {
    case SkillResult::Failure:
        return mineWork || setOrUnlock ? std::optional<int> {kSkillFailureStrRef} : std::nullopt;
    case SkillResult::Success:
        return mineWork ? std::optional<int> {kSkillSuccessStrRef} : std::nullopt;
    case SkillResult::Take20Failure:
        return mineWork || setOrUnlock ? std::optional<int> {kTake20FailureStrRef} : std::nullopt;
    case SkillResult::Hopeless:
        return actionStrRef == kSecurityStrRef ? std::optional<int> {kHopelessStrRef} : std::nullopt;
    default:
        return std::nullopt;
    }
}

// A value as the log shows it: wrapped into a range of the given size that
// starts at first.
static int reportedValue(int value, int range, int first) {
    return ((value - first) % range + range) % range + first;
}

void reportSkillUse(Game &game, Creature &actor, const std::shared_ptr<Object> &target,
                    int actionStrRef, int skillNameStrRef, int roll, int bonus, int dc, SkillResult result) {
    // Told only to the controlling player.
    if (game.party().getLeader().get() != &actor) {
        return;
    }
    auto &strings = game.services().resource.strings;
    const std::string actionName = actionStrRef ? strings.getText(actionStrRef) : "";
    if (result != SkillResult::Hopeless) {
        const int shownRoll = reportedValue(roll, kReportedRollRange, 0);
        const int shownBonus = reportedValue(bonus, kReportedBonusRange, -kReportedBonusRange / 2);
        const bool success = result == SkillResult::Success || result == SkillResult::OwnMine;
        game.setCustomToken(0, actor.name());
        game.setCustomToken(1, game.getFeedbackText(success ? kSkillSuccessStrRef : kSkillFailureStrRef));
        game.setCustomToken(2, actionName);
        game.setCustomToken(3, std::to_string(shownRoll + shownBonus));
        game.setCustomToken(4, std::to_string(shownRoll));
        game.setCustomToken(5, shownBonus < 0 ? "-" : "+");
        game.setCustomToken(6, strings.getText(skillNameStrRef));
        game.setCustomToken(7, std::to_string(std::abs(shownBonus)));
        game.setCustomToken(8, std::to_string(reportedValue(dc, kReportedDCRange, 0)));
        game.messageLog().add(
            MessageLog::kFeedbackMessageType,
            MessageLog::Style::Normal,
            game.getFeedbackText(kSkillReportStrRef));
    }
    if (!target) {
        return;
    }
    const auto textStrRef = overheadTextStrRef(game.isTSL(), actionStrRef, result);
    if (!textStrRef) {
        return;
    }
    const std::string text = strings.getText(*textStrRef);
    std::string overhead = actionName + " *" + text + "*";
    if (overhead.size() >= kOverheadNameLimit) {
        overhead = "*" + text + "*";
    }
    target->setFeedbackText(std::move(overhead), kOverheadSeconds);
}

void useUpCarriedItem(Game &game, Creature &actor, const std::shared_ptr<Item> &item) {
    if (!item) {
        return;
    }
    auto repository = game.getObjectById(actor.itemRepositoryOwner().id());
    bool last = false;
    if (repository->removeItem(item, last) && last) {
        game.destroyRuntimeObjectGraph(item);
    }
}

void UseSkillAction::reportSkill(Creature &actor, const std::shared_ptr<Object> &target, int actionStrRef, int roll, int rank, int dc, SkillResult result) const {
    reportSkillUse(_game, actor, target, actionStrRef,
                   _skill == SkillType::TreatInjury ? kTreatInjuryNameStrRef : kDemolitionsNameStrRef,
                   roll, rank, dc, result);
}

void UseSkillAction::signalDisarmed(Object &trap, Creature &actor) {
    _game.queueScriptEvent(trap, actor, Event(static_cast<int>(SavedSignalEventType::Disarmed)));
}

// A door or placeable disarmed or recovered by skill leaves the area's traps at
// once; the disarm signal that follows leaves that as it is.
void UseSkillAction::leaveAreaTraps() {
    if (auto trap = _target->findTrap()) {
        trap->get().leaveAreaTraps();
    }
}

void UseSkillAction::removeDisarmedMine(Trigger &mine, Creature &actor) {
    mine.setMineRemoval(MineRemoval::Disarmed);
    _game.postObjectDestruction(mine, actor.id(), 0.0f, true);
}

void UseSkillAction::resolveMine(Creature &actor) {
    const MineWork work = mineWork(_subSkill);
    const int rank = actor.getUnopposedSkillRank(SkillType::Demolitions);
    const bool take20 = !actor.isInCombat();
    const int roll = take20 ? 20 : randomInt(1, 20);
    const int total = roll + rank;
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
        // The level is the controlled creature's.
        const int level = _game.party().getLeader()->attributes().getAggregateLevel();
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
            signalDisarmed(*trigger, actor);
            removeDisarmedMine(*trigger, actor);
            // The award compares against an unset DC; it takes the lower rate.
            awardXP(0);
            reportSkill(actor, _target, kDisarmActionStrRef, roll, rank, disarmDC, SkillResult::OwnMine);
            return;
        }
        // A disarm DC is at least 1.
        const int dc = std::max(1, disarmDC);
        const bool success = dc <= autoFailDC && total >= dc;
        SkillResult result = success ? SkillResult::Success : take20        ? SkillResult::Take20Failure
                                                          : total < dc - 10 ? SkillResult::CriticalFailure
                                                                            : SkillResult::Failure;
        if (success) {
            signalDisarmed(*_target, actor);
            if (trigger) {
                removeDisarmedMine(*trigger, actor);
            } else {
                leaveAreaTraps();
            }
            awardXP(dc);
        }
        if (!success) {
            actor.playSound(resource::SoundSetEntry::UnlockFailed);
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
        reportSkill(actor, _target, kDisarmActionStrRef, roll, rank, dc, result);
        return;
    }
    case MineWork::Recover: {
        if (trigger && isOwnMine(_game, actor, *trigger, false)) {
            removeDisarmedMine(*trigger, actor);
            recoverItem();
            reportSkill(actor, _target, kRecoverActionStrRef, roll, rank, disarmDC + 10, SkillResult::OwnMine);
            return;
        }
        const int dc = std::max(1, disarmDC + 10);
        const bool success = total >= dc;
        SkillResult result = success ? SkillResult::Success : take20        ? SkillResult::Take20Failure
                                                          : total < dc - 10 ? SkillResult::CriticalFailure
                                                                            : SkillResult::Failure;
        if (success) {
            // A recovered mine goes untold; a door or placeable is told it was disarmed.
            if (trigger) {
                removeDisarmedMine(*trigger, actor);
            } else {
                signalDisarmed(*_target, actor);
                leaveAreaTraps();
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
        reportSkill(actor, _target, kRecoverActionStrRef, roll, rank, dc, result);
        return;
    }
    case MineWork::Flag: {
        if (trigger && isOwnMine(_game, actor, *trigger, true)) {
            detection.flagged = true;
            reportSkill(actor, _target, 0, roll, rank, disarmDC, SkillResult::OwnMine);
            return;
        }
        const int dc = std::max(1, disarmDC - 5);
        const bool success = total >= dc;
        if (success) {
            detection.flagged = true;
        }
        reportSkill(actor, _target, 0, roll, rank, dc,
                    success ? SkillResult::Success : take20        ? SkillResult::Take20Failure
                                                 : total < dc - 10 ? SkillResult::CriticalFailure
                                                                   : SkillResult::Failure);
        return;
    }
    case MineWork::Examine: {
        const int dc = std::max(1, disarmDC - 7);
        const bool own = trigger && isOwnMine(_game, actor, *trigger, true);
        const bool success = own || total >= dc;
        reportSkill(actor, _target, kExamineActionStrRef, roll, rank, dc,
                    own ? SkillResult::OwnMine : success       ? SkillResult::Success
                                             : take20          ? SkillResult::Take20Failure
                                             : total < dc - 10 ? SkillResult::CriticalFailure
                                                               : SkillResult::Failure);
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
    SkillResult result;
    if (total >= setDC) {
        result = SkillResult::Success;
    } else if (take20) {
        result = SkillResult::Take20Failure;
    } else if (total >= setDC - 10) {
        result = SkillResult::Failure;
    } else {
        result = SkillResult::CriticalFailure;
    }
    const bool planted = result == SkillResult::Success || result == SkillResult::CriticalFailure;
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
                const bool blast = _game.isTSL() && result != SkillResult::CriticalFailure;
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
        if (result == SkillResult::CriticalFailure) {
            if (groundMine) {
                groundMine->enterMine(actorPtr, true);
            } else if (auto *door = dyn_cast<Door>(_target.get())) {
                door->triggerTrap(actorPtr, true);
            } else if (auto *placeable = dyn_cast<Placeable>(_target.get())) {
                placeable->triggerTrap(actorPtr, true);
            }
        }
    }
    actor.playSound(result == SkillResult::Success ? resource::SoundSetEntry::LayMine : resource::SoundSetEntry::UnlockFailed);
    actor.interruptActivities(true);
    actor.removeCombatInvisibilityEffects();
    // A mine set on the ground reports over no target.
    reportSkill(actor, onObject ? _target : nullptr, kSetMineActionStrRef, roll, rank, setDC, result);
}

// END Demolitions

// Treat Injury

static constexpr float kTreatInjuryWorkTime = 2.0f;
// Animation row of the work: the looping cast.
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
    reportSkill(actor, _target, kTreatInjuryNameStrRef, roll, rank, dc,
                total >= dc ? SkillResult::Success : take20 ? SkillResult::Take20Failure
                                                            : SkillResult::Failure);
    useUpCarriedItem(_game, actor, _itemUsed);

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
// itself; setting one keeps the kit, the target and where the mine goes; a
// heal keeps the patient, the item, an unused integer, and whether the healer
// may still walk up to the patient.
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

// Clearing a mine's work gives the pose back to the creature's state; a
// one-shot still running plays on.
bool UseSkillAction::cancel(std::shared_ptr<Action> self, Object &actor) {
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
