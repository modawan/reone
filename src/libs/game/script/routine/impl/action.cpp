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

#include <algorithm>

#include "reone/game/action/barkstring.h"
#include "reone/game/action/castspellatlocation.h"
#include "reone/game/action/castspellatobject.h"
#include "reone/game/action/closedoor.h"
#include "reone/game/action/docommand.h"
#include "reone/game/action/equipitem.h"
#include "reone/game/action/follow.h"
#include "reone/game/action/followleader.h"
#include "reone/game/action/followowner.h"
#include "reone/game/action/giveitem.h"
#include "reone/game/action/jumptolocation.h"
#include "reone/game/action/jumptoobject.h"
#include "reone/game/action/lockobject.h"
#include "reone/game/action/moveawayfromlocation.h"
#include "reone/game/action/moveawayfromobject.h"
#include "reone/game/action/movetolocation.h"
#include "reone/game/action/movetoobject.h"
#include "reone/game/action/movetopoint.h"
#include "reone/game/action/opencontainer.h"
#include "reone/game/action/opendoor.h"
#include "reone/game/action/pickupitem.h"
#include "reone/game/action/playanimation.h"
#include "reone/game/action/putdownitem.h"
#include "reone/game/action/randomwalk.h"
#include "reone/game/action/resumeconversation.h"
#include "reone/game/action/speakstring.h"
#include "reone/game/action/speakstringbystrref.h"
#include "reone/game/action/startconversation.h"
#include "reone/game/action/surrendertoenemies.h"
#include "reone/game/action/switchweapons.h"
#include "reone/game/action/takeitem.h"
#include "reone/game/action/unequipitem.h"
#include "reone/game/action/unlockobject.h"
#include "reone/game/action/usefeat.h"
#include "reone/game/action/useskill.h"
#include "reone/game/action/usetalentatlocation.h"
#include "reone/game/action/usetalentonobject.h"
#include "reone/game/action/wait.h"
#include "reone/game/combat.h"
#include "reone/game/d20/spells.h"
#include "reone/game/effect/resurrection.h"
#include "reone/game/equipmentoperation.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/party.h"
#include "reone/game/script/routine/argutil.h"
#include "reone/game/script/routine/context.h"
#include "reone/game/script/routines.h"
#include "reone/script/routine/exception/notimplemented.h"
#include "reone/script/variable.h"

#define R_VOID script::VariableType::Void
#define R_INT script::VariableType::Int
#define R_FLOAT script::VariableType::Float
#define R_OBJECT script::VariableType::Object
#define R_STRING script::VariableType::String
#define R_EFFECT script::VariableType::Effect
#define R_EVENT script::VariableType::Event
#define R_LOCATION script::VariableType::Location
#define R_TALENT script::VariableType::Talent
#define R_VECTOR script::VariableType::Vector
#define R_ACTION script::VariableType::Action

using namespace reone::script;

namespace reone {

namespace game {

// Most action commands are taken only by a caller that can be commanded; the
// others go into its queue whatever it is.
static bool refusesCommand(const RoutineContext &ctx) {
    return !getCaller(ctx)->isCommandable();
}

static Variable ActionRandomWalk(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (refusesCommand(ctx)) {
        return Variable::ofNull();
    }
    // The walk wanders about where the caller stands now.
    auto action = ctx.game.newAction<RandomWalkAction>(getCaller(ctx)->position());
    getCaller(ctx)->addAction(std::move(action));
    return Variable::ofNull();
}

static Variable ActionMoveToLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto lDestination = getLocationArgument(args, 0);
    auto bRun = getIntOrElse(args, 1, 0);

    // Transform
    auto run = static_cast<bool>(bRun);

    // Execute
    auto action = ctx.game.newAction<MoveToLocationAction>(std::move(lDestination), run);
    getCaller(ctx)->addAction(std::move(action));
    return Variable::ofNull();
}

// Only a creature that can be commanded moves to an object, and only to one
// standing in an area. The move ends with the object within the range or the
// creature's use range for it, whichever is longer; a door or placeable is
// walked to at its use point, only to that use range.
static void queueMoveToObject(const RoutineContext &ctx, std::shared_ptr<Object> target, bool run, float range,
                              bool force, float timeout) {
    auto caller = std::dynamic_pointer_cast<Creature>(getCaller(ctx));
    if (!caller || !caller->isCommandable() || !target->spatialArea()) {
        return;
    }
    // The move and its check both take the longer of the range asked for and
    // the caller's use range; a door or placeable is then closed on to the use
    // range at the use point.
    const float moveRange = std::max(range, caller->useRange(*target).range);
    caller->addAction(ctx.game.newAction<MoveToObjectAction>(
        std::move(target), run, moveRange, force, timeout, false, moveRange, true));
}

static Variable ActionMoveToObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oMoveTo = getObject(args, 0, ctx);
    auto bRun = getIntOrElse(args, 1, 0);
    auto fRange = getFloatOrElse(args, 2, 1.0f);

    // Transform
    auto run = static_cast<bool>(bRun);

    // Execute
    queueMoveToObject(ctx, std::move(oMoveTo), run, fRange, false, -1.0f);
    return Variable::ofNull();
}

static Variable ActionMoveAwayFromObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFleeFrom = getObject(args, 0, ctx);
    auto bRun = getIntOrElse(args, 1, 0);
    auto fMoveAwayRange = getFloatOrElse(args, 2, 40.0f);

    // Transform
    auto run = static_cast<bool>(bRun);
    auto caller = getCaller(ctx);

    // Execute
    // Only a creature that takes commands can be sent away.
    if (!isa<Creature>(caller) || !caller->isCommandable()) {
        return Variable::ofNull();
    }
    auto action = ctx.game.newAction<MoveAwayFromObject>(std::move(oFleeFrom), run, fMoveAwayRange);
    caller->addAction(std::move(action), caller->actions().lastGroup());
    return Variable::ofNull();
}

static Variable ActionEquipItem(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObjectOrNull(args, 0, ctx);
    auto nInventorySlot = getInt(args, 1);
    auto bInstant = getIntOrElse(args, 2, 0);

    // Transform
    auto item = std::dynamic_pointer_cast<Item>(oItem);
    auto caller = getCaller(ctx);
    auto instant = static_cast<bool>(bInstant);

    // Execute
    // Only a creature that can be commanded equips, only an item, and only
    // into one of the slots. KotOR has no second weapon set, so its slots end
    // before the first of those.
    const int slotCount = ctx.game.isTSL() ? InventorySlots::count : InventorySlots::rightWeapon2;
    if (nInventorySlot < 0 || nInventorySlot >= slotCount || !item || !isa<Creature>(caller) || !caller->isCommandable()) {
        return Variable::ofNull();
    }
    auto action = ctx.game.newAction<EquipItemAction>(std::move(item), nInventorySlot, instant);
    caller->addAction(std::move(action));
    return Variable::ofNull();
}

static Variable ActionUnequipItem(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObjectOrNull(args, 0, ctx);
    auto bInstant = getIntOrElse(args, 1, 0);

    // Execute
    // An object that does not exist is not unequipped.
    if (!oItem) {
        return Variable::ofNull();
    }
    auto item = checkItem(oItem);
    auto instant = static_cast<bool>(bInstant);
    if (refusesCommand(ctx)) {
        return Variable::ofNull();
    }
    auto action = ctx.game.newAction<UnequipItemAction>(std::move(item), instant);
    getCaller(ctx)->addAction(std::move(action));
    return Variable::ofNull();
}

static Variable ActionPickUpItem(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObject(args, 0, ctx);

    // Transform
    auto item = checkItem(oItem);

    // Execute
    if (refusesCommand(ctx)) {
        return Variable::ofNull();
    }
    auto action = ctx.game.newAction<PickUpItemAction>(std::move(item));
    getCaller(ctx)->addAction(std::move(action));
    return Variable::ofNull();
}

static Variable ActionPutDownItem(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObject(args, 0, ctx);

    // Transform
    auto item = checkItem(oItem);

    // Execute
    if (refusesCommand(ctx)) {
        return Variable::ofNull();
    }
    auto action = ctx.game.newAction<PutDownItemAction>(std::move(item));
    getCaller(ctx)->addAction(std::move(action));
    return Variable::ofNull();
}

static Variable ActionAttack(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oAttackee = getObject(args, 0, ctx);

    // Execute
    // The attack is an entry on the caller's round, as the player's attack
    // order is, but not a user action. The passive flag does not reach it.
    auto caller = checkCreature(getCaller(ctx));
    ctx.game.combat().scheduleAttack(*caller, oAttackee);
    return Variable::ofNull();
}

static Variable ActionSpeakString(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sStringToSpeak = getString(args, 0);
    auto nTalkVolume = getIntOrElse(args, 1, 0);

    // Transform

    // Execute
    if (refusesCommand(ctx)) {
        return Variable::ofNull();
    }
    auto action = ctx.game.newAction<SpeakStringAction>(sStringToSpeak, nTalkVolume);
    auto caller = getCaller(ctx);
    caller->addAction(std::move(action));
    return Variable::ofNull();
}

static Variable ActionPlayAnimation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nAnimation = getInt(args, 0);
    auto fSpeed = getFloatOrElse(args, 1, 1.0f);
    auto fDurationSeconds = getFloatOrElse(args, 2, 0.0f);

    // Execute: it goes behind the caller's actions.
    requestScriptAnimation(ctx.game, *getCaller(ctx), nAnimation, fSpeed, fDurationSeconds, false);
    return Variable::ofNull();
}

static Variable ActionOpenDoor(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oDoor = getObject(args, 0, ctx);

    // Transform
    auto door = checkDoor(oDoor);

    // Execute
    if (refusesCommand(ctx)) {
        return Variable::ofNull();
    }
    auto action = ctx.game.newAction<OpenDoorAction>(std::move(door));
    getCaller(ctx)->addAction(std::move(action));
    return Variable::ofNull();
}

static Variable ActionCloseDoor(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oDoor = getObject(args, 0, ctx);

    // Transform
    auto door = checkDoor(oDoor);

    // Execute
    if (refusesCommand(ctx)) {
        return Variable::ofNull();
    }
    auto action = ctx.game.newAction<CloseDoorAction>(std::move(door));
    getCaller(ctx)->addAction(std::move(action));
    return Variable::ofNull();
}

// Only creatures and placeables cast for a script command.
static bool castsForScripts(const Object &caller) {
    return isa<Creature>(&caller) || isa<Placeable>(&caller);
}

// A real cast command needs a caster that can be commanded. A creature's cast
// that is not a cheat takes the casting source the command picks, and without
// one it casts nothing.
static bool scriptCastSource(const Object &caller, const Spell &spell, bool cheat,
                             std::optional<SpellSelection> &selection) {
    if (!castsForScripts(caller) || !caller.isCommandable()) {
        return false;
    }
    const auto *creature = dyn_cast<Creature>(&caller);
    if (!creature || cheat) {
        return true;
    }
    selection = scriptCastingSource(*creature, spell);
    return selection.has_value();
}

// A creature's cast goes on its round; a placeable's goes in its queue, unless
// it is aimed at an object in another area.
static void submitScriptCast(Game &game, Object &caller, std::shared_ptr<CastSpellAction> action) {
    if (auto *creature = dyn_cast<Creature>(&caller)) {
        game.combat().scheduleCast(*creature, action);
        return;
    }
    const auto &target = action->target();
    if (target && !isa<Area>(target.get()) && !isa<Module>(target.get()) && !target->sharesSpatialArea(caller)) {
        return;
    }
    caller.addAction(std::move(action));
}

static Variable ActionCastSpellAtObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nSpell = getInt(args, 0);
    auto oTarget = getObjectOrNull(args, 1, ctx);
    // nMetaMagic (2) changes nothing; nDomainLevel (4) is only carried by the
    // command.
    auto bCheat = getIntOrElse(args, 3, 0);
    auto nDomainLevel = getIntOrElse(args, 4, 0);
    auto nProjectilePathType = getIntOrElse(args, 5, 0);
    auto bInstantSpell = getIntOrElse(args, 6, 0);

    // Transform
    auto spell = ctx.services.game.spells.get(static_cast<SpellType>(nSpell));
    if (!spell) {
        return Variable::ofNull();
    }
    auto cheat = static_cast<bool>(bCheat);
    auto projectilePathType = projectilePathFromScript(nProjectilePathType);
    if (!projectilePathType) {
        return Variable::ofNull();
    }
    auto instantSpell = static_cast<bool>(bInstantSpell);

    // Execute: nothing is cast at an object that does not exist.
    if (!oTarget) {
        return Variable::ofNull();
    }
    auto caller = getCaller(ctx);
    std::optional<SpellSelection> selection;
    if (!scriptCastSource(*caller, *spell, cheat, selection)) {
        return Variable::ofNull();
    }
    // Only a creature's cast can be instant. Any instant value casts at once,
    // but only 1 also skips the range test and approach.
    const bool creatureInstant = instantSpell && isa<Creature>(caller.get());
    auto action = ctx.game.newAction<CastSpellAtObjectAction>(std::move(spell), std::move(oTarget), /*item=*/std::nullopt,
                                                              cheat, *projectilePathType, creatureInstant,
                                                              std::nullopt, std::nullopt, selection);
    action->setCommandParameters(nDomainLevel, 0, 0);
    action->setSkipsApproach(creatureInstant && bInstantSpell == 1);
    submitScriptCast(ctx.game, *caller, std::move(action));
    return Variable::ofNull();
}

// The caller gives an item it holds, or, as a party member, an item another
// party member holds.
static Variable ActionGiveItem(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObjectOrNull(args, 0, ctx);
    auto oGiveTo = getObjectOrNull(args, 1, ctx);

    // Transform
    auto item = std::dynamic_pointer_cast<Item>(oItem);

    // Execute
    if (!item || !oGiveTo) {
        return Variable::ofNull();
    }
    GiveItemAction::queue(ctx.game, *getCaller(ctx), std::move(item), std::move(oGiveTo), GiveItemAction::kWholeStack, true);
    return Variable::ofNull();
}

// The caller takes an item it does not hold; a creature first moves to the one
// it takes the item from, running when that is more than five metres away.
static Variable ActionTakeItem(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObjectOrNull(args, 0, ctx);
    auto oTakeFrom = getObjectOrNull(args, 1, ctx);

    // Transform
    auto item = std::dynamic_pointer_cast<Item>(oItem);

    // Execute
    if (!item || !oTakeFrom || refusesCommand(ctx)) {
        return Variable::ofNull();
    }
    auto caller = getCaller(ctx);
    if (item->owner() == caller->id()) {
        return Variable::ofNull();
    }
    if (isa<Creature>(*caller)) {
        static constexpr float kTakeItemRunDistance2 = 25.0f;
        const glm::vec3 offset {caller->position() - oTakeFrom->position()};
        const bool run = glm::dot(offset, offset) > kTakeItemRunDistance2;
        caller->addAction(ctx.game.newAction<MoveToPointAction>(oTakeFrom->position(), run));
    }
    auto action = ctx.game.newAction<TakeItemAction>(std::move(item), std::move(oTakeFrom));
    caller->addAction(std::move(action));
    return Variable::ofNull();
}

static Variable ActionForceFollowObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFollow = getObject(args, 0, ctx);
    auto fFollowDistance = getFloatOrElse(args, 1, 0.0f);

    // Transform

    // Execute
    auto action = ctx.game.newAction<FollowAction>(oFollow, fFollowDistance);
    auto caller = getCaller(ctx);
    caller->addAction(std::move(action));
    return Variable::ofNull();
}

static Variable ActionJumpToObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oToJumpTo = getObject(args, 0, ctx);
    auto bWalkStraightLineToPoint = getIntOrElse(args, 1, 1);

    // Transform
    auto walkStraightLine = static_cast<bool>(bWalkStraightLineToPoint);

    // Execute: only a creature that can be commanded jumps.
    auto caller = std::dynamic_pointer_cast<Creature>(getCaller(ctx));
    if (!caller || !caller->isCommandable()) {
        return Variable::ofNull();
    }
    auto action = ctx.game.newAction<JumpToObjectAction>(std::move(oToJumpTo), walkStraightLine);
    caller->addAction(std::move(action));
    return Variable::ofNull();
}

static Variable ActionWait(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fSeconds = getFloat(args, 0);

    // Transform

    // Execute
    if (refusesCommand(ctx)) {
        return Variable::ofNull();
    }
    auto action = ctx.game.newAction<WaitAction>(fSeconds);
    getCaller(ctx)->addAction(std::move(action));
    return Variable::ofNull();
}

/** The share of its vitality a downed member stands up with when a TSL conversation starts. */
static constexpr int kConversationRevivePercent = 2;

static Variable ActionStartConversation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObjectToConverse = getObject(args, 0, ctx);
    auto sDialogResRef = getStringOrElse(args, 1, "");
    auto bPrivateConversation = getIntOrElse(args, 2, 0);
    auto nConversationType = getIntOrElse(args, 3, 0);
    // bIgnoreStartRange (4) changes nothing: a scripted conversation starts
    // wherever its speaker stands.
    auto sNameObjectToIgnore1 = getStringOrElse(args, 5, "");
    auto sNameObjectToIgnore2 = getStringOrElse(args, 6, "");
    auto sNameObjectToIgnore3 = getStringOrElse(args, 7, "");
    auto sNameObjectToIgnore4 = getStringOrElse(args, 8, "");
    auto sNameObjectToIgnore5 = getStringOrElse(args, 9, "");
    auto sNameObjectToIgnore6 = getStringOrElse(args, 10, "");
    auto bUseLeader = getIntOrElse(args, 11, 0);
    auto nBarkX = getIntOrElse(args, 12, -1);
    auto nBarkY = getIntOrElse(args, 13, -1);
    auto bDontClearAllActions = getIntOrElse(args, 14, 0);

    // Transform
    std::string dialogResRef(sDialogResRef);
    auto caller = getCaller(ctx);
    auto privateConversation = static_cast<bool>(bPrivateConversation);
    auto conversationType = static_cast<resource::ConversationType>(nConversationType);
    auto namesToIgnore = std::vector<std::string> {sNameObjectToIgnore1,
                                                   sNameObjectToIgnore2,
                                                   sNameObjectToIgnore3,
                                                   sNameObjectToIgnore4,
                                                   sNameObjectToIgnore5,
                                                   sNameObjectToIgnore6};
    auto useLeader = static_cast<bool>(bUseLeader);
    auto dontClearAllActions = static_cast<bool>(bDontClearAllActions);

    // Execute
    // Whether the leader answers in conversations holds from now on, and the
    // party leader drops its actions and its orders, as the player's controls
    // clear them, even when the conversation is then refused.
    ctx.game.setLeaderAnswersInDialog(useLeader);
    auto &party = ctx.game.party();
    if (auto leader = party.getLeader()) {
        ctx.game.combat().clearAllOrders(*leader);
        leader->clearAllActions(true);
    }
    // TSL refuses while a conversation runs or while nobody in the party
    // stands: an empty first place counts as standing, an empty later one
    // as down.
    if (ctx.game.isTSL()) {
        bool partyDown = true;
        for (int slot = 0; slot < kPartyFollowSlots; ++slot) {
            const auto member = party.getMember(slot);
            if (member ? member->currentHitPoints() > 0 : slot == 0) {
                partyDown = false;
                break;
            }
        }
        if (partyDown || ctx.game.isConversationActive()) {
            return Variable::ofNull();
        }
    }
    if (refusesCommand(ctx)) {
        return Variable::ofNull();
    }
    // In TSL every downed member stands up before the conversation, at 2% of
    // its vitality.
    if (ctx.game.isTSL()) {
        for (int slot = 0; slot < kPartyFollowSlots; ++slot) {
            const auto member = party.getMember(slot);
            if (!member || member->currentHitPoints() > 0) {
                continue;
            }
            auto revive = ctx.game.newEffect<ResurrectionEffect>(kConversationRevivePercent);
            revive->setCategory(EffectCategory::Magical);
            revive->setSaveFacingCreator(caller);
            member->applyEffect(std::move(revive), DurationType::Instant);
        }
    }
    // The conversation is under way from now on. Unless told not to, the
    // caller drops what it was doing first.
    ctx.game.setDialogUnderWay(true);
    if (!dontClearAllActions) {
        caller->clearAllActions(true);
    }
    auto action = ctx.game.newAction<StartConversationAction>(
        std::move(oObjectToConverse),
        dialogResRef,
        privateConversation,
        conversationType,
        /*ignoreStartRange=*/true,
        namesToIgnore,
        useLeader,
        nBarkX,
        nBarkY,
        dontClearAllActions);
    caller->addAction(std::move(action));
    return Variable::ofNull();
}

static Variable ActionPauseConversation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute: a commandable caller pauses immediately rather than through its queue.
    auto caller = getCaller(ctx);
    if (caller->isCommandable()) {
        ctx.game.pauseConversationBy(*caller);
    }
    return Variable::ofNull();
}

static Variable ActionResumeConversation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (refusesCommand(ctx)) {
        return Variable::ofNull();
    }
    auto action = ctx.game.newAction<ResumeConversationAction>();
    getCaller(ctx)->addAction(std::move(action));
    return Variable::ofNull();
}

static Variable ActionJumpToLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto lLocation = getLocationArgument(args, 0);

    // Transform

    // Execute: only a creature that can be commanded jumps.
    auto caller = std::dynamic_pointer_cast<Creature>(getCaller(ctx));
    if (!caller || !caller->isCommandable()) {
        return Variable::ofNull();
    }
    auto action = ctx.game.newAction<JumpToLocationAction>(std::move(lLocation));
    caller->addAction(std::move(action));
    return Variable::ofNull();
}

static Variable ActionCastSpellAtLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nSpell = getInt(args, 0);
    auto lTargetLocation = getLocationArgument(args, 1);
    // nMetaMagic (2) changes nothing.
    auto bCheat = getIntOrElse(args, 3, 0);
    auto nProjectilePathType = getIntOrElse(args, 4, 0);
    // bInstantSpell (5) changes nothing either: a location cast is never
    // instant.

    // Transform
    auto spell = ctx.services.game.spells.get(static_cast<SpellType>(nSpell));
    if (!spell) {
        return Variable::ofNull();
    }
    auto cheat = static_cast<bool>(bCheat);
    auto projectilePathType = projectilePathFromScript(nProjectilePathType);
    if (!projectilePathType) {
        return Variable::ofNull();
    }

    // Execute
    auto caller = getCaller(ctx);
    std::optional<SpellSelection> selection;
    if (!scriptCastSource(*caller, *spell, cheat, selection)) {
        return Variable::ofNull();
    }
    auto action = ctx.game.newAction<CastSpellAtLocationAction>(spell, lTargetLocation, cheat,
                                                                *projectilePathType, /*instantSpell=*/false, std::nullopt, std::nullopt, std::nullopt, selection);
    submitScriptCast(ctx.game, *caller, std::move(action));
    return Variable::ofNull();
}

static Variable ActionSpeakStringByStrRef(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nStrRef = getInt(args, 0);
    auto nTalkVolume = getIntOrElse(args, 1, 0);

    // Transform

    // Execute
    if (refusesCommand(ctx)) {
        return Variable::ofNull();
    }
    auto action = ctx.game.newAction<SpeakStringByStrRefAction>(nStrRef, nTalkVolume);
    auto caller = getCaller(ctx);
    caller->addAction(std::move(action));
    return Variable::ofNull();
}

static Variable ActionUseFeat(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nFeat = getInt(args, 0);
    auto oTarget = getObject(args, 1, ctx);

    // Transform
    auto feat = static_cast<FeatType>(nFeat);

    // Execute
    // A creature in an area uses a feat it holds, at the highest rank of the
    // chain it holds; only an attack feat makes an attack. The attack is an
    // ordinary attack entry on the creature's round, without its dispatcher:
    // it waits there until a dispatcher heads the creature's queue.
    auto creature = std::dynamic_pointer_cast<Creature>(getCaller(ctx));
    if (!creature || !creature->spatialArea()) {
        return Variable::ofNull();
    }
    if (creature->useFeat(feat) == FeatType::Invalid) {
        return Variable::ofNull();
    }
    ctx.game.combat().addRoundAttack(*creature, oTarget);
    return Variable::ofNull();
}

static Variable ActionUseSkill(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nSkill = getInt(args, 0);

    // Stealth is used at once rather than queued, by a creature in an area,
    // and only on a target that exists.
    if (static_cast<SkillType>(nSkill) == SkillType::Stealth) {
        auto creature = std::dynamic_pointer_cast<Creature>(getCaller(ctx));
        if (creature && creature->spatialArea() && getObjectOrNull(args, 1, ctx)) {
            creature->useStealthSkill();
        }
        return Variable::ofNull();
    }

    auto oTarget = getObject(args, 1, ctx);
    auto nSubSkill = getIntOrElse(args, 2, 0);
    auto oItemUsed = getObjectOrNull(args, 3, ctx);

    // Transform
    auto skill = static_cast<SkillType>(nSkill);
    // The item is optional: only setting a mine and healing use one.
    auto itemUsed = std::dynamic_pointer_cast<Item>(oItemUsed);

    // Execute
    if (refusesCommand(ctx)) {
        return Variable::ofNull();
    }
    auto action = ctx.game.newAction<UseSkillAction>(skill, std::move(oTarget), nSubSkill, std::move(itemUsed));
    getCaller(ctx)->addAction(std::move(action));
    return Variable::ofNull();
}

static Variable ActionDoCommand(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto aActionToDo = getAction(args, 0);

    // Transform

    // Execute
    if (refusesCommand(ctx)) {
        return Variable::ofNull();
    }
    auto commandAction = ctx.game.newAction<DoCommandAction>(std::move(aActionToDo));
    getCaller(ctx)->addAction(std::move(commandAction));
    return Variable::ofNull();
}

static Variable ActionUseTalentOnObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto tChosenTalent = getTalent(args, 0);
    auto oTarget = getObject(args, 1, ctx);

    // Transform

    // Execute
    auto caller = getCaller(ctx);
    auto action = ctx.game.newAction<UseTalentOnObjectAction>(tChosenTalent, oTarget, *caller);
    const auto &talent = action->subAction();
    if (!talent) {
        return Variable::ofNull();
    }
    // A feat talent goes on the caster's round at once, as an attack with
    // that feat.
    if (auto *feat = dyn_cast<UseFeatAction>(talent.get())) {
        auto &caster = *dyn_cast<Creature>(caller.get());
        ctx.game.combat().scheduleAttack(caster, oTarget, feat->feat());
        return Variable::ofNull();
    }
    // A spell talent goes on the caster's round at once: an item's power as
    // the use of the item, any other as a cast.
    if (auto *cast = dyn_cast<CastSpellAtObjectAction>(talent.get())) {
        auto &caster = *dyn_cast<Creature>(caller.get());
        if (cast->item()) {
            ctx.game.useItem(caster, **cast->item(), *cast->itemProperty(), talent);
        } else {
            ctx.game.combat().scheduleCast(caster, talent);
        }
        return Variable::ofNull();
    }
    caller->addAction(std::move(action));
    return Variable::ofNull();
}

static Variable ActionUseTalentAtLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto tChosenTalent = getTalent(args, 0);
    auto lTargetLocation = getLocationArgument(args, 1);

    // Transform

    // Execute
    auto caller = getCaller(ctx);
    auto action = ctx.game.newAction<UseTalentAtLocationAction>(tChosenTalent, lTargetLocation, *caller);
    const auto &talent = action->subAction();
    if (!talent) {
        return Variable::ofNull();
    }
    // The talent goes on the caster's round at once: an item's power as the
    // use of the item, any other as a cast.
    auto *cast = dyn_cast<CastSpellAtLocationAction>(talent.get());
    auto &caster = *dyn_cast<Creature>(caller.get());
    if (cast->item()) {
        ctx.game.useItem(caster, **cast->item(), *cast->itemProperty(), talent);
    } else {
        ctx.game.combat().scheduleCast(caster, talent);
    }
    return Variable::ofNull();
}

static Variable ActionInteractObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oPlaceable = getObject(args, 0, ctx);

    // Transform
    auto placeable = checkPlaceable(oPlaceable);

    // Execute: a creature that takes commands uses the placeable.
    auto caller = getCaller(ctx);
    if (!isa<Creature>(caller) || refusesCommand(ctx)) {
        return Variable::ofNull();
    }
    auto action = ctx.game.newAction<OpenContainerAction>(std::move(placeable));
    caller->addAction(std::move(action));
    return Variable::ofNull();
}

static Variable ActionMoveAwayFromLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto lMoveAwayFrom = getLocationArgument(args, 0);
    auto bRun = getIntOrElse(args, 1, 0);
    auto fMoveAwayRange = getFloatOrElse(args, 2, 40.0f);

    // Transform
    auto run = static_cast<bool>(bRun);
    auto caller = getCaller(ctx);
    auto creature = dyn_cast<Creature>(caller);

    // Execute
    // Only a creature that takes commands can be sent away, and one already
    // out of range of the location stays where it is. The first leg is set
    // off now, from where the creature stands, in a straight line.
    if (!creature || !creature->isCommandable() ||
        creature->getSquareDistanceTo(lMoveAwayFrom->position()) > fMoveAwayRange * fMoveAwayRange) {
        return Variable::ofNull();
    }
    const glm::vec3 point = ctx.game.module()->area()->computeAwayPoint(*creature, lMoveAwayFrom->position(), fMoveAwayRange);
    static constexpr bool kForce = false;
    static constexpr float kNoTimeout = -1.0f;
    static constexpr float kToThePoint = 0.0f;
    static constexpr bool kStraight = true;
    creature->addAction(ctx.game.newAction<MoveToLocationAction>(
        std::make_shared<Location>(point, 0.0f), run, kForce, kNoTimeout, kToThePoint, kStraight));
    auto action = ctx.game.newAction<MoveAwayFromLocation>(std::move(lMoveAwayFrom), run, fMoveAwayRange);
    creature->addAction(std::move(action), creature->actions().lastGroup());
    return Variable::ofNull();
}

static Variable ActionSurrenderToEnemies(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Only a creature other than a player character queues a surrender.
    auto caller = dyn_cast<Creature>(getCaller(ctx));
    if (!caller || caller->isPC()) {
        return Variable::ofNull();
    }
    caller->addAction(ctx.game.newAction<SurrenderToEnemiesAction>());
    return Variable::ofNull();
}

static Variable ActionForceMoveToLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto lDestination = getLocationArgument(args, 0);
    auto bRun = getIntOrElse(args, 1, 0);
    auto fTimeout = getFloatOrElse(args, 2, 30.0f);

    // Transform
    auto run = static_cast<bool>(bRun);

    // Execute
    auto action = ctx.game.newAction<MoveToLocationAction>(std::move(lDestination), run, true, fTimeout);
    getCaller(ctx)->addAction(std::move(action));
    return Variable::ofNull();
}

static Variable ActionForceMoveToObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oMoveTo = getObject(args, 0, ctx);
    auto bRun = getIntOrElse(args, 1, 0);
    auto fRange = getFloatOrElse(args, 2, 1.0f);
    auto fTimeout = getFloatOrElse(args, 3, 30.0f);

    // Transform
    auto run = static_cast<bool>(bRun);

    // Execute
    queueMoveToObject(ctx, std::move(oMoveTo), run, fRange, true, fTimeout);
    return Variable::ofNull();
}

static Variable ActionEquipMostDamagingMelee(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oVersus = getObjectOrNull(args, 0, ctx);
    auto bOffHand = getIntOrElse(args, 1, 0);

    // Execute
    // The choice is made as the script runs; only the equip is queued.
    auto caller = std::dynamic_pointer_cast<Creature>(getCaller(ctx));
    if (caller && caller->isCommandable()) {
        equipMostDamagingMeleeWeapon(ctx.game, *caller, oVersus, bOffHand != 0);
    }
    return Variable::ofNull();
}

static Variable ActionEquipMostDamagingRanged(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oVersus = getObjectOrNull(args, 0, ctx);

    // Execute
    // Without a better ranged weapon the melee choice is made for the main hand.
    if (auto caller = std::dynamic_pointer_cast<Creature>(getCaller(ctx)); caller && caller->isCommandable()) {
        if (!equipMostDamagingRangedWeapon(ctx.game, *caller, oVersus)) {
            equipMostDamagingMeleeWeapon(ctx.game, *caller, oVersus, false);
        }
    }
    return Variable::ofNull();
}

static Variable ActionEquipMostEffectiveArmor(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // The choice is made as the script runs; only the equip is queued.
    auto caller = std::dynamic_pointer_cast<Creature>(getCaller(ctx));
    if (caller && caller->isCommandable()) {
        equipMostEffectiveArmor(ctx.game, *caller);
    }
    return Variable::ofNull();
}

static Variable ActionUnlockObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);

    // Transform

    // Execute
    if (refusesCommand(ctx)) {
        return Variable::ofNull();
    }
    auto action = ctx.game.newAction<UnlockObjectAction>(std::move(oTarget));
    getCaller(ctx)->addAction(std::move(action));
    return Variable::ofNull();
}

static Variable ActionLockObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);

    // Transform

    // Execute
    if (refusesCommand(ctx)) {
        return Variable::ofNull();
    }
    auto action = ctx.game.newAction<LockObjectAction>(std::move(oTarget));
    getCaller(ctx)->addAction(std::move(action));
    return Variable::ofNull();
}

static Variable ActionCastFakeSpellAtObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nSpell = getInt(args, 0);
    auto oTarget = getObjectOrNull(args, 1, ctx);
    auto nProjectilePathType = getIntOrElse(args, 2, 0);

    // Transform
    auto spell = ctx.services.game.spells.get(static_cast<SpellType>(nSpell));
    if (!spell) {
        return Variable::ofNull();
    }
    auto projectilePathType = projectilePathFromScript(nProjectilePathType);
    if (!projectilePathType) {
        return Variable::ofNull();
    }

    // Execute: nothing is cast at an object that does not exist.
    if (!oTarget) {
        return Variable::ofNull();
    }
    auto caller = getCaller(ctx);
    if (!castsForScripts(*caller)) {
        return Variable::ofNull();
    }
    auto action = ctx.game.newAction<CastSpellAtObjectAction>(std::move(spell), std::move(oTarget), /*item=*/std::nullopt,
                                                              /*cheat=*/false, *projectilePathType, /*instantSpell=*/false, std::nullopt, std::nullopt,
                                                              std::nullopt, std::nullopt, /*fake=*/true);
    submitScriptCast(ctx.game, *caller, std::move(action));
    return Variable::ofNull();
}

static Variable ActionCastFakeSpellAtLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nSpell = getInt(args, 0);
    auto lTarget = getLocationArgument(args, 1);
    auto nProjectilePathType = getIntOrElse(args, 2, 0);

    // Transform
    auto spell = ctx.services.game.spells.get(static_cast<SpellType>(nSpell));
    if (!spell) {
        return Variable::ofNull();
    }
    auto projectilePathType = projectilePathFromScript(nProjectilePathType);
    if (!projectilePathType) {
        return Variable::ofNull();
    }

    // Execute
    auto caller = getCaller(ctx);
    if (!castsForScripts(*caller)) {
        return Variable::ofNull();
    }
    auto action = ctx.game.newAction<CastSpellAtLocationAction>(std::move(spell), std::move(lTarget),
                                                                /*cheat=*/false, *projectilePathType, /*instantSpell=*/false, std::nullopt, std::nullopt, std::nullopt,
                                                                std::nullopt, std::nullopt, /*fake=*/true);
    submitScriptCast(ctx.game, *caller, std::move(action));
    return Variable::ofNull();
}

static Variable ActionBarkString(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto strRef = getInt(args, 0);

    // Transform

    // Execute
    auto action = ctx.game.newAction<BarkStringAction>(strRef);
    getCaller(ctx)->addAction(std::move(action));
    return Variable::ofNull();
}

static Variable ActionFollowLeader(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute: only a party member that can be commanded follows the leader.
    auto caller = std::dynamic_pointer_cast<Creature>(getCaller(ctx));
    if (!caller || !caller->isPartyMember() || !caller->isCommandable()) {
        return Variable::ofNull();
    }
    auto action = ctx.game.newAction<FollowLeaderAction>();
    caller->addAction(std::move(action));
    return Variable::ofNull();
}

static Variable ActionFollowOwner(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fRange = getFloatOrElse(args, 0, 2.5f);

    // Transform

    // Execute: only a puppet that can be commanded follows its owner.
    auto caller = std::dynamic_pointer_cast<Creature>(getCaller(ctx));
    if (!caller || !caller->isPuppet() || !caller->isCommandable()) {
        return Variable::ofNull();
    }
    auto action = ctx.game.newAction<FollowOwnerAction>(fRange);
    caller->addAction(std::move(action));
    return Variable::ofNull();
}

static Variable ActionSwitchWeapons(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto action = ctx.game.newAction<SwitchWeaponsAction>();
    getCaller(ctx)->addAction(std::move(action));
    return Variable::ofNull();
}

void Routines::registerActionKotorRoutines() {
    insert(20, "ActionRandomWalk", R_VOID, {}, &ActionRandomWalk);
    insert(21, "ActionMoveToLocation", R_VOID, {R_LOCATION, R_INT}, &ActionMoveToLocation);
    insert(22, "ActionMoveToObject", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &ActionMoveToObject);
    insert(23, "ActionMoveAwayFromObject", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &ActionMoveAwayFromObject);
    insert(32, "ActionEquipItem", R_VOID, {R_OBJECT, R_INT, R_INT}, &ActionEquipItem);
    insert(33, "ActionUnequipItem", R_VOID, {R_OBJECT, R_INT}, &ActionUnequipItem);
    insert(34, "ActionPickUpItem", R_VOID, {R_OBJECT}, &ActionPickUpItem);
    insert(35, "ActionPutDownItem", R_VOID, {R_OBJECT}, &ActionPutDownItem);
    insert(37, "ActionAttack", R_VOID, {R_OBJECT, R_INT}, &ActionAttack);
    insert(39, "ActionSpeakString", R_VOID, {R_STRING, R_INT}, &ActionSpeakString);
    insert(40, "ActionPlayAnimation", R_VOID, {R_INT, R_FLOAT, R_FLOAT}, &ActionPlayAnimation);
    insert(43, "ActionOpenDoor", R_VOID, {R_OBJECT}, &ActionOpenDoor);
    insert(44, "ActionCloseDoor", R_VOID, {R_OBJECT}, &ActionCloseDoor);
    insert(48, "ActionCastSpellAtObject", R_VOID, {R_INT, R_OBJECT, R_INT, R_INT, R_INT, R_INT, R_INT}, &ActionCastSpellAtObject);
    insert(135, "ActionGiveItem", R_VOID, {R_OBJECT, R_OBJECT}, &ActionGiveItem);
    insert(136, "ActionTakeItem", R_VOID, {R_OBJECT, R_OBJECT}, &ActionTakeItem);
    insert(167, "ActionForceFollowObject", R_VOID, {R_OBJECT, R_FLOAT}, &ActionForceFollowObject);
    insert(196, "ActionJumpToObject", R_VOID, {R_OBJECT, R_INT}, &ActionJumpToObject);
    insert(202, "ActionWait", R_VOID, {R_FLOAT}, &ActionWait);
    insert(204, "ActionStartConversation", R_VOID, {R_OBJECT, R_STRING, R_INT, R_INT, R_INT, R_STRING, R_STRING, R_STRING, R_STRING, R_STRING, R_STRING, R_INT}, &ActionStartConversation);
    insert(205, "ActionPauseConversation", R_VOID, {}, &ActionPauseConversation);
    insert(206, "ActionResumeConversation", R_VOID, {}, &ActionResumeConversation);
    insert(214, "ActionJumpToLocation", R_VOID, {R_LOCATION}, &ActionJumpToLocation);
    insert(234, "ActionCastSpellAtLocation", R_VOID, {R_INT, R_LOCATION, R_INT, R_INT, R_INT, R_INT}, &ActionCastSpellAtLocation);
    insert(240, "ActionSpeakStringByStrRef", R_VOID, {R_INT, R_INT}, &ActionSpeakStringByStrRef);
    insert(287, "ActionUseFeat", R_VOID, {R_INT, R_OBJECT}, &ActionUseFeat);
    insert(288, "ActionUseSkill", R_VOID, {R_INT, R_OBJECT, R_INT, R_OBJECT}, &ActionUseSkill);
    insert(294, "ActionDoCommand", R_VOID, {R_ACTION}, &ActionDoCommand);
    insert(309, "ActionUseTalentOnObject", R_VOID, {R_TALENT, R_OBJECT}, &ActionUseTalentOnObject);
    insert(310, "ActionUseTalentAtLocation", R_VOID, {R_TALENT, R_LOCATION}, &ActionUseTalentAtLocation);
    insert(329, "ActionInteractObject", R_VOID, {R_OBJECT}, &ActionInteractObject);
    insert(360, "ActionMoveAwayFromLocation", R_VOID, {R_LOCATION, R_INT, R_FLOAT}, &ActionMoveAwayFromLocation);
    insert(379, "ActionSurrenderToEnemies", R_VOID, {}, &ActionSurrenderToEnemies);
    insert(382, "ActionForceMoveToLocation", R_VOID, {R_LOCATION, R_INT, R_FLOAT}, &ActionForceMoveToLocation);
    insert(383, "ActionForceMoveToObject", R_VOID, {R_OBJECT, R_INT, R_FLOAT, R_FLOAT}, &ActionForceMoveToObject);
    insert(399, "ActionEquipMostDamagingMelee", R_VOID, {R_OBJECT, R_INT}, &ActionEquipMostDamagingMelee);
    insert(400, "ActionEquipMostDamagingRanged", R_VOID, {R_OBJECT}, &ActionEquipMostDamagingRanged);
    insert(404, "ActionEquipMostEffectiveArmor", R_VOID, {}, &ActionEquipMostEffectiveArmor);
    insert(483, "ActionUnlockObject", R_VOID, {R_OBJECT}, &ActionUnlockObject);
    insert(484, "ActionLockObject", R_VOID, {R_OBJECT}, &ActionLockObject);
    insert(501, "ActionCastFakeSpellAtObject", R_VOID, {R_INT, R_OBJECT, R_INT}, &ActionCastFakeSpellAtObject);
    insert(502, "ActionCastFakeSpellAtLocation", R_VOID, {R_INT, R_LOCATION, R_INT}, &ActionCastFakeSpellAtLocation);
    insert(700, "ActionBarkString", R_VOID, {R_INT}, &ActionBarkString);
    insert(730, "ActionFollowLeader", R_VOID, {}, &ActionFollowLeader);
}

void Routines::registerActionTslRoutines() {
    insert(20, "ActionRandomWalk", R_VOID, {}, &ActionRandomWalk);
    insert(21, "ActionMoveToLocation", R_VOID, {R_LOCATION, R_INT}, &ActionMoveToLocation);
    insert(22, "ActionMoveToObject", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &ActionMoveToObject);
    insert(23, "ActionMoveAwayFromObject", R_VOID, {R_OBJECT, R_INT, R_FLOAT}, &ActionMoveAwayFromObject);
    insert(32, "ActionEquipItem", R_VOID, {R_OBJECT, R_INT, R_INT}, &ActionEquipItem);
    insert(33, "ActionUnequipItem", R_VOID, {R_OBJECT, R_INT}, &ActionUnequipItem);
    insert(34, "ActionPickUpItem", R_VOID, {R_OBJECT}, &ActionPickUpItem);
    insert(35, "ActionPutDownItem", R_VOID, {R_OBJECT}, &ActionPutDownItem);
    insert(37, "ActionAttack", R_VOID, {R_OBJECT, R_INT}, &ActionAttack);
    insert(39, "ActionSpeakString", R_VOID, {R_STRING, R_INT}, &ActionSpeakString);
    insert(40, "ActionPlayAnimation", R_VOID, {R_INT, R_FLOAT, R_FLOAT}, &ActionPlayAnimation);
    insert(43, "ActionOpenDoor", R_VOID, {R_OBJECT}, &ActionOpenDoor);
    insert(44, "ActionCloseDoor", R_VOID, {R_OBJECT}, &ActionCloseDoor);
    insert(48, "ActionCastSpellAtObject", R_VOID, {R_INT, R_OBJECT, R_INT, R_INT, R_INT, R_INT, R_INT}, &ActionCastSpellAtObject);
    insert(135, "ActionGiveItem", R_VOID, {R_OBJECT, R_OBJECT}, &ActionGiveItem);
    insert(136, "ActionTakeItem", R_VOID, {R_OBJECT, R_OBJECT}, &ActionTakeItem);
    insert(167, "ActionForceFollowObject", R_VOID, {R_OBJECT, R_FLOAT}, &ActionForceFollowObject);
    insert(196, "ActionJumpToObject", R_VOID, {R_OBJECT, R_INT}, &ActionJumpToObject);
    insert(202, "ActionWait", R_VOID, {R_FLOAT}, &ActionWait);
    insert(204, "ActionStartConversation", R_VOID, {R_OBJECT, R_STRING, R_INT, R_INT, R_INT, R_STRING, R_STRING, R_STRING, R_STRING, R_STRING, R_STRING, R_INT, R_INT, R_INT, R_INT}, &ActionStartConversation);
    insert(205, "ActionPauseConversation", R_VOID, {}, &ActionPauseConversation);
    insert(206, "ActionResumeConversation", R_VOID, {}, &ActionResumeConversation);
    insert(214, "ActionJumpToLocation", R_VOID, {R_LOCATION}, &ActionJumpToLocation);
    insert(234, "ActionCastSpellAtLocation", R_VOID, {R_INT, R_LOCATION, R_INT, R_INT, R_INT, R_INT}, &ActionCastSpellAtLocation);
    insert(240, "ActionSpeakStringByStrRef", R_VOID, {R_INT, R_INT}, &ActionSpeakStringByStrRef);
    insert(287, "ActionUseFeat", R_VOID, {R_INT, R_OBJECT}, &ActionUseFeat);
    insert(288, "ActionUseSkill", R_VOID, {R_INT, R_OBJECT, R_INT, R_OBJECT}, &ActionUseSkill);
    insert(294, "ActionDoCommand", R_VOID, {R_ACTION}, &ActionDoCommand);
    insert(309, "ActionUseTalentOnObject", R_VOID, {R_TALENT, R_OBJECT}, &ActionUseTalentOnObject);
    insert(310, "ActionUseTalentAtLocation", R_VOID, {R_TALENT, R_LOCATION}, &ActionUseTalentAtLocation);
    insert(329, "ActionInteractObject", R_VOID, {R_OBJECT}, &ActionInteractObject);
    insert(360, "ActionMoveAwayFromLocation", R_VOID, {R_LOCATION, R_INT, R_FLOAT}, &ActionMoveAwayFromLocation);
    insert(379, "ActionSurrenderToEnemies", R_VOID, {}, &ActionSurrenderToEnemies);
    insert(382, "ActionForceMoveToLocation", R_VOID, {R_LOCATION, R_INT, R_FLOAT}, &ActionForceMoveToLocation);
    insert(383, "ActionForceMoveToObject", R_VOID, {R_OBJECT, R_INT, R_FLOAT, R_FLOAT}, &ActionForceMoveToObject);
    insert(399, "ActionEquipMostDamagingMelee", R_VOID, {R_OBJECT, R_INT}, &ActionEquipMostDamagingMelee);
    insert(400, "ActionEquipMostDamagingRanged", R_VOID, {R_OBJECT}, &ActionEquipMostDamagingRanged);
    insert(404, "ActionEquipMostEffectiveArmor", R_VOID, {}, &ActionEquipMostEffectiveArmor);
    insert(483, "ActionUnlockObject", R_VOID, {R_OBJECT}, &ActionUnlockObject);
    insert(484, "ActionLockObject", R_VOID, {R_OBJECT}, &ActionLockObject);
    insert(501, "ActionCastFakeSpellAtObject", R_VOID, {R_INT, R_OBJECT, R_INT}, &ActionCastFakeSpellAtObject);
    insert(502, "ActionCastFakeSpellAtLocation", R_VOID, {R_INT, R_LOCATION, R_INT}, &ActionCastFakeSpellAtLocation);
    insert(700, "ActionBarkString", R_VOID, {R_INT}, &ActionBarkString);
    insert(730, "ActionFollowLeader", R_VOID, {}, &ActionFollowLeader);
    insert(843, "ActionFollowOwner", R_VOID, {R_FLOAT}, &ActionFollowOwner);
    insert(853, "ActionSwitchWeapons", R_VOID, {}, &ActionSwitchWeapons);
}

} // namespace game

} // namespace reone
