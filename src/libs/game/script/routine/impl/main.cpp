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
#include <array>
#include <functional>

#include "reone/game/action/attackobject.h"
#include "reone/game/action/docommand.h"
#include "reone/game/action/jumptolocation.h"
#include "reone/game/action/jumptoobject.h"
#include "reone/game/action/playanimation.h"
#include "reone/game/combatfeedback.h"
#include "reone/game/d20/feats.h"
#include "reone/game/d20/spells.h"
#include "reone/game/difficultyoptions.h"
#include "reone/game/effect/creaturestate.h"
#include "reone/game/effect/linkeffects.h"
#include "reone/game/effect/visual.h"
#include "reone/game/equipmentrules.h"
#include "reone/game/event.h"
#include "reone/game/forcerules.h"
#include "reone/game/game.h"
#include "reone/game/menupresentation.h"
#include "reone/game/object/areaofeffect.h"
#include "reone/game/object/door.h"
#include "reone/game/object/encounter.h"
#include "reone/game/object/placeable.h"
#include "reone/game/object/trigger.h"
#include "reone/game/reputes.h"
#include "reone/game/script/routine/argutil.h"
#include "reone/game/script/routine/context.h"
#include "reone/game/script/routine/objectutil.h"
#include "reone/game/script/routines.h"
#include "reone/game/shaperules.h"
#include "reone/game/talent.h"
#include "reone/game/twodautil.h"
#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/strings.h"
#include "reone/scene/collision.h"
#include "reone/script/executioncontext.h"
#include "reone/script/routine/exception/argument.h"
#include "reone/script/routine/exception/notimplemented.h"
#include "reone/script/variable.h"
#include "reone/system/logutil.h"
#include "reone/system/randomutil.h"

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

#ifdef _DEBUG
static constexpr bool kShipBuild = false;
#else
static constexpr bool kShipBuild = true;
#endif

using namespace reone::script;
using namespace reone::scene;

namespace reone {

namespace game {

// An optional creature argument: scripts pass OBJECT_INVALID for it, and a
// routine treats that like an object that is not a creature.
static std::shared_ptr<Creature> getCreatureOrNull(const std::vector<Variable> &args, int index, const RoutineContext &ctx) {
    auto object = getObjectOrNull(args, index, ctx);
    return object ? dyn_cast<Creature>(object) : nullptr;
}

// The metamagic answered outside a cast: none.
static constexpr int kNoMetaMagic = 255;

// An object that may be absent, as the rules take it: none for OBJECT_INVALID.
static std::optional<std::reference_wrapper<const Object>> getOptionalObject(const std::shared_ptr<Object> &object) {
    if (!object) {
        return std::nullopt;
    }
    return std::cref(*object);
}

static Variable Random(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nMaxInteger = getInt(args, 0);
    if (nMaxInteger <= 0) {
        return Variable::ofInt(0);
    }

    // Transform

    // Execute
    return Variable::ofInt(randomInt(0, nMaxInteger - 1));
}

static Variable PrintString(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sString = getString(args, 0);

    // Transform

    // Execute
    info(sString);
    return Variable::ofNull();
}

static Variable PrintFloat(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fFloat = getFloat(args, 0);
    auto nWidth = getIntOrElse(args, 1, 18);
    auto nDecimals = getIntOrElse(args, 2, 9);

    // Transform

    // Execute
    throw RoutineNotImplementedException("PrintFloat");
}

static Variable FloatToString(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fFloat = getFloat(args, 0);
    auto nWidth = getIntOrElse(args, 1, 18);
    auto nDecimals = getIntOrElse(args, 2, 9);

    // Transform

    // Execute
    return Variable::ofString(std::to_string(fFloat));
}

static Variable PrintInteger(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nInteger = getInt(args, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("PrintInteger");
}

static Variable PrintObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    throw RoutineNotImplementedException("PrintObject");
}

static Variable AssignCommand(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oActionSubject = getObject(args, 0, ctx);
    auto aActionToAssign = getAction(args, 1);

    // Transform

    // Execute
    // An assigned command runs in place as the subject, it is not itself
    // queued. What it contains decides what reaches the queue: an Action
    // routine inside it queues on the subject, anything else takes effect now.
    //
    // Queueing the command starves it behind whatever the subject is already
    // doing, and scripts rely on the difference. 103PER rolls the player
    // mid-run with AssignCommand(oPC, PlayOverlayAnimation(...)) while an
    // ActionMoveToLocation is still in flight, then queues a head turn behind
    // that move - a sequence that only reads correctly if the overlay lands
    // immediately and the head turn waits.
    //
    // The subject takes the command as it takes any event: a creature that has
    // not yet run its creation script runs it first, so what that script
    // queues comes before the command.
    if (auto creature = dyn_cast<Creature>(oActionSubject)) {
        creature->runSpawnScript();
    }
    runCommandAsActor(*aActionToAssign, *oActionSubject);
    return Variable::ofNull();
}

static Variable DelayCommand(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fSeconds = getFloat(args, 0);
    auto aActionToDelay = getAction(args, 1);

    // Transform

    // Execute
    auto commandAction = ctx.game.newAction<DoCommandAction>(std::move(aActionToDelay));
    getCaller(ctx)->delayAction(std::move(commandAction), fSeconds);
    return Variable::ofNull();
}

static Variable ExecuteScript(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sScript = getString(args, 0);
    auto oTarget = getObject(args, 1, ctx);
    auto nScriptVar = getIntOrElse(args, 2, -1);

    // Transform

    // Execute
    std::vector<script::Argument> scriptArgs;
    scriptArgs.emplace_back(script::ArgKind::Caller, Variable::ofObject(oTarget->id()));
    scriptArgs.emplace_back(script::ArgKind::ScriptVar, Variable::ofInt(nScriptVar));

    // Copy all arguments from the parent execution context. This essentially
    // extends lifetime of arguments to the callee script. For example, onNotice
    // scripts call ExecuteScript for k_ai_master, which in turn needs
    // GetLastPerception and other perception arguments.
    for (const Argument &origArg : ctx.execution.args) {
        scriptArgs.emplace_back(origArg);
    }

    ctx.game.scriptRunner().run(sScript, scriptArgs);

    return Variable::ofNull();
}

static Variable ClearAllActions(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    getCaller(ctx)->clearAllActions();
    return Variable::ofNull();
}

static Variable SetFacing(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fDirection = getFloat(args, 0);

    // Transform

    // Execute: a creature's model turns to the new facing, giving up any
    // object it kept turning toward.
    auto caller = getCaller(ctx);
    const float facing = objectFacingFromScript(glm::radians(fDirection));
    if (auto creature = dyn_cast<Creature>(caller)) {
        creature->setOrientationLock(script::kObjectInvalid);
        creature->turnTo(facing);
    } else {
        caller->setFacing(facing);
    }
    return Variable::ofNull();
}

static Variable SwitchPlayerCharacter(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);

    // Transform

    // Execute
    Party &party = ctx.game.party();

    // The canonical PC is not a roster entry: -1 asks for it back. Only an
    // available companion can take control, and asking for the actor already
    // in control changes nothing.
    const int previousNpc = party.controlledNpc();
    if (nNPC != kNpcPlayer && !party.isMemberAvailable(nNPC)) {
        return Variable::ofInt(0);
    }
    if (nNPC == previousNpc) {
        return Variable::ofInt(1);
    }
    // A companion giving up control stops being a player character, and its
    // record is saved as it stands.
    const auto previous = party.player();
    if (previousNpc != kNpcPlayer && previous) {
        previous->setPC(false);
        ctx.game.saveRosterState({RosterKind::Npc, previousNpc}, *previous);
    }
    std::shared_ptr<Creature> creature =
        nNPC == kNpcPlayer ? party.actualPlayer() : party.getAvailableMember(nNPC, true);
    if (!creature) {
        warn("Party: NPC not found: " + std::to_string(nNPC));
        return Variable::ofInt(0);
    }

    // An actor not yet in the world arrives where the one giving up control
    // stands; one already there stays where it is.
    auto currentLeader = party.getLeader();
    glm::vec3 position(currentLeader ? currentLeader->position() : creature->position());
    float facing = currentLeader ? currentLeader->getFacing() : creature->getFacing();

    auto module = ctx.game.module();
    auto area = module ? module->area() : nullptr;
    const bool resident = area && area->isObjectResident(*creature);

    // The actor giving up control leaves the world: a companion's creature is
    // destroyed, and the player character waits outside it until it takes
    // control again. Either way the events still pending for it are dropped.
    if (previous) {
        if (previousNpc != kNpcPlayer) {
            ctx.game.killRosterCreature({RosterKind::Npc, previousNpc});
        } else {
            if (module) {
                module->dropPendingEvents(*previous);
            }
            if (area) {
                area->retirePartyMemberAreaRuntime(previous);
            }
        }
    }
    // Every other companion travelling with the party leaves it, last first,
    // and its creature is taken out of the world. One the party refuses to
    // let go stays with it, and alive.
    std::vector<int> leaving;
    for (const auto &member : party.members()) {
        if (member.npc != kNpcPlayer && member.npc != nNPC && member.npc != party.controlledNpc()) {
            leaving.push_back(member.npc);
        }
    }
    for (auto npc = leaving.rbegin(); npc != leaving.rend(); ++npc) {
        party.removeMember(*npc);
        if (!party.isFollower(*npc)) {
            ctx.game.killRosterCreature({RosterKind::Npc, *npc});
        }
    }
    party.setControlledMember(nNPC, creature);

    if (area) {
        if (!resident) {
            area->placeControlledCreature(creature, position, facing);
        }
        area->onPartyLeaderMoved(true);
        area->update3rdPersonCameraFacing();
    }
    return Variable::ofInt(1);
}

static Variable SetTime(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nHour = getInt(args, 0);
    auto nMinute = getInt(args, 1);
    auto nSecond = getInt(args, 2);
    auto nMillisecond = getInt(args, 3);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SetTime");
}

static Variable SetPartyLeader(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);

    // Transform

    // Execute
    return Variable::ofInt(static_cast<int>(ctx.game.party().setPartyLeader(nNPC)));
}

static Variable SetAreaUnescapable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bUnescapable = getInt(args, 0);

    // Transform
    auto unescapable = static_cast<bool>(bUnescapable);

    // Execute
    ctx.game.module()->area()->setUnescapable(unescapable);
    return Variable::ofNull();
}

static Variable GetAreaUnescapable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    bool unescapable = ctx.game.module()->area()->isUnescapable();
    return Variable::ofInt(static_cast<int>(unescapable));
}

static Variable GetTimeHour(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("GetTimeHour");
}

static Variable GetTimeMinute(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("GetTimeMinute");
}

static Variable GetTimeSecond(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("GetTimeSecond");
}

static Variable GetTimeMillisecond(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("GetTimeMillisecond");
}

static Variable GetArea(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);

    // Transform

    // Execute
    auto area = ctx.game.module()->area();
    return Variable::ofObject(getObjectIdOrInvalid(area));
}

static Variable GetEnteringObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute: the object that last came into, or at, the caller; only the
    // module, areas, triggers, encounters, areas of effect, doors and
    // placeables keep one.
    const Variable *callerArg = ctx.execution.findArg(ArgKind::Caller);
    auto caller = callerArg ? ctx.game.getObjectById(callerArg->objectId) : nullptr;
    if (!caller) {
        return Variable::ofObject(kObjectInvalid);
    }
    switch (caller->type()) {
    case ObjectType::Module:
        return Variable::ofObject(static_cast<const Module &>(*caller).lastEnteredId());
    case ObjectType::Area:
        return Variable::ofObject(static_cast<const Area &>(*caller).lastEnteredId());
    case ObjectType::Trigger:
        return Variable::ofObject(static_cast<const Trigger &>(*caller).lastEnteredId());
    case ObjectType::Encounter:
        return Variable::ofObject(getObjectIdOrInvalid(static_cast<const Encounter &>(*caller).lastEntered()));
    case ObjectType::AreaOfEffect:
        return Variable::ofObject(getObjectIdOrInvalid(static_cast<const AreaOfEffect &>(*caller).lastEntered()));
    case ObjectType::Door:
        return Variable::ofObject(static_cast<const Door &>(*caller).lastEnteredId());
    case ObjectType::Placeable:
        return Variable::ofObject(static_cast<const Placeable &>(*caller).lastEnteredId());
    default:
        return Variable::ofObject(kObjectInvalid);
    }
}

static Variable GetExitingObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute: the object that last left the caller; only areas, triggers,
    // encounters and areas of effect keep one.
    const Variable *callerArg = ctx.execution.findArg(ArgKind::Caller);
    auto caller = callerArg ? ctx.game.getObjectById(callerArg->objectId) : nullptr;
    if (!caller) {
        return Variable::ofObject(kObjectInvalid);
    }
    switch (caller->type()) {
    case ObjectType::Area:
        return Variable::ofObject(static_cast<const Area &>(*caller).lastLeftId());
    case ObjectType::Trigger:
        return Variable::ofObject(static_cast<const Trigger &>(*caller).lastLeftId());
    case ObjectType::Encounter:
        return Variable::ofObject(getObjectIdOrInvalid(static_cast<const Encounter &>(*caller).lastLeft()));
    case ObjectType::AreaOfEffect:
        return Variable::ofObject(getObjectIdOrInvalid(static_cast<const AreaOfEffect &>(*caller).lastLeft()));
    default:
        return Variable::ofObject(kObjectInvalid);
    }
}

static Variable GetPosition(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);

    // Transform

    // Execute
    return Variable::ofVector(oTarget->position());
}

static Variable GetFacing(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);

    // Transform

    // Execute: counter-clockwise from +X, within [0, 360)
    float facing = std::fmod(glm::degrees(scriptFacingFromObject(oTarget->getFacing())) + 360.0f, 360.0f);
    return Variable::ofFloat(facing);
}

static Variable GetItemPossessor(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObjectOrNull(args, 0, ctx);

    // Transform
    auto item = oItem ? dyn_cast<Item>(oItem) : nullptr;

    // Execute: whoever holds the item (the player character for the party
    // inventory, or a container or store); none for an item on the ground.
    return Variable::ofObject(item ? item->owner() : kObjectInvalid);
}

static Variable GetItemPossessedBy(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);
    auto sItemTag = getString(args, 1);

    // Transform
    auto itemTag = boost::to_lower_copy(sItemTag);

    // Execute
    if (itemTag.empty()) {
        return Variable::ofObject(kObjectInvalid);
    }
    auto item = oObject->getItemByTag(itemTag);
    if (!item) {
        if (auto creature = dyn_cast<Creature>(oObject)) {
            for (auto &[slot, equippedItem] : creature->equipment()) {
                if (equippedItem->tag() == itemTag) {
                    item = equippedItem;
                }
            }
        }
    }

    return Variable::ofObject(getObjectIdOrInvalid(item));
}

static Variable CreateItemOnObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sItemTemplate = getString(args, 0);
    auto oTarget = getObjectOrNull(args, 1, ctx);
    auto nStackSize = getIntOrElse(args, 2, 1);
    auto nHideMessage = getIntOrElse(args, 3, 0);

    // Transform
    auto itemTemplate = boost::to_lower_copy(sItemTemplate);

    // Execute
    // Nothing is created without a target or a template, and only creatures,
    // placeables and stores receive the item. The stack holds at most as many
    // as the base item stacks, and nothing is created for a stack of none.
    const bool receives = oTarget && (oTarget->type() == ObjectType::Creature ||
                                      oTarget->type() == ObjectType::Placeable ||
                                      oTarget->type() == ObjectType::Store);
    if (!receives || itemTemplate.empty() ||
        !ctx.services.resource.gffs.get(itemTemplate, resource::ResType::Uti)) {
        return Variable::ofObject(kObjectInvalid);
    }
    auto item = ctx.game.newItemFromBlueprint(itemTemplate);
    const int stackSize = std::min(nStackSize, item->maxStackSize());
    if (stackSize <= 0) {
        ctx.game.destroyRuntimeObjectGraph(item);
        return Variable::ofObject(kObjectInvalid);
    }
    item->setStackSize(stackSize);
    item->setDropable(true);
    const std::string name = item->localizedName();
    // The object answered is the stack now holding the item, or none when the
    // item was counted instead of kept.
    auto held = ctx.game.party().sharedInventoryReceiver(oTarget)->addItem(item);
    // An item created on a party member is reported in the status summary,
    // unless the script hides it.
    if (ctx.game.party().isMember(*oTarget) && !nHideMessage) {
        ctx.game.submitStatusSummary(StatusSummaryCategory::ItemsReceived, 0, {name});
    }
    return Variable::ofObject(getObjectIdOrInvalid(held));
}

static Variable GetLastAttacker(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto recipient = getObjectOrCaller(args, 0, ctx);
    auto attacker = recipient ? recipient->savedReference("LastAttacker") : nullptr;
    return Variable::ofObject(getObjectIdOrInvalid(attacker));
}

// The three criteria pairs of the nearest creature searches: the first two
// arguments, then the pairs after the search centre and nNth.
static Area::SearchCriteriaList getCreatureSearchCriteria(const std::vector<Variable> &args) {
    return {
        {static_cast<CreatureType>(getInt(args, 0)), getInt(args, 1)},
        {static_cast<CreatureType>(getIntOrElse(args, 4, -1)), getIntOrElse(args, 5, -1)},
        {static_cast<CreatureType>(getIntOrElse(args, 6, -1)), getIntOrElse(args, 7, -1)}};
}

static Variable GetNearestCreature(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObjectOrCaller(args, 2, ctx);
    auto nNth = getIntOrElse(args, 3, 1);

    // Execute
    auto creature = ctx.game.module()->area()->getNearestCreature(oTarget, getCreatureSearchCriteria(args), nNth - 1);
    return Variable::ofObject(getObjectIdOrInvalid(creature));
}

static Variable GetDistanceToObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    auto caller = getCaller(ctx);
    return Variable::ofFloat(caller->getDistanceTo(*oObject));
}

static Variable GetIsObjectValid(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto object = getObjectOrNull(args, 0, ctx);
    return Variable::ofInt(static_cast<int>(static_cast<bool>(object)));
}

static Variable SetCameraFacing(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fDirection = getFloat(args, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SetCameraFacing");
}

static Variable PlaySound(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sSoundName = getString(args, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("PlaySound");
}

// The script's caller; none when it no longer exists.
static std::shared_ptr<Object> getCallerOrNull(const RoutineContext &ctx) {
    const auto caller = ctx.execution.findArg(ArgKind::Caller);
    return caller ? ctx.game.getObjectById(caller->objectId) : nullptr;
}

static Variable GetSpellTargetObject(const std::vector<Variable> &, const RoutineContext &ctx) {
    const auto caller = getCallerOrNull(ctx);
    const auto target = caller && (isa<Creature>(caller) || isa<Placeable>(caller))
                            ? caller->spellScriptContext().target()
                            : nullptr;
    return Variable::ofObject(target ? target->id() : script::kObjectInvalid);
}

static Variable GetCurrentHitPoints(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObjectOrCaller(args, 0, ctx);

    // Transform

    // Execute
    int hitPoints = oObject->currentHitPoints();
    return Variable::ofInt(hitPoints);
}

static Variable GetMaxHitPoints(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObjectOrCaller(args, 0, ctx);

    // Transform

    // Execute
    int hitPoints = oObject->maxHitPoints();
    return Variable::ofInt(hitPoints);
}

static Variable GetLastItemEquipped(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofObject(ctx.game.module()->itemEvents().equipped);
}

static Variable GetSubScreenID(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("GetSubScreenID");
}

static Variable CancelCombat(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto creature = getCreatureOrNull(args, 0, ctx);
    const int runEndRound = getIntOrElse(args, 1, 0);
    if (creature) {
        creature->cancelCombat(runEndRound);
    }
    return Variable::ofNull();
}

static Variable GetCurrentForcePoints(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto creature = getCreatureOrNull(args, 0, ctx);
    return Variable::ofInt(creature ? creature->currentForce() : 0);
}

static Variable GetMaxForcePoints(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto creature = getCreatureOrNull(args, 0, ctx);
    return Variable::ofInt(creature ? creature->maxForcePoints() : 0);
}

static Variable PauseGame(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bPause = getInt(args, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("PauseGame");
}

static Variable SetPlayerRestrictMode(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bRestrict = getInt(args, 0);

    // Transform
    auto restrict = static_cast<bool>(bRestrict);

    // Execute
    ctx.game.module()->player().setRestrictMode(restrict);
    if (auto area = ctx.game.module()->area()) {
        area->setPlayerRestrictMode(restrict);
    }
    return Variable::ofNull();
}

static Variable GetStringLength(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sString = getString(args, 0);

    // Transform

    // Execute
    return Variable::ofInt(static_cast<int>(sString.length()));
}

static Variable GetStringUpperCase(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sString = getString(args, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("GetStringUpperCase");
}

static Variable GetStringLowerCase(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sString = getString(args, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("GetStringLowerCase");
}

static Variable GetStringRight(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sString = getString(args, 0);
    auto nCount = getInt(args, 1);

    // Transform

    // Execute
    std::string right;
    if (sString.size() >= nCount) {
        right = sString.substr(sString.length() - nCount, nCount);
    }
    return Variable::ofString(std::move(right));
}

static Variable GetStringLeft(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sString = getString(args, 0);
    auto nCount = getInt(args, 1);

    // Transform

    // Execute
    std::string left;
    if (sString.size() >= nCount) {
        left = sString.substr(0, nCount);
    }
    return Variable::ofString(std::move(left));
}

static Variable InsertString(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sDestination = getString(args, 0);
    auto sString = getString(args, 1);
    auto nPosition = getInt(args, 2);

    // Transform

    // Execute
    throw RoutineNotImplementedException("InsertString");
}

static Variable GetSubString(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sString = getString(args, 0);
    auto nStart = getInt(args, 1);
    auto nCount = getInt(args, 2);

    // Transform

    // Execute
    // nCount is a character count, not an end position. Out-of-range requests
    // yield the empty string the declaration documents for an error, the way
    // GetStringLeft and GetStringRight already handle theirs. Widened so that
    // extreme arguments cannot overflow the range check.
    std::string substring;
    if (nStart >= 0 && nCount >= 0 &&
        static_cast<int64_t>(nStart) + nCount <= static_cast<int64_t>(sString.size())) {
        substring = sString.substr(nStart, nCount);
    }
    return Variable::ofString(std::move(substring));
}

static Variable FindSubString(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sString = getString(args, 0);
    auto sSubString = getString(args, 1);

    // Transform

    // Execute
    size_t pos = sString.find(sSubString);
    return Variable::ofInt(pos != std::string::npos ? static_cast<int>(pos) : -1);
}

// Script angles are in degrees. Out of an inverse function's domain, and for
// logarithms and roots of values that are not positive, the math routines
// answer 0.
static double degreesToRadians(float degrees) {
    return static_cast<double>(degrees) * glm::radians(1.0f);
}

static float radiansToDegrees(double radians) {
    return static_cast<float>(radians) * glm::degrees(1.0f);
}

static Variable fabs(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fValue = getFloat(args, 0);

    // Transform

    // Execute
    return Variable::ofFloat(std::fabs(fValue));
}

static Variable cos(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fValue = getFloat(args, 0);

    // Transform

    // Execute
    return Variable::ofFloat(static_cast<float>(std::cos(degreesToRadians(fValue))));
}

static Variable sin(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fValue = getFloat(args, 0);

    // Transform

    // Execute
    return Variable::ofFloat(static_cast<float>(std::sin(degreesToRadians(fValue))));
}

static Variable tan(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fValue = getFloat(args, 0);

    // Transform

    // Execute
    return Variable::ofFloat(static_cast<float>(std::tan(degreesToRadians(fValue))));
}

static Variable acos(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fValue = getFloat(args, 0);

    // Transform

    // Execute
    if (!(std::fabs(fValue) <= 1.0f)) {
        return Variable::ofFloat(0.0f);
    }
    return Variable::ofFloat(radiansToDegrees(std::acos(static_cast<double>(fValue))));
}

static Variable asin(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fValue = getFloat(args, 0);

    // Transform

    // Execute
    if (!(std::fabs(fValue) <= 1.0f)) {
        return Variable::ofFloat(0.0f);
    }
    return Variable::ofFloat(radiansToDegrees(std::asin(static_cast<double>(fValue))));
}

static Variable atan(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fValue = getFloat(args, 0);

    // Transform

    // Execute
    return Variable::ofFloat(radiansToDegrees(std::atan(static_cast<double>(fValue))));
}

static Variable log(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fValue = getFloat(args, 0);

    // Transform

    // Execute
    if (!(fValue > 0.0f)) {
        return Variable::ofFloat(0.0f);
    }
    return Variable::ofFloat(static_cast<float>(std::log(static_cast<double>(fValue))));
}

static Variable pow(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fValue = getFloat(args, 0);
    auto fExponent = getFloat(args, 1);

    // Transform

    // Execute
    // Zero has no negative power.
    if (fValue == 0.0f && !(fExponent >= 0.0f)) {
        return Variable::ofFloat(0.0f);
    }
    return Variable::ofFloat(static_cast<float>(std::pow(static_cast<double>(fValue), static_cast<double>(fExponent))));
}

static Variable sqrt(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fValue = getFloat(args, 0);

    // Transform

    // Execute
    if (!(fValue > 0.0f)) {
        return Variable::ofFloat(0.0f);
    }
    return Variable::ofFloat(std::sqrt(fValue));
}

static Variable abs(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nValue = getInt(args, 0);

    // Transform

    // Execute
    return Variable::ofInt(std::abs(nValue));
}

static Variable GetPlayerRestrictMode(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObjectOrCaller(args, 0, ctx);

    // Transform

    // Execute
    bool restrict = ctx.game.module()->player().isRestrictMode();
    return Variable::ofInt(static_cast<int>(restrict));
}

static Variable GetCasterLevel(const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto object = getObjectOrNull(args, 0, ctx);
    if (!object) {
        return Variable::ofInt(0);
    }
    return Variable::ofInt(queryCasterLevel(*object));
}

static Variable GetFirstEffect(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto object = getObjectOrNull(args, 0, ctx);
    return Variable::ofEffect(object ? object->getFirstEffect() : nullptr);
}

static Variable GetNextEffect(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto object = getObjectOrNull(args, 0, ctx);
    return Variable::ofEffect(object ? object->getNextEffect() : nullptr);
}

static Variable RemoveEffect(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto object = getObjectOrNull(args, 0, ctx);
    auto effect = getEffect(args, 1);
    if (object)
        object->queueScriptEffectRemoval(ScriptEffectRemovalMatch::PackageId,
                                         effect->saveFacingInstance());
    return Variable::ofNull();
}

static Variable GetIsEffectValid(const std::vector<Variable> &args, const RoutineContext &ctx) {
    bool valid;
    try {
        auto eEffect = getEffect(args, 0);
        valid = static_cast<bool>(eEffect);
    } catch (const RoutineArgumentException &ignored) {
        valid = false;
    }
    return Variable::ofInt(static_cast<int>(valid));
}

static Variable GetEffectDurationType(const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto record = getEffect(args, 0)->saveFacingInstance();
    return Variable::ofInt(static_cast<int>(record.durationType()));
}

static Variable GetEffectSubType(const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto record = getEffect(args, 0)->saveFacingInstance();
    return Variable::ofInt(static_cast<int>(record.category()));
}

static Variable GetEffectCreator(const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto record = getEffect(args, 0)->saveFacingInstance();
    return Variable::ofObject(record.creatorId);
}

static Variable IntToString(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nInteger = getInt(args, 0);

    // Transform

    // Execute
    return Variable::ofString(std::to_string(nInteger));
}

struct ObjectsInArea : public EngineType {
    std::vector<uint32_t> objects;
    uint32_t area {script::kObjectInvalid};
    int objectFilter {0};
};

// An invalid area makes an area routine do nothing.
static std::shared_ptr<Area> getAreaOrNull(const std::vector<Variable> &args, int index, const RoutineContext &ctx) {
    auto object = getObjectOrNull(args, index, ctx);
    return object ? dyn_cast<Area>(object) : nullptr;
}

static std::shared_ptr<Area> getAreaOrCurrent(const std::vector<Variable> &args, int index, const RoutineContext &ctx) {
    auto module = ctx.game.module();
    auto currentArea = module ? module->area() : nullptr;
    if (index < 0 || index >= args.size()) {
        return currentArea;
    }
    if (args[index].type != VariableType::Object) {
        throw RoutineArgumentException("Expected object argument");
    }
    if (args[index].objectId == script::kObjectInvalid) {
        return currentArea;
    }

    return dyn_cast<Area>(getObject(args, index, ctx));
}

static bool matchesObjectFilter(const Object &object, int objectFilter) {
    return static_cast<int>(object.type()) & objectFilter;
}

static Variable GetFirstObjectInArea(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getAreaOrCurrent(args, 0, ctx);
    auto nObjectFilter = getIntOrElse(args, 1, 1);

    // Transform

    // Execute
    if (!oArea) {
        return Variable::ofObject(script::kObjectInvalid);
    }

    SmallVector<uint32_t, 16> foundObjects;
    for (auto it = oArea->objects().rbegin(), end = oArea->objects().rend(); it != end; ++it) {
        const std::shared_ptr<Object> &object = *it;
        if (!matchesObjectFilter(*object, nObjectFilter)) {
            continue;
        }
        foundObjects.push_back(object->id());
    }

    if (foundObjects.empty()) {
        return Variable::ofObject(script::kObjectInvalid);
    }

    uint32_t firstObject = foundObjects.back();
    foundObjects.resize(foundObjects.size() - 1);
    if (foundObjects.empty()) {
        return Variable::ofObject(firstObject);
    }

    auto objectsArg = std::make_shared<ObjectsInArea>();
    objectsArg->objects.reserve(foundObjects.size());
    for (uint32_t object : foundObjects) {
        objectsArg->objects.push_back(object);
    }
    objectsArg->area = oArea->id();
    objectsArg->objectFilter = nObjectFilter;

    ctx.execution.args.push_back(
        Argument(ArgKind::ObjectsInArea, Variable::ofCustom(objectsArg)));

    return Variable::ofObject(firstObject);
}

static Variable GetNextObjectInArea(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getAreaOrCurrent(args, 0, ctx);
    auto nObjectFilter = getIntOrElse(args, 1, 1);

    // Transform

    // Execute
    if (!oArea) {
        return Variable::ofObject(script::kObjectInvalid);
    }

    auto &scriptArgs = ctx.execution.args;
    for (auto it = scriptArgs.rbegin(), end = scriptArgs.rend(); it != end; ++it) {
        if (it->kind != ArgKind::ObjectsInArea) {
            continue;
        }
        assert(it->var.type == VariableType::Custom);

        auto objects = std::static_pointer_cast<ObjectsInArea>(it->var.engineType);
        if (objects->area != oArea->id() || objects->objectFilter != nObjectFilter) {
            continue;
        }

        if (objects->objects.empty()) {
            return Variable::ofObject(script::kObjectInvalid);
        }

        uint32_t nextObject = objects->objects.back();
        objects->objects.pop_back();
        return Variable::ofObject(nextObject);
    }

    return Variable::ofObject(script::kObjectInvalid);
}

static Variable d2(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNumDice = getIntOrElse(args, 0, 1);

    // Transform
    auto numDice = std::max(1, nNumDice);

    // Execute
    int total = 0;
    for (int i = 0; i < numDice; ++i) {
        total += reone::randomInt(1, 2);
    }
    return Variable::ofInt(total);
}

static Variable d3(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNumDice = getIntOrElse(args, 0, 1);

    // Transform
    auto numDice = std::max(1, nNumDice);

    // Execute
    int total = 0;
    for (int i = 0; i < numDice; ++i) {
        total += reone::randomInt(1, 3);
    }
    return Variable::ofInt(total);
}

static Variable d4(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNumDice = getIntOrElse(args, 0, 1);

    // Transform
    auto numDice = std::max(1, nNumDice);

    // Execute
    int total = 0;
    for (int i = 0; i < numDice; ++i) {
        total += reone::randomInt(1, 4);
    }
    return Variable::ofInt(total);
}

static Variable d6(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNumDice = getIntOrElse(args, 0, 1);

    // Transform
    auto numDice = std::max(1, nNumDice);

    // Execute
    int total = 0;
    for (int i = 0; i < numDice; ++i) {
        total += reone::randomInt(1, 6);
    }
    return Variable::ofInt(total);
}

static Variable d8(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNumDice = getIntOrElse(args, 0, 1);

    // Transform
    auto numDice = std::max(1, nNumDice);

    // Execute
    int total = 0;
    for (int i = 0; i < numDice; ++i) {
        total += reone::randomInt(1, 8);
    }
    return Variable::ofInt(total);
}

static Variable d10(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNumDice = getIntOrElse(args, 0, 1);

    // Transform
    auto numDice = std::max(1, nNumDice);

    // Execute
    int total = 0;
    for (int i = 0; i < numDice; ++i) {
        total += reone::randomInt(1, 10);
    }
    return Variable::ofInt(total);
}

static Variable d12(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNumDice = getIntOrElse(args, 0, 1);

    // Transform
    auto numDice = std::max(1, nNumDice);

    // Execute
    int total = 0;
    for (int i = 0; i < numDice; ++i) {
        total += reone::randomInt(1, 12);
    }
    return Variable::ofInt(total);
}

static Variable d20(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNumDice = getIntOrElse(args, 0, 1);

    // Transform
    auto numDice = std::max(1, nNumDice);

    // Execute
    int total = 0;
    for (int i = 0; i < numDice; ++i) {
        total += reone::randomInt(1, 20);
    }
    return Variable::ofInt(total);
}

static Variable d100(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNumDice = getIntOrElse(args, 0, 1);

    // Transform
    auto numDice = std::max(1, nNumDice);

    // Execute
    int total = 0;
    for (int i = 0; i < numDice; ++i) {
        total += reone::randomInt(1, 100);
    }
    return Variable::ofInt(total);
}

static Variable VectorMagnitude(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vVector = getVector(args, 0);

    // Transform

    // Execute
    return Variable::ofFloat(glm::length(vVector));
}

static Variable GetMetaMagicFeat(const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto caller = ctx.execution.findArg(ArgKind::Caller);
    const auto object = caller ? ctx.game.getObjectById(caller->objectId) : nullptr;
    return Variable::ofInt(object ? object->spellCastContext().metaMagic : kNoMetaMagic);
}

static Variable GetObjectType(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);

    // Transform

    // Execute
    return Variable::ofInt(static_cast<int>(oTarget->type()));
}

static Variable GetRacialType(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    return Variable::ofInt(static_cast<int>(creature->racialType()));
}

// A script's saving throw is reported to the saver and to the creature it is
// made against.
static SavingThrowResult rollReportedSavingThrow(const RoutineContext &ctx, const Creature &creature,
                                                 SavingThrow save, int dc, SavingThrowType subtype,
                                                 std::optional<std::reference_wrapper<const Object>> versus) {
    const auto breakdown = creature.getSavingThrowBreakdown(save, subtype, versus);
    const int roll = randomInt(1, 20);
    addSavingThrowFeedback(ctx.game, creature, versus, save, breakdown, roll, dc);
    return creature.getSavingThrowResult(roll + breakdown.total(), dc, subtype, versus);
}

static Variable rollScriptSavingThrow(
    SavingThrow save, const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto creature = getCreatureOrNull(args, 0, ctx);
    if (!creature) {
        return Variable::ofInt(0);
    }
    const int dc = getInt(args, 1);
    const auto subtype = static_cast<SavingThrowType>(getIntOrElse(args, 2, 0));
    auto versus = args.size() > 3 ? getObjectOrNull(args, 3, ctx) : getCaller(ctx);
    return Variable::ofInt(static_cast<int>(rollReportedSavingThrow(ctx, *creature, save, dc, subtype, getOptionalObject(versus))));
}

static Variable FortitudeSave(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return rollScriptSavingThrow(SavingThrow::Fortitude, args, ctx);
}
static Variable ReflexSave(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return rollScriptSavingThrow(SavingThrow::Reflex, args, ctx);
}
static Variable WillSave(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return rollScriptSavingThrow(SavingThrow::Will, args, ctx);
}

static Variable GetSpellSaveDC(const std::vector<Variable> &args, const RoutineContext &ctx) {
    constexpr int kInvalidCasterDC = 14;
    const Variable *caller = ctx.execution.findArg(ArgKind::Caller);
    if (!caller) {
        return Variable::ofInt(kInvalidCasterDC);
    }

    auto object = ctx.game.getObjectById(caller->objectId);
    // An area of effect answers with its creator's DC for its spell, or with
    // the DC it was given when the creator is gone.
    if (auto *areaOfEffect = object ? dyn_cast<AreaOfEffect>(object.get()) : nullptr) {
        auto creator = std::dynamic_pointer_cast<Creature>(areaOfEffect->creator());
        return Variable::ofInt(creator
                                   ? creator->getSpellSaveDC(static_cast<int>(areaOfEffect->effectSpellId()))
                                   : areaOfEffect->spellSaveDC());
    }
    auto *creature = object ? dyn_cast<Creature>(object.get()) : nullptr;
    const int spellId = object ? object->spellCastContext().spellId : -1;
    return Variable::ofInt(
        creature ? creature->getSpellSaveDC(spellId) : kInvalidCasterDC);
}

static Variable withEffectCategory(const std::vector<Variable> &args, EffectCategory category) {
    auto effect = getEffect(args, 0)->cloneEffect();
    effect->setCategory(category);
    return Variable::ofEffect(std::move(effect));
}

static Variable MagicalEffect(const std::vector<Variable> &args, const RoutineContext &) {
    return withEffectCategory(args, EffectCategory::Magical);
}

static Variable SupernaturalEffect(const std::vector<Variable> &args, const RoutineContext &) {
    return withEffectCategory(args, EffectCategory::Supernatural);
}

static Variable ExtraordinaryEffect(const std::vector<Variable> &args, const RoutineContext &) {
    return withEffectCategory(args, EffectCategory::Extraordinary);
}

static Variable GetAC(const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto object = getObjectOrNull(args, 0, ctx);
    if (!object) {
        return Variable::ofInt(-1);
    }
    if (const auto *creature = dyn_cast<Creature>(object.get())) {
        return Variable::ofInt(creature->getDefense());
    }
    switch (object->type()) {
    case ObjectType::Item:
    case ObjectType::Door:
    case ObjectType::Placeable:
        return Variable::ofInt(0);
    default:
        return Variable::ofInt(-1);
    }
}

static Variable RoundsToSeconds(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nRounds = getInt(args, 0);

    // Transform

    // Execute
    return Variable::ofFloat(nRounds / 6.0f);
}

static Variable HoursToSeconds(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nHours = getInt(args, 0);

    // Transform

    // Execute
    return Variable::ofInt(nHours * 3600);
}

static Variable TurnsToSeconds(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nTurns = getInt(args, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("TurnsToSeconds");
}

static Variable SoundObjectSetFixedVariance(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oSound = getObject(args, 0, ctx);
    auto fFixedVariance = getFloat(args, 1);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SoundObjectSetFixedVariance");
}

static Variable GetGoodEvilValue(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto creature = getCreatureOrNull(args, 0, ctx);

    // Execute: anything but a creature reads -1.
    return Variable::ofInt(creature ? creature->goodEvil() : -1);
}

static Variable GetPartyMemberCount(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofInt(ctx.game.party().getSize());
}

static Variable GetAlignmentGoodEvil(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto creature = getCreatureOrNull(args, 0, ctx);

    // Execute: anything but a creature reads -1.
    return Variable::ofInt(creature ? static_cast<int>(creature->alignment()) : -1);
}

struct ShapeParams {
    float size;
    float sizeSquared;
    glm::vec3 target;
};

/** The radius of a spell cylinder about its axis. */
static constexpr float kSpellCylinderRadius = 1.5f;

static bool matchSphere(
    const glm::vec3 &position,
    const ShapeParams &params) {

    return glm::length2(params.target - position) <= params.sizeSquared;
}

static bool matchCube(
    const glm::vec3 &position,
    const ShapeParams &params) {

    glm::vec3 distance = glm::abs(params.target - position);
    return distance.x <= params.size &&
           distance.y <= params.size &&
           distance.z <= params.size;
}

static glm::vec3 getShapeDirection(
    const glm::vec3 &origin,
    const glm::vec3 &target) {
    return shape::normalize(target - origin);
}

static bool matchSpellCylinder(
    const glm::vec3 &position,
    const ShapeParams &params,
    const glm::vec3 &origin) {

    glm::vec3 relative = position - origin;
    if (glm::length2(relative) > params.sizeSquared) {
        return false;
    }

    glm::vec3 direction = getShapeDirection(origin, params.target);
    if (!(glm::dot(direction, shape::normalize(relative)) >= 0.0f)) {
        return false;
    }

    glm::vec3 projected = shape::project(origin, params.target, position);
    return glm::length2(position - projected) <= kSpellCylinderRadius * kSpellCylinderRadius;
}

static ShapeParams getShapeParams(
    float size,
    const std::shared_ptr<Location> &target) {

    ShapeParams params;
    params.size = size;
    params.sizeSquared = size * size;
    params.target = target->position();
    return params;
}

static glm::vec3 getShapeOrigin(
    Shape shape,
    const glm::vec3 &supplied,
    const RoutineContext &ctx) {

    switch (shape) {
    case Shape::SpellCylinder:
    case Shape::SpellCone: {
        return getCaller(ctx)->position();
    }
    case Shape::Cone:
        return supplied;
    case Shape::Sphere:
    case Shape::Cube:
        return glm::vec3(0.0f);
    }
    throw RoutineArgumentException("Invalid shape");
}

static bool matchesShape(
    Shape shape,
    const glm::vec3 &position,
    const ShapeParams &params,
    const glm::vec3 &origin) {

    switch (shape) {
    case Shape::Sphere:
        return matchSphere(position, params);
    case Shape::Cube:
        return matchCube(position, params);
    case Shape::SpellCylinder:
        return matchSpellCylinder(position, params, origin);
    case Shape::Cone:
        return shape::matchCone(position, params.target, origin, params.size);
    case Shape::SpellCone:
        return shape::matchSpellCone(position, params.target, origin, params.size);
    }
    throw RoutineArgumentException("Invalid shape");
}

static Variable getObjectInShape(
    const std::vector<Variable> &args, const RoutineContext &ctx, bool first) {
    int shapeValue = getInt(args, 0);
    float size = getFloat(args, 1);
    auto target = getLocationArgument(args, 2);
    int lineOfSight = getIntOrElse(args, 3, 0);
    int objectFilter = getIntOrElse(args, 4, 1);
    glm::vec3 suppliedOrigin = getVectorOrElse(args, 5, glm::vec3(0.0f));
    (void)lineOfSight; // The argument is accepted but does not affect filtering.

    // A shape number past the spell cone is tested as a sphere.
    auto shape = shapeValue >= static_cast<int>(Shape::SpellCylinder) && shapeValue <= static_cast<int>(Shape::SpellCone)
                     ? static_cast<Shape>(shapeValue)
                     : Shape::Sphere;
    ShapeParams params = getShapeParams(size, target);
    glm::vec3 origin = getShapeOrigin(shape, suppliedOrigin, ctx);
    auto area = ctx.game.module()->area();

    float minX = params.target.x - size;
    float maxX = params.target.x + size;
    if (shape == Shape::Cone || shape == Shape::SpellCone) {
        minX = std::min(origin.x, params.target.x) - size;
        maxX = std::max(origin.x, params.target.x) + size;
    } else if (shape == Shape::SpellCylinder) {
        glm::vec3 direction = shape::normalize(params.target - origin);
        params.target = origin + direction * size;
        const float sideX = -direction.y * kSpellCylinderRadius;
        const float corners[] = {origin.x + sideX, origin.x - sideX,
                                 params.target.x + sideX, params.target.x - sideX};
        minX = *std::min_element(std::begin(corners), std::end(corners));
        maxX = *std::max_element(std::begin(corners), std::end(corners));
    }

    // OBJECT_TYPE_ALL takes every object; any other filter takes the placed
    // object types it names, creature to sound.
    const int all = static_cast<int>(ObjectType::All);
    auto object = area->getObjectInShape(first, minX, maxX, [&](const Object &entry) {
        const int type = static_cast<int>(entry.type());
        const bool placedType = type >= static_cast<int>(ObjectType::Creature) && type <= static_cast<int>(ObjectType::Sound);
        return ((placedType && (type & objectFilter) != 0) || (objectFilter & all) == all) &&
               matchesShape(shape, entry.position(), params, origin);
    });
    return Variable::ofObject(object ? object->get().id() : script::kObjectInvalid);
}

static Variable GetFirstObjectInShape(
    const std::vector<Variable> &args, const RoutineContext &ctx) {
    return getObjectInShape(args, ctx, true);
}

static Variable GetNextObjectInShape(
    const std::vector<Variable> &args, const RoutineContext &ctx) {
    return getObjectInShape(args, ctx, false);
}

static Variable SignalEvent(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto object = getObjectOrNull(args, 0, ctx);
    // An event for an object that does not exist is dropped and the script
    // goes on. A caller that no longer exists sends it as no one.
    if (object) {
        std::optional<std::reference_wrapper<const Object>> caller;
        if (auto callerObject = getCallerOrNull(ctx)) {
            caller = *callerObject;
        }
        ctx.game.queueScriptEvent(*object, caller, *getEvent(args, 1));
    }
    return Variable::ofNull();
}

static Variable EventUserDefined(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return Variable::ofEvent(ctx.game.newEvent(static_cast<int>(SavedSignalEventType::UserDefined),
                                               std::vector<int32_t> {getInt(args, 0)}, std::vector<float> {},
                                               std::vector<std::string> {}, std::vector<std::shared_ptr<Object>> {}));
}

static Variable VectorNormalize(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vVector = getVector(args, 0);

    // Transform

    // Execute
    return Variable::ofVector(glm::normalize(vVector));
}

static Variable GetItemStackSize(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObjectOrNull(args, 0, ctx);

    // Transform
    auto item = std::dynamic_pointer_cast<Item>(oItem);

    // Execute
    // Anything but an item has no stack.
    return Variable::ofInt(item ? item->stackSize() : 0);
}

static Variable GetAbilityScore(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);
    auto nAbilityType = getInt(args, 1);

    // Transform
    auto creature = checkCreature(oCreature);
    auto ability = static_cast<Ability>(nAbilityType);

    // Execute
    return Variable::ofInt(creature->getEffectiveAbilityScore(ability));
}

static Variable GetIsDead(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    return Variable::ofInt(static_cast<int>(creature->isDead()));
}

static Variable PrintVector(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vVector = getVector(args, 0);
    auto bPrepend = getInt(args, 1);

    // Transform

    // Execute
    throw RoutineNotImplementedException("PrintVector");
}

static Variable Vector(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto x = getFloatOrElse(args, 0, 0.0f);
    auto y = getFloatOrElse(args, 1, 0.0f);
    auto z = getFloatOrElse(args, 2, 0.0f);

    // Transform

    // Execute
    return Variable::ofVector(glm::vec3(x, y, z));
}

static Variable SetFacingPoint(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vTarget = getVector(args, 0);

    // Transform

    // Execute: a creature gives up any object it kept turning toward.
    auto caller = getCaller(ctx);
    if (auto creature = dyn_cast<Creature>(caller)) {
        creature->setOrientationLock(script::kObjectInvalid);
        creature->turnToward(vTarget);
    } else {
        caller->face(vTarget);
    }
    return Variable::ofNull();
}

static Variable AngleToVector(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fAngle = getFloat(args, 0);

    // Transform

    // Execute
    const double radians = degreesToRadians(fAngle);
    auto vector = glm::vec3(static_cast<float>(std::cos(radians)), static_cast<float>(std::sin(radians)), 0.0f);
    return Variable::ofVector(std::move(vector));
}

static Variable VectorToAngle(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vVector = getVector(args, 0);

    // Transform

    // Execute
    // The angle comes back as the arc cosine of the unit vector's x in
    // radians, taken from 360 when y is negative. A vector too short to have a
    // direction points along x.
    const float length = glm::length(vVector);
    const glm::vec3 direction = length < 1e-9f ? glm::vec3(1.0f, 0.0f, 0.0f) : vVector * (1.0f / length);
    const float angle = static_cast<float>(std::acos(static_cast<double>(direction.x)));
    return Variable::ofFloat(direction.y < 0.0f ? 360.0f - angle : angle);
}

static constexpr int kStrRefTouchAttack = 1405;
static constexpr int kStrRefTouchAttackRanged = 1410;
static constexpr int kStrRefTouchAttackMelee = 1411;
static constexpr int kStrRefTouchAttackHit = 1371;
static constexpr int kStrRefTouchAttackMiss = 1373;
static constexpr int kStrRefTouchAttackCritical = 1390;
static constexpr int kStrRefUnnamedTrigger = 1421;
static constexpr int kStrRefUnknownObjectTSL = 649;

// The name a feedback line shows for an object. A placeable without a name
// takes its appearance's, a trigger without one a generic name, and objects
// of other types have none, so the line shows the unknown-object text.
static std::string getFeedbackObjectName(const Object &object, const RoutineContext &ctx) {
    auto &strings = ctx.services.resource.strings;
    switch (object.type()) {
    case ObjectType::Creature:
    case ObjectType::Door:
    case ObjectType::Item:
        return object.name();
    case ObjectType::Placeable: {
        if (!object.name().empty()) {
            return object.name();
        }
        auto placeables = getRequiredTwoDA(ctx.services.resource.twoDas, "placeables");
        const auto &placeable = static_cast<const Placeable &>(object);
        return strings.getText(placeables->getInt(placeable.appearance(), "strref", -1));
    }
    case ObjectType::Trigger:
        return object.name().empty() ? strings.getText(kStrRefUnnamedTrigger) : object.name();
    default:
        return strings.getText(ctx.game.isTSL() ? kStrRefUnknownObjectTSL : 0);
    }
}

// Returns 0 for a miss or a deflection, 1 for a hit and 2 for a critical hit.
// The roll starts no action and deals no damage. Doors and placeables are
// always hit.
static Variable touchAttack(const std::vector<Variable> &args, const RoutineContext &ctx, bool ranged) {
    auto target = getObjectOrNull(args, 0, ctx);
    const int displayFeedback = getIntOrElse(args, 1, 1);
    uint32_t callerId = kObjectInvalid;
    if (const Variable *callerArg = ctx.execution.findArg(ArgKind::Caller)) {
        callerId = callerArg->objectId;
    }
    auto caller = ctx.game.getObjectById(callerId);
    if (!target || !caller) {
        return Variable::ofInt(0);
    }

    int result = 0;
    int attackBonus = 0;
    int roll = 0;
    auto deflection = AttackResultType::Invalid;
    auto *attacker = dyn_cast<Creature>(caller.get());
    auto *defender = dyn_cast<Creature>(target.get());
    if (attacker && !defender) {
        result = target->type() == ObjectType::Door || target->type() == ObjectType::Placeable ? 1 : 0;
        // The line reports the result as the bonus and 9 as the roll.
        attackBonus = result;
        roll = 9;
    } else if (attacker) {
        attackBonus = attacker->getTouchAttackBonus(ranged);
        const auto weapon = attacker->getEquippedItem(InventorySlots::rightWeapon);
        const int damageFlags = weapon ? weapon->damageFlags() : static_cast<int>(DamageType::Bludgeoning);
        const int defense = defender->getDefenseBreakdown(*attacker, damageFlags, true).total;
        roll = randomInt(1, 20);
        // Only while the caller's round record is a ranged one can the
        // target deflect, against that record's own last roll.
        deflection = attacker->resolveTouchDeflection(*defender);
        if (deflection == AttackResultType::Invalid && roll != 1 &&
            (roll + attackBonus >= defense || roll == 20)) {
            result = 1;
            // Only a natural 20 threatens.
            if (roll == 20 && randomInt(1, 20) + attackBonus >= defense) {
                result = 2;
            }
        }
    }
    if (displayFeedback != 1) {
        return Variable::ofInt(result);
    }

    // The line is sent to the caller and to the target; each shows it only
    // while it is the controlled creature.
    auto leader = ctx.game.party().getLeader();
    const int recipients = static_cast<int>(leader && leader == caller) + static_cast<int>(leader && leader == target);
    if (recipients == 0) {
        return Variable::ofInt(result);
    }
    // A deflection shows no result text.
    const std::string resultText = result == 2   ? ctx.game.getFeedbackText(kStrRefTouchAttackCritical)
                                   : result == 1 ? ctx.game.getFeedbackText(kStrRefTouchAttackHit)
                                   : deflection == AttackResultType::Invalid
                                       ? ctx.game.getFeedbackText(kStrRefTouchAttackMiss)
                                       : std::string();
    const std::string text = ctx.game.getFeedbackText(
        kStrRefTouchAttack,
        {
            {0, getFeedbackObjectName(*caller, ctx)},
            {1, ctx.game.getFeedbackText(ranged ? kStrRefTouchAttackRanged : kStrRefTouchAttackMelee)},
            {2, getFeedbackObjectName(*target, ctx)},
            {3, resultText},
            {4, std::to_string(roll)},
            {5, attackBonus < 0 ? "-" : "+"},
            {6, std::to_string(std::abs(attackBonus))},
            {7, std::to_string(roll + attackBonus)},
            {8, ""},
        });
    // The line is not highlighted; TSL writes it to the combat list.
    for (int i = 0; i < recipients; ++i)
        ctx.game.messageLog().add(MessageLog::kFeedbackMessageType, MessageLog::Style::Normal, text,
                                  MessageLog::Buffer::Combat);
    return Variable::ofInt(result);
}

static Variable TouchAttackMelee(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return touchAttack(args, ctx, false);
}

static Variable TouchAttackRanged(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return touchAttack(args, ctx, true);
}

static Variable SetItemStackSize(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObjectOrNull(args, 0, ctx);
    auto nStackSize = getInt(args, 1);

    // Transform
    auto item = std::dynamic_pointer_cast<Item>(oItem);

    // Execute
    if (!item) {
        return Variable::ofNull();
    }
    // The size is kept within the base item's stack; an unchanged size does
    // nothing. An item that grows is reported as received, one that shrinks
    // as lost; TSL reports only a party member's items. The possessor, when
    // the player controls it, is told the item grew or shrank.
    static constexpr int kStackGrewStrRef = 1449;
    static constexpr int kStackShrankStrRef = 1450;
    const int size = std::max(1, std::min(nStackSize, item->maxStackSize()));
    const int previous = item->stackSize();
    if (size == previous) {
        return Variable::ofNull();
    }
    item->setStackSize(size);
    auto possessor = ctx.game.getObjectById(item->owner());
    if (!ctx.game.isTSL() || (possessor && ctx.game.party().isMember(*possessor))) {
        if (possessor && possessor == ctx.game.party().getLeader()) {
            ctx.game.addFeedbackMessage(size > previous ? kStackGrewStrRef : kStackShrankStrRef, {{0, item->localizedName()}});
        }
        if (size > previous) {
            ctx.game.submitStatusSummary(StatusSummaryCategory::ItemsReceived, 0, {item->localizedName()});
        } else {
            ctx.game.submitStatusSummary(StatusSummaryCategory::ItemsLost);
        }
    }
    return Variable::ofNull();
}

static Variable GetDistanceBetween(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObjectA = getObject(args, 0, ctx);
    auto oObjectB = getObject(args, 1, ctx);

    // Transform

    // Execute
    return Variable::ofFloat(oObjectA->getDistanceTo(*oObjectB));
}

static Variable SetReturnStrref(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bShow = getInt(args, 0);
    auto srStringRef = getIntOrElse(args, 1, 0);
    auto srReturnQueryStrRef = getIntOrElse(args, 2, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SetReturnStrref");
}

static Variable GetItemInSlot(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nInventorySlot = getInt(args, 0);
    auto oCreature = getObjectOrNull(args, 1, ctx);

    // Transform
    auto creature = std::dynamic_pointer_cast<Creature>(oCreature);

    // Execute
    // Only a creature has anything in a slot.
    auto item = creature ? creature->getEquippedItem(nInventorySlot) : nullptr;
    return Variable::ofObject(getObjectIdOrInvalid(item));
}

static Variable SetGlobalString(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sIdentifier = getString(args, 0);
    auto sValue = getString(args, 1);

    // Transform

    // Execute
    ctx.game.setGlobalString(sIdentifier, sValue);
    return Variable::ofNull();
}

static Variable SetCommandable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bCommandable = getInt(args, 0);
    auto oTarget = getObjectOrCaller(args, 1, ctx);

    // Transform
    bool commandable = static_cast<bool>(bCommandable);

    // Execute
    oTarget->setCommandable(commandable);
    return Variable::ofNull();
}

static Variable GetCommandable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObjectOrCaller(args, 0, ctx);

    // Transform

    // Execute
    return Variable::ofInt(static_cast<int>(oTarget->isCommandable()));
}

static Variable GetHitDice(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    return Variable::ofInt(creature->attributes().getAggregateLevel());
}

static Variable GetTag(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    return Variable::ofString(oObject->tag());
}

static constexpr int kStrRefForceResisted = 38598;
static constexpr int kStrRefForceResistanceRoll = 42019;

// A resisted power tells the caster and then the target, first that it was
// resisted and then how. Each shows the lines only while it is the controlled
// creature.
static void addForceResistedFeedback(const Object &caster, const Creature &target, int roll, int level,
                                     int resistance, const RoutineContext &ctx) {
    auto leader = ctx.game.party().getLeader();
    const int recipients = static_cast<int>(leader && leader.get() == &caster) +
                           static_cast<int>(leader && leader.get() == &target);
    if (recipients == 0) {
        return;
    }
    const std::string targetName = getFeedbackObjectName(target, ctx);
    const std::string resisted = ctx.game.getFeedbackText(kStrRefForceResisted, {{0, targetName}});
    const std::string reported = ctx.game.getFeedbackText(
        kStrRefForceResistanceRoll,
        {
            {0, getFeedbackObjectName(caster, ctx)},
            {1, std::to_string(roll)},
            {2, std::to_string(level)},
            {3, targetName},
            {4, std::to_string(resistance)},
        });
    for (const std::string *line : {&resisted, &reported}) {
        if (line->empty()) {
            continue;
        }
        for (int i = 0; i < recipients; ++i)
            ctx.game.messageLog().add(MessageLog::kFeedbackMessageType, MessageLog::Style::Normal, *line,
                                      MessageLog::Buffer::Combat);
    }
}

static Variable ResistForce(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto sourceObject = getObjectOrNull(args, 0, ctx);
    auto targetObject = getObjectOrNull(args, 1, ctx);
    auto *target = targetObject ? dyn_cast<Creature>(targetObject.get()) : nullptr;
    if (!sourceObject || !target) {
        return Variable::ofInt(0);
    }
    // The spell is the source's current one.
    const int spellId = sourceObject->spellCastContext().spellId;
    if (spellId < 0) {
        return Variable::ofInt(0);
    }
    const auto spellType = static_cast<SpellType>(spellId);
    auto spell = ctx.services.game.spells.get(spellType);
    if (!spell) {
        return Variable::ofInt(0);
    }
    // Spell immunity is selected by spell ID without source or versus
    // qualification, and is reported with the spell's name.
    for (const EffectInstance &effect : target->effects()) {
        if (effect.type() == EffectType::SpellImmunity &&
            (effect.integerParameter(0) == static_cast<int>(SpellType::All) ||
             effect.integerParameter(0) == spellId)) {
            addSpellImmunityFeedback(ctx.game, *target, *sourceObject, spell->name);
            return Variable::ofInt(2);
        }
    }
    // Run by an area of effect, the caster level is the one it took from its
    // creator when it was placed. A creature source uses the level an item
    // use set for its impact, else, for a power from a class, its total
    // level without drained levels. Anything else, a spell-like ability and
    // a power from no class use the power's innate level.
    const auto caller = getCallerOrNull(ctx);
    auto *source = dyn_cast<Creature>(sourceObject.get());
    const auto *areaOfEffect = caller ? dyn_cast<AreaOfEffect>(caller.get()) : nullptr;
    int level = 2 * spell->innateLevel - 1;
    if (areaOfEffect) {
        level = areaOfEffect->spellLevel();
    } else if (source) {
        const auto &levelOverride = source->spellScriptContext().levelOverride;
        if (levelOverride) {
            level = *levelOverride;
        } else if (castingSourceKind(source->spellCastContext().castingClass) == CastingSourceKind::Class) {
            level = source->getSpellLevel(false);
        }
    }
    const int resistance = target->forceResistance().value();
    int roll = 0;
    const int result = resolveForceResistance(spell->userType, resistance, level, [&roll] {
        roll = randomInt(1, 20);
        return roll;
    });
    if (result == 1) {
        addForceResistedFeedback(*sourceObject, *target, roll, level, resistance, ctx);
    }
    return Variable::ofInt(result);
}

static Variable GetEffectType(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto effect = getEffect(args, 0);
    // A linked pair has no script type of its own.
    if (effect->type() == EffectType::LinkEffects) {
        return Variable::ofInt(0);
    }
    return Variable::ofInt(effect->saveFacingInstance().scriptEffectType(ctx.game.isTSL()));
}

static Variable GetFactionEqual(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFirstObject = getObject(args, 0, ctx);
    auto oSecondObject = getObjectOrCaller(args, 1, ctx);

    // Transform
    auto firstObject = checkCreature(oFirstObject);
    auto secondObject = checkCreature(oSecondObject);

    // Execute
    return Variable::ofInt(static_cast<int>(firstObject->faction() == secondObject->faction()));
}

static Variable ChangeFaction(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObjectToChangeFaction = getObject(args, 0, ctx);
    auto oMemberOfFactionToJoin = getObject(args, 1, ctx);

    // Transform
    auto target = checkCreature(oObjectToChangeFaction);
    auto source = checkCreature(oMemberOfFactionToJoin);

    // Execute
    target->setFaction(source->faction());
    return Variable::ofNull();
}

static Variable GetIsListening(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObjectOrNull(args, 0, ctx);

    // Execute
    return Variable::ofInt(oObject && oObject->isListening() ? 1 : 0);
}

static Variable SetListening(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObjectOrNull(args, 0, ctx);
    auto bValue = getInt(args, 1);

    // Execute
    if (oObject) {
        oObject->setListening(bValue != 0);
    }
    return Variable::ofNull();
}

static Variable SetListenPattern(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObjectOrNull(args, 0, ctx);
    auto sPattern = getString(args, 1);
    auto nNumber = getIntOrElse(args, 2, 0);

    // Execute
    if (oObject) {
        oObject->setListenPattern(sPattern, nNumber);
    }
    return Variable::ofNull();
}

static Variable TestStringAgainstPattern(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sPattern = getString(args, 0);
    auto sStringToTest = getString(args, 1);

    // Transform
    auto pattern = ListenPattern::parse(sPattern);

    // Execute
    return Variable::ofInt(pattern && pattern->match(sStringToTest) ? 1 : 0);
}

static Variable GetMatchedSubstring(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nString = getInt(args, 0);
    auto caller = getCallerOrNull(ctx);

    // Execute
    if (!caller || nString < 0 || static_cast<size_t>(nString) >= caller->matchedSubstrings().size()) {
        return Variable::ofString("");
    }
    return Variable::ofString(caller->matchedSubstrings()[nString]);
}

static Variable GetMatchedSubstringsCount(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto caller = getCallerOrNull(ctx);

    // Execute
    return Variable::ofInt(caller ? static_cast<int>(caller->matchedSubstrings().size()) : 0);
}

// A faction query weighs every creature of the member's faction, the member
// among them, and keeps the first that beats the best so far: higher or lower
// than it, starting from initial. With bMustBeVisible, a creature counts only
// while the line from its position to the member's is clear. Damage queries
// pass over the dead and downed party members.
//
// A search for the lowest score starts from kFactionScoreCeiling, so a member
// scoring that or more is never chosen.
static constexpr int kFactionScoreCeiling = 999;

static Variable selectFactionMember(const std::vector<Variable> &args, const RoutineContext &ctx,
                                    int initial, bool higher, bool livingOnly,
                                    const std::function<int(const Creature &)> &score) {
    auto factionMember = getCreatureOrNull(args, 0, ctx);
    const bool mustBeVisible = getIntOrElse(args, 1, 1) != 0;
    if (!factionMember) {
        return Variable::ofObject(kObjectInvalid);
    }

    auto area = ctx.game.module()->area();
    const Faction faction = factionMember->faction();
    std::shared_ptr<Creature> selected;
    int best = initial;
    for (const auto &object : area->getObjectsByType(ObjectType::Creature)) {
        auto member = std::static_pointer_cast<Creature>(object);
        if (member->faction() != faction) {
            continue;
        }
        if (livingOnly && (member->isDead() || member->isTemporarilyDead())) {
            continue;
        }
        const int value = score(*member);
        if (higher ? value <= best : value >= best) {
            continue;
        }
        if (mustBeVisible &&
            !area->isEyeLineClear(member->position(), factionMember->position(), *member, *factionMember))
            continue;
        selected = member;
        best = value;
    }
    return Variable::ofObject(selected ? selected->id() : kObjectInvalid);
}

static Variable GetFactionWeakestMember(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return selectFactionMember(args, ctx, kFactionScoreCeiling, false, false,
                               [](const Creature &member) { return member.attributes().getAggregateLevel(); });
}

static Variable GetFactionStrongestMember(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return selectFactionMember(args, ctx, 0, true, false,
                               [](const Creature &member) { return member.attributes().getAggregateLevel(); });
}

static Variable GetFactionMostDamagedMember(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return selectFactionMember(args, ctx, -1, true, true,
                               [](const Creature &member) { return member.maxHitPoints() - member.currentHitPoints(); });
}

static Variable GetFactionLeastDamagedMember(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return selectFactionMember(args, ctx, kFactionScoreCeiling, false, true,
                               [](const Creature &member) { return member.maxHitPoints() - member.currentHitPoints(); });
}

// The creature an object argument names, when it stands in the current area.
// The faction summaries below answer -1 for anything else.
static std::shared_ptr<Creature> getResidentCreature(const std::vector<Variable> &args, int index, const RoutineContext &ctx) {
    auto object = getObjectOrNull(args, index, ctx);
    auto creature = object ? dyn_cast<Creature>(object) : nullptr;
    if (!creature || !ctx.game.module()->area()->isObjectResident(*creature)) {
        return nullptr;
    }
    return creature;
}

// The creatures of a creature's faction in the current area, the creature among
// them.
static std::vector<std::shared_ptr<Creature>> getFactionMembers(const Creature &creature, const RoutineContext &ctx) {
    std::vector<std::shared_ptr<Creature>> members;
    const Faction faction = creature.faction();
    for (const auto &object : ctx.game.module()->area()->getObjectsByType(ObjectType::Creature)) {
        auto member = std::static_pointer_cast<Creature>(object);
        if (member->faction() == faction) {
            members.push_back(std::move(member));
        }
    }
    return members;
}

// A faction holds no gold of its own.
static Variable GetFactionGold(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return Variable::ofInt(getResidentCreature(args, 0, ctx) ? 0 : -1);
}

// How the target regards the members of the source's faction, averaged with
// truncation and kept within 0 to 100. A target that is not a creature regards
// every member at 0.
static Variable GetFactionAverageReputation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto source = getResidentCreature(args, 0, ctx);
    if (!source) {
        return Variable::ofInt(-1);
    }
    auto targetObject = getObjectOrNull(args, 1, ctx);
    auto target = targetObject ? dyn_cast<Creature>(targetObject) : nullptr;
    const auto members = getFactionMembers(*source, ctx);
    int total = 0;
    if (target) {
        for (const auto &member : members) {
            total += getObjectReputation(*target, *member, ctx.game);
        }
    }
    return Variable::ofInt(std::clamp(total / static_cast<int>(members.size()), 0, 100));
}

// Each member counts as dark side (3) up to 40, neutral (1) from 41 to 59 and
// light side (2) from 60. A lone member gives its own alignment. Otherwise the
// truncated mean goes to whichever of light and dark side is nearer, dark side
// on a tie, so a mean of neutral reads as light side.
static Variable GetFactionAverageGoodEvilAlignment(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto creature = getResidentCreature(args, 0, ctx);
    if (!creature) {
        return Variable::ofInt(-1);
    }
    const auto members = getFactionMembers(*creature, ctx);
    int total = 0;
    for (const auto &member : members) {
        total += static_cast<int>(member->alignment());
    }
    if (members.size() == 1) {
        return Variable::ofInt(total);
    }
    const float mean = static_cast<float>(total / static_cast<int>(members.size()));
    const auto darkSide = static_cast<float>(Alignment::DarkSide);
    const auto lightSide = static_cast<float>(Alignment::LightSide);
    const auto nearer = std::fabs(mean - darkSide) <= std::fabs(mean - lightSide) ? Alignment::DarkSide : Alignment::LightSide;
    return Variable::ofInt(static_cast<int>(nearer));
}

static Variable SoundObjectGetFixedVariance(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oSound = getObject(args, 0, ctx);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SoundObjectGetFixedVariance");
}

// The mean of the members' total levels, truncated.
static Variable GetFactionAverageLevel(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto creature = getResidentCreature(args, 0, ctx);
    if (!creature) {
        return Variable::ofInt(-1);
    }
    const auto members = getFactionMembers(*creature, ctx);
    int total = 0;
    for (const auto &member : members) {
        total += member->attributes().getAggregateLevel();
    }
    return Variable::ofInt(total / static_cast<int>(members.size()));
}

// The mean of the members' experience, truncated.
static Variable GetFactionAverageXP(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto creature = getResidentCreature(args, 0, ctx);
    if (!creature) {
        return Variable::ofInt(-1);
    }
    const auto members = getFactionMembers(*creature, ctx);
    int total = 0;
    for (const auto &member : members) {
        total += member->xp();
    }
    return Variable::ofInt(total / static_cast<int>(members.size()));
}

// The tally runs on across every class of every member without restarting, so
// each class taken in turn outscores the one before: the answer is the last
// class of the last member that has one, or 0 when none has.
static Variable GetFactionMostFrequentClass(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto creature = getResidentCreature(args, 0, ctx);
    if (!creature) {
        return Variable::ofInt(-1);
    }
    int result = 0;
    for (const auto &member : getFactionMembers(*creature, ctx)) {
        const auto &attributes = member->attributes();
        if (!attributes.classLevels().empty()) {
            result = static_cast<int>(attributes.getEffectiveClass());
        }
    }
    return Variable::ofInt(result);
}

static Variable GetFactionWorstAC(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return selectFactionMember(args, ctx, kFactionScoreCeiling, false, false,
                               [](const Creature &member) { return member.getDefense(); });
}

static Variable GetFactionBestAC(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return selectFactionMember(args, ctx, 0, true, false,
                               [](const Creature &member) { return member.getDefense(); });
}

static Variable GetGlobalString(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sIdentifier = getString(args, 0);

    // Transform

    // Execute
    return Variable::ofString(ctx.game.getGlobalString(sIdentifier));
}

static Variable GetListenPatternNumber(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto caller = getCallerOrNull(ctx);

    // Execute
    return Variable::ofInt(caller ? caller->listenPatternNumber() : 0);
}

static Variable GetWaypointByTag(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sWaypointTag = getString(args, 0);

    // Transform
    auto waypointTag = boost::to_lower_copy(sWaypointTag);

    // Execute
    std::shared_ptr<Object> waypoint;
    for (auto &object : ctx.game.module()->area()->getObjectsByType(ObjectType::Waypoint)) {
        if (object->tag() == waypointTag) {
            waypoint = object;
            break;
        }
    }
    return Variable::ofObject(getObjectIdOrInvalid(waypoint));
}

static Variable GetTransitionTarget(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTransition = getObject(args, 0, ctx);

    // Transform

    // Execute
    throw RoutineNotImplementedException("GetTransitionTarget");
}

static Variable GetObjectByTag(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sTag = getString(args, 0);
    auto nNth = getIntOrElse(args, 1, 0);

    // Transform
    auto tag = boost::to_lower_copy(sTag);

    // Execute
    std::shared_ptr<Object> object;
    if (!tag.empty()) {
        object = ctx.game.module()->area()->getObjectByTag(tag, nNth);
    } else {
        object = ctx.game.party().player();
    }
    return Variable::ofObject(getObjectIdOrInvalid(object));
}

// The good/evil value halfway between the dark and light sides.
static constexpr int kBalancedGoodEvil = 50;

// The good/evil change an alignment award makes: toward the dark or light
// side by the shift, or toward balance without passing it. Any other
// alignment changes nothing.
static int alignmentShift(int alignment, int shift, int current) {
    switch (static_cast<Alignment>(alignment)) {
    case Alignment::DarkSide:
        return -shift;
    case Alignment::LightSide:
        return shift;
    case Alignment::Neutral:
        return current > kBalancedGoodEvil ? std::max(kBalancedGoodEvil - current, -shift) : std::min(shift, kBalancedGoodEvil - current);
    default:
        return 0;
    }
}

// TSL keeps running totals of the player character's shifts for scripts. A
// total that reached 127 stays there; none goes above it.
static void addAlignmentTotal(Game &game, const std::string &name, int amount) {
    static constexpr int kMaxTotal = 127;
    const int total = game.getGlobalNumber(name);
    if (total == kMaxTotal) {
        return;
    }
    game.setGlobalNumber(name, std::min(total + amount, kMaxTotal));
}

static Variable AdjustAlignment(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto creature = getCreatureOrNull(args, 0, ctx);
    auto nAlignment = getInt(args, 1);
    auto nShift = getInt(args, 2);
    auto bDontModifyNPCs = getIntOrElse(args, 3, 0);

    // Transform
    const bool dontModifyNPCs = ctx.game.isTSL() && bDontModifyNPCs != 0;

    // Execute
    if (!creature) {
        return Variable::ofNull();
    }
    static constexpr char kLightTotalGlobal[] = "G_PC_Light_Total";
    static constexpr char kDarkTotalGlobal[] = "G_PC_Dark_Total";
    static constexpr int kAlignmentShiftStrRef = 1471;
    static constexpr int kLargeShiftStrRef = 41921;
    static constexpr int kMediumShiftStrRef = 41922;
    static constexpr int kSmallShiftStrRef = 41923;
    static constexpr int kLightSideStrRef = 41924;
    static constexpr int kDarkSideStrRef = 41925;
    // An award above this size is large, and one below the other small.
    static constexpr int kLargestMediumShift = 9;
    static constexpr int kSmallestMediumShift = 5;
    const int current = creature->goodEvil();
    const int shift = alignmentShift(nAlignment, nShift, current);
    if (ctx.game.isTSL() && creature->isPlayerCreated() && !dontModifyNPCs) {
        switch (static_cast<Alignment>(nAlignment)) {
        case Alignment::LightSide:
            addAlignmentTotal(ctx.game, kLightTotalGlobal, nShift);
            break;
        case Alignment::DarkSide:
            addAlignmentTotal(ctx.game, kDarkTotalGlobal, nShift);
            break;
        case Alignment::Neutral:
            if (shift > 0) {
                addAlignmentTotal(ctx.game, kLightTotalGlobal, shift);
            }
            if (shift < 0) {
                addAlignmentTotal(ctx.game, kDarkTotalGlobal, -shift);
            }
            break;
        default:
            break;
        }
    }
    creature->modifyAlignment(shift, dontModifyNPCs);
    if (dontModifyNPCs) {
        return Variable::ofNull();
    }
    // The controlled creature is told the side and size of the award asked
    // for, not the change made. An award toward balance names the side it
    // moves toward.
    if (creature == ctx.game.party().getLeader()) {
        int side = nAlignment;
        int size = nShift;
        if (static_cast<Alignment>(nAlignment) == Alignment::Neutral) {
            side = static_cast<int>(current > kBalancedGoodEvil ? Alignment::DarkSide : Alignment::LightSide);
            size = std::abs(nShift);
        }
        const int sizeStrRef = size > kLargestMediumShift ? kLargeShiftStrRef : (size < kSmallestMediumShift ? kSmallShiftStrRef : kMediumShiftStrRef);
        const int sideStrRef = side == static_cast<int>(Alignment::LightSide) ? kLightSideStrRef : kDarkSideStrRef;
        ctx.game.addFeedbackMessage(kAlignmentShiftStrRef, {{0, ctx.game.getInterfaceText(sizeStrRef)},
                                                            {1, ctx.game.getInterfaceText(sideStrRef)}});
    }
    if (shift != 0 && ctx.game.party().isMember(*creature)) {
        ctx.game.submitStatusSummary(
            shift > 0 ? StatusSummaryCategory::LightSideShift : StatusSummaryCategory::DarkSideShift,
            std::abs(shift));
    }
    return Variable::ofNull();
}

static Variable SetAreaTransitionBMP(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nPredefinedAreaTransition = getInt(args, 0);
    auto sCustomAreaTransitionBMP = getStringOrElse(args, 1, "");

    // Transform

    // Execute
    throw RoutineNotImplementedException("SetAreaTransitionBMP");
}

// An object regards an invalid object at the neutral reputation; it regards
// an enemy at most at the enemy reputation and a friend at least at the friend
// reputation.
static constexpr int kNeutralReputation = 50;
static constexpr int kEnemyReputation = 10;
static constexpr int kFriendReputation = 90;

// Faction ids are the rows of the reputation table.
static bool isKnownFaction(Faction faction, const RoutineContext &ctx) {
    const int id = static_cast<int>(faction);
    return id >= 0 && static_cast<size_t>(id) < ctx.services.game.reputes.state().factions.size();
}

static Variable GetReputation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto other = getObjectOrNull(args, 0, ctx);
    auto receiver = getObjectOrNull(args, 1, ctx);
    if (!receiver) {
        return Variable::ofInt(-1);
    }
    return Variable::ofInt(other ? getObjectReputation(*receiver, *other, ctx.game) : kNeutralReputation);
}

static Variable AdjustReputation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto receiver = getObjectOrNull(args, 0, ctx);
    auto factionMember = getObjectOrNull(args, 1, ctx);
    auto adjustment = getInt(args, 2);
    if (!receiver || !factionMember) {
        return Variable::ofNull();
    }
    auto target = dyn_cast<Creature>(receiver);
    if (!target) {
        return Variable::ofNull();
    }
    if (auto creature = dyn_cast<Creature>(factionMember); creature && creature->plotFlag()) {
        return Variable::ofNull();
    }
    auto otherFaction = getObjectFaction(*factionMember);
    // A member of the player faction, or of none, changes nothing.
    if (!otherFaction || static_cast<int>(*otherFaction) <= static_cast<int>(Faction::Player)) {
        return Variable::ofNull();
    }

    const auto receiverFaction = target->faction();
    auto &reputes = ctx.services.game.reputes;
    // Read the other faction's attitude, but write the receiver's attitude.
    // Player relationships use the player-source cell for both operations.
    const int previous = receiverFaction == Faction::Player
                             ? reputes.getReputation(receiverFaction, *otherFaction)
                             : reputes.getReputation(*otherFaction, receiverFaction);
    const int updated = std::clamp(previous + adjustment, 0, 100);
    const int current = reputes.getReputation(receiverFaction, *otherFaction);
    reputes.adjustReputation(receiverFaction, *otherFaction, updated - current);
    return Variable::ofNull();
}

static Variable GetModuleFileName(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("GetModuleFileName");
}

static Variable GetGoingToBeAttackedBy(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto creature = getCreatureOrNull(args, 0, ctx);
    return Variable::ofObject(creature ? creature->getGoingToBeAttackedBy() : kObjectInvalid);
}

static Variable GetLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    return Variable::ofLocation(ctx.game.newLocation(oObject->position(), scriptFacingFromObject(oObject->getFacing())));
}

static Variable CreateLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vPosition = getVector(args, 0);
    auto fOrientation = getFloat(args, 1);

    // Transform
    auto orientation = glm::radians(fOrientation);

    // Execute
    return Variable::ofLocation(ctx.game.newLocation(std::move(vPosition), orientation));
}

static Variable ApplyEffectAtLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Any duration type but instant or temporary is permanent.
    auto nDurationType = getInt(args, 0);
    auto duration = nDurationType == static_cast<int>(DurationType::Instant)     ? DurationType::Instant
                    : nDurationType == static_cast<int>(DurationType::Temporary) ? DurationType::Temporary
                                                                                 : DurationType::Permanent;
    auto value = getEffect(args, 1);
    auto location = getLocationArgument(args, 2);
    float seconds = getFloatOrElse(args, 3, 0.0f);
    auto record = value->saveFacingInstance();
    record.effect = value;
    record.setDuration(duration, seconds);
    ctx.game.module()->area()->applyEffectAtLocation(std::move(record), *location);
    return Variable::ofNull();
}

static Variable GetIsPC(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute: TSL reads the creature's own player-character flag; KotOR
    // asks whether it is the creature the player controls.
    const bool pc = ctx.game.isTSL() ? creature->isPC() : creature == ctx.game.party().getLeader();
    return Variable::ofInt(static_cast<int>(pc));
}

static Variable FeetToMeters(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fFeet = getFloat(args, 0);

    // Transform

    // Execute
    return Variable::ofFloat(fFeet * 0.3048f);
}

static Variable YardsToMeters(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fYards = getFloat(args, 0);

    // Transform

    // Execute
    return Variable::ofFloat(fYards * 0.9144f);
}

static Variable ApplyEffectToObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nDurationType = getInt(args, 0);
    auto eEffect = getEffect(args, 1);
    auto oTarget = getObject(args, 2, ctx);
    auto fDuration = getFloatOrElse(args, 3, 0.0f);

    // Transform
    // Any duration type but instant or temporary applies the effect permanently.
    auto durationType = nDurationType == static_cast<int>(DurationType::Instant)     ? DurationType::Instant
                        : nDurationType == static_cast<int>(DurationType::Temporary) ? DurationType::Temporary
                                                                                     : DurationType::Permanent;

    // Execute
    // Only objects placed in the world take effects; an area or the module
    // takes nothing.
    if (isa<Area>(oTarget) || isa<Module>(oTarget)) {
        return Variable::ofNull();
    }
    oTarget->applyEffect(eEffect, durationType, fDuration);
    return Variable::ofNull();
}

static Variable SpeakString(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sStringToSpeak = getString(args, 0);
    auto nTalkVolume = getIntOrElse(args, 1, 0);
    auto caller = getCallerOrNull(ctx);

    // Execute
    // Only an object placed in an area is heard.
    if (auto area = caller ? caller->spatialArea() : std::nullopt) {
        area->get().broadcastDialog(*caller, sStringToSpeak, nTalkVolume);
    }
    return Variable::ofNull();
}

static Variable GetSpellTargetLocation(const std::vector<Variable> &, const RoutineContext &ctx) {
    const auto caller = getCallerOrNull(ctx);
    return Variable::ofLocation(caller && (isa<Creature>(caller) || isa<Placeable>(caller))
                                    ? caller->spellScriptContext().location()
                                    : nullptr);
}

static Variable GetPositionFromLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto lLocation = getLocationArgument(args, 0);

    // Transform

    // Execute
    return Variable::ofVector(lLocation->position());
}

static Variable GetFacingFromLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto lLocation = getLocationArgument(args, 0);

    // Transform

    // Execute
    return Variable::ofFloat(glm::degrees(lLocation->facing()));
}

static Variable GetNearestCreatureToLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto lLocation = getLocationArgument(args, 2);
    auto nNth = getIntOrElse(args, 3, 1);

    // Execute
    auto creature = ctx.game.module()->area()->getNearestCreatureToLocation(*lLocation, getCreatureSearchCriteria(args), nNth - 1);
    return Variable::ofObject(getObjectIdOrInvalid(creature));
}

// The object type argument is a mask of object types. A mask holding every
// type bit matches any object.
static bool matchesObjectTypeMask(const Object &object, int mask) {
    const int all = static_cast<int>(ObjectType::All);
    return (mask & static_cast<int>(object.type())) != 0 || (mask & all) == all;
}

static Variable GetNearestObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nObjectType = getIntOrElse(args, 0, static_cast<int>(ObjectType::All));
    auto oTarget = getObjectOrNull(args, 1, ctx);
    auto nNth = getIntOrElse(args, 2, 1);

    // Execute
    if (!oTarget) {
        return Variable::ofObject(kObjectInvalid);
    }
    auto object = ctx.game.module()->area()->getNearestObject(*oTarget, nNth - 1, [nObjectType](const Object &object) {
        return matchesObjectTypeMask(object, nObjectType);
    });
    return Variable::ofObject(object ? object->get().id() : kObjectInvalid);
}

static Variable GetNearestObjectToLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nObjectType = getIntOrElse(args, 0, static_cast<int>(ObjectType::All));
    auto lLocation = getLocationArgument(args, 1);
    auto nNth = getIntOrElse(args, 2, 1);

    // Execute
    auto object = ctx.game.module()->area()->getNearestObjectToLocation(lLocation->position(), nNth - 1, [nObjectType](const Object &object) {
        return matchesObjectTypeMask(object, nObjectType);
    });
    return Variable::ofObject(object ? object->get().id() : kObjectInvalid);
}

static Variable GetNearestObjectByTag(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sTag = getString(args, 0);
    auto oTarget = getObjectOrNull(args, 1, ctx);
    auto nNth = getIntOrElse(args, 2, 1);

    // Transform
    auto tag = boost::to_lower_copy(sTag);

    // Execute
    if (!oTarget) {
        return Variable::ofObject(kObjectInvalid);
    }
    auto object = ctx.game.module()->area()->getNearestObject(*oTarget, nNth - 1, [&tag](const Object &object) {
        return object.tag() == tag;
    });
    return Variable::ofObject(object ? object->get().id() : kObjectInvalid);
}

static Variable IntToFloat(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nInteger = getInt(args, 0);

    // Transform

    // Execute
    return Variable::ofFloat(static_cast<float>(nInteger));
}

static Variable FloatToInt(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fFloat = getFloat(args, 0);

    // Transform

    // Execute
    return Variable::ofInt(static_cast<int>(fFloat));
}

static Variable StringToInt(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sNumber = getString(args, 0);

    // Transform

    // Execute
    int intValue = 0;
    if (!sNumber.empty()) {
        intValue = stoi(sNumber);
    }
    return Variable::ofInt(intValue);
}

static Variable StringToFloat(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sNumber = getString(args, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("StringToFloat");
}

// How oTarget regards oSource, by default the caller. An invalid source is
// regarded at 50; an invalid target answers nothing.
static std::optional<int> getTargetRegardForSource(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto target = getObjectOrNull(args, 0, ctx);
    if (!target) {
        return std::nullopt;
    }
    auto source = args.size() > 1 ? getObjectOrNull(args, 1, ctx) : getCallerOrNull(ctx);
    return source ? getObjectReputation(*target, *source, ctx.game) : kNeutralReputation;
}

// oTarget is an enemy of oSource when it regards oSource at 10 or less. A
// placeable whose appearance is not hostile is no one's enemy.
static Variable GetIsEnemy(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto target = getObjectOrNull(args, 0, ctx);
    if (auto *placeable = target ? dyn_cast<Placeable>(target.get()) : nullptr;
        placeable && !placeable->isHostileAppearance()) {
        return Variable::ofInt(0);
    }
    const auto regard = getTargetRegardForSource(args, ctx);
    return Variable::ofInt(regard && *regard <= kEnemyReputation ? 1 : 0);
}

// A friend regards oSource at 90 or more.
static Variable GetIsFriend(const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto regard = getTargetRegardForSource(args, ctx);
    return Variable::ofInt(regard && *regard >= kFriendReputation ? 1 : 0);
}

// A neutral regards oSource at 11 to 89.
static Variable GetIsNeutral(const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto regard = getTargetRegardForSource(args, ctx);
    return Variable::ofInt(regard && *regard > kEnemyReputation && *regard < kFriendReputation ? 1 : 0);
}

static Variable GetPCSpeaker(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto player = ctx.game.party().player();
    return Variable::ofObject(getObjectIdOrInvalid(player));
}

static Variable GetStringByStrRef(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nStrRef = getInt(args, 0);

    // Transform

    // Execute
    return Variable::ofString(ctx.services.resource.strings.getText(nStrRef));
}

static Variable DestroyObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oDestroy = getObject(args, 0, ctx);
    auto fDelay = getFloatOrElse(args, 1, 0.0f);
    auto bNoFade = getIntOrElse(args, 2, 0);
    auto fDelayUntilFade = getFloatOrElse(args, 3, 0.0f);
    auto nHideFeedback = getIntOrElse(args, 4, 0);

    // Transform

    // Execute
    // The body's fade is set at once: none, or a wait of fDelayUntilFade
    // before it fades. The party's roster slot forgets the creature at once
    // too. The destroy event is delivered after the delay; delivery detaches
    // an item from its owner and removes the object, and an earlier event
    // wins. With nHideFeedback set, a creature takes the death fade instead.
    oDestroy->setDeleteNoFade(bNoFade != 0);
    oDestroy->setFadeOutTime(static_cast<uint32_t>(static_cast<int>(fDelayUntilFade * 1000.0f)));
    if (auto creature = dyn_cast<Creature>(oDestroy)) {
        // The creature leading hands the lead along the party to the first
        // place naming a creature, dead or alive, and then leaves the party.
        // A follower keeps its place, naming nothing, and stays listed with
        // the party, as a puppet stays listed with it.
        auto &party = ctx.game.party();
        if (party.getLeader() == creature) {
            for (int tries = party.getSize(); tries > 0; --tries) {
                party.switchLeader();
                if (party.getLeader()) {
                    break;
                }
            }
            party.removeMemberEntries(*creature);
        } else {
            party.vacateMemberEntries(*creature);
        }
        party.releaseRosterSlot(*creature);
    }
    const Variable *callerArg = ctx.execution.findArg(ArgKind::Caller);
    auto caller = callerArg ? ctx.game.getObjectById(callerArg->objectId) : nullptr;
    ctx.game.postObjectDestruction(*oDestroy, getOptionalObject(caller), fDelay, nHideFeedback == 0);
    return Variable::ofNull();
}

static Variable GetModule(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofObject(getObjectIdOrInvalid(ctx.game.module()));
}

static Variable CreateObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nObjectType = getInt(args, 0);
    auto sTemplate = getString(args, 1);
    auto lLocation = getLocationArgument(args, 2);
    auto bUseAppearAnimation = getIntOrElse(args, 3, 0);

    // Transform
    auto objectType = static_cast<ObjectType>(nObjectType);
    auto tmplt = boost::to_lower_copy(sTemplate);

    // Execute
    auto object = ctx.game.module()->area()->createObject(objectType, tmplt, lLocation, bUseAppearAnimation != 0);
    return Variable::ofObject(getObjectIdOrInvalid(object));
}

static Variable EventSpellCastAt(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // The event keeps the caster whatever it names, and the harmful value as given.
    auto caster = getObjectOrNull(args, 0, ctx);
    return Variable::ofEvent(ctx.game.newEvent(static_cast<int>(SavedSignalEventType::SpellCastAt),
                                               std::vector<int32_t> {getInt(args, 1), getIntOrElse(args, 2, 1)},
                                               std::vector<float> {}, std::vector<std::string> {},
                                               std::vector<std::shared_ptr<Object>> {caster}));
}

static Variable GetLastSpellCaster(const std::vector<Variable> &args, const RoutineContext &ctx) {
    if (const Variable *caster = ctx.execution.findArg(ArgKind::LastSpellCaster)) {
        return *caster;
    }
    return Variable::ofObject(script::kObjectInvalid);
}

static Variable GetLastSpell(const std::vector<Variable> &args, const RoutineContext &ctx) {
    if (const Variable *spell = ctx.execution.findArg(ArgKind::LastSpell)) {
        return *spell;
    }
    return Variable::ofInt(-1);
}

static Variable GetUserDefinedEventNumber(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (const Variable *eventNum = ctx.execution.findArg(ArgKind::UserDefinedEventNumber)) {
        return *eventNum;
    }

    return Variable::ofInt(-1);
}

static Variable GetSpellId(const std::vector<Variable> &, const RoutineContext &ctx) {
    const auto caller = getCallerOrNull(ctx);
    return Variable::ofInt(caller && isa<Creature>(caller) ? caller->spellCastContext().spellId : -1);
}

static Variable RandomName(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("RandomName");
}

static Variable GetLoadFromSaveGame(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofInt(static_cast<int>(ctx.game.isLoadingFromSaveGame()));
}

static Variable GetName(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    return Variable::ofString(oObject->name());
}

static Variable GetLastSpeaker(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto caller = getCallerOrNull(ctx);

    // Execute
    // Only a creature answers, though placeables and doors keep a last
    // speaker too.
    auto creature = caller ? dyn_cast<Creature>(caller) : nullptr;
    return Variable::ofObject(creature ? creature->lastSpeaker() : kObjectInvalid);
}

static Variable BeginConversation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sResRef = getStringOrElse(args, 0, "");
    auto oObjectToDialog = getObjectOrNull(args, 1, ctx);

    // Transform

    // Execute
    throw RoutineNotImplementedException("BeginConversation");
}

// The perception routines read the caller's last perception change.
static std::shared_ptr<Creature> perceivingCaller(const RoutineContext &ctx) {
    const Variable *caller = ctx.execution.findArg(ArgKind::Caller);
    return caller ? ctx.game.getObjectById<Creature>(caller->objectId) : nullptr;
}

static Variable GetLastPerceived(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto creature = perceivingCaller(ctx);
    return Variable::ofObject(creature ? creature->lastPerceivedId() : kObjectInvalid);
}

static Variable lastPerception(const RoutineContext &ctx, PerceptionEvent event) {
    auto creature = perceivingCaller(ctx);
    return Variable::ofInt(creature && creature->lastPerception(event) ? 1 : 0);
}

static Variable GetLastPerceptionHeard(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return lastPerception(ctx, PerceptionEvent::Heard);
}

static Variable GetLastPerceptionInaudible(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return lastPerception(ctx, PerceptionEvent::Inaudible);
}

static Variable GetLastPerceptionSeen(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return lastPerception(ctx, PerceptionEvent::Seen);
}

static Variable GetLastClosedBy(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (const Variable *lastClosed = ctx.execution.findArg(ArgKind::LastClosedBy)) {
        return *lastClosed;
    }
    return Variable::ofObject(kObjectInvalid);
}

static Variable GetLastPerceptionVanished(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return lastPerception(ctx, PerceptionEvent::Vanished);
}

static Variable getInPersistentObject(const std::vector<Variable> &args, const RoutineContext &ctx, bool first) {
    auto oPersistentObject = getObjectOrCaller(args, 0, ctx);
    auto nResidentObjectType = getIntOrElse(args, 1, 1);
    auto nPersistentZone = static_cast<PersistentZone>(getIntOrElse(args, 2, 0));
    auto area = oPersistentObject->spatialArea();
    auto object = area
                      ? area->get().getObjectInPersistentObject(*oPersistentObject, first, nResidentObjectType, nPersistentZone)
                      : std::nullopt;
    return Variable::ofObject(object ? object->get().id() : kObjectInvalid);
}

static Variable GetFirstInPersistentObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return getInPersistentObject(args, ctx, true);
}

static Variable GetNextInPersistentObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return getInPersistentObject(args, ctx, false);
}

static Variable GetAreaOfEffectCreator(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto oAreaOfEffectObject = getObjectOrCaller(args, 0, ctx);
    auto *areaOfEffect = dyn_cast<AreaOfEffect>(oAreaOfEffectObject.get());
    return Variable::ofObject(areaOfEffect ? getObjectIdOrInvalid(areaOfEffect->creator()) : kObjectInvalid);
}

static Variable ShowLevelUpGUI(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("ShowLevelUpGUI");
}

static Variable SetItemNonEquippable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto object = getObjectOrNull(args, 0, ctx);
    auto item = object ? dyn_cast<Item>(object) : nullptr;
    auto bNonEquippable = getInt(args, 1);

    // Execute
    if (item) {
        item->setNonEquippable(bNonEquippable != 0);
    }
    return Variable::ofNull();
}

static Variable GetButtonMashCheck(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("GetButtonMashCheck");
}

static Variable SetButtonMashCheck(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nCheck = getInt(args, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SetButtonMashCheck");
}

static Variable GiveItem(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObject(args, 0, ctx);
    auto oGiveTo = getObject(args, 1, ctx);

    // Transform
    auto item = checkItem(oItem);

    // Execute
    transferItemTo(ctx.game, item, *oGiveTo);
    return Variable::ofNull();
}

static Variable ObjectToString(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    return Variable::ofString(str(boost::format("%x") % oObject->id()));
}

static Variable GetIsImmune(const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto object = getObjectOrNull(args, 0, ctx);
    const int immunityType = getInt(args, 1);
    const auto versus = getObjectOrNull(args, 2, ctx);
    const auto *creature = object ? dyn_cast<Creature>(object.get()) : nullptr;
    // Only creatures have immunities, and only the types from 1 up name one.
    const bool immune = creature && immunityType > 0 &&
                        immunityType < immunityTypeCount(ctx.game.isTSL()) &&
                        creature->hasEffectImmunity(static_cast<ImmunityType>(immunityType), Creature::asCreature(versus));
    return Variable::ofInt(static_cast<int>(immune));
}

static Variable GetEncounterActive(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto encounter = dyn_cast<Encounter>(getObjectOrCaller(args, 0, ctx));

    // Execute
    return Variable::ofInt(encounter && encounter->isActive() ? 1 : 0);
}

static Variable SetEncounterActive(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNewValue = getInt(args, 0);
    auto encounter = dyn_cast<Encounter>(getObjectOrCaller(args, 1, ctx));

    // Execute
    if (encounter) {
        encounter->setActive(nNewValue != 0);
    }
    return Variable::ofNull();
}

static Variable GetEncounterSpawnsMax(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto encounter = dyn_cast<Encounter>(getObjectOrCaller(args, 0, ctx));

    // Execute
    return Variable::ofInt(encounter ? encounter->respawns() : 0);
}

static Variable SetEncounterSpawnsMax(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNewValue = getInt(args, 0);
    auto encounter = dyn_cast<Encounter>(getObjectOrCaller(args, 1, ctx));

    // Execute
    if (encounter) {
        encounter->setRespawns(nNewValue);
    }
    return Variable::ofNull();
}

static Variable GetEncounterSpawnsCurrent(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto encounter = dyn_cast<Encounter>(getObjectOrCaller(args, 0, ctx));

    // Execute
    return Variable::ofInt(encounter ? encounter->currentSpawns() : 0);
}

static Variable SetEncounterSpawnsCurrent(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNewValue = getInt(args, 0);
    auto encounter = dyn_cast<Encounter>(getObjectOrCaller(args, 1, ctx));

    // Execute
    if (encounter) {
        encounter->setCurrentSpawns(nNewValue);
    }
    return Variable::ofNull();
}

static Variable GetModuleItemAcquired(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofObject(ctx.game.module()->itemEvents().acquired);
}

static Variable GetModuleItemAcquiredFrom(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofObject(ctx.game.module()->itemEvents().acquiredFrom);
}

static Variable SetCustomToken(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nCustomTokenNumber = getInt(args, 0);
    auto sTokenValue = getString(args, 1);

    // Transform

    // Execute
    if (nCustomTokenNumber <= Game::kLastReservedCustomToken) {
        return Variable::ofNull();
    }
    ctx.game.setCustomToken(nCustomTokenNumber, std::move(sTokenValue));
    return Variable::ofNull();
}

// Whether the creature has a use of the feat left; anything else has none.
static Variable GetHasFeat(const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto feat = static_cast<FeatType>(getInt(args, 0));
    const auto creature = getCreatureOrNull(args, 1, ctx);
    return Variable::ofInt(creature && creature->featRemainingUses(feat) != 0);
}

static Variable GetHasSkill(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nSkill = getInt(args, 0);
    auto oCreature = getObjectOrCaller(args, 1, ctx);

    // Transform
    auto skill = static_cast<SkillType>(nSkill);
    auto creature = checkCreature(oCreature);

    // Execute
    bool hasSkill = creature->attributes().hasSkill(skill);
    return Variable::ofInt(static_cast<int>(hasSkill));
}

// A source that is not a creature perceives nothing.
static Variable GetObjectSeen(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto target = getObjectOrNull(args, 0, ctx);
    auto source = getCreatureOrNull(args, 1, ctx);

    // Execute
    return Variable::ofInt(source && target && source->perception().sees(target->id()) ? 1 : 0);
}

static Variable GetObjectHeard(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto target = getObjectOrNull(args, 0, ctx);
    auto source = getCreatureOrNull(args, 1, ctx);

    // Execute
    return Variable::ofInt(source && target && source->perception().hears(target->id()) ? 1 : 0);
}

static Variable GetLastPlayerDied(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofObject(ctx.game.module()->lastPlayerDied());
}

static Variable GetModuleItemLost(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofObject(ctx.game.module()->itemEvents().lost);
}

static Variable GetModuleItemLostBy(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofObject(ctx.game.module()->itemEvents().lostBy);
}

static Variable EventConversation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    static constexpr int kConversationEvent = 7;
    return Variable::ofEvent(ctx.game.newEvent(kConversationEvent,
                                               std::vector<int32_t>(), std::vector<float>(), std::vector<std::string>(),
                                               std::vector<std::shared_ptr<Object>>()));
}

static Variable SetEncounterDifficulty(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nEncounterDifficulty = getInt(args, 0);
    auto encounter = dyn_cast<Encounter>(getObjectOrCaller(args, 1, ctx));

    // Execute
    if (encounter) {
        encounter->setDifficulty(nEncounterDifficulty);
    }
    return Variable::ofNull();
}

static Variable GetEncounterDifficulty(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto encounter = dyn_cast<Encounter>(getObjectOrCaller(args, 0, ctx));

    // Execute
    return Variable::ofInt(encounter ? encounter->difficulty() : 0);
}

static Variable GetDistanceBetweenLocations(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto lLocationA = getLocationArgument(args, 0);
    auto lLocationB = getLocationArgument(args, 1);

    // Transform

    // Execute
    return Variable::ofFloat(glm::distance(lLocationA->position(), lLocationB->position()));
}

static Variable GetReflexAdjustedDamage(const std::vector<Variable> &args, const RoutineContext &ctx) {
    static constexpr int kReflexSaveVisualEffect = 4000;

    int damage = getInt(args, 0);
    auto creature = getCreatureOrNull(args, 1, ctx);
    if (!creature) {
        return Variable::ofInt(damage);
    }
    const int dc = getInt(args, 2);
    const auto subtype = static_cast<SavingThrowType>(getIntOrElse(args, 3, 0));
    auto versus = args.size() > 4 ? getObjectOrNull(args, 4, ctx) : getCaller(ctx);

    // A Reflex save halves the damage, or with Evasion (TSL) avoids it, and
    // shows the dodge visual on the saver.
    if (rollReportedSavingThrow(ctx, *creature, SavingThrow::Reflex, dc, subtype, getOptionalObject(versus)) ==
        SavingThrowResult::Failed) {
        return Variable::ofInt(damage);
    }
    damage = ctx.game.isTSL() && creature->hasEffectiveFeat(FeatType::Evasion) ? 0 : damage / 2;
    auto visual = ctx.game.newEffect<VisualEffect>(kReflexSaveVisualEffect, false, ctx.services);
    visual->setSaveFacingCreator(versus);
    creature->applyEffect(std::move(visual), DurationType::Instant);
    return Variable::ofInt(damage);
}

static Variable PlayAnimation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nAnimation = getInt(args, 0);
    auto fSpeed = getFloatOrElse(args, 1, 1.0f);
    auto fSeconds = getFloatOrElse(args, 2, 0.0f);

    // Execute: it replaces the caller's actions.
    requestScriptAnimation(ctx.game, *getCaller(ctx), nAnimation, fSpeed, fSeconds, true);
    return Variable::ofNull();
}

static Variable TalentSpell(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nSpell = getInt(args, 0);

    // Transform

    // Execute
    return Variable::ofTalent(ctx.game.newTalent(TalentType::Spell, nSpell));
}

static Variable TalentFeat(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nFeat = getInt(args, 0);

    // Transform

    // Execute
    return Variable::ofTalent(ctx.game.newTalent(TalentType::Feat, nFeat));
}

static Variable TalentSkill(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nSkill = getInt(args, 0);

    // Transform

    // Execute
    return Variable::ofTalent(ctx.game.newTalent(TalentType::Skill, nSkill));
}

static Variable GetHasSpellEffect(const std::vector<Variable> &args, const RoutineContext &ctx) {
    int spell = getInt(args, 0);
    auto object = getObjectOrCaller(args, 1, ctx);
    return Variable::ofInt(static_cast<int>(object->hasSpellEffect(spell)));
}

static Variable GetEffectSpellId(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto eSpellEffect = getEffect(args, 0);

    uint32_t spellId = eSpellEffect->saveFacingInstance().spellId;
    return Variable::ofInt(static_cast<int32_t>(spellId));
}

static Variable GetCreatureHasTalent(const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto talent = getTalent(args, 0);
    const auto object = getObjectOrCaller(args, 1, ctx);
    const auto *creature = dyn_cast<Creature>(object.get());
    return Variable::ofInt(creature && creature->hasTalent(talent->type(), talent->value()));
}

static Variable GetCreatureTalentRandom(const std::vector<Variable> &args, const RoutineContext &ctx) {
    const int category = getInt(args, 0);
    const auto object = getObjectOrCaller(args, 1, ctx);
    const int inclusion = getIntOrElse(args, 2, 0);
    const auto *creature = dyn_cast<Creature>(object.get());
    return Variable::ofTalent(creature
                                  ? creature->selectTalent(category, -1, inclusion, -1, -1)
                                  : ctx.game.newTalent(Talent::kNoType, -1));
}

static Variable GetCreatureTalentBest(const std::vector<Variable> &args, const RoutineContext &ctx) {
    const int category = getInt(args, 0);
    const int crMax = getInt(args, 1);
    const auto object = getObjectOrCaller(args, 2, ctx);
    const int inclusion = getIntOrElse(args, 3, 0);
    const int excludeType = getIntOrElse(args, 4, -1);
    const int excludeId = getIntOrElse(args, 5, -1);
    const auto *creature = dyn_cast<Creature>(object.get());
    return Variable::ofTalent(creature
                                  ? creature->selectTalent(category, crMax, inclusion, excludeType, excludeId)
                                  : ctx.game.newTalent(Talent::kNoType, -1));
}

static Variable GetGoldPieceValue(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObject(args, 0, ctx);

    // Transform

    // Execute
    throw RoutineNotImplementedException("GetGoldPieceValue");
}

static Variable GetIsPlayableRacialType(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    // Transform

    // Execute
    throw RoutineNotImplementedException("GetIsPlayableRacialType");
}

static Variable JumpToLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto lDestination = getLocationArgument(args, 0);

    // Transform

    // Execute: only a creature that can be commanded jumps.
    auto caller = std::dynamic_pointer_cast<Creature>(getCaller(ctx));
    if (!caller || !caller->isCommandable()) {
        return Variable::ofNull();
    }
    auto action = ctx.game.newAction<JumpToLocationAction>(std::move(lDestination));
    caller->addActionOnTop(std::move(action));
    return Variable::ofNull();
}

static Variable GetSkillRank(const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto skill = static_cast<SkillType>(getInt(args, 0));
    const auto creature = getCreatureOrNull(args, 1, ctx);
    return Variable::ofInt(creature ? creature->getUnopposedSkillRank(skill) : -1);
}

static Variable GetAttackTarget(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrCaller(args, 0, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    auto target = creature->getAttackTarget();
    return Variable::ofObject(getObjectIdOrInvalid(target));
}

static Variable GetLastAttackType(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto creature = getCreatureOrNull(args, 0, ctx);
    return Variable::ofInt(creature ? creature->getLastAttackType() : 0);
}

static Variable GetLastAttackMode(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto creature = getCreatureOrNull(args, 0, ctx);
    return Variable::ofInt(creature ? creature->getLastAttackMode() : kObjectInvalid);
}

static Variable GetDistanceBetween2D(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObjectA = getObject(args, 0, ctx);
    auto oObjectB = getObject(args, 1, ctx);

    // Transform

    // Execute
    float distance = oObjectA->getDistanceTo(glm::vec2(oObjectB->position()));
    return Variable::ofFloat(distance);
}

static Variable GetIsInCombat(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrCaller(args, 0, ctx);
    const bool onlyCountReal = ctx.game.isTSL() && getIntOrElse(args, 1, 0) != 0;

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    return Variable::ofInt(creature->isInCombat() &&
                           (!onlyCountReal || creature->combatActivationType() == CombatActivation::Direct));
}

static Variable GetLastAssociateCommand(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oAssociate = getObjectOrCaller(args, 0, ctx);

    // Transform

    // Execute
    throw RoutineNotImplementedException("GetLastAssociateCommand");
}

// A positive amount of credits goes to a creature; a party member's go to the
// shared pool and are reported in the status summary.
static Variable GiveGoldToCreature(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto creature = getCreatureOrNull(args, 0, ctx);
    auto nGP = getInt(args, 1);
    if (nGP <= 0 || !creature) {
        return Variable::ofNull();
    }
    ctx.game.party().addCreatureGold(*creature, nGP);
    if (ctx.game.party().isMember(*creature)) {
        ctx.game.submitStatusSummary(StatusSummaryCategory::Credits, nGP);
    }
    return Variable::ofNull();
}

static Variable SetIsDestroyable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bDestroyable = getInt(args, 0);
    auto bRaiseable = getIntOrElse(args, 1, 1);
    auto bSelectableWhenDead = getIntOrElse(args, 2, 0);

    // Transform

    // Execute
    getCaller(ctx)->setDestroyability(bDestroyable != 0, bRaiseable != 0, bSelectableWhenDead != 0);
    return Variable::ofNull();
}

static Variable SetLocked(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);
    auto bLocked = getInt(args, 1);

    // Transform
    auto target = checkDoor(oTarget);
    bool locked = static_cast<bool>(bLocked);

    // Execute
    target->setLocked(locked);
    return Variable::ofNull();
}

static Variable GetLocked(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);

    // Transform
    auto target = checkDoor(oTarget);

    // Execute
    return Variable::ofInt(static_cast<int>(target->isLocked()));
}

static Variable GetClickingObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (const Variable *object = ctx.execution.findArg(ArgKind::ClickingObject)) {
        return *object;
    }
    return Variable::ofObject(kObjectInvalid);
}

static Variable SetAssociateListenPatterns(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // Does nothing in either game.
    return Variable::ofNull();
}

static Variable GetLastWeaponUsed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    auto *creature = dyn_cast<Creature>(oCreature.get());
    return Variable::ofObject(creature ? creature->lastWeaponUsed() : kObjectInvalid);
}

static Variable GetLastUsedBy(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (const Variable *usedBy = ctx.execution.findArg(ArgKind::LastUsedBy)) {
        return *usedBy;
    }
    return Variable::ofObject(kObjectInvalid);
}

static Variable GetAbilityModifier(const std::vector<Variable> &args, const RoutineContext &ctx) {
    int abilityValue = getInt(args, 0);
    auto object = args.size() > 1
                      ? getObjectOrNull(args, 1, ctx)
                      : getCaller(ctx);
    auto creature = object ? dyn_cast<Creature>(object.get()) : nullptr;
    if (!creature || abilityValue < static_cast<int>(Ability::Strength) || abilityValue > static_cast<int>(Ability::Charisma)) {
        return Variable::ofInt(0);
    }

    return Variable::ofInt(creature->getEffectiveAbilityModifier(
        static_cast<Ability>(abilityValue)));
}

static Variable GetIdentified(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObject(args, 0, ctx);

    // Transform

    // Execute
    throw RoutineNotImplementedException("GetIdentified");
}

static Variable SetIdentified(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObject(args, 0, ctx);
    auto bIdentified = getInt(args, 1);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SetIdentified");
}

static Variable GetDistanceBetweenLocations2D(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto lLocationA = getLocationArgument(args, 0);
    auto lLocationB = getLocationArgument(args, 1);

    // Transform

    // Execute
    return Variable::ofFloat(glm::distance(glm::vec2(lLocationA->position()), glm::vec2(lLocationB->position())));
}

static Variable GetDistanceToObject2D(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    auto caller = getCaller(ctx);
    float result = caller->getDistanceTo(glm::vec2(oObject->position()));
    return Variable::ofFloat(result);
}

static Variable GetBlockingDoor(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute: the door of the calling creature's last blocked event. Validity
    // is left to GetIsObjectValid, as for any other object a script holds on to.
    const Variable *caller = ctx.execution.findArg(ArgKind::Caller);
    auto creature = caller ? ctx.game.getObjectById<Creature>(caller->objectId) : nullptr;
    return Variable::ofObject(creature ? creature->lastBlockingDoorId() : kObjectInvalid);
}

static Variable GetIsDoorActionPossible(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTargetDoor = getObject(args, 0, ctx);
    auto nDoorAction = getInt(args, 1);

    // Transform
    auto targetDoor = checkDoor(oTargetDoor);
    auto doorAction = static_cast<DoorAction>(nDoorAction);

    // Execute
    bool possible = false;
    switch (doorAction) {
    case DoorAction::Open:
        possible = !targetDoor->isLocked();
        break;
    case DoorAction::Bash: {
        auto caller = std::dynamic_pointer_cast<Creature>(getCaller(ctx));
        possible = caller && canBashDoor(*targetDoor);
        break;
    }
    default:
        // UNLOCK, IGNORE, and KNOCK are not implemented.
        debug(str(boost::format("Unsupported door action: %d") % nDoorAction), LogChannel::Script);
        break;
    }

    return Variable::ofInt(static_cast<int>(possible));
}

static Variable DoDoorAction(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTargetDoor = getObject(args, 0, ctx);
    auto nDoorAction = getInt(args, 1);

    // Transform
    auto targetDoor = checkDoor(oTargetDoor);
    auto doorAction = static_cast<DoorAction>(nDoorAction);
    auto caller = getCaller(ctx);

    // Execute
    switch (doorAction) {
    case DoorAction::Open:
        // Opening is immediate: the routine returns void and the door state,
        // animation, walkmesh swap and OnOpen event are the same ones a door
        // opened through OpenDoorAction goes through. Nothing is queued, so a
        // movement action that was blocked by this door stays in place and
        // resumes once the door stops obstructing.
        if (!targetDoor->isLocked()) {
            targetDoor->open(*caller);
            targetDoor->onOpen(caller->id());
        }
        break;
    case DoorAction::Bash: {
        // Bashing is a sustained attack rather than an immediate state change,
        // so it goes on top of the queue: whatever the creature was doing stays
        // behind it instead of being discarded.
        auto creature = std::dynamic_pointer_cast<Creature>(caller);
        if (creature && canBashDoor(*targetDoor)) {
            creature->addActionOnTop(ctx.game.newAction<AttackObjectAction>(targetDoor));
        }
        break;
    }
    default:
        debug(str(boost::format("Unsupported door action: %d") % nDoorAction), LogChannel::Script);
        break;
    }

    return Variable::ofNull();
}

static Variable GetFirstItemInInventory(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObjectOrCaller(args, 0, ctx);

    // Transform

    // Execute
    auto item = oTarget->getFirstItem();
    return Variable::ofObject(getObjectIdOrInvalid(item));
}

static Variable GetNextItemInInventory(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObjectOrCaller(args, 0, ctx);

    // Transform

    // Execute
    auto item = oTarget->getNextItem();
    return Variable::ofObject(getObjectIdOrInvalid(item));
}

static Variable GetClassByPosition(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nClassPosition = getInt(args, 0);
    auto oCreature = getObjectOrCaller(args, 1, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    auto clazz = creature->attributes().getClassByPosition(nClassPosition);
    return Variable::ofInt(static_cast<int>(clazz));
}

static Variable GetLevelByPosition(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nClassPosition = getInt(args, 0);
    auto oCreature = getObjectOrCaller(args, 1, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    int level = creature->attributes().getLevelByPosition(nClassPosition);
    return Variable::ofInt(level);
}

static Variable GetLevelByClass(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nClassType = getInt(args, 0);
    auto oCreature = getObjectOrCaller(args, 1, ctx);

    // Transform
    auto creature = checkCreature(oCreature);
    auto classType = static_cast<ClassType>(nClassType);

    // Execute
    int level = creature->attributes().getClassLevel(classType);
    return Variable::ofInt(level);
}

static Variable GetDamageDealtByType(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nDamageType = getInt(args, 0);

    auto object = getCaller(ctx);

    // Execute
    switch (object->type()) {
    case ObjectType::Creature:
    case ObjectType::Door:
    case ObjectType::Placeable:
        return Variable::ofInt(object->getLastDamageAmountByType(nDamageType));
    default:
        return Variable::ofInt(0);
    }
}

static Variable GetTotalDamageDealt(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto object = getCaller(ctx);

    // Execute
    switch (object->type()) {
    case ObjectType::Creature:
    case ObjectType::Door:
    case ObjectType::Placeable:
        return Variable::ofInt(object->getTotalDamageDealt());
    default:
        return Variable::ofInt(0);
    }
}

static Variable GetLastDamager(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto object = getCaller(ctx);
    return Variable::ofObject(object->getLastDamager());
}

static Variable GetLastDisarmed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (const Variable *disarmed = ctx.execution.findArg(ArgKind::LastDisarmed)) {
        return *disarmed;
    }
    return Variable::ofObject(kObjectInvalid);
}

static Variable GetLastDisturbed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (const Variable *lastDisturbed = ctx.execution.findArg(ArgKind::LastDisturbed)) {
        return *lastDisturbed;
    }
    return Variable::ofObject(kObjectInvalid);
}

static Variable GetLastLocked(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("GetLastLocked");
}

static Variable GetLastUnlocked(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("GetLastUnlocked");
}

static Variable GetInventoryDisturbType(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (const Variable *disturbType = ctx.execution.findArg(ArgKind::InventoryDisturbType)) {
        return *disturbType;
    }
    return Variable::ofInt(0);
}

static Variable GetInventoryDisturbItem(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (const Variable *disturbItem = ctx.execution.findArg(ArgKind::InventoryDisturbItem)) {
        return *disturbItem;
    }
    return Variable::ofObject(kObjectInvalid);
}

static Variable ShowUpgradeScreen(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObjectOrNull(args, 0, ctx);
    auto oCharacter = getObjectOrNull(args, 1, ctx);
    auto nDisableItemCreation = getIntOrElse(args, 2, 0);
    auto nDisableUpgrade = getIntOrElse(args, 3, 0);
    auto sOverride2DA = getStringOrElse(args, 4, "");

    // Transform

    // Execute
    throw RoutineNotImplementedException("ShowUpgradeScreen");
}

static Variable VersusAlignmentEffect(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto eEffect = getEffect(args, 0);
    auto nLawChaos = getIntOrElse(args, 1, 0);
    auto nGoodEvil = getIntOrElse(args, 2, 0);

    if (eEffect && nLawChaos >= 0 && nLawChaos <= 3) {
        eEffect->setVersusAlignment(nLawChaos, nGoodEvil);
    }
    return Variable::ofEffect(std::move(eEffect));
}

static Variable VersusRacialTypeEffect(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto eEffect = getEffect(args, 0);
    auto nRacialType = getInt(args, 1);

    if (eEffect &&
        nRacialType >= static_cast<int>(RacialType::Unknown) &&
        nRacialType <= static_cast<int>(RacialType::All)) {
        eEffect->setVersusRacialType(nRacialType);
    }
    return Variable::ofEffect(std::move(eEffect));
}

static Variable VersusTrapEffect(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Unlike the alignment and racial type qualifiers, the trap qualifier
    // leaves the effect unchanged.
    return Variable::ofEffect(getEffect(args, 0));
}

static Variable GetGender(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    return Variable::ofInt(static_cast<int>(creature->gender()));
}

static Variable GetIsTalentValid(const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto talent = getTalent(args, 0);
    return Variable::ofInt(talent->type() != Talent::kNoType);
}

static Variable GetAttemptedAttackTarget(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto caller = checkCreature(getCaller(ctx));
    return Variable::ofObject(caller->getAttemptedAttackTarget());
}

static Variable GetTypeFromTalent(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto tTalent = getTalent(args, 0);

    // Transform

    // Execute
    return Variable::ofInt(static_cast<int>(tTalent->type()));
}

static Variable GetIdFromTalent(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto tTalent = getTalent(args, 0);

    // Transform

    // Execute
    return Variable::ofInt(tTalent->value());
}

static Variable PlayPazaak(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nOpponentPazaakDeck = getInt(args, 0);
    auto sEndScript = getString(args, 1);
    auto nMaxWager = getInt(args, 2);
    auto bShowTutorial = getIntOrElse(args, 3, 0);
    auto oOpponent = getObjectOrNull(args, 4, ctx);
    if (!oOpponent) {
        // K1's authored k_act_mispaz call omits the optional opponent. In
        // NWScript that argument defaults to OBJECT_INVALID; the script caller
        // is the only authored identity available.
        oOpponent = getCaller(ctx);
    }

    auto endScript = boost::to_lower_copy(sEndScript);

    // Execute
    ctx.game.playPazaak(
        nOpponentPazaakDeck,
        std::move(endScript),
        nMaxWager,
        bShowTutorial != 0,
        oOpponent);
    return Variable::ofNull();
}

static Variable GetLastPazaakResult(const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto &result = ctx.game.lastPazaakResult();
    if (!result) {
        return Variable::ofInt(0);
    }
    // Shipped K1 scripts consume 1 as a player win and 0 as a player loss.
    // Forfeit follows the loss path.
    return Variable::ofInt(
        *result == PazaakCompletedResult::PlayerWon ? 1 : 0);
}

// The text floats over the object for a few seconds. A constant outside the
// table, a blank string reference or a string reference of 0 shows
// "BAD STRREF".
static Variable DisplayFeedBackText(const std::vector<Variable> &args, const RoutineContext &ctx) {
    static constexpr float kFeedbackTextSeconds = 5.0f;
    static constexpr char kBadStrRefText[] = "BAD STRREF";

    auto object = getObject(args, 0, ctx);
    int textConstant = getInt(args, 1);

    std::string text {kBadStrRefText};
    auto feedback = getRequiredTwoDA(
        ctx.services.resource.twoDas,
        "feedbacktext");
    if (textConstant >= 0 && textConstant < feedback->getRowCount()) {
        if (auto strRef = feedback->getIntOpt(textConstant, "strref")) {
            if (*strRef != 0) {
                text = ctx.services.resource.strings.getText(*strRef);
            }
        }
    }
    object->setFeedbackText(std::move(text), kFeedbackTextSeconds);
    return Variable::ofNull();
}

static Variable AddJournalQuestEntry(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto szPlotID = getString(args, 0);
    auto nState = getInt(args, 1);
    auto bAllowOverrideHigher = getIntOrElse(args, 2, 0);

    // Transform
    auto allowOverrideHigher = static_cast<bool>(bAllowOverrideHigher);

    // Execute
    ctx.game.journal().addEntry(szPlotID, nState, allowOverrideHigher);
    return Variable::ofNull();
}

static Variable RemoveJournalQuestEntry(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto szPlotID = getString(args, 0);

    // Execute
    ctx.game.journal().removeEntry(szPlotID);
    return Variable::ofNull();
}

static Variable GetJournalEntry(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto szPlotID = getString(args, 0);

    // Execute
    return Variable::ofInt(ctx.game.journal().getEntryState(szPlotID));
}

static Variable PlayRumblePattern(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nPattern = getInt(args, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("PlayRumblePattern");
}

static Variable StopRumblePattern(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nPattern = getInt(args, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("StopRumblePattern");
}

// An empty text adds no line to the message list.
static void addScriptFloatingText(std::string text, const RoutineContext &ctx) {
    if (text.empty()) {
        return;
    }
    ctx.game.messageLog().add(MessageLog::kFeedbackMessageType, MessageLog::Style::Normal, std::move(text));
}

// The text reaches the message list only when the creature is the controlled
// one.
static Variable SendMessageToPC(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oPlayer = getObjectOrNull(args, 0, ctx);
    auto szMessage = getString(args, 1);

    // Execute
    if (oPlayer && oPlayer == ctx.game.party().getLeader()) {
        addScriptFloatingText(szMessage, ctx);
    }
    return Variable::ofNull();
}

static Variable GetAttemptedSpellTarget(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto creature = dyn_cast<Creature>(getCaller(ctx));
    auto target = creature ? creature->attemptedSpellTarget() : nullptr;
    return Variable::ofObject(target ? target->id() : kObjectInvalid);
}

static Variable GetLastOpenedBy(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (const Variable *opened = ctx.execution.findArg(ArgKind::LastOpenedBy)) {
        return *opened;
    }
    return Variable::ofObject(kObjectInvalid);
}

// Whether a class of the creature knows the power and the creature can meet
// its Force point cost; anything else has no powers.
static Variable GetHasSpell(const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto spell = ctx.services.game.spells.get(static_cast<SpellType>(getInt(args, 0)));
    const auto creature = getCreatureOrNull(args, 1, ctx);
    return Variable::ofInt(creature && spell && creature->hasAffordableSpell(*spell));
}

static Variable OpenStore(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oStore = getObject(args, 0, ctx);
    auto oPC = getObject(args, 1, ctx);
    auto nBonusMarkUp = getIntOrElse(args, 2, 0);
    auto nBonusMarkDown = getIntOrElse(args, 3, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("OpenStore");
}

// Any object with a faction names the faction to step through; only
// creatures are its members. bPCOnly picks player characters when 1 and every
// other creature when 0; any other value picks no one.
static Variable stepFactionMembers(const std::vector<Variable> &args, const RoutineContext &ctx, bool first) {
    auto memberOfFaction = getObjectOrNull(args, 0, ctx);
    const int pc = getIntOrElse(args, 1, 1);
    if (!memberOfFaction) {
        return Variable::ofObject(kObjectInvalid);
    }
    auto faction = getObjectFaction(*memberOfFaction);
    if (!faction || !isKnownFaction(*faction, ctx)) {
        return Variable::ofObject(kObjectInvalid);
    }

    auto area = ctx.game.module()->area();
    auto member = first ? area->getFirstFactionMember(*faction, pc) : area->getNextFactionMember(*faction, pc);
    return Variable::ofObject(member ? member->id() : kObjectInvalid);
}

static Variable GetFirstFactionMember(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return stepFactionMembers(args, ctx, true);
}

static Variable GetNextFactionMember(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return stepFactionMembers(args, ctx, false);
}

static Variable GetJournalQuestExperience(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto szPlotID = getString(args, 0);

    // Execute
    return Variable::ofInt(ctx.game.getPlotXP(szPlotID));
}

static Variable JumpToObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oToJumpTo = getObject(args, 0, ctx);
    auto nWalkStraightLineToPoint = getIntOrElse(args, 1, 1);

    // Transform
    auto walkStraightLineToPoint = static_cast<bool>(nWalkStraightLineToPoint);

    // Execute: only a creature that can be commanded jumps.
    auto caller = std::dynamic_pointer_cast<Creature>(getCaller(ctx));
    if (!caller || !caller->isCommandable()) {
        return Variable::ofNull();
    }
    auto action = ctx.game.newAction<JumpToObjectAction>(std::move(oToJumpTo), walkStraightLineToPoint);
    caller->addActionOnTop(std::move(action));
    return Variable::ofNull();
}

static Variable SetMapPinEnabled(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oMapPin = getObject(args, 0, ctx);
    auto nEnabled = getInt(args, 1);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SetMapPinEnabled");
}

static Variable PopUpGUIPanel(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oPC = getObject(args, 0, ctx);
    auto nGUIPanel = getInt(args, 1);

    // Transform

    // Execute
    throw RoutineNotImplementedException("PopUpGUIPanel");
}

static Variable AddMultiClass(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nClassType = getInt(args, 0);
    auto oSource = getObject(args, 1, ctx);

    // Transform

    // Execute
    throw RoutineNotImplementedException("AddMultiClass");
}

static Variable GetIsLinkImmune(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);
    auto eEffect = getEffect(args, 1);

    auto *creature = dyn_cast<Creature>(oTarget.get());
    return Variable::ofInt(creature && creature->isEffectLinkImmune(*eEffect) ? 1 : 0);
}

static Variable GiveXPToCreature(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);
    auto nXpAmount = getInt(args, 1);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute: the party receives the award whichever creature is named; only
    // a party member's award reaches the status summary.
    if (nXpAmount <= 0) {
        return Variable::ofNull();
    }
    ctx.game.party().awardXP(nXpAmount, ctx.game.party().isMember(*creature) ? XPSource::Plot : XPSource::Script);
    return Variable::ofNull();
}

static Variable SetXP(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);
    auto nXpAmount = getInt(args, 1);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute: only the named creature changes, and a script cannot lower
    // its experience.
    if (nXpAmount >= creature->xp()) {
        creature->setXP(nXpAmount);
    }
    return Variable::ofNull();
}

static Variable GetXP(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    return Variable::ofInt(creature->xp());
}

static Variable IntToHexString(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nInteger = getInt(args, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("IntToHexString");
}

static Variable GetBaseItemType(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObject(args, 0, ctx);

    // Transform
    auto item = checkItem(oItem);

    // Execute
    return Variable::ofInt(item->baseItemType());
}

static Variable GetItemHasItemProperty(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObjectOrNull(args, 0, ctx);
    auto nProperty = getInt(args, 1);

    // Transform
    auto item = oItem ? dyn_cast<Item>(oItem) : nullptr;

    // Execute: only the property type is compared. A use property counts only
    // when its upgrade is installed; any other property counts regardless.
    if (item) {
        for (const auto &property : item->properties()) {
            if (property.propertyName != nProperty) {
                continue;
            }
            if (!Item::isUseProperty(property.propertyName) || item->isPropertyActive(property)) {
                return Variable::ofInt(1);
            }
        }
    }
    return Variable::ofInt(0);
}

static Variable GetItemACValue(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObjectOrNull(args, 0, ctx);

    // Transform
    auto item = oItem ? dyn_cast<Item>(oItem) : nullptr;

    // Execute: every armour class bonus counts, installed upgrade or not.
    return Variable::ofInt(item ? item->armorValue(false) : 0);
}

static Variable ExploreAreaForPlayer(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getObject(args, 0, ctx);
    auto oPlayer = getObject(args, 1, ctx);

    // Transform

    // Execute
    throw RoutineNotImplementedException("ExploreAreaForPlayer");
}

static Variable GetIsDay(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("GetIsDay");
}

static Variable GetIsNight(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("GetIsNight");
}

static Variable GetIsDawn(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("GetIsDawn");
}

static Variable GetIsDusk(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("GetIsDusk");
}

static Variable GetIsEncounterCreature(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrCaller(args, 0, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    return Variable::ofInt(creature->isEncounterCreature() ? 1 : 0);
}

// Nothing in either game ever notes a dying player.
static Variable GetLastPlayerDying(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofObject(kObjectInvalid);
}

static Variable GetStartingLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("GetStartingLocation");
}

static Variable ChangeToStandardFaction(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreatureToChange = getObjectOrNull(args, 0, ctx);
    auto nStandardFaction = getInt(args, 1);

    // Execute: only an NPC faction is taken, and a player character keeps
    // its own. A placeable, and in TSL a trigger, takes the faction directly.
    const size_t factionCount = ctx.game.services().game.reputes.state().factions.size();
    if (!oCreatureToChange || nStandardFaction < 1 || static_cast<size_t>(nStandardFaction) >= factionCount) {
        return Variable::ofNull();
    }
    const auto faction = static_cast<Faction>(nStandardFaction);
    if (auto creature = dyn_cast<Creature>(oCreatureToChange)) {
        if (!creature->isPC()) {
            creature->setFaction(faction);
        }
    } else if (auto placeable = dyn_cast<Placeable>(oCreatureToChange)) {
        placeable->setFaction(faction);
    } else if (auto trigger = dyn_cast<Trigger>(oCreatureToChange); trigger && ctx.game.isTSL()) {
        trigger->setFaction(faction);
    }
    return Variable::ofNull();
}

static Variable SoundObjectPlay(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oSound = getObject(args, 0, ctx);

    // Transform
    auto sound = checkSound(oSound);

    // Execute
    sound->setActive(true);
    return Variable::ofNull();
}

static Variable SoundObjectStop(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oSound = getObject(args, 0, ctx);

    // Transform
    auto sound = checkSound(oSound);

    // Execute
    sound->setActive(false);
    return Variable::ofNull();
}

static Variable SoundObjectSetVolume(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oSound = getObject(args, 0, ctx);
    auto nVolume = getInt(args, 1);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SoundObjectSetVolume");
}

static Variable SoundObjectSetPosition(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oSound = getObject(args, 0, ctx);
    auto vPosition = getVector(args, 1);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SoundObjectSetPosition");
}

static Variable SpeakOneLinerConversation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sDialogResRef = getStringOrElse(args, 0, "");
    auto oTokenTarget = getObjectOrNull(args, 1, ctx);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SpeakOneLinerConversation");
}

static Variable GetGold(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObjectOrCaller(args, 0, ctx);

    // Transform
    auto creature = checkCreature(oTarget);

    // Execute
    // Credits are a single party-shared pool in KOTOR; party members (incl. the
    // PC speaker / leader) report the party total.
    if (creature && ctx.game.party().isMember(*creature)) {
        return Variable::ofInt(ctx.game.party().gold());
    }
    return Variable::ofInt(creature->gold());
}

static Variable GetLastRespawnButtonPresser(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("GetLastRespawnButtonPresser");
}

static Variable SetLightsaberPowered(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto creature = getCreatureOrNull(args, 0, ctx);
    auto bOverride = getInt(args, 1);
    auto bPowered = getIntOrElse(args, 2, 1);
    auto bShowTransition = getIntOrElse(args, 3, 0);

    // Execute
    // Anything but a creature is ignored. Both hand items are powered on or
    // off, with the power-up or power-down clip and sound only when the
    // transition is shown.
    if (creature) {
        creature->overrideLightsabers(bOverride, bPowered != 0, bShowTransition != 0);
    }
    return Variable::ofNull();
}

static Variable GetIsWeaponEffective(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto oVersus = getObjectOrNull(args, 0, ctx);
    auto bOffHand = getIntOrElse(args, 1, 0);

    // A caller other than a creature has no weapon to test.
    const Variable *callerArg = ctx.execution.findArg(ArgKind::Caller);
    auto caller = ctx.game.getObjectById<Creature>(callerArg ? callerArg->objectId : kObjectInvalid);
    if (!caller) {
        return Variable::ofInt(0);
    }
    std::optional<std::reference_wrapper<Object>> versus;
    if (oVersus) {
        versus = std::ref(*oVersus);
    }
    return Variable::ofInt(caller->isWeaponEffective(versus, bOffHand != 0) ? 1 : 0);
}

static Variable GetLastSpellHarmful(const std::vector<Variable> &args, const RoutineContext &ctx) {
    if (const Variable *harmful = ctx.execution.findArg(ArgKind::LastSpellHarmful)) {
        return *harmful;
    }
    return Variable::ofInt(0);
}

static Variable EventActivateItem(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObjectOrNull(args, 0, ctx);
    auto lTarget = getLocationArgument(args, 1);
    auto oTarget = getObjectOrNull(args, 2, ctx);

    // Transform: the script caller activates; the location keeps only its
    // position. An item or caller that doesn't exist is carried as none.
    auto caller = getCallerOrNull(ctx);
    const glm::vec3 &position = lTarget->position();

    // Execute
    return Variable::ofEvent(ctx.game.newEvent(static_cast<int>(SavedSignalEventType::ItemActivated),
                                               std::vector<int32_t> {}, std::vector<float> {position.x, position.y, position.z},
                                               std::vector<std::string> {},
                                               std::vector<std::shared_ptr<Object>> {oItem, caller, nullptr, oTarget}));
}

static Variable MusicBackgroundPlay(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getAreaOrNull(args, 0, ctx);

    // Execute
    if (oArea) {
        oArea->playMusic(true);
    }
    return Variable::ofNull();
}

static Variable MusicBackgroundStop(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getAreaOrNull(args, 0, ctx);

    // Execute
    if (oArea) {
        oArea->playMusic(false);
    }
    return Variable::ofNull();
}

static Variable MusicBackgroundSetDelay(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getAreaOrNull(args, 0, ctx);
    auto nDelay = getInt(args, 1);

    // Execute
    if (oArea) {
        oArea->setMusicDelay(nDelay);
    }
    return Variable::ofNull();
}

static Variable MusicBackgroundChangeDay(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getAreaOrNull(args, 0, ctx);
    auto nTrack = getInt(args, 1);

    // Execute
    if (oArea) {
        oArea->setMusicDayTrack(nTrack);
    }
    return Variable::ofNull();
}

static Variable MusicBackgroundChangeNight(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getAreaOrNull(args, 0, ctx);
    auto nTrack = getInt(args, 1);

    // Execute
    if (oArea) {
        oArea->setMusicNightTrack(nTrack);
    }
    return Variable::ofNull();
}

static Variable MusicBattlePlay(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getAreaOrNull(args, 0, ctx);

    // Execute
    if (oArea) {
        oArea->playBattleMusic(true);
    }
    return Variable::ofNull();
}

static Variable MusicBattleStop(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getAreaOrNull(args, 0, ctx);

    // Execute
    if (oArea) {
        oArea->playBattleMusic(false);
    }
    return Variable::ofNull();
}

static Variable MusicBattleChange(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getAreaOrNull(args, 0, ctx);
    auto nTrack = getInt(args, 1);

    // Execute
    if (oArea) {
        oArea->setBattleMusicTrack(nTrack);
    }
    return Variable::ofNull();
}

static Variable AmbientSoundPlay(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getAreaOrNull(args, 0, ctx);

    // Execute
    if (oArea) {
        oArea->playAmbientSound(true);
    }
    return Variable::ofNull();
}

static Variable AmbientSoundStop(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getAreaOrNull(args, 0, ctx);

    // Execute
    if (oArea) {
        oArea->playAmbientSound(false);
    }
    return Variable::ofNull();
}

static Variable AmbientSoundChangeDay(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getAreaOrNull(args, 0, ctx);
    auto nTrack = getInt(args, 1);

    // Execute
    if (oArea) {
        oArea->setAmbientSoundDayTrack(nTrack);
    }
    return Variable::ofNull();
}

static Variable AmbientSoundChangeNight(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getAreaOrNull(args, 0, ctx);
    auto nTrack = getInt(args, 1);

    // Execute
    if (oArea) {
        oArea->setAmbientSoundNightTrack(nTrack);
    }
    return Variable::ofNull();
}

static Variable GetLastKiller(const std::vector<Variable> &, const RoutineContext &ctx) {
    const auto caller = getCallerOrNull(ctx);
    return Variable::ofObject(caller ? caller->getLastKiller() : script::kObjectInvalid);
}

static Variable GetSpellCastItem(const std::vector<Variable> &, const RoutineContext &ctx) {
    const auto caller = getCallerOrNull(ctx);
    const auto item = caller && isa<Creature>(caller) ? caller->spellScriptContext().item() : nullptr;
    return Variable::ofObject(item ? item->id() : script::kObjectInvalid);
}

static Variable GetItemActivated(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofObject(ctx.game.module()->itemEvents().activated);
}

static Variable GetItemActivator(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofObject(ctx.game.module()->itemEvents().activator);
}

static Variable GetItemActivatedTargetLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute: the facing points from the activator to the stored position.
    const auto &events = ctx.game.module()->itemEvents();
    glm::vec3 orientation {0.0f};
    auto activator = ctx.game.getObjectById<Creature>(events.activator);
    if (activator && activator->position() != events.activatedPosition) {
        orientation = glm::normalize(events.activatedPosition - activator->position());
    }
    return Variable::ofLocation(ctx.game.newLocation(events.activatedPosition, orientation));
}

static Variable GetItemActivatedTarget(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofObject(ctx.game.module()->itemEvents().activatedTarget);
}

static Variable GetIsOpen(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    return Variable::ofInt(static_cast<int>(oObject->isOpen()));
}

// The caller takes up to the creature's credits: a creature caller keeps them,
// a placeable caller with an inventory holds them as a stack of credits, unless
// they are destroyed. A party member's loss is reported in the status summary.
static Variable TakeGoldFromCreature(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto nAmount = getInt(args, 0);
    auto creatureToTakeFrom = getCreatureOrNull(args, 1, ctx);
    auto bDestroy = getIntOrElse(args, 2, 0);
    if (nAmount <= 0 || !creatureToTakeFrom) {
        return Variable::ofNull();
    }

    auto &party = ctx.game.party();
    const int amount = std::min(nAmount, party.creatureGold(*creatureToTakeFrom));
    auto caller = getCaller(ctx);
    // Neither an area nor the module takes gold.
    if (amount <= 0 || caller->type() == ObjectType::Area || caller->type() == ObjectType::Module) {
        return Variable::ofNull();
    }

    party.removeCreatureGold(*creatureToTakeFrom, amount);
    if (!bDestroy) {
        if (auto *creature = dyn_cast<Creature>(caller.get())) {
            party.addCreatureGold(*creature, amount);
        } else if (auto *placeable = dyn_cast<Placeable>(caller.get()); placeable && placeable->hasInventory()) {
            placeable->addItem(Item::kCreditsResRef, amount);
        } else if (auto *item = dyn_cast<Item>(caller.get()); item && item->isContainer()) {
            item->addItem(Item::kCreditsResRef, amount);
        }
    }
    if (party.isMember(*creatureToTakeFrom)) {
        ctx.game.submitStatusSummary(StatusSummaryCategory::Credits, -amount);
    }
    return Variable::ofNull();
}

static Variable GetIsInConversation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);
    // Execute
    return Variable::ofInt(oObject->isInConversation());
}

static Variable GetPlotFlag(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObjectOrCaller(args, 0, ctx);

    // Transform

    // Execute
    bool plotFlag = oTarget->plotFlag();
    return Variable::ofInt(static_cast<int>(plotFlag));
}

static Variable SetPlotFlag(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);
    auto nPlotFlag = getInt(args, 1);

    // Transform
    bool plotFlag = static_cast<bool>(nPlotFlag);

    // Execute
    oTarget->setPlotFlag(plotFlag);
    return Variable::ofNull();
}

static Variable SetDialogPlaceableCamera(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nCameraId = getInt(args, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SetDialogPlaceableCamera");
}

static Variable GetSoloMode(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    bool solo = ctx.game.party().isSoloMode();
    return Variable::ofInt(static_cast<int>(solo));
}

static Variable GetMaxStealthXP(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofInt(ctx.game.module()->area()->maxStealthXP());
}

static Variable SetMaxStealthXP(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nMax = getInt(args, 0);

    // Transform

    // Execute
    ctx.game.module()->area()->setMaxStealthXP(nMax);
    return Variable::ofNull();
}

static Variable GetCurrentStealthXP(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    int xp = ctx.game.module()->area()->currentStealthXP();
    return Variable::ofInt(xp);
}

static Variable GetNumStackedItems(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    if (args.empty() || args[0].type != VariableType::Object) {
        return Variable::ofInt(-1);
    }

    // Transform
    uint32_t objectId = args[0].objectId;
    if (objectId == kObjectSelf) {
        if (const Variable *caller = ctx.execution.findArg(ArgKind::Caller)) {
            objectId = caller->objectId;
        } else {
            objectId = kObjectInvalid;
        }
    }
    auto object = ctx.game.getObjectById(objectId);
    if (!object || object->type() != ObjectType::Item) {
        return Variable::ofInt(-1);
    }
    auto item = std::static_pointer_cast<Item>(object);

    // Execute
    return Variable::ofInt(item->stackSize());
}

// Only a creature other than a player character can surrender.
static void surrenderCaller(const RoutineContext &ctx, bool retainOwnEffects) {
    auto caller = dyn_cast<Creature>(getCaller(ctx));
    if (caller && !caller->isPC()) {
        caller->surrenderToEnemies(retainOwnEffects);
    }
}

static Variable SurrenderToEnemies(const std::vector<Variable> &args, const RoutineContext &ctx) {
    surrenderCaller(ctx, false);
    return Variable::ofNull();
}

static Variable SetCurrentStealthXP(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nCurrent = getInt(args, 0);

    // Transform

    // Execute
    ctx.game.module()->area()->setCurrentStealthXP(nCurrent);
    return Variable::ofNull();
}

static Variable GetCreatureSize(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto object = getObjectOrNull(args, 0, ctx);
    auto creature = object ? dyn_cast<Creature>(object.get()) : nullptr;
    return Variable::ofInt(static_cast<int>(
        creature ? creature->size() : CreatureSize::Invalid));
}

static Variable AwardStealthXP(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObjectOrNull(args, 0, ctx);

    // Execute: a creature's area pays out its stealth experience once, and
    // stealth experience is then off there.
    auto area = ctx.game.module()->area();
    if (!oTarget || !isa<Creature>(oTarget)) {
        return Variable::ofNull();
    }
    if (!area->isStealthXPEnabled() || area->currentStealthXP() == 0) {
        return Variable::ofNull();
    }
    ctx.game.party().awardXP(area->currentStealthXP(), XPSource::Stealth);
    area->setCurrentStealthXP(0);
    area->setStealthXPEnabled(false);
    return Variable::ofNull();
}

static Variable GetStealthXPEnabled(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofInt(ctx.game.module()->area()->isStealthXPEnabled() ? 1 : 0);
}

static Variable SetStealthXPEnabled(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bEnabled = getInt(args, 0);

    // Transform
    bool enabled = static_cast<bool>(bEnabled);

    // Execute
    ctx.game.module()->area()->setStealthXPEnabled(enabled);
    return Variable::ofNull();
}

// Trap routines answer for triggers, doors and placeables; any other object
// or an invalid one gets the routine's default.
template <class Visit>
static Variable visitTrap(const std::shared_ptr<Object> &object, Variable fallback, Visit visit) {
    if (!object) {
        return fallback;
    }
    if (auto *trigger = dyn_cast<Trigger>(object.get())) {
        return visit(*trigger);
    }
    if (auto *door = dyn_cast<Door>(object.get())) {
        return visit(*door);
    }
    if (auto *placeable = dyn_cast<Placeable>(object.get())) {
        return visit(*placeable);
    }
    return fallback;
}

static uint32_t getObjectIdOrCaller(const std::vector<Variable> &args, int index, const RoutineContext &ctx) {
    if (index >= static_cast<int>(args.size()) || args[index].type != VariableType::Object) {
        return kObjectInvalid;
    }
    uint32_t id = args[index].objectId;
    if (id == kObjectSelf) {
        const Variable *caller = ctx.execution.findArg(ArgKind::Caller);
        id = caller ? caller->objectId : kObjectInvalid;
    }
    return id;
}

static Variable GetLastTrapDetected(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Nothing records the last detected trap.
    return Variable::ofObject(kObjectInvalid);
}

static Variable GetNearestTrapToObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObjectOrNull(args, 0, ctx);
    auto nTrapDetected = getIntOrElse(args, 1, 1);

    // Execute
    if (!oTarget) {
        return Variable::ofObject(kObjectInvalid);
    }
    auto area = ctx.game.module()->area();
    std::shared_ptr<Object> nearest;
    float nearestDistance2 = 1e8f;
    for (const auto &object : area->objects()) {
        // Candidates are the area's traps; only a request for detected traps
        // also requires the target to have found them.
        const auto candidate = visitTrap(object, Variable::ofInt(0), [&oTarget, nTrapDetected](auto &trap) {
            return Variable::ofInt(trap.isTrapped() &&
                                           (nTrapDetected != 1 || trap.trapDetection().isDetectedBy(oTarget->id()))
                                       ? 1
                                       : 0);
        });
        if (candidate.intValue == 0) {
            continue;
        }
        const float distance2 = oTarget->getSquareDistanceTo(*object);
        if (distance2 < nearestDistance2) {
            nearestDistance2 = distance2;
            nearest = object;
        }
    }
    return Variable::ofObject(nearest ? nearest->id() : kObjectInvalid);
}

static Variable GetAttemptedMovementTarget(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto caller = dyn_cast<Creature>(getCaller(ctx).get());
    return Variable::ofObject(caller ? caller->attemptedMovementTarget() : kObjectInvalid);
}

// The creature a walking creature last walked into and planned a way round.
// Any other object answers OBJECT_INVALID.
static Variable GetBlockingCreature(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObjectOrNull(args, 0, ctx);

    // Transform
    auto creature = dyn_cast<Creature>(oTarget.get());

    // Execute
    return Variable::ofObject(creature ? creature->blockingCreature() : kObjectInvalid);
}

// Creatures answer with their current save; doors and placeables with the
// fixed save from their template. Any other object answers 0.
static Variable getScriptSavingThrow(
    SavingThrow save, const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto object = getObjectOrNull(args, 0, ctx);
    if (!object) {
        return Variable::ofInt(0);
    }
    if (const auto creature = dyn_cast<Creature>(object)) {
        return Variable::ofInt(creature->getSavingThrow(save));
    }
    if (const auto door = dyn_cast<Door>(object)) {
        return Variable::ofInt(door->savingThrow(save));
    }
    if (const auto placeable = dyn_cast<Placeable>(object)) {
        return Variable::ofInt(placeable->savingThrow(save));
    }
    return Variable::ofInt(0);
}

static Variable GetFortitudeSavingThrow(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return getScriptSavingThrow(SavingThrow::Fortitude, args, ctx);
}
static Variable GetWillSavingThrow(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return getScriptSavingThrow(SavingThrow::Will, args, ctx);
}
static Variable GetReflexSavingThrow(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return getScriptSavingThrow(SavingThrow::Reflex, args, ctx);
}

static Variable GetChallengeRating(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrNull(args, 0, ctx);

    // Transform
    auto creature = oCreature ? dyn_cast<Creature>(oCreature) : nullptr;

    // Execute
    return Variable::ofFloat(creature ? creature->challengeRating() : 0.0f);
}

// The last hostile creature a walking creature found blocking it or standing
// on its way round. Any other object answers OBJECT_INVALID.
static Variable GetFoundEnemyCreature(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObjectOrNull(args, 0, ctx);

    // Transform
    auto creature = dyn_cast<Creature>(oTarget.get());

    // Execute
    return Variable::ofObject(creature ? creature->foundEnemyCreature() : kObjectInvalid);
}

static Variable GetMovementRate(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrNull(args, 0, ctx);

    // Execute
    auto creature = dyn_cast<Creature>(oCreature.get());
    return Variable::ofInt(creature ? creature->movementRateRow() : 0);
}

static Variable GetSubRace(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    return Variable::ofInt(static_cast<int>(creature->subrace()));
}

static Variable GetStealthXPDecrement(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("GetStealthXPDecrement");
}

static Variable SetStealthXPDecrement(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nDecrement = getInt(args, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SetStealthXPDecrement");
}

static Variable DuplicateHeadAppearance(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oidCreatureToChange = getObject(args, 0, ctx);
    auto oidCreatureToMatch = getObject(args, 1, ctx);

    // Transform

    // Execute
    throw RoutineNotImplementedException("DuplicateHeadAppearance");
}

static Variable CutsceneAttack(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObjectOrNull(args, 0, ctx);
    auto nAnimation = getInt(args, 1);
    auto nAttackResult = getInt(args, 2);
    auto nDamage = getInt(args, 3);

    // Transform
    const struct CutsceneAttack cutscene {
        nAnimation, nAttackResult, nDamage
    };

    // Execute
    // An ordinary attack on the caller's round, with its swing, result and
    // damage forced. A caller that is not a creature does nothing.
    auto caller = std::dynamic_pointer_cast<Creature>(getCaller(ctx));
    if (caller) {
        ctx.game.combat().scheduleAttack(*caller, oTarget, FeatType::Invalid, cutscene);
    }
    return Variable::ofNull();
}

static Variable SetCameraMode(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oPlayer = getObject(args, 0, ctx);
    auto nCameraMode = getInt(args, 1);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SetCameraMode");
}

static Variable SetLockOrientationInDialog(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObjectOrNull(args, 0, ctx);
    auto nValue = getInt(args, 1);

    // Execute: 1 locks, anything else unlocks.
    if (oObject) {
        ctx.game.setDialogOrientationLocked(oObject->id(), nValue == 1);
    }
    return Variable::ofNull();
}

static Variable SetLockHeadFollowInDialog(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObjectOrNull(args, 0, ctx);
    auto nValue = getInt(args, 1);

    // Execute: 1 locks and ends any head look, anything else unlocks.
    if (!oObject) {
        return Variable::ofNull();
    }
    ctx.game.setDialogHeadFollowLocked(oObject->id(), nValue == 1);
    if (nValue == 1) {
        if (auto *creature = dyn_cast<Creature>(oObject.get())) {
            creature->lookAt(nullptr, 0.0f);
        }
    }
    return Variable::ofNull();
}

// The caller moves, to the object if it is still there when the move is
// taken, else to the point.
static Variable CutsceneMove(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObjectOrNull(args, 0, ctx);
    auto vPosition = getVector(args, 1);
    auto nRun = getInt(args, 2);

    // Execute
    // Only a creature moves.
    auto caller = std::dynamic_pointer_cast<Creature>(getCaller(ctx));
    if (!caller) {
        return Variable::ofNull();
    }
    ctx.game.combat().scheduleCutsceneMove(*caller, oObject, vPosition, nRun != 0);
    return Variable::ofNull();
}

static Variable EnableVideoEffect(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nEffectType = getInt(args, 0);

    // Execute
    ctx.game.enableScriptVideoEffect(nEffectType);
    return Variable::ofNull();
}

static Variable StartNewModule(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sModuleName = getString(args, 0);
    auto sWayPoint = getStringOrElse(args, 1, "");
    auto sMovie1 = getStringOrElse(args, 2, "");
    auto sMovie2 = getStringOrElse(args, 3, "");
    auto sMovie3 = getStringOrElse(args, 4, "");
    auto sMovie4 = getStringOrElse(args, 5, "");
    auto sMovie5 = getStringOrElse(args, 6, "");
    auto sMovie6 = getStringOrElse(args, 7, "");

    // Transform
    auto moduleName = boost::to_lower_copy(sModuleName);
    auto waypoint = boost::to_lower_copy(sWayPoint);
    std::vector<std::string> movies;
    auto addMovie = [&movies](const std::string &movie) {
        if (!movie.empty()) {
            movies.push_back(boost::to_lower_copy(movie));
        }
    };
    addMovie(sMovie1);
    addMovie(sMovie2);
    addMovie(sMovie3);
    addMovie(sMovie4);
    addMovie(sMovie5);
    addMovie(sMovie6);

    // Execute
    if (movies.empty()) {
        ctx.game.scheduleModuleTransition(moduleName, waypoint);
    } else {
        ctx.game.scheduleModuleTransitionWithMovies(moduleName, waypoint, std::move(movies));
    }
    return Variable::ofNull();
}

static Variable DisableVideoEffect(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    ctx.game.disableScriptVideoEffect();
    return Variable::ofNull();
}

static Variable GetWeaponRanged(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObjectOrNull(args, 0, ctx);

    // Execute
    // Anything but an item is not a ranged weapon.
    auto item = oItem ? dyn_cast<Item>(oItem) : nullptr;
    return Variable::ofInt(item && item->isRanged() ? 1 : 0);
}

static Variable DoSinglePlayerAutoSave(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    ctx.game.requestAutoSave();
    return Variable::ofNull();
}

static Variable GetGameDifficulty(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // TSL scripts read the difficulty chosen in the gameplay options; KotOR
    // scripts always read the normal difficulty.
    return Variable::ofInt(ctx.game.isTSL()
                               ? ctx.game.options().game.clientDifficulty
                               : static_cast<int>(GameDifficulty::Normal));
}

static Variable GetUserActionsPending(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto caller = checkCreature(getCaller(ctx));
    return Variable::ofInt(static_cast<int>(caller->hasUserActionsPending()));
}

static Variable RevealMap(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vPoint = getVectorOrElse(args, 0, glm::vec3(0.0f, 0.0f, 0.0f));
    auto nRadius = getIntOrElse(args, 1, -1);

    // Transform

    // Execute
    throw RoutineNotImplementedException("RevealMap");
}

static Variable SetTutorialWindowsEnabled(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bEnabled = getInt(args, 0);

    // Transform

    // Execute: the configuration is not written.
    ctx.game.setTutorialWindowsEnabled(bEnabled != 0);
    return Variable::ofNull();
}

static Variable ShowTutorialWindow(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nWindow = getInt(args, 0);

    // Transform: KotOR scripts name three windows by their own numbers, 0 to
    // 2; any other number shows nothing.
    static constexpr std::array<int, 3> kKotorScriptTutorialWindows {9, 40, 42};
    int id = nWindow;
    if (!ctx.game.isTSL()) {
        if (nWindow < 0 || nWindow >= static_cast<int>(kKotorScriptTutorialWindows.size())) {
            return Variable::ofNull();
        }
        id = kKotorScriptTutorialWindows[nWindow];
    }

    // Execute
    ctx.game.requestTutorialWindow(id);
    return Variable::ofNull();
}

// The optional second argument names the music played with the credits. The
// sequel's routine does nothing.
static Variable StartCreditSequence(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bTransparentBackground = getInt(args, 0);
    auto sMusic = getStringOrElse(args, 1, "");

    // Execute
    if (!ctx.game.isTSL()) {
        ctx.game.startCreditSequence(bTransparentBackground != 0, sMusic);
    }
    return Variable::ofNull();
}

static Variable IsCreditSequenceInProgress(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofInt(ctx.game.isCreditSequenceInProgress() ? 1 : 0);
}

static Variable GetCurrentAction(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = args.empty() ? getCaller(ctx) : getObjectOrNull(args, 0, ctx);

    // Execute
    // An object that does not exist, or has no action queue, reports
    // ACTION_INVALID.
    if (!oObject || oObject->type() == ObjectType::Area || oObject->type() == ObjectType::Module) {
        return Variable::ofInt(static_cast<int>(ActionType::Invalid));
    }
    return Variable::ofInt(oObject->currentScriptAction());
}

// The multiplier of the chosen difficulty (difficultyopt.2da MULTIPLIER).
static Variable GetDifficultyModifier(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    const int difficulty = ctx.game.options().game.clientDifficulty;
    return Variable::ofFloat(ctx.services.game.difficultyOptions.get(difficulty).damageMultiplier);
}

static Variable GetAppearanceType(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto object = getObjectOrNull(args, 0, ctx);
    auto creature = object ? dyn_cast<Creature>(object.get()) : nullptr;
    return Variable::ofInt(creature ? creature->appearance() : 0);
}

// Script floating text is shown as a feedback line, and only when it is not
// broadcast and floats above the controlled creature.
static bool isScriptFloatingTextShown(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto object = getObjectOrNull(args, 1, ctx);
    auto creature = object ? dyn_cast<Creature>(object.get()) : nullptr;
    if (!creature || getIntOrElse(args, 2, 1) != 0) {
        return false;
    }
    auto leader = ctx.game.party().getLeader();
    return leader && leader->id() == creature->id();
}

static Variable FloatingTextStrRefOnCreature(const std::vector<Variable> &args, const RoutineContext &ctx) {
    if (isScriptFloatingTextShown(args, ctx)) {
        addScriptFloatingText(ctx.game.getFeedbackText(getInt(args, 0)), ctx);
    }
    return Variable::ofNull();
}

static Variable FloatingTextStringOnCreature(const std::vector<Variable> &args, const RoutineContext &ctx) {
    if (isScriptFloatingTextShown(args, ctx)) {
        addScriptFloatingText(getString(args, 0), ctx);
    }
    return Variable::ofNull();
}

static Variable GetTrapDisarmable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto oTrapObject = getObjectOrNull(args, 0, ctx);
    return visitTrap(oTrapObject, Variable::ofInt(0), [](auto &trap) { return Variable::ofInt(trap.trapDisarmable() ? 1 : 0); });
}

static Variable GetTrapDetectable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto oTrapObject = getObjectOrNull(args, 0, ctx);
    return visitTrap(oTrapObject, Variable::ofInt(0), [](auto &trap) { return Variable::ofInt(trap.trapDetectable() ? 1 : 0); });
}

static Variable GetTrapDetectedBy(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTrapObject = getObjectOrNull(args, 0, ctx);
    // The creature is matched by id and need not exist.
    const uint32_t creatureId = getObjectIdOrCaller(args, 1, ctx);

    // Execute
    return visitTrap(oTrapObject, Variable::ofInt(0), [creatureId](auto &trap) {
        return Variable::ofInt(trap.trapDetection().isDetectedBy(creatureId) ? 1 : 0);
    });
}

static Variable GetTrapFlagged(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto oTrapObject = getObjectOrNull(args, 0, ctx);
    return visitTrap(oTrapObject, Variable::ofInt(0), [](auto &trap) { return Variable::ofInt(trap.trapDetection().flagged ? 1 : 0); });
}

static Variable GetTrapBaseType(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto oTrapObject = getObjectOrNull(args, 0, ctx);
    return visitTrap(oTrapObject, Variable::ofInt(-1), [](auto &trap) { return Variable::ofInt(trap.trapBaseType()); });
}

static Variable GetTrapOneShot(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto oTrapObject = getObjectOrNull(args, 0, ctx);
    return visitTrap(oTrapObject, Variable::ofInt(0), [](auto &trap) { return Variable::ofInt(trap.trapOneShot() ? 1 : 0); });
}

static Variable GetTrapCreator(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Only a trigger has a creator; it is reported without checking it still exists.
    auto trigger = std::dynamic_pointer_cast<Trigger>(getObjectOrNull(args, 0, ctx));
    return Variable::ofObject(trigger ? trigger->trapCreatorId() : kObjectInvalid);
}

static Variable GetTrapKeyTag(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto oTrapObject = getObjectOrNull(args, 0, ctx);
    return visitTrap(oTrapObject, Variable::ofString(""), [](auto &trap) { return Variable::ofString(trap.trapKeyTag()); });
}

static Variable GetTrapDisarmDC(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto oTrapObject = getObjectOrNull(args, 0, ctx);
    return visitTrap(oTrapObject, Variable::ofInt(0), [](auto &trap) { return Variable::ofInt(trap.trapDisarmDC()); });
}

static Variable GetTrapDetectDC(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto oTrapObject = getObjectOrNull(args, 0, ctx);
    return visitTrap(oTrapObject, Variable::ofInt(0), [](auto &trap) { return Variable::ofInt(trap.trapDetectDC()); });
}

static Variable GetLockKeyRequired(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    throw RoutineNotImplementedException("GetLockKeyRequired");
}

static Variable GetLockKeyTag(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    throw RoutineNotImplementedException("GetLockKeyTag");
}

static Variable GetLockLockable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    throw RoutineNotImplementedException("GetLockLockable");
}

static Variable GetLockUnlockDC(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    throw RoutineNotImplementedException("GetLockUnlockDC");
}

static Variable GetLockLockDC(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    throw RoutineNotImplementedException("GetLockLockDC");
}

static Variable GetPCLevellingUp(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("GetPCLevellingUp");
}

static Variable GetHasFeatEffect(const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto feat = ctx.services.game.feats.get(
        static_cast<FeatType>(getInt(args, 0)));
    const auto object = getObjectOrNull(args, 1, ctx);
    // A feat's effects are the effects of the spell it names.
    if (!feat || !feat->spellId || !object) {
        return Variable::ofInt(0);
    }
    const uint32_t spellId = static_cast<uint32_t>(*feat->spellId);
    const bool hasEffect = std::any_of(
        object->effects().begin(), object->effects().end(),
        [spellId](const EffectInstance &applied) { return applied.spellId == spellId; });
    return Variable::ofInt(static_cast<int>(hasEffect));
}

static Variable SetPlaceableIllumination(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oPlaceable = getObjectOrCaller(args, 0, ctx);
    auto bIlluminate = getIntOrElse(args, 1, 1);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SetPlaceableIllumination");
}

static Variable GetPlaceableIllumination(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oPlaceable = getObjectOrCaller(args, 0, ctx);

    // Transform

    // Execute
    throw RoutineNotImplementedException("GetPlaceableIllumination");
}

static Variable GetIsPlaceableObjectActionPossible(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oPlaceable = getObject(args, 0, ctx);
    auto nPlaceableAction = getInt(args, 1);

    // Transform

    // Execute
    throw RoutineNotImplementedException("GetIsPlaceableObjectActionPossible");
}

static Variable DoPlaceableObjectAction(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oPlaceable = getObject(args, 0, ctx);
    auto nPlaceableAction = getInt(args, 1);

    // Transform

    // Execute
    throw RoutineNotImplementedException("DoPlaceableObjectAction");
}

static Variable GetFirstPC(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto player = ctx.game.party().player();
    return Variable::ofObject(getObjectIdOrInvalid(player));
}

static Variable GetNextPC(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofObject(kObjectInvalid);
}

static Variable SetTrapDetectedBy(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTrap = getObjectOrNull(args, 0, ctx);
    auto oDetector = std::dynamic_pointer_cast<Creature>(getObjectOrNull(args, 1, ctx));

    // Execute: only the detector is added, with no party expansion or feedback.
    if (!oDetector) {
        return Variable::ofInt(0);
    }
    return visitTrap(oTrap, Variable::ofInt(0), [&oDetector](auto &trap) {
        trap.trapDetection().addDetectedBy(oDetector->id());
        return Variable::ofInt(1);
    });
}

static Variable GetIsTrapped(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObjectOrNull(args, 0, ctx);

    // Execute
    return visitTrap(oObject, Variable::ofInt(0), [](auto &trap) { return Variable::ofInt(trap.isTrapped() ? 1 : 0); });
}

static Variable SetEffectIcon(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto effect = getEffect(args, 0);
    auto icon = ctx.game.newEffect<EffectIconMarkerEffect>(getInt(args, 1));
    auto link = ctx.game.newEffect<LinkEffectsEffect>(icon, effect);
    // The link keeps the effect's identity, category and spell and is created
    // by the caller.
    link->setSaveFacingId(effect->scriptValueId());
    link->Effect::setCategory(effect->category());
    link->setSaveFacingSpellId(static_cast<int32_t>(effect->saveFacingInstance().spellId));
    const Variable *callerArg = ctx.execution.findArg(ArgKind::Caller);
    link->setCreatorFromCaller(callerArg ? ctx.game.getObjectById(callerArg->objectId) : nullptr);
    return Variable::ofEffect(link);
}

static Variable FaceObjectAwayFromObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFacer = getObject(args, 0, ctx);
    auto oObjectToFaceAwayFrom = getObject(args, 1, ctx);

    // Transform

    // Execute
    if (auto creature = dyn_cast<Creature>(oFacer)) {
        creature->turnAwayFrom(*oObjectToFaceAwayFrom);
    } else {
        oFacer->faceAwayFrom(*oObjectToFaceAwayFrom);
    }
    return Variable::ofNull();
}

// The death panel request is discarded when it arrives, so nothing shows.
static Variable PopUpDeathGUIPanel(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofNull();
}

static Variable SetTrapDisabled(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTrap = getObjectOrNull(args, 0, ctx);
    auto caller = getCaller(ctx);

    // Execute: the trap is disarmed by the caller; a mine is also removed.
    if (auto trigger = std::dynamic_pointer_cast<Trigger>(oTrap); trigger && trigger->isTrap()) {
        trigger->disarmTrap(*caller);
        ctx.game.module()->area()->destroyObject(*trigger);
    } else if (auto door = std::dynamic_pointer_cast<Door>(oTrap); door && door->isTrapped()) {
        door->disarmTrap(*caller);
    } else if (auto placeable = std::dynamic_pointer_cast<Placeable>(oTrap); placeable && placeable->isTrapped()) {
        placeable->disarmTrap(*caller);
    }
    return Variable::ofNull();
}

static Variable GetLastHostileActor(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oVictim = getObjectOrCaller(args, 0, ctx);

    // Execute: a plot victim reports no one and forgets its stored actor.
    if (oVictim->plotFlag()) {
        if (oVictim->getLastHostileActor() != script::kObjectInvalid) {
            oVictim->setLastHostileActor(script::kObjectInvalid, true);
        }
        return Variable::ofObject(script::kObjectInvalid);
    }
    uint32_t actorId = oVictim->getLastHostileActor();
    if (actorId != script::kObjectInvalid) {
        auto object = ctx.game.getObjectById(actorId);
        // An area of effect reports its creator.
        if (object && object->type() == ObjectType::AreaOfEffect) {
            object = object->effectSource();
        }
        auto actor = std::dynamic_pointer_cast<Creature>(object);
        if (!actor || actor->isDead() || actor->isTemporarilyDead()) {
            actorId = script::kObjectInvalid;
            oVictim->setLastHostileActor(actorId, true);
        } else {
            actorId = actor->id();
        }
    }
    return Variable::ofObject(actorId);
}

static Variable ExportAllCharacters(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("ExportAllCharacters");
}

static Variable MusicBackgroundGetDayTrack(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getAreaOrNull(args, 0, ctx);

    // Execute
    return Variable::ofInt(oArea ? oArea->ambientAudio().musicDay : 0);
}

static Variable MusicBackgroundGetNightTrack(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getAreaOrNull(args, 0, ctx);

    // Execute
    return Variable::ofInt(oArea ? oArea->ambientAudio().musicNight : 0);
}

static Variable WriteTimestampedLogEntry(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sLogEntry = getString(args, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("WriteTimestampedLogEntry");
}

static Variable GetModuleName(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // The module's authored Mod_Name, not its resource name: shipped scripts
    // compare the result case-sensitively and take substrings of it. Scripts do
    // not run without a module, so the empty string here is only defensive.
    auto module = ctx.game.module();
    return Variable::ofString(module ? module->localizedName() : "");
}

// The leader of an NPC faction is its member of the highest challenge rating
// above zero, the first of equals; the player faction is led by the player
// character.
static Variable GetFactionLeader(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto memberOfFaction = getCreatureOrNull(args, 0, ctx);
    if (!memberOfFaction) {
        return Variable::ofObject(kObjectInvalid);
    }
    const Faction faction = memberOfFaction->faction();
    if (!isKnownFaction(faction, ctx)) {
        return Variable::ofObject(kObjectInvalid);
    }

    const bool npcFaction = faction != Faction::Player;
    std::shared_ptr<Creature> leader;
    float bestRating = 0.0f;
    for (const auto &object : ctx.game.module()->area()->getObjectsByType(ObjectType::Creature)) {
        auto member = std::static_pointer_cast<Creature>(object);
        if (member->faction() != faction) {
            continue;
        }
        if (!npcFaction) {
            if (member->isPC()) {
                return Variable::ofObject(member->id());
            }
            continue;
        }
        if (member->challengeRating() > bestRating) {
            leader = member;
            bestRating = member->challengeRating();
        }
    }
    return Variable::ofObject(leader ? leader->id() : kObjectInvalid);
}

static Variable EndGame(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nShowEndGameGui = getIntOrElse(args, 0, 1);

    // Execute
    ctx.game.endGame(nShowEndGameGui != 0);
    return Variable::ofNull();
}

static Variable GetRunScriptVar(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (const Variable *var = ctx.execution.findArg(ArgKind::ScriptVar)) {
        return *var;
    }
    return Variable::ofInt(-1);
}

static Variable GetCreatureMovmentType(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto object = getObjectOrNull(args, 0, ctx);
    auto creature = object ? dyn_cast<Creature>(object.get()) : nullptr;
    return Variable::ofInt(static_cast<int>(
        creature ? creature->movementType() : Creature::MovementType::Walk));
}

static Variable AmbientSoundSetDayVolume(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getAreaOrNull(args, 0, ctx);
    auto nVolume = getInt(args, 1);

    // Execute
    if (oArea) {
        oArea->setAmbientSoundDayVolume(nVolume);
    }
    return Variable::ofNull();
}

static Variable AmbientSoundSetNightVolume(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getAreaOrNull(args, 0, ctx);
    auto nVolume = getInt(args, 1);

    // Execute
    if (oArea) {
        oArea->setAmbientSoundNightVolume(nVolume);
    }
    return Variable::ofNull();
}

static Variable MusicBackgroundGetBattleTrack(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getAreaOrNull(args, 0, ctx);

    // Execute
    return Variable::ofInt(oArea ? oArea->ambientAudio().musicBattle : 0);
}

static Variable GetHasInventory(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    throw RoutineNotImplementedException("GetHasInventory");
}

static Variable GetStrRefSoundDuration(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nStrRef = getInt(args, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("GetStrRefSoundDuration");
}

static Variable AddToParty(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oPC = getObject(args, 0, ctx);
    auto oPartyLeader = getObject(args, 1, ctx);

    // Transform

    // Execute
    throw RoutineNotImplementedException("AddToParty");
}

static Variable RemoveFromParty(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oPC = getObject(args, 0, ctx);

    // Transform

    // Execute
    throw RoutineNotImplementedException("RemoveFromParty");
}

static Variable AddPartyMember(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);
    auto oCreature = getObject(args, 1, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    bool added = ctx.game.party().addMember(nNPC, creature);
    // A companion joining the party takes up the alignment its influence gives.
    if (added && nNPC >= 0) {
        creature->recomputeInfluenceAlignment();
    }
    return Variable::ofInt(static_cast<int>(added));
}

static Variable RemovePartyMember(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);

    // Transform

    // Execute
    bool removed = ctx.game.party().removeMember(nNPC);
    return Variable::ofInt(static_cast<int>(removed));
}

static Variable IsObjectPartyMember(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    bool member = ctx.game.party().isMember(*creature);
    return Variable::ofInt(static_cast<int>(member));
}

static Variable GetPartyMemberByIndex(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nIndex = getInt(args, 0);

    // Transform

    // Execute
    auto member = ctx.game.party().getMember(nIndex);
    return Variable::ofObject(getObjectIdOrInvalid(member));
}

static Variable GetGlobalBoolean(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sIdentifier = getString(args, 0);

    // Transform

    // Execute
    bool value = ctx.game.getGlobalBoolean(sIdentifier);
    return Variable::ofInt(static_cast<int>(value));
}

static Variable SetGlobalBoolean(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sIdentifier = getString(args, 0);
    auto nValue = getInt(args, 1);

    // Transform

    // Execute
    ctx.game.setGlobalBoolean(sIdentifier, nValue);
    return Variable::ofNull();
}

static Variable GetGlobalNumber(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sIdentifier = getString(args, 0);

    // Transform

    // Execute
    int value = ctx.game.getGlobalNumber(sIdentifier);
    return Variable::ofInt(value);
}

static Variable SetGlobalNumber(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sIdentifier = getString(args, 0);
    auto nValue = getInt(args, 1);

    // Transform

    // Execute
    ctx.game.setGlobalNumber(sIdentifier, nValue);
    return Variable::ofNull();
}

static Variable AurPostString(const std::vector<Variable> &args, const RoutineContext &ctx) {
    (void)getString(args, 0);
    (void)getInt(args, 1);
    (void)getInt(args, 2);
    (void)getFloat(args, 3);
    return Variable::ofNull();
}

static Variable AddJournalWorldEntry(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nIndex = getInt(args, 0);
    auto szEntry = getString(args, 1);
    auto szTitle = getStringOrElse(args, 2, "World Entry");

    // Transform

    // Execute
    throw RoutineNotImplementedException("AddJournalWorldEntry");
}

static Variable AddJournalWorldEntryStrref(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto strref = getInt(args, 0);
    auto strrefTitle = getInt(args, 1);

    // Transform

    // Execute
    throw RoutineNotImplementedException("AddJournalWorldEntryStrref");
}

static Variable BarkString(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);
    auto strRef = getInt(args, 1);
    auto nBarkX = getIntOrElse(args, 2, -1);
    auto nBarkY = getIntOrElse(args, 3, -1);

    // Transform

    // Execute
    throw RoutineNotImplementedException("BarkString");
}

static Variable DeleteJournalWorldAllEntries(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("DeleteJournalWorldAllEntries");
}

static Variable DeleteJournalWorldEntry(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nIndex = getInt(args, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("DeleteJournalWorldEntry");
}

static Variable DeleteJournalWorldEntryStrref(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto strref = getInt(args, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("DeleteJournalWorldEntryStrref");
}

static Variable PlayVisualAreaEffect(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nEffectID = getInt(args, 0);
    auto lTarget = getLocationArgument(args, 1);

    // Execute: the visual plays once at the point, in the controlled
    // creature's area.
    auto leader = ctx.game.party().getLeader();
    auto area = leader ? leader->spatialArea() : std::nullopt;
    if (area) {
        area->get().presentVisualAt(nEffectID, lTarget->position());
    }
    return Variable::ofNull();
}

static Variable SetJournalQuestEntryPicture(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto szPlotID = getString(args, 0);
    auto oObject = getObject(args, 1, ctx);
    auto nPictureIndex = getInt(args, 2);
    auto bAllPartyMemebers = getIntOrElse(args, 3, 1);
    auto bAllPlayers = getIntOrElse(args, 4, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SetJournalQuestEntryPicture");
}

static Variable GetLocalBoolean(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);
    auto nIndex = getInt(args, 1);

    // Transform

    // Execute
    bool value = oObject->getLocalBoolean(nIndex);
    return Variable::ofInt(static_cast<int>(value));
}

static Variable SetLocalBoolean(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);
    auto nIndex = getInt(args, 1);
    auto nValue = getInt(args, 2);

    // Transform

    // Execute
    oObject->setLocalBoolean(nIndex, nValue);
    return Variable::ofNull();
}

static Variable GetLocalNumber(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);
    auto nIndex = getInt(args, 1);

    // Transform

    // Execute
    return Variable::ofInt(oObject->getLocalNumber(nIndex));
}

static Variable SetLocalNumber(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);
    auto nIndex = getInt(args, 1);
    auto nValue = getInt(args, 2);

    // Transform

    // Execute
    oObject->setLocalNumber(nIndex, nValue);
    return Variable::ofNull();
}

static Variable SoundObjectGetPitchVariance(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oSound = getObject(args, 0, ctx);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SoundObjectGetPitchVariance");
}

static Variable SoundObjectSetPitchVariance(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oSound = getObject(args, 0, ctx);
    auto fVariance = getFloat(args, 1);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SoundObjectSetPitchVariance");
}

static Variable SoundObjectGetVolume(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oSound = getObject(args, 0, ctx);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SoundObjectGetVolume");
}

static Variable GetGlobalLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sIdentifier = getString(args, 0);

    // Transform

    // Execute
    return Variable::ofLocation(ctx.game.getGlobalLocation(sIdentifier));
}

static Variable SetGlobalLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sIdentifier = getString(args, 0);
    auto lValue = getLocationArgument(args, 1);

    // Transform

    // Execute
    ctx.game.setGlobalLocation(sIdentifier, lValue);
    return Variable::ofNull();
}

static Variable AddAvailableNPCByObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);
    auto oCreature = getObject(args, 1, ctx);

    auto creature = checkCreature(oCreature);

    // Execute
    return Variable::ofInt(ctx.game.party().addAvailableRosterRecord(
        {RosterKind::Npc, nNPC}, creature));
}

static Variable RemoveAvailableNPC(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);

    // Transform

    // Execute
    bool removed = ctx.game.party().removeAvailableMember(nNPC);
    return Variable::ofInt(static_cast<int>(removed));
}

static Variable IsAvailableCreature(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);

    // Transform

    // Execute
    bool available = ctx.game.party().isMemberAvailable(nNPC);
    return Variable::ofInt(static_cast<int>(available));
}

static Variable AddAvailableNPCByTemplate(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);
    auto sTemplate = getString(args, 1);

    // Transform
    auto tmplt = boost::to_lower_copy(sTemplate);

    // Execute
    bool added = ctx.game.party().addAvailableMember(nNPC, tmplt);
    return Variable::ofInt(static_cast<int>(added));
}

static Variable SpawnAvailableNPC(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);
    auto lPosition = getLocationArgument(args, 1);

    // Execute
    auto member = ctx.game.party().getAvailableMember(nNPC, true);
    if (!member) {
        return Variable::ofObject(kObjectInvalid);
    }
    auto module = ctx.game.module();
    auto area = module ? module->area() : nullptr;
    if (!area) {
        return Variable::ofObject(kObjectInvalid);
    }
    ctx.game.party().catchUpExperience(nNPC, *member);
    ctx.game.party().spawnIntoPlayerFaction(*member);
    // A spawned member drops its actions, commandable or not.
    member->clearAllActions(true, true);
    member->setPosition(lPosition->position());
    member->setFacing(objectFacingFromScript(lPosition->facing()));
    area->landObject(*member);
    const bool alreadyResident = std::find(
        area->objects().begin(), area->objects().end(), member) !=
        area->objects().end();
    if (!alreadyResident) {
        area->add(member);
    }
    // A spawned companion takes up the alignment its influence gives.
    member->recomputeInfluenceAlignment();
    return Variable::ofObject(member->id());
}

static Variable IsNPCPartyMember(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);

    // Transform

    // Execute: a follower is a member, even one whose creature was destroyed;
    // in TSL so is the companion index under control, which is -1 while the
    // player character is.
    const Party &party = ctx.game.party();
    bool member = party.isFollower(nNPC) || (ctx.game.isTSL() && party.controlledNpc() == nNPC);
    return Variable::ofInt(static_cast<int>(member));
}

static Variable GetIsConversationActive(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    bool active = ctx.game.isConversationActive();
    return Variable::ofInt(active);
}

static Variable GetPartyAIStyle(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofInt(ctx.game.party().aiStyle());
}

// A creature's combat style; 0 for anything else.
static Variable GetNPCAIStyle(const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto creature = getCreatureOrNull(args, 0, ctx);
    return Variable::ofInt(creature ? static_cast<int>(creature->aiStyle()) : 0);
}

static Variable SetPartyAIStyle(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nStyle = getInt(args, 0);

    // Execute
    ctx.game.party().setAIStyle(nStyle);
    return Variable::ofNull();
}

// Sets a creature's combat style; anything else is left alone.
static Variable SetNPCAIStyle(const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto creature = getCreatureOrNull(args, 0, ctx);
    const auto style = static_cast<NPCAIStyle>(getInt(args, 1));
    if (creature) {
        creature->setAIStyle(style);
    }
    return Variable::ofNull();
}

static Variable SetNPCSelectability(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);
    auto nSelectability = getInt(args, 1);

    // Execute
    ctx.game.party().setRosterSelectable(
        {RosterKind::Npc, nNPC}, nSelectability != 0);
    return Variable::ofNull();
}

static Variable GetNPCSelectability(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);

    // Execute
    const RosterIdentity identity {RosterKind::Npc, nNPC};
    if (!ctx.game.party().isRosterAvailable(identity)) {
        return Variable::ofInt(-1);
    }
    return Variable::ofInt(ctx.game.party().isRosterSelectable(identity));
}

static Variable ClearAllEffects(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto caller = getCaller(ctx);
    caller->clearAllEffects();
    return Variable::ofNull();
}

static Variable GetLastConversation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("GetLastConversation");
}

static Variable ShowPartySelectionGUI(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sExitScript = getStringOrElse(args, 0, "");
    auto nForceNPC1 = getIntOrElse(args, 1, -1);
    auto nForceNPC2 = getIntOrElse(args, 2, -1);
    auto nAllowCancel = getIntOrElse(args, 3, 0);

    // Transform
    auto exitScript = boost::to_lower_copy(sExitScript);

    // Execute
    PartySelectionContext partyCtx;
    partyCtx.exitScript = exitScript;
    partyCtx.forceNpc1 = nForceNPC1;
    partyCtx.forceNpc2 = nForceNPC2;
    ctx.game.openPartySelection(partyCtx);
    return Variable::ofNull();
}

static Variable GetStandardFaction(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform
    auto creature = checkCreature(oObject);

    // Execute
    return Variable::ofInt(static_cast<int>(creature->faction()));
}

static Variable GivePlotXP(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sPlotName = getString(args, 0);
    auto nPercentage = getInt(args, 1);

    // Execute
    ctx.game.awardPlotXP(sPlotName, nPercentage);
    return Variable::ofNull();
}

static Variable GetMinOneHP(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    return Variable::ofInt(static_cast<int>(oObject->isMinOneHP()));
}

static Variable SetMinOneHP(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);
    auto nMinOneHP = getInt(args, 1);

    // Transform
    auto minOneHP = static_cast<bool>(nMinOneHP);

    // Execute
    oObject->setMinOneHP(minOneHP);
    return Variable::ofNull();
}

static Variable SetGlobalFadeIn(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fWait = getFloatOrElse(args, 0, 0.0f);
    auto fLength = getFloatOrElse(args, 1, 0.0f);
    auto fR = getFloatOrElse(args, 2, 0.0f);
    auto fG = getFloatOrElse(args, 3, 0.0f);
    auto fB = getFloatOrElse(args, 4, 0.0f);

    // Transform

    // Execute
    ctx.game.globalFade().request(GlobalFade::Direction::In, fWait, fLength,
                                  {fR, fG, fB}, GlobalFade::Source::Script);
    return Variable::ofNull();
}

static Variable SetGlobalFadeOut(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fWait = getFloatOrElse(args, 0, 0.0f);
    auto fLength = getFloatOrElse(args, 1, 0.0f);
    auto fR = getFloatOrElse(args, 2, 0.0f);
    auto fG = getFloatOrElse(args, 3, 0.0f);
    auto fB = getFloatOrElse(args, 4, 0.0f);

    // Transform

    // Execute
    ctx.game.globalFade().request(GlobalFade::Direction::Out, fWait, fLength,
                                  {fR, fG, fB}, GlobalFade::Source::Script);
    return Variable::ofNull();
}

static Variable GetLastHostileTarget(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto attacker = args.empty() ? dyn_cast<Creature>(getCaller(ctx)) : getCreatureOrNull(args, 0, ctx);
    return Variable::ofObject(attacker ? attacker->getLastHostileTarget() : kObjectInvalid);
}

static Variable GetLastAttackAction(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto attacker = args.empty() ? dyn_cast<Creature>(getCaller(ctx)) : getCreatureOrNull(args, 0, ctx);
    return Variable::ofInt(attacker ? attacker->getLastAttackAction() : 0);
}

static Variable GetLastForcePowerUsed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto attacker = args.empty() ? dyn_cast<Creature>(getCaller(ctx)) : getCreatureOrNull(args, 0, ctx);
    return Variable::ofInt(attacker ? attacker->lastForcePowerUsed() : 0);
}

static Variable GetLastCombatFeatUsed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto attacker = args.empty() ? dyn_cast<Creature>(getCaller(ctx)) : getCreatureOrNull(args, 0, ctx);
    return Variable::ofInt(attacker ? static_cast<int>(attacker->getLastCombatFeat()) : 0);
}

static Variable GetLastAttackResult(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto attacker = args.empty() ? dyn_cast<Creature>(getCaller(ctx)) : getCreatureOrNull(args, 0, ctx);
    return Variable::ofInt(attacker ? static_cast<int>(attacker->getLastAttackResult()) : 0);
}

static Variable GetWasForcePowerSuccessful(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // No power ever records an outcome, so every creature answers -1.
    auto attacker = args.empty() ? dyn_cast<Creature>(getCaller(ctx)) : getCreatureOrNull(args, 0, ctx);
    return Variable::ofInt(attacker ? -1 : 0);
}

static Variable GetFirstAttacker(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto creature = args.empty() ? dyn_cast<Creature>(getCaller(ctx)) : getCreatureOrNull(args, 0, ctx);
    if (creature) {
        creature->getFirstAttacker();
    }
    // The walk over the creature's attackers advances, but the answer is
    // always OBJECT_INVALID.
    return Variable::ofObject(kObjectInvalid);
}

static Variable GetNextAttacker(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto creature = args.empty() ? dyn_cast<Creature>(getCaller(ctx)) : getCreatureOrNull(args, 0, ctx);
    if (creature) {
        creature->getNextAttacker();
    }
    // The walk over the creature's attackers advances, but the answer is
    // always OBJECT_INVALID.
    return Variable::ofObject(kObjectInvalid);
}

static Variable SetFormation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oAnchor = getObject(args, 0, ctx);
    auto oCreature = getObject(args, 1, ctx);
    auto nFormationPattern = getInt(args, 2);
    auto nPosition = getInt(args, 3);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SetFormation");
}

static Variable SetForcePowerUnsuccessful(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // The command takes its result where an object belongs and refuses the
    // integer it is given: it records nothing and stops the calling script.
    throw std::runtime_error("SetForcePowerUnsuccessful refuses its integer result");
}

static Variable GetIsDebilitated(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrCaller(args, 0, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    return Variable::ofInt(static_cast<int>(creature->isDebilitated()));
}

static Variable PlayMovie(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sMovie = getString(args, 0);
    auto nStreamingMusic = getIntOrElse(args, 1, 0);

    // Transform
    auto movie = boost::to_lower_copy(sMovie);

    // Execute
    ctx.game.playVideo(movie);
    return Variable::ofNull();
}

static Variable SaveNPCState(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);

    // Transform

    // Execute
    ctx.game.saveNpcState(nNPC);

    return Variable::ofNull();
}

static Variable GetCategoryFromTalent(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto tTalent = getTalent(args, 0);

    // Transform

    // Execute
    // Spells and feats carry a category; skills have none.
    switch (tTalent->type()) {
    case TalentType::Spell: {
        auto spell = ctx.services.game.spells.get(static_cast<SpellType>(tTalent->value()));
        return Variable::ofInt(spell ? static_cast<int>(spell->category) : 0);
    }
    case TalentType::Feat: {
        auto feat = ctx.services.game.feats.get(static_cast<FeatType>(tTalent->value()));
        return Variable::ofInt(feat ? feat->category : 0);
    }
    default:
        return Variable::ofInt(0);
    }
}

// Every creature of the caller's area in the first faction joins the second.
// A surrendering creature leaves combat, drops its actions and is left alone by
// those that targeted it; otherwise it perceives everything afresh. A caller
// outside any area changes nothing.
static void changeFactionByFaction(const std::vector<Variable> &args, const RoutineContext &ctx, bool surrender) {
    const auto factionFrom = static_cast<Faction>(getInt(args, 0));
    const auto factionTo = static_cast<Faction>(getInt(args, 1));
    auto caller = getCaller(ctx);
    std::optional<std::reference_wrapper<Area>> area = caller->type() == ObjectType::Area ? static_cast<Area &>(*caller) : caller->spatialArea();
    if (!area) {
        return;
    }

    const ObjectList creatures = area->get().getObjectsByType(ObjectType::Creature);
    for (const auto &object : creatures) {
        auto &creature = static_cast<Creature &>(*object);
        if (creature.faction() != factionFrom) {
            continue;
        }
        creature.setFaction(factionTo);
        if (surrender) {
            creature.setCombatState(false);
            creature.clearAllActions(true);
            creature.pacify();
        } else {
            // It forgets what it perceived, silently, and looks around anew;
            // its notices follow with the next update.
            creature.perceiveAfresh();
        }
    }
}

static Variable SurrenderByFaction(const std::vector<Variable> &args, const RoutineContext &ctx) {
    changeFactionByFaction(args, ctx, true);
    return Variable::ofNull();
}

static Variable ChangeFactionByFaction(const std::vector<Variable> &args, const RoutineContext &ctx) {
    changeFactionByFaction(args, ctx, false);
    return Variable::ofNull();
}

static Variable PlayRoomAnimation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sRoom = getString(args, 0);
    auto nAnimation = getInt(args, 1);

    // Transform

    // Execute
    throw RoutineNotImplementedException("PlayRoomAnimation");
}

static Variable ShowGalaxyMap(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nPlanet = getInt(args, 0);

    // Transform

    // Execute
    ctx.game.openGalaxyMap(nPlanet);
    return {};
}

static Variable SetPlanetSelectable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nPlanet = getInt(args, 0);
    auto bSelectable = getInt(args, 1);

    // Transform

    // Execute
    ctx.game.party().galaxyMap().setSelectable(nPlanet, bSelectable != 0);
    return {};
}

static Variable GetPlanetSelectable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nPlanet = getInt(args, 0);

    // Transform

    // Execute
    return Variable::ofInt(ctx.game.party().galaxyMap().selectable(nPlanet) ? 1 : 0);
}

static Variable SetPlanetAvailable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nPlanet = getInt(args, 0);
    auto bAvailable = getInt(args, 1);

    // Transform

    // Execute
    ctx.game.party().galaxyMap().setAvailable(nPlanet, bAvailable != 0);
    return {};
}

static Variable GetPlanetAvailable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nPlanet = getInt(args, 0);

    // Transform

    // Execute
    return Variable::ofInt(ctx.game.party().galaxyMap().available(nPlanet) ? 1 : 0);
}

static Variable GetSelectedPlanet(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofInt(ctx.game.party().galaxyMap().selectedPlanet());
}

static Variable SoundObjectFadeAndStop(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oSound = getObject(args, 0, ctx);
    auto fSeconds = getFloat(args, 1);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SoundObjectFadeAndStop");
}

static Variable SetAreaFogColor(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getObject(args, 0, ctx);
    auto fRed = getFloat(args, 1);
    auto fGreen = getFloat(args, 2);
    auto fBlue = getFloat(args, 3);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SetAreaFogColor");
}

static Variable ChangeItemCost(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sItem = getString(args, 0);
    auto fCostMultiplier = getFloat(args, 1);

    // Execute
    // The multiplier belongs to the template's base item, so it applies to
    // every item of that kind.
    auto uti = ctx.services.resource.gffs.get(sItem, resource::ResType::Uti);
    if (uti) {
        ctx.game.setBaseItemCostMultiplier(uti->getInt("BaseItem"), fCostMultiplier);
    }
    return Variable::ofNull();
}

static Variable GetIsLiveContentAvailable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nPkg = getInt(args, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("GetIsLiveContentAvailable");
}

static Variable ResetDialogState(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("ResetDialogState");
}

// The good/evil value runs from 0, the dark side, to 100, the light side.
static constexpr int kMinGoodEvil = 0;
static constexpr int kMaxGoodEvil = 100;

static Variable SetGoodEvilValue(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto creature = getCreatureOrNull(args, 0, ctx);
    auto nAlignment = getInt(args, 1);

    // Execute: TSL writes the value alone; KotOR moves the alignment to it,
    // Pure Good or Evil powers following.
    if (!creature) {
        return Variable::ofNull();
    }
    if (ctx.game.isTSL()) {
        creature->setGoodEvil(std::clamp(nAlignment, kMinGoodEvil, kMaxGoodEvil));
    } else {
        creature->modifyAlignment(nAlignment - creature->goodEvil(), false);
    }
    return Variable::ofNull();
}

static Variable GetIsPoisoned(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto object = getObject(args, 0, ctx);
    const auto *creature = dyn_cast<Creature>(object.get());
    return Variable::ofInt(creature && creature->activePoisonEffectId() != kUnassignedEffectId);
}

static Variable GetSpellTarget(const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto object = getObjectOrNull(args, 0, ctx);
    const auto target = object && isa<Creature>(object) ? object->spellScriptContext().activeTarget() : nullptr;
    return Variable::ofObject(target ? target->id() : script::kObjectInvalid);
}

static Variable SetSoloMode(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bActivate = getInt(args, 0);

    // Transform
    auto activate = static_cast<bool>(bActivate);

    // Execute
    ctx.game.party().setSoloMode(activate);
    return Variable::ofNull();
}

static Variable CancelPostDialogCharacterSwitch(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    ctx.game.cancelPostDialogCharacterSwitch();
    return Variable::ofNull();
}

static Variable SetMaxHitPoints(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);
    auto nMaxHP = getInt(args, 1);

    // Transform

    // Execute: the base maximum and the current hit points both become the
    // amount.
    oObject->setMaxHitPoints(nMaxHP);
    oObject->setCurrentHitPoints(nMaxHP);
    return Variable::ofNull();
}

static Variable NoClicksFor(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fDuration = getFloat(args, 0);

    // Execute
    if (fDuration > 0.0f) {
        ctx.game.markNoClickEvent(static_cast<uint32_t>(fDuration * 1000.0f));
    }
    return Variable::ofNull();
}

static Variable HoldWorldFadeInForDialog(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    ctx.game.globalFade().holdForDialog();
    return Variable::ofNull();
}

static Variable ShipBuild(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofInt(static_cast<int>(kShipBuild));
}

static Variable SurrenderRetainBuffs(const std::vector<Variable> &args, const RoutineContext &ctx) {
    surrenderCaller(ctx, true);
    return Variable::ofNull();
}

// The next reports to the status summary, as many as given, are swallowed.
static Variable SuppressStatusSummaryEntry(const std::vector<Variable> &args, const RoutineContext &ctx) {
    ctx.game.statusSummary().suppress(getIntOrElse(args, 0, 1));
    return Variable::ofNull();
}

static Variable GetCheatCode(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nCode = getInt(args, 0);

    // Transform

    // Execute
    return Variable::ofInt(0);
}

static Variable SetMusicVolume(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fVolume = getFloatOrElse(args, 0, 1.0f);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SetMusicVolume");
}

static Variable CreateItemOnFloor(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sTemplate = getString(args, 0);
    auto lLocation = getLocationArgument(args, 1);
    auto bUseAppearAnimation = getIntOrElse(args, 2, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("CreateItemOnFloor");
}

static Variable SetAvailableNPCId(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);
    auto oidNPC = getObjectOrNull(args, 1, ctx);

    // Execute
    const RosterIdentity identity {RosterKind::Npc, nNPC};
    if (!oidNPC) {
        if (args[1].objectId == kObjectInvalid &&
            ctx.game.party().isRosterAvailable(identity)) {
            ctx.game.party().clearRosterCreature(identity);
        }
    } else {
        ctx.game.party().bindRosterCreature(identity, checkCreature(oidNPC));
    }
    return Variable::ofNull();
}

static Variable GetScriptParameter(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nIndex = getInt(args, 0);

    // Transform
    const Variable *param = nullptr;
    switch (nIndex) {
    case 1:
        param = ctx.execution.findArg(ArgKind::ScriptParam1);
        break;
    case 2:
        param = ctx.execution.findArg(ArgKind::ScriptParam2);
        break;
    case 3:
        param = ctx.execution.findArg(ArgKind::ScriptParam3);
        break;
    case 4:
        param = ctx.execution.findArg(ArgKind::ScriptParam4);
        break;
    case 5:
        param = ctx.execution.findArg(ArgKind::ScriptParam5);
        break;
    default:
        break;
    }

    // Execute
    return Variable::ofInt(param ? param->intValue : 0);
}

static Variable SetFadeUntilScript(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    ctx.game.globalFade().lockUntilScript();
    return Variable::ofNull();
}

static Variable GetItemComponent(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("GetItemComponent");
}

static Variable GetItemComponentPieceValue(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("GetItemComponentPieceValue");
}

static Variable ShowChemicalUpgradeScreen(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCharacter = getObject(args, 0, ctx);

    // Transform

    // Execute
    throw RoutineNotImplementedException("ShowChemicalUpgradeScreen");
}

static Variable GetChemicals(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("GetChemicals");
}

static Variable GetChemicalPieceValue(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("GetChemicalPieceValue");
}

static Variable GetSpellForcePointCost(const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto caller = ctx.execution.findArg(ArgKind::Caller);
    const auto object = caller ? ctx.game.getObjectById(caller->objectId) : nullptr;
    return Variable::ofInt(object ? object->spellCastContext().forcePointCost : 0);
}

// Whether the creature owns the feat, learned or granted by an effect,
// whatever uses it has left.
static Variable GetFeatAcquired(const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto feat = static_cast<FeatType>(getInt(args, 0));
    const auto creature = getCreatureOrNull(args, 1, ctx);
    return Variable::ofInt(creature && creature->hasEffectiveFeat(feat));
}

// Whether any of the creature's classes knows the power, whatever it costs.
static Variable GetSpellAcquired(const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto spell = static_cast<SpellType>(getInt(args, 0));
    const auto creature = getCreatureOrNull(args, 1, ctx);
    return Variable::ofInt(creature && creature->attributes().hasSpell(spell));
}

static Variable ShowSwoopUpgradeScreen(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("ShowSwoopUpgradeScreen");
}

// Adds an existing feat to the creature's feats.
static Variable GrantFeat(const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto feat = static_cast<FeatType>(getInt(args, 0));
    const auto creature = getCreatureOrNull(args, 1, ctx);
    if (creature && ctx.services.game.feats.get(feat)) {
        creature->attributes().addFeat(feat);
    }
    return Variable::ofNull();
}

// Teaches an existing power to the creature's last class. A player
// character learns it only when that class is a Jedi class.
static Variable GrantSpell(const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto spell = static_cast<SpellType>(getInt(args, 0));
    const auto creature = getCreatureOrNull(args, 1, ctx);
    if (!creature) {
        return Variable::ofNull();
    }
    auto &attributes = creature->attributes();
    const ClassType lastClass = attributes.getEffectiveClass();
    if (lastClass == ClassType::Invalid || !ctx.services.game.spells.get(spell)) {
        return Variable::ofNull();
    }
    if (creature->isPC() && !isForceUsingClass(lastClass, true)) {
        return Variable::ofNull();
    }
    attributes.addSpell(spell, lastClass);
    return Variable::ofNull();
}

static Variable SpawnMine(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nMineType = getInt(args, 0);
    auto lPoint = getLocationArgument(args, 1);
    auto nDetectDCBase = getInt(args, 2);
    auto nDisarmDCBase = getInt(args, 3);
    auto oCreator = getObjectOrNull(args, 4, ctx);

    // Execute: the mine takes its creator's side, or is hostile without one.
    Faction faction = Faction::Hostile1;
    int ownerSkill = 0;
    if (auto creature = std::dynamic_pointer_cast<Creature>(oCreator)) {
        faction = creature->faction();
        ownerSkill = creature->getUnopposedSkillRank(SkillType::Demolitions);
    } else if (auto placeable = std::dynamic_pointer_cast<Placeable>(oCreator)) {
        faction = placeable->faction();
    }
    // A blank modifier adds nothing to the DC.
    const auto row = Trap::typeRow(ctx.services.resource.twoDas, nMineType);
    ctx.game.module()->area()->spawnMine(
        nMineType,
        lPoint->position(),
        oCreator,
        faction,
        row.detectDCMod.value_or(0) + nDetectDCBase,
        row.disarmDCMod.value_or(0) + nDisarmDCBase,
        ownerSkill);
    return Variable::ofNull();
}

static Variable SetFakeCombatState(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto creature = getCreatureOrNull(args, 0, ctx);
    const auto enable = getInt(args, 1);
    if (creature) {
        if (enable) {
            // Do not replace active real combat with a held indirect state.
            if (!creature->isInCombat() || creature->combatActivationType() != CombatActivation::Direct) {
                creature->setCombatState(true, CombatActivation::Indirect, true);
            }
        } else if (creature->combatActivationType() == CombatActivation::Indirect) {
            creature->setCombatState(false, CombatActivation::Indirect, false);
        }
    }
    return Variable::ofNull();
}

static Variable GetOwnerDemolitionsSkill(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObjectOrNull(args, 0, ctx);

    // Execute
    return visitTrap(oObject, Variable::ofInt(0), [](auto &trap) { return Variable::ofInt(trap.ownerDemolitionsSkill()); });
}

static Variable SetOrientOnClick(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrCaller(args, 0, ctx);
    auto nState = getIntOrElse(args, 1, 1);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SetOrientOnClick");
}

static Variable GetInfluence(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);

    // Execute: an NPC outside the party's reads 0, and one whose influence
    // was never set reads -1.
    static constexpr int kNeverSetInfluence = -1;
    if (!Party::isNpcSlot(nNPC)) {
        return Variable::ofInt(0);
    }
    return Variable::ofInt(ctx.game.party().influence(nNPC).value_or(kNeverSetInfluence));
}

// Influence runs from 0 to 100.
static constexpr int kMinInfluence = 0;
static constexpr int kMaxInfluence = 100;

// A changed influence is reported on the status summary, and the companion,
// if it has a creature, takes up the alignment the influence gives.
static void reportInfluenceChange(const RoutineContext &ctx, int npc, bool gained) {
    ctx.game.submitStatusSummary(
        gained ? StatusSummaryCategory::InfluenceGained : StatusSummaryCategory::InfluenceLost, npc);
    if (auto companion = ctx.game.party().getAvailableMember(npc)) {
        companion->recomputeInfluenceAlignment();
    }
}

static Variable SetInfluence(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);
    auto nInfluence = getInt(args, 1);

    // Transform
    const int influence = std::clamp(nInfluence, kMinInfluence, kMaxInfluence);

    // Execute: an NPC outside the party's is ignored.
    if (!Party::isNpcSlot(nNPC)) {
        return Variable::ofNull();
    }
    Party &party = ctx.game.party();
    const auto previous = party.influence(nNPC);
    party.setInfluence(nNPC, influence);
    if (previous != influence) {
        reportInfluenceChange(ctx, nNPC, previous < influence);
    }
    return Variable::ofNull();
}

static Variable ModifyInfluence(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);
    auto nModifier = getInt(args, 1);

    // Execute: an NPC outside the party's is ignored, and influence never set
    // starts from 50.
    static constexpr int kStartingInfluence = 50;
    if (!Party::isNpcSlot(nNPC)) {
        return Variable::ofNull();
    }
    Party &party = ctx.game.party();
    const int previous = party.influence(nNPC).value_or(kStartingInfluence);
    const int influence = std::clamp(previous + nModifier, kMinInfluence, kMaxInfluence);
    party.setInfluence(nNPC, influence);
    if (previous != influence) {
        reportInfluenceChange(ctx, nNPC, nModifier > 0);
    }
    return Variable::ofNull();
}

static Variable GetRacialSubType(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);

    // Transform

    // Execute
    throw RoutineNotImplementedException("GetRacialSubType");
}

// Number globals are signed 8-bit values. IncrementGlobalNumber and
// DecrementGlobalNumber are documented as failing with a warning when the
// resulting amount would leave that domain, so a rejected call leaves the stored
// value alone: it neither wraps nor clamps to the bound.
static constexpr int kMinGlobalNumber = -128;
static constexpr int kMaxGlobalNumber = 127;

// Add a signed delta to a named number global, rejecting a result outside the
// domain. Widened to 64-bit so that an extreme amount cannot overflow before the
// bounds check decides the call.
static void applyGlobalNumberDelta(
    const RoutineContext &ctx,
    const std::string &identifier,
    int64_t delta) {

    int64_t result = static_cast<int64_t>(ctx.game.getGlobalNumber(identifier)) + delta;
    if (result < kMinGlobalNumber || result > kMaxGlobalNumber) {
        debug(str(boost::format("Global number out of range: %s %d") % identifier % result),
              LogChannel::Script);
        return;
    }
    ctx.game.setGlobalNumber(identifier, static_cast<int>(result));
}

static Variable IncrementGlobalNumber(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sIdentifier = getString(args, 0);
    auto nAmount = getInt(args, 1);

    // Transform

    // Execute
    applyGlobalNumberDelta(ctx, sIdentifier, nAmount);
    return Variable::ofNull();
}

static Variable DecrementGlobalNumber(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sIdentifier = getString(args, 0);
    auto nAmount = getInt(args, 1);

    // Transform

    // Execute
    // Negated as 64-bit: the amount may be the most negative int, which has no
    // positive counterpart in its own width.
    applyGlobalNumberDelta(ctx, sIdentifier, -static_cast<int64_t>(nAmount));
    return Variable::ofNull();
}

// A change to the player character's bonus Force points is reported in the
// status summary as maximum Force points gained or lost, with the new bonus.
static Variable SetBonusForcePoints(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto creature = getCreatureOrNull(args, 0, ctx);
    const int bonus = getInt(args, 1);
    if (!creature) {
        return Variable::ofNull();
    }
    const int previous = creature->bonusForcePoints();
    if (creature->isPC() && previous != bonus) {
        ctx.game.submitStatusSummary(
            previous < bonus ? StatusSummaryCategory::MaxForcePointsGained : StatusSummaryCategory::MaxForcePointsLost,
            bonus);
    }
    creature->setBonusForcePoints(bonus);
    return Variable::ofNull();
}

// Added bonus Force points of the player character are reported in the status
// summary as maximum Force points gained, or lost for a negative amount.
static Variable AddBonusForcePoints(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto creature = getCreatureOrNull(args, 0, ctx);
    const int amount = getInt(args, 1);
    if (!creature) {
        return Variable::ofNull();
    }
    creature->setBonusForcePoints(creature->bonusForcePoints() + amount);
    if (creature->isPC()) {
        ctx.game.submitStatusSummary(
            amount >= 0 ? StatusSummaryCategory::MaxForcePointsGained : StatusSummaryCategory::MaxForcePointsLost,
            amount);
    }
    return Variable::ofNull();
}

static Variable GetBonusForcePoints(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto creature = getCreatureOrNull(args, 0, ctx);
    return Variable::ofInt(creature ? creature->bonusForcePoints() : 0);
}

static Variable IsMoviePlaying(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("IsMoviePlaying");
}

static Variable QueueMovie(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sMovie = getString(args, 0);
    auto nSkippable = getIntOrElse(args, 1, 1);

    // Transform

    // Execute
    throw RoutineNotImplementedException("QueueMovie");
}

static Variable PlayMovieQueue(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nAllowSkips = getIntOrElse(args, 0, 1);

    // Transform

    // Execute
    throw RoutineNotImplementedException("PlayMovieQueue");
}

static Variable YavinHackDoorClose(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    // Transform

    // Execute
    throw RoutineNotImplementedException("YavinHackDoorClose");
}

static Variable IsStealthed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    // Transform

    // Execute
    const auto creature = dyn_cast<Creature>(oCreature);
    return Variable::ofInt(creature && creature->isStealthed());
}

static Variable IsMeditating(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    // Transform
    auto creature = dyn_cast<Creature>(oCreature);

    // Execute
    return Variable::ofInt(static_cast<int>(
        creature && creature->combatStance() == CombatStance::Meditative));
}

static Variable IsInTotalDefense(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    // Transform
    auto creature = dyn_cast<Creature>(oCreature);

    // Execute
    return Variable::ofInt(static_cast<int>(creature && creature->isInTotalDefense()));
}

static Variable SetHealTarget(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oidHealer = getObjectOrNull(args, 0, ctx);
    auto oidTarget = getObjectOrNull(args, 1, ctx);

    // Transform
    auto healer = oidHealer ? dyn_cast<Creature>(oidHealer) : nullptr;

    // Execute
    if (healer) {
        healer->setHealTarget(getObjectIdOrInvalid(oidTarget));
    }
    return Variable::ofNull();
}

static Variable GetHealTarget(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oidHealer = getObjectOrNull(args, 0, ctx);

    // Transform
    auto healer = oidHealer ? dyn_cast<Creature>(oidHealer) : nullptr;

    // Execute
    return Variable::ofObject(healer ? healer->healTarget() : kObjectInvalid);
}

static Variable GetRandomDestination(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrNull(args, 0, ctx);
    auto rangeLimit = getInt(args, 1);

    // Execute
    auto creature = dyn_cast<Creature>(oCreature.get());
    auto area = ctx.game.module()->area();
    if (!creature || !area->isObjectResident(*creature)) {
        return Variable::ofVector(glm::vec3(0.0f));
    }
    // With a range of nothing the creature stays where it is.
    if (rangeLimit == 0) {
        return Variable::ofVector(creature->position());
    }
    return Variable::ofVector(area->randomDestination(*creature, rangeLimit));
}

static Variable IsFormActive(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto creature = getCreatureOrNull(args, 0, ctx);
    auto nFormID = getInt(args, 1);

    // Transform
    auto form = static_cast<CombatForm>(nFormID);

    // Execute
    if (!creature || creature->currentForm() != form) {
        return Variable::ofInt(0);
    }
    if (!isSaberForm(form)) {
        return Variable::ofInt(1);
    }

    auto weapon = creature->getEquippedItem(InventorySlots::rightWeapon);
    return Variable::ofInt(
        static_cast<int>(weapon && weapon->isLightsaber()));
}

static Variable GetSpellFormMask(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nSpellID = getInt(args, 0);

    // Transform
    auto spell = ctx.services.game.spells.get(
        static_cast<SpellType>(nSpellID));

    // Execute
    return Variable::ofInt(spell ? static_cast<int>(spell->formMask) : 0);
}

static Variable GetSpellBaseForcePointCost(const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto spell = ctx.services.game.spells.get(static_cast<SpellType>(getInt(args, 0)));
    return Variable::ofInt(spell ? spell->forcePointCost : 0);
}

static Variable SetKeepStealthInDialog(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nStealthState = getInt(args, 0);

    // Transform

    // Execute
    ctx.game.setKeepStealthInDialog(nStealthState != 0);
    return Variable::ofNull();
}

static Variable HasLineOfSight(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vSource = getVector(args, 0);
    auto vTarget = getVector(args, 1);
    auto oSource = getObjectOrNull(args, 2, ctx);
    auto oTarget = getObjectOrNull(args, 3, ctx);

    // Execute
    // The line is tested in the caller's area, so only an area, a creature, a
    // door or a placeable can ask. The source and target never block it.
    const Variable *callerArg = ctx.execution.findArg(ArgKind::Caller);
    auto caller = callerArg ? ctx.game.getObjectById(callerArg->objectId) : nullptr;
    if (!caller || !(isa<Area>(caller) || isa<Creature>(caller) || isa<Door>(caller) || isa<Placeable>(caller))) {
        return Variable::ofInt(0);
    }
    bool clear = ctx.game.module()->area()->isEyeLineClear(vSource, vTarget, getOptionalObject(oSource), getOptionalObject(oTarget));
    return Variable::ofInt(static_cast<int>(clear));
}

static Variable ShowDemoScreen(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sTexture = getString(args, 0);
    auto nTimeout = getInt(args, 1);
    auto nDisplayString = getInt(args, 2);
    auto nDisplayX = getInt(args, 3);
    auto nDisplayY = getInt(args, 4);

    // Transform

    // Execute
    throw RoutineNotImplementedException("ShowDemoScreen");
}

static Variable ForceHeartbeat(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    // Execute
    if (auto creature = std::dynamic_pointer_cast<Creature>(oCreature)) {
        creature->forceHeartbeat();
    }
    return Variable::ofNull();
}

static Variable IsRunning(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto creature = getCreatureOrNull(args, 0, ctx);

    // Execute
    return Variable::ofInt(creature && creature->isRunning());
}

static Variable SetForfeitConditions(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nForfeitFlags = getInt(args, 0);

    // Execute
    ctx.game.party().setForfeitConditions(nForfeitFlags);
    return Variable::ofNull();
}

static Variable GetLastForfeitViolation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofInt(ctx.game.party().lastForfeitViolation());
}

static Variable modifySavingThrowBonus(
    SavingThrow save, const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto creature = getCreatureOrNull(args, 0, ctx);
    const int delta = getInt(args, 1);
    if (creature) {
        creature->modifySavingThrowBonus(save, delta);
    }
    return Variable::ofNull();
}

static Variable ModifyReflexSavingThrowBase(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return modifySavingThrowBonus(SavingThrow::Reflex, args, ctx);
}

static Variable ModifyFortitudeSavingThrowBase(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return modifySavingThrowBonus(SavingThrow::Fortitude, args, ctx);
}

static Variable ModifyWillSavingThrowBase(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return modifySavingThrowBonus(SavingThrow::Will, args, ctx);
}

static Variable GetScriptStringParameter(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    const Variable *param = ctx.execution.findArg(ArgKind::ScriptStringParam);
    return Variable::ofString(param ? param->strValue : "");
}

static Variable GetObjectPersonalSpace(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto aObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    throw RoutineNotImplementedException("GetObjectPersonalSpace");
}

static Variable AdjustCreatureAttributes(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto creature = getCreatureOrNull(args, 0, ctx);
    auto nAttribute = getInt(args, 1);
    auto nAmount = getInt(args, 2);

    // Execute
    // Only creatures and the six abilities are adjusted; anything else is ignored.
    if (!creature || nAttribute < static_cast<int>(Ability::Strength) || nAttribute > static_cast<int>(Ability::Charisma)) {
        return Variable::ofNull();
    }
    creature->adjustBaseAbilityScore(static_cast<Ability>(nAttribute), nAmount);
    return Variable::ofNull();
}

static Variable SetCreatureAILevel(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto creature = getCreatureOrNull(args, 0, ctx);
    auto nPriority = getInt(args, 1);

    // Execute
    if (creature) {
        creature->setScriptAILevel(nPriority);
    }
    return Variable::ofNull();
}

static Variable ResetCreatureAILevel(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto creature = getCreatureOrNull(args, 0, ctx);

    // Execute
    if (creature) {
        creature->resetScriptAILevel();
    }
    return Variable::ofNull();
}

static Variable AddAvailablePUPByTemplate(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nPUP = getInt(args, 0);
    auto sTemplate = getString(args, 1);

    auto tmplt = boost::to_lower_copy(sTemplate);

    // Execute
    return Variable::ofInt(ctx.game.party().addAvailableRosterRecord(
        {RosterKind::Puppet, nPUP}, tmplt));
}

static Variable AddAvailablePUPByObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nPUP = getInt(args, 0);
    auto oPuppet = getObject(args, 1, ctx);

    auto creature = checkCreature(oPuppet);

    // Execute
    return Variable::ofInt(ctx.game.party().addAvailableRosterRecord(
        {RosterKind::Puppet, nPUP}, creature));
}

static Variable AssignPUP(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nPUP = getInt(args, 0);
    auto nNPC = getInt(args, 1);

    // Execute
    return Variable::ofInt(ctx.game.party().assignPuppet(nPUP, nNPC));
}

static Variable SpawnAvailablePUP(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nPUP = getInt(args, 0);
    auto lLocation = getLocationArgument(args, 1);

    // Execute
    auto puppet = ctx.game.party().getAvailablePuppet(nPUP, true);
    auto module = ctx.game.module();
    auto area = module ? module->area() : nullptr;
    if (!puppet || !area) {
        return Variable::ofObject(kObjectInvalid);
    }
    puppet->setPuppet(true);
    // A spawned puppet drops its actions, commandable or not.
    puppet->clearAllActions(true, true);
    puppet->setPosition(lLocation->position());
    puppet->setFacing(objectFacingFromScript(lLocation->facing()));
    area->landObject(*puppet);
    const bool alreadyResident = std::find(
        area->objects().begin(), area->objects().end(), puppet) !=
        area->objects().end();
    if (!alreadyResident) {
        area->add(puppet);
    }
    return Variable::ofObject(puppet->id());
}

static Variable AddPartyPuppet(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nPUP = getInt(args, 0);
    auto oidCreature = getObject(args, 1, ctx);

    auto creature = checkCreature(oidCreature);

    // Execute
    return Variable::ofInt(ctx.game.party().addPuppet(nPUP, creature));
}

static Variable GetPUPOwner(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oPUP = getObjectOrCaller(args, 0, ctx);

    // Execute
    auto creature = dyn_cast<Creature>(oPUP);
    auto owner = creature ? ctx.game.party().puppetOwner(*creature) : nullptr;
    return Variable::ofObject(getObjectIdOrInvalid(owner));
}

static Variable GetIsPuppet(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oPUP = getObjectOrCaller(args, 0, ctx);

    // Execute
    auto creature = dyn_cast<Creature>(oPUP);
    return Variable::ofInt(creature && creature->isPuppet());
}

static Variable GetIsPartyLeader(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCharacter = getObjectOrNull(args, 0, ctx);

    // Execute
    // Anything but the creature under the player's control, the party's
    // first member, is not the leader.
    return Variable::ofInt(oCharacter && oCharacter == ctx.game.party().getLeader() ? 1 : 0);
}

static Variable GetPartyLeader(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto leader = ctx.game.party().getLeader();
    return Variable::ofObject(getObjectIdOrInvalid(leader));
}

static Variable RemoveNPCFromPartyToBase(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);

    // Transform
    if (nNPC < 0 || nNPC >= static_cast<int>(Party::kK2NpcCount)) {
        return Variable::ofInt(0);
    }

    // Execute
    Party &party = ctx.game.party();
    const bool wasControlled = party.controlledNpc() == nNPC;
    // The companion leaves the party as RemovePartyMember removes it, then
    // its creature is taken out of the world, unless it is still travelling
    // with the party as a follower. Nothing else follows: a controlled
    // companion taken out this way leaves no one under control.
    party.removeMember(nNPC);
    if (!wasControlled && party.isFollower(nNPC)) {
        return Variable::ofInt(0);
    }
    return Variable::ofInt(static_cast<int>(ctx.game.killRosterCreature({RosterKind::Npc, nNPC})));
}

static Variable CreatureFlourishWeapon(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Execute
    if (auto creature = dyn_cast<Creature>(oObject)) {
        creature->flourishWeapons(false);
    }

    return Variable::ofNull();
}

static Variable ChangeObjectAppearance(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObjectToChange = getObject(args, 0, ctx);
    auto nAppearance = getInt(args, 1);

    // Execute
    if (auto creature = dyn_cast<Creature>(oObjectToChange)) {
        creature->changeAppearance(nAppearance);
    }

    return Variable::ofNull();
}

static Variable GetIsXBox(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    throw RoutineNotImplementedException("GetIsXBox");
}

static Variable PlayOverlayAnimation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);
    auto nAnimation = getInt(args, 1);

    // Transform
    auto target = checkCreature(oTarget);
    auto animation = static_cast<AnimationType>(nAnimation);

    // Execute
    // As nwscript describes it: plays on the creature even while it is moving,
    // and places no action on the queue.
    target->playOverlayAnimation(animation);
    return Variable::ofNull();
}

// Every song of the main menu's music list becomes available, in the
// configuration rather than the saved game.
static Variable UnlockAllSongs(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    saveUnlockedPlanetSongs(ctx.game.options().game.configurationPath, kAllPlanetSongs);
    return Variable::ofNull();
}

static Variable DisableMap(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nFlag = getIntOrElse(args, 0, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("DisableMap");
}

static Variable DetonateMine(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oMine = getObjectOrNull(args, 0, ctx);

    // Execute: the mine is removed without running any script.
    if (auto trigger = std::dynamic_pointer_cast<Trigger>(oMine)) {
        ctx.game.module()->area()->destroyObject(*trigger);
    }
    return Variable::ofNull();
}

static Variable DisableHealthRegen(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nFlag = getIntOrElse(args, 0, 0);

    // Transform

    ctx.game.party().setHealthRegenerationDisabled(nFlag != 0);
    return Variable::ofNull();
}

static Variable SetCurrentForm(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto creature = getCreatureOrNull(args, 0, ctx);
    auto nFormID = getInt(args, 1);

    // Transform
    auto formSpell = static_cast<SpellType>(nFormID);

    // Execute
    if (creature && creature->attributes().hasSpell(formSpell)) {
        creature->setCurrentForm(static_cast<CombatForm>(nFormID));
    }
    return Variable::ofNull();
}

static Variable SetDisableTransit(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nFlag = getIntOrElse(args, 0, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SetDisableTransit");
}

static Variable SetInputClass(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nClass = getInt(args, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("SetInputClass");
}

static Variable SetForceAlwaysUpdate(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);
    auto nFlag = getInt(args, 1);

    oObject->setForceAlwaysUpdate(nFlag);
    return Variable::ofNull();
}

static Variable EnableRain(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nFlag = getInt(args, 0);

    // Transform

    // Execute
    throw RoutineNotImplementedException("EnableRain");
}

static Variable DisplayMessageBox(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nStrRef = getInt(args, 0);
    auto sIcon = getStringOrElse(args, 1, "");

    // Transform

    // Execute
    throw RoutineNotImplementedException("DisplayMessageBox");
}

static Variable DisplayDatapad(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oDatapad = getObject(args, 0, ctx);

    // Transform

    // Execute
    throw RoutineNotImplementedException("DisplayDatapad");
}

static Variable RemoveHeartbeat(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oPlaceable = getObject(args, 0, ctx);

    // Transform

    // Execute
    // Only a placeable's heartbeat script is dropped, and it stays dropped: no
    // routine can assign an event script back. A heartbeat that removes its
    // own script still runs to completion.
    if (isa<Placeable>(oPlaceable.get())) {
        oPlaceable->clearOnHeartbeat();
    }
    return Variable::ofNull();
}

static Variable RemoveEffectByID(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto object = getObjectOrNull(args, 0, ctx);
    EffectInstance selector;
    selector.setIntegerParameter(0, getInt(args, 1));
    if (object) {
        object->queueScriptEffectRemoval(ScriptEffectRemovalMatch::Integer0, selector);
    }
    return Variable::ofNull();
}

static Variable RemoveEffectByExactMatch(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto object = getObjectOrNull(args, 0, ctx);
    const auto value = getEffect(args, 1)->saveFacingInstance();
    if (object) {
        object->queueScriptEffectRemoval(ScriptEffectRemovalMatch::TypeAndFirstTwoIntegers, value);
    }
    return Variable::ofNull();
}

static Variable AdjustCreatureSkills(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);
    auto nSkill = getInt(args, 1);
    auto nAmount = getInt(args, 2);

    // Transform

    // Execute
    throw RoutineNotImplementedException("AdjustCreatureSkills");
}

static Variable GetSkillRankBase(const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto skill = static_cast<SkillType>(getInt(args, 0));
    const auto creature = getCreatureOrNull(args, 1, ctx);
    return Variable::ofInt(creature ? creature->getUnopposedSkillRank(skill, true) : -1);
}

static Variable EnableRendering(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);
    auto bEnable = getInt(args, 1);

    // Transform

    // Execute
    throw RoutineNotImplementedException("EnableRendering");
}

static Variable GetCombatActionsPending(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Only creatures have combat actions; anything else answers 0.
    auto creature = getCreatureOrNull(args, 0, ctx);
    return Variable::ofInt(creature && creature->hasOrdinaryActionsPending() ? 1 : 0);
}

static Variable SaveNPCByObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);
    auto oidCharacter = getObject(args, 1, ctx);

    auto creature = checkCreature(oidCharacter);

    // Execute
    creature->clearAllActions();
    ctx.game.saveRosterState({RosterKind::Npc, nNPC}, *creature);
    return Variable::ofNull();
}

static Variable SavePUPByObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nPUP = getInt(args, 0);
    auto oidPuppet = getObject(args, 1, ctx);

    auto creature = checkCreature(oidPuppet);

    // Execute
    creature->clearAllActions();
    ctx.game.saveRosterState({RosterKind::Puppet, nPUP}, *creature);
    return Variable::ofNull();
}

static Variable GetIsPlayerMadeCharacter(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oidCharacter = getObject(args, 0, ctx);

    // Execute
    // Anything but a creature reads as not player-made.
    auto creature = std::dynamic_pointer_cast<Creature>(oidCharacter);
    return Variable::ofInt(creature && creature->isPlayerCreated() ? 1 : 0);
}

static Variable RebuildPartyTable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    const auto &area = *ctx.game.module()->area();

    // Each available companion slot takes the creature standing in the area
    // with the companion's tag, and the puppet slot the one tagged "remote".
    auto &party = ctx.game.party();
    for (int slot = 0; slot < static_cast<int>(Party::kK2NpcCount); ++slot) {
        if (party.isMemberAvailable(slot)) {
            party.bindPlacedCompanion(slot, area);
        }
    }
    if (party.isRosterAvailable({RosterKind::Puppet, 0})) {
        for (const auto &object : area.objects()) {
            if (object->type() == ObjectType::Creature && boost::iequals(object->tag(), "remote")) {
                party.bindRosterCreature({RosterKind::Puppet, 0}, std::static_pointer_cast<Creature>(object));
                break;
            }
        }
    }
    return Variable::ofNull();
}

void Routines::registerMainKotorRoutines() {
    insert(0, "Random", R_INT, {R_INT}, &Random);
    insert(1, "PrintString", R_VOID, {R_STRING}, &PrintString);
    insert(2, "PrintFloat", R_VOID, {R_FLOAT, R_INT, R_INT}, &PrintFloat);
    insert(3, "FloatToString", R_STRING, {R_FLOAT, R_INT, R_INT}, &FloatToString);
    insert(4, "PrintInteger", R_VOID, {R_INT}, &PrintInteger);
    insert(5, "PrintObject", R_VOID, {R_OBJECT}, &PrintObject);
    insert(6, "AssignCommand", R_VOID, {R_OBJECT, R_ACTION}, &AssignCommand);
    insert(7, "DelayCommand", R_VOID, {R_FLOAT, R_ACTION}, &DelayCommand);
    insert(8, "ExecuteScript", R_VOID, {R_STRING, R_OBJECT, R_INT}, &ExecuteScript);
    insert(9, "ClearAllActions", R_VOID, {}, &ClearAllActions);
    insert(10, "SetFacing", R_VOID, {R_FLOAT}, &SetFacing);
    insert(11, "SwitchPlayerCharacter", R_INT, {R_INT}, &SwitchPlayerCharacter);
    insert(12, "SetTime", R_VOID, {R_INT, R_INT, R_INT, R_INT}, &SetTime);
    insert(13, "SetPartyLeader", R_INT, {R_INT}, &SetPartyLeader);
    insert(14, "SetAreaUnescapable", R_VOID, {R_INT}, &SetAreaUnescapable);
    insert(15, "GetAreaUnescapable", R_INT, {}, &GetAreaUnescapable);
    insert(16, "GetTimeHour", R_INT, {}, &GetTimeHour);
    insert(17, "GetTimeMinute", R_INT, {}, &GetTimeMinute);
    insert(18, "GetTimeSecond", R_INT, {}, &GetTimeSecond);
    insert(19, "GetTimeMillisecond", R_INT, {}, &GetTimeMillisecond);
    insert(24, "GetArea", R_OBJECT, {R_OBJECT}, &GetArea);
    insert(25, "GetEnteringObject", R_OBJECT, {}, &GetEnteringObject);
    insert(26, "GetExitingObject", R_OBJECT, {}, &GetExitingObject);
    insert(27, "GetPosition", R_VECTOR, {R_OBJECT}, &GetPosition);
    insert(28, "GetFacing", R_FLOAT, {R_OBJECT}, &GetFacing);
    insert(29, "GetItemPossessor", R_OBJECT, {R_OBJECT}, &GetItemPossessor);
    insert(30, "GetItemPossessedBy", R_OBJECT, {R_OBJECT, R_STRING}, &GetItemPossessedBy);
    insert(31, "CreateItemOnObject", R_OBJECT, {R_STRING, R_OBJECT, R_INT}, &CreateItemOnObject);
    insert(36, "GetLastAttacker", R_OBJECT, {R_OBJECT}, &GetLastAttacker);
    insert(38, "GetNearestCreature", R_OBJECT, {R_INT, R_INT, R_OBJECT, R_INT, R_INT, R_INT, R_INT, R_INT}, &GetNearestCreature);
    insert(41, "GetDistanceToObject", R_FLOAT, {R_OBJECT}, &GetDistanceToObject);
    insert(42, "GetIsObjectValid", R_INT, {R_OBJECT}, &GetIsObjectValid);
    insert(45, "SetCameraFacing", R_VOID, {R_FLOAT}, &SetCameraFacing);
    insert(46, "PlaySound", R_VOID, {R_STRING}, &PlaySound);
    insert(47, "GetSpellTargetObject", R_OBJECT, {}, &GetSpellTargetObject);
    insert(49, "GetCurrentHitPoints", R_INT, {R_OBJECT}, &GetCurrentHitPoints);
    insert(50, "GetMaxHitPoints", R_INT, {R_OBJECT}, &GetMaxHitPoints);
    insert(52, "GetLastItemEquipped", R_OBJECT, {}, &GetLastItemEquipped);
    insert(53, "GetSubScreenID", R_INT, {}, &GetSubScreenID);
    insert(54, "CancelCombat", R_VOID, {R_OBJECT, R_INT}, &CancelCombat);
    insert(55, "GetCurrentForcePoints", R_INT, {R_OBJECT}, &GetCurrentForcePoints);
    insert(56, "GetMaxForcePoints", R_INT, {R_OBJECT}, &GetMaxForcePoints);
    insert(57, "PauseGame", R_VOID, {R_INT}, &PauseGame);
    insert(58, "SetPlayerRestrictMode", R_VOID, {R_INT}, &SetPlayerRestrictMode);
    insert(59, "GetStringLength", R_INT, {R_STRING}, &GetStringLength);
    insert(60, "GetStringUpperCase", R_STRING, {R_STRING}, &GetStringUpperCase);
    insert(61, "GetStringLowerCase", R_STRING, {R_STRING}, &GetStringLowerCase);
    insert(62, "GetStringRight", R_STRING, {R_STRING, R_INT}, &GetStringRight);
    insert(63, "GetStringLeft", R_STRING, {R_STRING, R_INT}, &GetStringLeft);
    insert(64, "InsertString", R_STRING, {R_STRING, R_STRING, R_INT}, &InsertString);
    insert(65, "GetSubString", R_STRING, {R_STRING, R_INT, R_INT}, &GetSubString);
    insert(66, "FindSubString", R_INT, {R_STRING, R_STRING}, &FindSubString);
    insert(67, "fabs", R_FLOAT, {R_FLOAT}, &fabs);
    insert(68, "cos", R_FLOAT, {R_FLOAT}, &cos);
    insert(69, "sin", R_FLOAT, {R_FLOAT}, &sin);
    insert(70, "tan", R_FLOAT, {R_FLOAT}, &tan);
    insert(71, "acos", R_FLOAT, {R_FLOAT}, &acos);
    insert(72, "asin", R_FLOAT, {R_FLOAT}, &asin);
    insert(73, "atan", R_FLOAT, {R_FLOAT}, &atan);
    insert(74, "log", R_FLOAT, {R_FLOAT}, &log);
    insert(75, "pow", R_FLOAT, {R_FLOAT, R_FLOAT}, &pow);
    insert(76, "sqrt", R_FLOAT, {R_FLOAT}, &sqrt);
    insert(77, "abs", R_INT, {R_INT}, &abs);
    insert(83, "GetPlayerRestrictMode", R_INT, {R_OBJECT}, &GetPlayerRestrictMode);
    insert(84, "GetCasterLevel", R_INT, {R_OBJECT}, &GetCasterLevel);
    insert(85, "GetFirstEffect", R_EFFECT, {R_OBJECT}, &GetFirstEffect);
    insert(86, "GetNextEffect", R_EFFECT, {R_OBJECT}, &GetNextEffect);
    insert(87, "RemoveEffect", R_VOID, {R_OBJECT, R_EFFECT}, &RemoveEffect);
    insert(88, "GetIsEffectValid", R_INT, {R_EFFECT}, &GetIsEffectValid);
    insert(89, "GetEffectDurationType", R_INT, {R_EFFECT}, &GetEffectDurationType);
    insert(90, "GetEffectSubType", R_INT, {R_EFFECT}, &GetEffectSubType);
    insert(91, "GetEffectCreator", R_OBJECT, {R_EFFECT}, &GetEffectCreator);
    insert(92, "IntToString", R_STRING, {R_INT}, &IntToString);
    insert(93, "GetFirstObjectInArea", R_OBJECT, {R_OBJECT, R_INT}, &GetFirstObjectInArea);
    insert(94, "GetNextObjectInArea", R_OBJECT, {R_OBJECT, R_INT}, &GetNextObjectInArea);
    insert(95, "d2", R_INT, {R_INT}, &d2);
    insert(96, "d3", R_INT, {R_INT}, &d3);
    insert(97, "d4", R_INT, {R_INT}, &d4);
    insert(98, "d6", R_INT, {R_INT}, &d6);
    insert(99, "d8", R_INT, {R_INT}, &d8);
    insert(100, "d10", R_INT, {R_INT}, &d10);
    insert(101, "d12", R_INT, {R_INT}, &d12);
    insert(102, "d20", R_INT, {R_INT}, &d20);
    insert(103, "d100", R_INT, {R_INT}, &d100);
    insert(104, "VectorMagnitude", R_FLOAT, {R_VECTOR}, &VectorMagnitude);
    insert(105, "GetMetaMagicFeat", R_INT, {}, &GetMetaMagicFeat);
    insert(106, "GetObjectType", R_INT, {R_OBJECT}, &GetObjectType);
    insert(107, "GetRacialType", R_INT, {R_OBJECT}, &GetRacialType);
    insert(108, "FortitudeSave", R_INT, {R_OBJECT, R_INT, R_INT, R_OBJECT}, &FortitudeSave);
    insert(109, "ReflexSave", R_INT, {R_OBJECT, R_INT, R_INT, R_OBJECT}, &ReflexSave);
    insert(110, "WillSave", R_INT, {R_OBJECT, R_INT, R_INT, R_OBJECT}, &WillSave);
    insert(111, "GetSpellSaveDC", R_INT, {}, &GetSpellSaveDC);
    insert(112, "MagicalEffect", R_EFFECT, {R_EFFECT}, &MagicalEffect);
    insert(113, "SupernaturalEffect", R_EFFECT, {R_EFFECT}, &SupernaturalEffect);
    insert(114, "ExtraordinaryEffect", R_EFFECT, {R_EFFECT}, &ExtraordinaryEffect);
    insert(116, "GetAC", R_INT, {R_OBJECT, R_INT}, &GetAC);
    insert(121, "RoundsToSeconds", R_FLOAT, {R_INT}, &RoundsToSeconds);
    insert(122, "HoursToSeconds", R_FLOAT, {R_INT}, &HoursToSeconds);
    insert(123, "TurnsToSeconds", R_FLOAT, {R_INT}, &TurnsToSeconds);
    insert(124, "SoundObjectSetFixedVariance", R_VOID, {R_OBJECT, R_FLOAT}, &SoundObjectSetFixedVariance);
    insert(125, "GetGoodEvilValue", R_INT, {R_OBJECT}, &GetGoodEvilValue);
    insert(126, "GetPartyMemberCount", R_INT, {}, &GetPartyMemberCount);
    insert(127, "GetAlignmentGoodEvil", R_INT, {R_OBJECT}, &GetAlignmentGoodEvil);
    insert(128, "GetFirstObjectInShape", R_OBJECT, {R_INT, R_FLOAT, R_LOCATION, R_INT, R_INT, R_VECTOR}, &GetFirstObjectInShape);
    insert(129, "GetNextObjectInShape", R_OBJECT, {R_INT, R_FLOAT, R_LOCATION, R_INT, R_INT, R_VECTOR}, &GetNextObjectInShape);
    insert(131, "SignalEvent", R_VOID, {R_OBJECT, R_EVENT}, &SignalEvent);
    insert(132, "EventUserDefined", R_EVENT, {R_INT}, &EventUserDefined);
    insert(137, "VectorNormalize", R_VECTOR, {R_VECTOR}, &VectorNormalize);
    insert(138, "GetItemStackSize", R_INT, {R_OBJECT}, &GetItemStackSize);
    insert(139, "GetAbilityScore", R_INT, {R_OBJECT, R_INT}, &GetAbilityScore);
    insert(140, "GetIsDead", R_INT, {R_OBJECT}, &GetIsDead);
    insert(141, "PrintVector", R_VOID, {R_VECTOR, R_INT}, &PrintVector);
    insert(142, "Vector", R_VECTOR, {R_FLOAT, R_FLOAT, R_FLOAT}, &Vector);
    insert(143, "SetFacingPoint", R_VOID, {R_VECTOR}, &SetFacingPoint);
    insert(144, "AngleToVector", R_VECTOR, {R_FLOAT}, &AngleToVector);
    insert(145, "VectorToAngle", R_FLOAT, {R_VECTOR}, &VectorToAngle);
    insert(146, "TouchAttackMelee", R_INT, {R_OBJECT, R_INT}, &TouchAttackMelee);
    insert(147, "TouchAttackRanged", R_INT, {R_OBJECT, R_INT}, &TouchAttackRanged);
    insert(150, "SetItemStackSize", R_VOID, {R_OBJECT, R_INT}, &SetItemStackSize);
    insert(151, "GetDistanceBetween", R_FLOAT, {R_OBJECT, R_OBJECT}, &GetDistanceBetween);
    insert(152, "SetReturnStrref", R_VOID, {R_INT, R_INT, R_INT}, &SetReturnStrref);
    insert(155, "GetItemInSlot", R_OBJECT, {R_INT, R_OBJECT}, &GetItemInSlot);
    insert(160, "SetGlobalString", R_VOID, {R_STRING, R_STRING}, &SetGlobalString);
    insert(162, "SetCommandable", R_VOID, {R_INT, R_OBJECT}, &SetCommandable);
    insert(163, "GetCommandable", R_INT, {R_OBJECT}, &GetCommandable);
    insert(166, "GetHitDice", R_INT, {R_OBJECT}, &GetHitDice);
    insert(168, "GetTag", R_STRING, {R_OBJECT}, &GetTag);
    insert(169, "ResistForce", R_INT, {R_OBJECT, R_OBJECT}, &ResistForce);
    insert(170, "GetEffectType", R_INT, {R_EFFECT}, &GetEffectType);
    insert(172, "GetFactionEqual", R_INT, {R_OBJECT, R_OBJECT}, &GetFactionEqual);
    insert(173, "ChangeFaction", R_VOID, {R_OBJECT, R_OBJECT}, &ChangeFaction);
    insert(174, "GetIsListening", R_INT, {R_OBJECT}, &GetIsListening);
    insert(175, "SetListening", R_VOID, {R_OBJECT, R_INT}, &SetListening);
    insert(176, "SetListenPattern", R_VOID, {R_OBJECT, R_STRING, R_INT}, &SetListenPattern);
    insert(177, "TestStringAgainstPattern", R_INT, {R_STRING, R_STRING}, &TestStringAgainstPattern);
    insert(178, "GetMatchedSubstring", R_STRING, {R_INT}, &GetMatchedSubstring);
    insert(179, "GetMatchedSubstringsCount", R_INT, {}, &GetMatchedSubstringsCount);
    insert(181, "GetFactionWeakestMember", R_OBJECT, {R_OBJECT, R_INT}, &GetFactionWeakestMember);
    insert(182, "GetFactionStrongestMember", R_OBJECT, {R_OBJECT, R_INT}, &GetFactionStrongestMember);
    insert(183, "GetFactionMostDamagedMember", R_OBJECT, {R_OBJECT, R_INT}, &GetFactionMostDamagedMember);
    insert(184, "GetFactionLeastDamagedMember", R_OBJECT, {R_OBJECT, R_INT}, &GetFactionLeastDamagedMember);
    insert(185, "GetFactionGold", R_INT, {R_OBJECT}, &GetFactionGold);
    insert(186, "GetFactionAverageReputation", R_INT, {R_OBJECT, R_OBJECT}, &GetFactionAverageReputation);
    insert(187, "GetFactionAverageGoodEvilAlignment", R_INT, {R_OBJECT}, &GetFactionAverageGoodEvilAlignment);
    insert(188, "SoundObjectGetFixedVariance", R_FLOAT, {R_OBJECT}, &SoundObjectGetFixedVariance);
    insert(189, "GetFactionAverageLevel", R_INT, {R_OBJECT}, &GetFactionAverageLevel);
    insert(190, "GetFactionAverageXP", R_INT, {R_OBJECT}, &GetFactionAverageXP);
    insert(191, "GetFactionMostFrequentClass", R_INT, {R_OBJECT}, &GetFactionMostFrequentClass);
    insert(192, "GetFactionWorstAC", R_OBJECT, {R_OBJECT, R_INT}, &GetFactionWorstAC);
    insert(193, "GetFactionBestAC", R_OBJECT, {R_OBJECT, R_INT}, &GetFactionBestAC);
    insert(194, "GetGlobalString", R_STRING, {R_STRING}, &GetGlobalString);
    insert(195, "GetListenPatternNumber", R_INT, {}, &GetListenPatternNumber);
    insert(197, "GetWaypointByTag", R_OBJECT, {R_STRING}, &GetWaypointByTag);
    insert(198, "GetTransitionTarget", R_OBJECT, {R_OBJECT}, &GetTransitionTarget);
    insert(200, "GetObjectByTag", R_OBJECT, {R_STRING, R_INT}, &GetObjectByTag);
    insert(201, "AdjustAlignment", R_VOID, {R_OBJECT, R_INT, R_INT}, &AdjustAlignment);
    insert(203, "SetAreaTransitionBMP", R_VOID, {R_INT, R_STRING}, &SetAreaTransitionBMP);
    insert(208, "GetReputation", R_INT, {R_OBJECT, R_OBJECT}, &GetReputation);
    insert(209, "AdjustReputation", R_VOID, {R_OBJECT, R_OBJECT, R_INT}, &AdjustReputation);
    insert(210, "GetModuleFileName", R_STRING, {}, &GetModuleFileName);
    insert(211, "GetGoingToBeAttackedBy", R_OBJECT, {R_OBJECT}, &GetGoingToBeAttackedBy);
    insert(213, "GetLocation", R_LOCATION, {R_OBJECT}, &GetLocation);
    insert(215, "Location", R_LOCATION, {R_VECTOR, R_FLOAT}, &CreateLocation);
    insert(216, "ApplyEffectAtLocation", R_VOID, {R_INT, R_EFFECT, R_LOCATION, R_FLOAT}, &ApplyEffectAtLocation);
    insert(217, "GetIsPC", R_INT, {R_OBJECT}, &GetIsPC);
    insert(218, "FeetToMeters", R_FLOAT, {R_FLOAT}, &FeetToMeters);
    insert(219, "YardsToMeters", R_FLOAT, {R_FLOAT}, &YardsToMeters);
    insert(220, "ApplyEffectToObject", R_VOID, {R_INT, R_EFFECT, R_OBJECT, R_FLOAT}, &ApplyEffectToObject);
    insert(221, "SpeakString", R_VOID, {R_STRING, R_INT}, &SpeakString);
    insert(222, "GetSpellTargetLocation", R_LOCATION, {}, &GetSpellTargetLocation);
    insert(223, "GetPositionFromLocation", R_VECTOR, {R_LOCATION}, &GetPositionFromLocation);
    insert(225, "GetFacingFromLocation", R_FLOAT, {R_LOCATION}, &GetFacingFromLocation);
    insert(226, "GetNearestCreatureToLocation", R_OBJECT, {R_INT, R_INT, R_LOCATION, R_INT, R_INT, R_INT, R_INT, R_INT}, &GetNearestCreatureToLocation);
    insert(227, "GetNearestObject", R_OBJECT, {R_INT, R_OBJECT, R_INT}, &GetNearestObject);
    insert(228, "GetNearestObjectToLocation", R_OBJECT, {R_INT, R_LOCATION, R_INT}, &GetNearestObjectToLocation);
    insert(229, "GetNearestObjectByTag", R_OBJECT, {R_STRING, R_OBJECT, R_INT}, &GetNearestObjectByTag);
    insert(230, "IntToFloat", R_FLOAT, {R_INT}, &IntToFloat);
    insert(231, "FloatToInt", R_INT, {R_FLOAT}, &FloatToInt);
    insert(232, "StringToInt", R_INT, {R_STRING}, &StringToInt);
    insert(233, "StringToFloat", R_FLOAT, {R_STRING}, &StringToFloat);
    insert(235, "GetIsEnemy", R_INT, {R_OBJECT, R_OBJECT}, &GetIsEnemy);
    insert(236, "GetIsFriend", R_INT, {R_OBJECT, R_OBJECT}, &GetIsFriend);
    insert(237, "GetIsNeutral", R_INT, {R_OBJECT, R_OBJECT}, &GetIsNeutral);
    insert(238, "GetPCSpeaker", R_OBJECT, {}, &GetPCSpeaker);
    insert(239, "GetStringByStrRef", R_STRING, {R_INT}, &GetStringByStrRef);
    insert(241, "DestroyObject", R_VOID, {R_OBJECT, R_FLOAT, R_INT, R_FLOAT}, &DestroyObject);
    insert(242, "GetModule", R_OBJECT, {}, &GetModule);
    insert(243, "CreateObject", R_OBJECT, {R_INT, R_STRING, R_LOCATION, R_INT}, &CreateObject);
    insert(244, "EventSpellCastAt", R_EVENT, {R_OBJECT, R_INT, R_INT}, &EventSpellCastAt);
    insert(245, "GetLastSpellCaster", R_OBJECT, {}, &GetLastSpellCaster);
    insert(246, "GetLastSpell", R_INT, {}, &GetLastSpell);
    insert(247, "GetUserDefinedEventNumber", R_INT, {}, &GetUserDefinedEventNumber);
    insert(248, "GetSpellId", R_INT, {}, &GetSpellId);
    insert(249, "RandomName", R_STRING, {}, &RandomName);
    insert(251, "GetLoadFromSaveGame", R_INT, {}, &GetLoadFromSaveGame);
    insert(253, "GetName", R_STRING, {R_OBJECT}, &GetName);
    insert(254, "GetLastSpeaker", R_OBJECT, {}, &GetLastSpeaker);
    insert(255, "BeginConversation", R_INT, {R_STRING, R_OBJECT}, &BeginConversation);
    insert(256, "GetLastPerceived", R_OBJECT, {}, &GetLastPerceived);
    insert(257, "GetLastPerceptionHeard", R_INT, {}, &GetLastPerceptionHeard);
    insert(258, "GetLastPerceptionInaudible", R_INT, {}, &GetLastPerceptionInaudible);
    insert(259, "GetLastPerceptionSeen", R_INT, {}, &GetLastPerceptionSeen);
    insert(260, "GetLastClosedBy", R_OBJECT, {}, &GetLastClosedBy);
    insert(261, "GetLastPerceptionVanished", R_INT, {}, &GetLastPerceptionVanished);
    insert(262, "GetFirstInPersistentObject", R_OBJECT, {R_OBJECT, R_INT, R_INT}, &GetFirstInPersistentObject);
    insert(263, "GetNextInPersistentObject", R_OBJECT, {R_OBJECT, R_INT, R_INT}, &GetNextInPersistentObject);
    insert(264, "GetAreaOfEffectCreator", R_OBJECT, {R_OBJECT}, &GetAreaOfEffectCreator);
    insert(265, "ShowLevelUpGUI", R_INT, {}, &ShowLevelUpGUI);
    insert(266, "SetItemNonEquippable", R_VOID, {R_OBJECT, R_INT}, &SetItemNonEquippable);
    insert(267, "GetButtonMashCheck", R_INT, {}, &GetButtonMashCheck);
    insert(268, "SetButtonMashCheck", R_VOID, {R_INT}, &SetButtonMashCheck);
    insert(271, "GiveItem", R_VOID, {R_OBJECT, R_OBJECT}, &GiveItem);
    insert(272, "ObjectToString", R_STRING, {R_OBJECT}, &ObjectToString);
    insert(274, "GetIsImmune", R_INT, {R_OBJECT, R_INT, R_OBJECT}, &GetIsImmune);
    insert(276, "GetEncounterActive", R_INT, {R_OBJECT}, &GetEncounterActive);
    insert(277, "SetEncounterActive", R_VOID, {R_INT, R_OBJECT}, &SetEncounterActive);
    insert(278, "GetEncounterSpawnsMax", R_INT, {R_OBJECT}, &GetEncounterSpawnsMax);
    insert(279, "SetEncounterSpawnsMax", R_VOID, {R_INT, R_OBJECT}, &SetEncounterSpawnsMax);
    insert(280, "GetEncounterSpawnsCurrent", R_INT, {R_OBJECT}, &GetEncounterSpawnsCurrent);
    insert(281, "SetEncounterSpawnsCurrent", R_VOID, {R_INT, R_OBJECT}, &SetEncounterSpawnsCurrent);
    insert(282, "GetModuleItemAcquired", R_OBJECT, {}, &GetModuleItemAcquired);
    insert(283, "GetModuleItemAcquiredFrom", R_OBJECT, {}, &GetModuleItemAcquiredFrom);
    insert(284, "SetCustomToken", R_VOID, {R_INT, R_STRING}, &SetCustomToken);
    insert(285, "GetHasFeat", R_INT, {R_INT, R_OBJECT}, &GetHasFeat);
    insert(286, "GetHasSkill", R_INT, {R_INT, R_OBJECT}, &GetHasSkill);
    insert(289, "GetObjectSeen", R_INT, {R_OBJECT, R_OBJECT}, &GetObjectSeen);
    insert(290, "GetObjectHeard", R_INT, {R_OBJECT, R_OBJECT}, &GetObjectHeard);
    insert(291, "GetLastPlayerDied", R_OBJECT, {}, &GetLastPlayerDied);
    insert(292, "GetModuleItemLost", R_OBJECT, {}, &GetModuleItemLost);
    insert(293, "GetModuleItemLostBy", R_OBJECT, {}, &GetModuleItemLostBy);
    insert(295, "EventConversation", R_EVENT, {}, &EventConversation);
    insert(296, "SetEncounterDifficulty", R_VOID, {R_INT, R_OBJECT}, &SetEncounterDifficulty);
    insert(297, "GetEncounterDifficulty", R_INT, {R_OBJECT}, &GetEncounterDifficulty);
    insert(298, "GetDistanceBetweenLocations", R_FLOAT, {R_LOCATION, R_LOCATION}, &GetDistanceBetweenLocations);
    insert(299, "GetReflexAdjustedDamage", R_INT, {R_INT, R_OBJECT, R_INT, R_INT, R_OBJECT}, &GetReflexAdjustedDamage);
    insert(300, "PlayAnimation", R_VOID, {R_INT, R_FLOAT, R_FLOAT}, &PlayAnimation);
    insert(301, "TalentSpell", R_TALENT, {R_INT}, &TalentSpell);
    insert(302, "TalentFeat", R_TALENT, {R_INT}, &TalentFeat);
    insert(303, "TalentSkill", R_TALENT, {R_INT}, &TalentSkill);
    insert(304, "GetHasSpellEffect", R_INT, {R_INT, R_OBJECT}, &GetHasSpellEffect);
    insert(305, "GetEffectSpellId", R_INT, {R_EFFECT}, &GetEffectSpellId);
    insert(306, "GetCreatureHasTalent", R_INT, {R_TALENT, R_OBJECT}, &GetCreatureHasTalent);
    insert(307, "GetCreatureTalentRandom", R_TALENT, {R_INT, R_OBJECT, R_INT}, &GetCreatureTalentRandom);
    insert(308, "GetCreatureTalentBest", R_TALENT, {R_INT, R_INT, R_OBJECT, R_INT, R_INT, R_INT}, &GetCreatureTalentBest);
    insert(311, "GetGoldPieceValue", R_INT, {R_OBJECT}, &GetGoldPieceValue);
    insert(312, "GetIsPlayableRacialType", R_INT, {R_OBJECT}, &GetIsPlayableRacialType);
    insert(313, "JumpToLocation", R_VOID, {R_LOCATION}, &JumpToLocation);
    insert(315, "GetSkillRank", R_INT, {R_INT, R_OBJECT}, &GetSkillRank);
    insert(316, "GetAttackTarget", R_OBJECT, {R_OBJECT}, &GetAttackTarget);
    insert(317, "GetLastAttackType", R_INT, {R_OBJECT}, &GetLastAttackType);
    insert(318, "GetLastAttackMode", R_INT, {R_OBJECT}, &GetLastAttackMode);
    insert(319, "GetDistanceBetween2D", R_FLOAT, {R_OBJECT, R_OBJECT}, &GetDistanceBetween2D);
    insert(320, "GetIsInCombat", R_INT, {R_OBJECT}, &GetIsInCombat);
    insert(321, "GetLastAssociateCommand", R_INT, {R_OBJECT}, &GetLastAssociateCommand);
    insert(322, "GiveGoldToCreature", R_VOID, {R_OBJECT, R_INT}, &GiveGoldToCreature);
    insert(323, "SetIsDestroyable", R_VOID, {R_INT, R_INT, R_INT}, &SetIsDestroyable);
    insert(324, "SetLocked", R_VOID, {R_OBJECT, R_INT}, &SetLocked);
    insert(325, "GetLocked", R_INT, {R_OBJECT}, &GetLocked);
    insert(326, "GetClickingObject", R_OBJECT, {}, &GetClickingObject);
    insert(327, "SetAssociateListenPatterns", R_VOID, {R_OBJECT}, &SetAssociateListenPatterns);
    insert(328, "GetLastWeaponUsed", R_OBJECT, {R_OBJECT}, &GetLastWeaponUsed);
    insert(330, "GetLastUsedBy", R_OBJECT, {}, &GetLastUsedBy);
    insert(331, "GetAbilityModifier", R_INT, {R_INT, R_OBJECT}, &GetAbilityModifier);
    insert(332, "GetIdentified", R_INT, {R_OBJECT}, &GetIdentified);
    insert(333, "SetIdentified", R_VOID, {R_OBJECT, R_INT}, &SetIdentified);
    insert(334, "GetDistanceBetweenLocations2D", R_FLOAT, {R_LOCATION, R_LOCATION}, &GetDistanceBetweenLocations2D);
    insert(335, "GetDistanceToObject2D", R_FLOAT, {R_OBJECT}, &GetDistanceToObject2D);
    insert(336, "GetBlockingDoor", R_OBJECT, {}, &GetBlockingDoor);
    insert(337, "GetIsDoorActionPossible", R_INT, {R_OBJECT, R_INT}, &GetIsDoorActionPossible);
    insert(338, "DoDoorAction", R_VOID, {R_OBJECT, R_INT}, &DoDoorAction);
    insert(339, "GetFirstItemInInventory", R_OBJECT, {R_OBJECT}, &GetFirstItemInInventory);
    insert(340, "GetNextItemInInventory", R_OBJECT, {R_OBJECT}, &GetNextItemInInventory);
    insert(341, "GetClassByPosition", R_INT, {R_INT, R_OBJECT}, &GetClassByPosition);
    insert(342, "GetLevelByPosition", R_INT, {R_INT, R_OBJECT}, &GetLevelByPosition);
    insert(343, "GetLevelByClass", R_INT, {R_INT, R_OBJECT}, &GetLevelByClass);
    insert(344, "GetDamageDealtByType", R_INT, {R_INT}, &GetDamageDealtByType);
    insert(345, "GetTotalDamageDealt", R_INT, {}, &GetTotalDamageDealt);
    insert(346, "GetLastDamager", R_OBJECT, {}, &GetLastDamager);
    insert(347, "GetLastDisarmed", R_OBJECT, {}, &GetLastDisarmed);
    insert(348, "GetLastDisturbed", R_OBJECT, {}, &GetLastDisturbed);
    insert(349, "GetLastLocked", R_OBJECT, {}, &GetLastLocked);
    insert(350, "GetLastUnlocked", R_OBJECT, {}, &GetLastUnlocked);
    insert(352, "GetInventoryDisturbType", R_INT, {}, &GetInventoryDisturbType);
    insert(353, "GetInventoryDisturbItem", R_OBJECT, {}, &GetInventoryDisturbItem);
    insert(354, "ShowUpgradeScreen", R_VOID, {R_OBJECT}, &ShowUpgradeScreen);
    insert(355, "VersusAlignmentEffect", R_EFFECT, {R_EFFECT, R_INT, R_INT}, &VersusAlignmentEffect);
    insert(356, "VersusRacialTypeEffect", R_EFFECT, {R_EFFECT, R_INT}, &VersusRacialTypeEffect);
    insert(357, "VersusTrapEffect", R_EFFECT, {R_EFFECT}, &VersusTrapEffect);
    insert(358, "GetGender", R_INT, {R_OBJECT}, &GetGender);
    insert(359, "GetIsTalentValid", R_INT, {R_TALENT}, &GetIsTalentValid);
    insert(361, "GetAttemptedAttackTarget", R_OBJECT, {}, &GetAttemptedAttackTarget);
    insert(362, "GetTypeFromTalent", R_INT, {R_TALENT}, &GetTypeFromTalent);
    insert(363, "GetIdFromTalent", R_INT, {R_TALENT}, &GetIdFromTalent);
    insert(364, "PlayPazaak", R_VOID, {R_INT, R_STRING, R_INT, R_INT, R_OBJECT}, &PlayPazaak);
    insert(365, "GetLastPazaakResult", R_INT, {}, &GetLastPazaakResult);
    insert(366, "DisplayFeedBackText", R_VOID, {R_OBJECT, R_INT}, &DisplayFeedBackText);
    insert(367, "AddJournalQuestEntry", R_VOID, {R_STRING, R_INT, R_INT}, &AddJournalQuestEntry);
    insert(368, "RemoveJournalQuestEntry", R_VOID, {R_STRING}, &RemoveJournalQuestEntry);
    insert(369, "GetJournalEntry", R_INT, {R_STRING}, &GetJournalEntry);
    insert(370, "PlayRumblePattern", R_INT, {R_INT}, &PlayRumblePattern);
    insert(371, "StopRumblePattern", R_INT, {R_INT}, &StopRumblePattern);
    insert(374, "SendMessageToPC", R_VOID, {R_OBJECT, R_STRING}, &SendMessageToPC);
    insert(375, "GetAttemptedSpellTarget", R_OBJECT, {}, &GetAttemptedSpellTarget);
    insert(376, "GetLastOpenedBy", R_OBJECT, {}, &GetLastOpenedBy);
    insert(377, "GetHasSpell", R_INT, {R_INT, R_OBJECT}, &GetHasSpell);
    insert(378, "OpenStore", R_VOID, {R_OBJECT, R_OBJECT, R_INT, R_INT}, &OpenStore);
    insert(380, "GetFirstFactionMember", R_OBJECT, {R_OBJECT, R_INT}, &GetFirstFactionMember);
    insert(381, "GetNextFactionMember", R_OBJECT, {R_OBJECT, R_INT}, &GetNextFactionMember);
    insert(384, "GetJournalQuestExperience", R_INT, {R_STRING}, &GetJournalQuestExperience);
    insert(385, "JumpToObject", R_VOID, {R_OBJECT, R_INT}, &JumpToObject);
    insert(386, "SetMapPinEnabled", R_VOID, {R_OBJECT, R_INT}, &SetMapPinEnabled);
    insert(388, "PopUpGUIPanel", R_VOID, {R_OBJECT, R_INT}, &PopUpGUIPanel);
    insert(389, "AddMultiClass", R_VOID, {R_INT, R_OBJECT}, &AddMultiClass);
    insert(390, "GetIsLinkImmune", R_INT, {R_OBJECT, R_EFFECT}, &GetIsLinkImmune);
    insert(393, "GiveXPToCreature", R_VOID, {R_OBJECT, R_INT}, &GiveXPToCreature);
    insert(394, "SetXP", R_VOID, {R_OBJECT, R_INT}, &SetXP);
    insert(395, "GetXP", R_INT, {R_OBJECT}, &GetXP);
    insert(396, "IntToHexString", R_STRING, {R_INT}, &IntToHexString);
    insert(397, "GetBaseItemType", R_INT, {R_OBJECT}, &GetBaseItemType);
    insert(398, "GetItemHasItemProperty", R_INT, {R_OBJECT, R_INT}, &GetItemHasItemProperty);
    insert(401, "GetItemACValue", R_INT, {R_OBJECT}, &GetItemACValue);
    insert(403, "ExploreAreaForPlayer", R_VOID, {R_OBJECT, R_OBJECT}, &ExploreAreaForPlayer);
    insert(405, "GetIsDay", R_INT, {}, &GetIsDay);
    insert(406, "GetIsNight", R_INT, {}, &GetIsNight);
    insert(407, "GetIsDawn", R_INT, {}, &GetIsDawn);
    insert(408, "GetIsDusk", R_INT, {}, &GetIsDusk);
    insert(409, "GetIsEncounterCreature", R_INT, {R_OBJECT}, &GetIsEncounterCreature);
    insert(410, "GetLastPlayerDying", R_OBJECT, {}, &GetLastPlayerDying);
    insert(411, "GetStartingLocation", R_LOCATION, {}, &GetStartingLocation);
    insert(412, "ChangeToStandardFaction", R_VOID, {R_OBJECT, R_INT}, &ChangeToStandardFaction);
    insert(413, "SoundObjectPlay", R_VOID, {R_OBJECT}, &SoundObjectPlay);
    insert(414, "SoundObjectStop", R_VOID, {R_OBJECT}, &SoundObjectStop);
    insert(415, "SoundObjectSetVolume", R_VOID, {R_OBJECT, R_INT}, &SoundObjectSetVolume);
    insert(416, "SoundObjectSetPosition", R_VOID, {R_OBJECT, R_VECTOR}, &SoundObjectSetPosition);
    insert(417, "SpeakOneLinerConversation", R_VOID, {R_STRING, R_OBJECT}, &SpeakOneLinerConversation);
    insert(418, "GetGold", R_INT, {R_OBJECT}, &GetGold);
    insert(419, "GetLastRespawnButtonPresser", R_OBJECT, {}, &GetLastRespawnButtonPresser);
    insert(421, "SetLightsaberPowered", R_VOID, {R_OBJECT, R_INT, R_INT, R_INT}, &SetLightsaberPowered);
    insert(422, "GetIsWeaponEffective", R_INT, {R_OBJECT, R_INT}, &GetIsWeaponEffective);
    insert(423, "GetLastSpellHarmful", R_INT, {}, &GetLastSpellHarmful);
    insert(424, "EventActivateItem", R_EVENT, {R_OBJECT, R_LOCATION, R_OBJECT}, &EventActivateItem);
    insert(425, "MusicBackgroundPlay", R_VOID, {R_OBJECT}, &MusicBackgroundPlay);
    insert(426, "MusicBackgroundStop", R_VOID, {R_OBJECT}, &MusicBackgroundStop);
    insert(427, "MusicBackgroundSetDelay", R_VOID, {R_OBJECT, R_INT}, &MusicBackgroundSetDelay);
    insert(428, "MusicBackgroundChangeDay", R_VOID, {R_OBJECT, R_INT}, &MusicBackgroundChangeDay);
    insert(429, "MusicBackgroundChangeNight", R_VOID, {R_OBJECT, R_INT}, &MusicBackgroundChangeNight);
    insert(430, "MusicBattlePlay", R_VOID, {R_OBJECT}, &MusicBattlePlay);
    insert(431, "MusicBattleStop", R_VOID, {R_OBJECT}, &MusicBattleStop);
    insert(432, "MusicBattleChange", R_VOID, {R_OBJECT, R_INT}, &MusicBattleChange);
    insert(433, "AmbientSoundPlay", R_VOID, {R_OBJECT}, &AmbientSoundPlay);
    insert(434, "AmbientSoundStop", R_VOID, {R_OBJECT}, &AmbientSoundStop);
    insert(435, "AmbientSoundChangeDay", R_VOID, {R_OBJECT, R_INT}, &AmbientSoundChangeDay);
    insert(436, "AmbientSoundChangeNight", R_VOID, {R_OBJECT, R_INT}, &AmbientSoundChangeNight);
    insert(437, "GetLastKiller", R_OBJECT, {}, &GetLastKiller);
    insert(438, "GetSpellCastItem", R_OBJECT, {}, &GetSpellCastItem);
    insert(439, "GetItemActivated", R_OBJECT, {}, &GetItemActivated);
    insert(440, "GetItemActivator", R_OBJECT, {}, &GetItemActivator);
    insert(441, "GetItemActivatedTargetLocation", R_LOCATION, {}, &GetItemActivatedTargetLocation);
    insert(442, "GetItemActivatedTarget", R_OBJECT, {}, &GetItemActivatedTarget);
    insert(443, "GetIsOpen", R_INT, {R_OBJECT}, &GetIsOpen);
    insert(444, "TakeGoldFromCreature", R_VOID, {R_INT, R_OBJECT, R_INT}, &TakeGoldFromCreature);
    insert(445, "GetIsInConversation", R_INT, {R_OBJECT}, &GetIsInConversation);
    insert(455, "GetPlotFlag", R_INT, {R_OBJECT}, &GetPlotFlag);
    insert(456, "SetPlotFlag", R_VOID, {R_OBJECT, R_INT}, &SetPlotFlag);
    insert(461, "SetDialogPlaceableCamera", R_VOID, {R_INT}, &SetDialogPlaceableCamera);
    insert(462, "GetSoloMode", R_INT, {}, &GetSoloMode);
    insert(464, "GetMaxStealthXP", R_INT, {}, &GetMaxStealthXP);
    insert(468, "SetMaxStealthXP", R_VOID, {R_INT}, &SetMaxStealthXP);
    insert(474, "GetCurrentStealthXP", R_INT, {}, &GetCurrentStealthXP);
    insert(475, "GetNumStackedItems", R_INT, {R_OBJECT}, &GetNumStackedItems);
    insert(476, "SurrenderToEnemies", R_VOID, {}, &SurrenderToEnemies);
    insert(478, "SetCurrentStealthXP", R_VOID, {R_INT}, &SetCurrentStealthXP);
    insert(479, "GetCreatureSize", R_INT, {R_OBJECT}, &GetCreatureSize);
    insert(480, "AwardStealthXP", R_VOID, {R_OBJECT}, &AwardStealthXP);
    insert(481, "GetStealthXPEnabled", R_INT, {}, &GetStealthXPEnabled);
    insert(482, "SetStealthXPEnabled", R_VOID, {R_INT}, &SetStealthXPEnabled);
    insert(486, "GetLastTrapDetected", R_OBJECT, {R_OBJECT}, &GetLastTrapDetected);
    insert(488, "GetNearestTrapToObject", R_OBJECT, {R_OBJECT, R_INT}, &GetNearestTrapToObject);
    insert(489, "GetAttemptedMovementTarget", R_OBJECT, {}, &GetAttemptedMovementTarget);
    insert(490, "GetBlockingCreature", R_OBJECT, {R_OBJECT}, &GetBlockingCreature);
    insert(491, "GetFortitudeSavingThrow", R_INT, {R_OBJECT}, &GetFortitudeSavingThrow);
    insert(492, "GetWillSavingThrow", R_INT, {R_OBJECT}, &GetWillSavingThrow);
    insert(493, "GetReflexSavingThrow", R_INT, {R_OBJECT}, &GetReflexSavingThrow);
    insert(494, "GetChallengeRating", R_FLOAT, {R_OBJECT}, &GetChallengeRating);
    insert(495, "GetFoundEnemyCreature", R_OBJECT, {R_OBJECT}, &GetFoundEnemyCreature);
    insert(496, "GetMovementRate", R_INT, {R_OBJECT}, &GetMovementRate);
    insert(497, "GetSubRace", R_INT, {R_OBJECT}, &GetSubRace);
    insert(498, "GetStealthXPDecrement", R_INT, {}, &GetStealthXPDecrement);
    insert(499, "SetStealthXPDecrement", R_VOID, {R_INT}, &SetStealthXPDecrement);
    insert(500, "DuplicateHeadAppearance", R_VOID, {R_OBJECT, R_OBJECT}, &DuplicateHeadAppearance);
    insert(503, "CutsceneAttack", R_VOID, {R_OBJECT, R_INT, R_INT, R_INT}, &CutsceneAttack);
    insert(504, "SetCameraMode", R_VOID, {R_OBJECT, R_INT}, &SetCameraMode);
    insert(505, "SetLockOrientationInDialog", R_VOID, {R_OBJECT, R_INT}, &SetLockOrientationInDialog);
    insert(506, "SetLockHeadFollowInDialog", R_VOID, {R_OBJECT, R_INT}, &SetLockHeadFollowInDialog);
    insert(507, "CutsceneMove", R_VOID, {R_OBJECT, R_VECTOR, R_INT}, &CutsceneMove);
    insert(508, "EnableVideoEffect", R_VOID, {R_INT}, &EnableVideoEffect);
    insert(509, "StartNewModule", R_VOID, {R_STRING, R_STRING, R_STRING, R_STRING, R_STRING, R_STRING, R_STRING, R_STRING}, &StartNewModule);
    insert(510, "DisableVideoEffect", R_VOID, {}, &DisableVideoEffect);
    insert(511, "GetWeaponRanged", R_INT, {R_OBJECT}, &GetWeaponRanged);
    insert(512, "DoSinglePlayerAutoSave", R_VOID, {}, &DoSinglePlayerAutoSave);
    insert(513, "GetGameDifficulty", R_INT, {}, &GetGameDifficulty);
    insert(514, "GetUserActionsPending", R_INT, {}, &GetUserActionsPending);
    insert(515, "RevealMap", R_VOID, {R_VECTOR, R_INT}, &RevealMap);
    insert(516, "SetTutorialWindowsEnabled", R_VOID, {R_INT}, &SetTutorialWindowsEnabled);
    insert(517, "ShowTutorialWindow", R_VOID, {R_INT}, &ShowTutorialWindow);
    insert(518, "StartCreditSequence", R_VOID, {R_INT, R_STRING}, &StartCreditSequence);
    insert(519, "IsCreditSequenceInProgress", R_INT, {}, &IsCreditSequenceInProgress);
    insert(522, "GetCurrentAction", R_INT, {R_OBJECT}, &GetCurrentAction);
    insert(523, "GetDifficultyModifier", R_FLOAT, {}, &GetDifficultyModifier);
    insert(524, "GetAppearanceType", R_INT, {R_OBJECT}, &GetAppearanceType);
    insert(525, "FloatingTextStrRefOnCreature", R_VOID, {R_INT, R_OBJECT, R_INT}, &FloatingTextStrRefOnCreature);
    insert(526, "FloatingTextStringOnCreature", R_VOID, {R_STRING, R_OBJECT, R_INT}, &FloatingTextStringOnCreature);
    insert(527, "GetTrapDisarmable", R_INT, {R_OBJECT}, &GetTrapDisarmable);
    insert(528, "GetTrapDetectable", R_INT, {R_OBJECT}, &GetTrapDetectable);
    insert(529, "GetTrapDetectedBy", R_INT, {R_OBJECT, R_OBJECT}, &GetTrapDetectedBy);
    insert(530, "GetTrapFlagged", R_INT, {R_OBJECT}, &GetTrapFlagged);
    insert(531, "GetTrapBaseType", R_INT, {R_OBJECT}, &GetTrapBaseType);
    insert(532, "GetTrapOneShot", R_INT, {R_OBJECT}, &GetTrapOneShot);
    insert(533, "GetTrapCreator", R_OBJECT, {R_OBJECT}, &GetTrapCreator);
    insert(534, "GetTrapKeyTag", R_STRING, {R_OBJECT}, &GetTrapKeyTag);
    insert(535, "GetTrapDisarmDC", R_INT, {R_OBJECT}, &GetTrapDisarmDC);
    insert(536, "GetTrapDetectDC", R_INT, {R_OBJECT}, &GetTrapDetectDC);
    insert(537, "GetLockKeyRequired", R_INT, {R_OBJECT}, &GetLockKeyRequired);
    insert(538, "GetLockKeyTag", R_INT, {R_OBJECT}, &GetLockKeyTag);
    insert(539, "GetLockLockable", R_INT, {R_OBJECT}, &GetLockLockable);
    insert(540, "GetLockUnlockDC", R_INT, {R_OBJECT}, &GetLockUnlockDC);
    insert(541, "GetLockLockDC", R_INT, {R_OBJECT}, &GetLockLockDC);
    insert(542, "GetPCLevellingUp", R_OBJECT, {}, &GetPCLevellingUp);
    insert(543, "GetHasFeatEffect", R_INT, {R_INT, R_OBJECT}, &GetHasFeatEffect);
    insert(544, "SetPlaceableIllumination", R_VOID, {R_OBJECT, R_INT}, &SetPlaceableIllumination);
    insert(545, "GetPlaceableIllumination", R_INT, {R_OBJECT}, &GetPlaceableIllumination);
    insert(546, "GetIsPlaceableObjectActionPossible", R_INT, {R_OBJECT, R_INT}, &GetIsPlaceableObjectActionPossible);
    insert(547, "DoPlaceableObjectAction", R_VOID, {R_OBJECT, R_INT}, &DoPlaceableObjectAction);
    insert(548, "GetFirstPC", R_OBJECT, {}, &GetFirstPC);
    insert(549, "GetNextPC", R_OBJECT, {}, &GetNextPC);
    insert(550, "SetTrapDetectedBy", R_INT, {R_OBJECT, R_OBJECT}, &SetTrapDetectedBy);
    insert(551, "GetIsTrapped", R_INT, {R_OBJECT}, &GetIsTrapped);
    insert(552, "SetEffectIcon", R_EFFECT, {R_EFFECT, R_INT}, &SetEffectIcon);
    insert(553, "FaceObjectAwayFromObject", R_VOID, {R_OBJECT, R_OBJECT}, &FaceObjectAwayFromObject);
    insert(554, "PopUpDeathGUIPanel", R_VOID, {R_OBJECT, R_INT, R_INT, R_INT, R_STRING}, &PopUpDeathGUIPanel);
    insert(555, "SetTrapDisabled", R_VOID, {R_OBJECT}, &SetTrapDisabled);
    insert(556, "GetLastHostileActor", R_OBJECT, {R_OBJECT}, &GetLastHostileActor);
    insert(557, "ExportAllCharacters", R_VOID, {}, &ExportAllCharacters);
    insert(558, "MusicBackgroundGetDayTrack", R_INT, {R_OBJECT}, &MusicBackgroundGetDayTrack);
    insert(559, "MusicBackgroundGetNightTrack", R_INT, {R_OBJECT}, &MusicBackgroundGetNightTrack);
    insert(560, "WriteTimestampedLogEntry", R_VOID, {R_STRING}, &WriteTimestampedLogEntry);
    insert(561, "GetModuleName", R_STRING, {}, &GetModuleName);
    insert(562, "GetFactionLeader", R_OBJECT, {R_OBJECT}, &GetFactionLeader);
    insert(564, "EndGame", R_VOID, {R_INT}, &EndGame);
    insert(565, "GetRunScriptVar", R_INT, {}, &GetRunScriptVar);
    insert(566, "GetCreatureMovmentType", R_INT, {R_OBJECT}, &GetCreatureMovmentType);
    insert(567, "AmbientSoundSetDayVolume", R_VOID, {R_OBJECT, R_INT}, &AmbientSoundSetDayVolume);
    insert(568, "AmbientSoundSetNightVolume", R_VOID, {R_OBJECT, R_INT}, &AmbientSoundSetNightVolume);
    insert(569, "MusicBackgroundGetBattleTrack", R_INT, {R_OBJECT}, &MusicBackgroundGetBattleTrack);
    insert(570, "GetHasInventory", R_INT, {R_OBJECT}, &GetHasInventory);
    insert(571, "GetStrRefSoundDuration", R_FLOAT, {R_INT}, &GetStrRefSoundDuration);
    insert(572, "AddToParty", R_VOID, {R_OBJECT, R_OBJECT}, &AddToParty);
    insert(573, "RemoveFromParty", R_VOID, {R_OBJECT}, &RemoveFromParty);
    insert(574, "AddPartyMember", R_INT, {R_INT, R_OBJECT}, &AddPartyMember);
    insert(575, "RemovePartyMember", R_INT, {R_INT}, &RemovePartyMember);
    insert(576, "IsObjectPartyMember", R_INT, {R_OBJECT}, &IsObjectPartyMember);
    insert(577, "GetPartyMemberByIndex", R_OBJECT, {R_INT}, &GetPartyMemberByIndex);
    insert(578, "GetGlobalBoolean", R_INT, {R_STRING}, &GetGlobalBoolean);
    insert(579, "SetGlobalBoolean", R_VOID, {R_STRING, R_INT}, &SetGlobalBoolean);
    insert(580, "GetGlobalNumber", R_INT, {R_STRING}, &GetGlobalNumber);
    insert(581, "SetGlobalNumber", R_VOID, {R_STRING, R_INT}, &SetGlobalNumber);
    insert(582, "AurPostString", R_VOID, {R_STRING, R_INT, R_INT, R_FLOAT}, &AurPostString);
    insert(669, "AddJournalWorldEntry", R_VOID, {R_INT, R_STRING, R_STRING}, &AddJournalWorldEntry);
    insert(670, "AddJournalWorldEntryStrref", R_VOID, {R_INT, R_INT}, &AddJournalWorldEntryStrref);
    insert(671, "BarkString", R_VOID, {R_OBJECT, R_INT}, &BarkString);
    insert(672, "DeleteJournalWorldAllEntries", R_VOID, {}, &DeleteJournalWorldAllEntries);
    insert(673, "DeleteJournalWorldEntry", R_VOID, {R_INT}, &DeleteJournalWorldEntry);
    insert(674, "DeleteJournalWorldEntryStrref", R_VOID, {R_INT}, &DeleteJournalWorldEntryStrref);
    insert(677, "PlayVisualAreaEffect", R_VOID, {R_INT, R_LOCATION}, &PlayVisualAreaEffect);
    insert(678, "SetJournalQuestEntryPicture", R_VOID, {R_STRING, R_OBJECT, R_INT, R_INT, R_INT}, &SetJournalQuestEntryPicture);
    insert(679, "GetLocalBoolean", R_INT, {R_OBJECT, R_INT}, &GetLocalBoolean);
    insert(680, "SetLocalBoolean", R_VOID, {R_OBJECT, R_INT, R_INT}, &SetLocalBoolean);
    insert(681, "GetLocalNumber", R_INT, {R_OBJECT, R_INT}, &GetLocalNumber);
    insert(682, "SetLocalNumber", R_VOID, {R_OBJECT, R_INT, R_INT}, &SetLocalNumber);
    insert(689, "SoundObjectGetPitchVariance", R_FLOAT, {R_OBJECT}, &SoundObjectGetPitchVariance);
    insert(690, "SoundObjectSetPitchVariance", R_VOID, {R_OBJECT, R_FLOAT}, &SoundObjectSetPitchVariance);
    insert(691, "SoundObjectGetVolume", R_INT, {R_OBJECT}, &SoundObjectGetVolume);
    insert(692, "GetGlobalLocation", R_LOCATION, {R_STRING}, &GetGlobalLocation);
    insert(693, "SetGlobalLocation", R_VOID, {R_STRING, R_LOCATION}, &SetGlobalLocation);
    insert(694, "AddAvailableNPCByObject", R_INT, {R_INT, R_OBJECT}, &AddAvailableNPCByObject);
    insert(695, "RemoveAvailableNPC", R_INT, {R_INT}, &RemoveAvailableNPC);
    insert(696, "IsAvailableCreature", R_INT, {R_INT}, &IsAvailableCreature);
    insert(697, "AddAvailableNPCByTemplate", R_INT, {R_INT, R_STRING}, &AddAvailableNPCByTemplate);
    insert(698, "SpawnAvailableNPC", R_OBJECT, {R_INT, R_LOCATION}, &SpawnAvailableNPC);
    insert(699, "IsNPCPartyMember", R_INT, {R_INT}, &IsNPCPartyMember);
    insert(701, "GetIsConversationActive", R_INT, {}, &GetIsConversationActive);
    insert(704, "GetPartyAIStyle", R_INT, {}, &GetPartyAIStyle);
    insert(705, "GetNPCAIStyle", R_INT, {R_OBJECT}, &GetNPCAIStyle);
    insert(706, "SetPartyAIStyle", R_VOID, {R_INT}, &SetPartyAIStyle);
    insert(707, "SetNPCAIStyle", R_VOID, {R_OBJECT, R_INT}, &SetNPCAIStyle);
    insert(708, "SetNPCSelectability", R_VOID, {R_INT, R_INT}, &SetNPCSelectability);
    insert(709, "GetNPCSelectability", R_INT, {R_INT}, &GetNPCSelectability);
    insert(710, "ClearAllEffects", R_VOID, {}, &ClearAllEffects);
    insert(711, "GetLastConversation", R_STRING, {}, &GetLastConversation);
    insert(712, "ShowPartySelectionGUI", R_VOID, {R_STRING, R_INT, R_INT}, &ShowPartySelectionGUI);
    insert(713, "GetStandardFaction", R_INT, {R_OBJECT}, &GetStandardFaction);
    insert(714, "GivePlotXP", R_VOID, {R_STRING, R_INT}, &GivePlotXP);
    insert(715, "GetMinOneHP", R_INT, {R_OBJECT}, &GetMinOneHP);
    insert(716, "SetMinOneHP", R_VOID, {R_OBJECT, R_INT}, &SetMinOneHP);
    insert(719, "SetGlobalFadeIn", R_VOID, {R_FLOAT, R_FLOAT, R_FLOAT, R_FLOAT, R_FLOAT}, &SetGlobalFadeIn);
    insert(720, "SetGlobalFadeOut", R_VOID, {R_FLOAT, R_FLOAT, R_FLOAT, R_FLOAT, R_FLOAT}, &SetGlobalFadeOut);
    insert(721, "GetLastHostileTarget", R_OBJECT, {R_OBJECT}, &GetLastHostileTarget);
    insert(722, "GetLastAttackAction", R_INT, {R_OBJECT}, &GetLastAttackAction);
    insert(723, "GetLastForcePowerUsed", R_INT, {R_OBJECT}, &GetLastForcePowerUsed);
    insert(724, "GetLastCombatFeatUsed", R_INT, {R_OBJECT}, &GetLastCombatFeatUsed);
    insert(725, "GetLastAttackResult", R_INT, {R_OBJECT}, &GetLastAttackResult);
    insert(726, "GetWasForcePowerSuccessful", R_INT, {R_OBJECT}, &GetWasForcePowerSuccessful);
    insert(727, "GetFirstAttacker", R_OBJECT, {R_OBJECT}, &GetFirstAttacker);
    insert(728, "GetNextAttacker", R_OBJECT, {R_OBJECT}, &GetNextAttacker);
    insert(729, "SetFormation", R_VOID, {R_OBJECT, R_OBJECT, R_INT, R_INT}, &SetFormation);
    insert(731, "SetForcePowerUnsuccessful", R_VOID, {R_INT, R_OBJECT}, &SetForcePowerUnsuccessful);
    insert(732, "GetIsDebilitated", R_INT, {R_OBJECT}, &GetIsDebilitated);
    insert(733, "PlayMovie", R_VOID, {R_STRING}, &PlayMovie);
    insert(734, "SaveNPCState", R_VOID, {R_INT}, &SaveNPCState);
    insert(735, "GetCategoryFromTalent", R_INT, {R_TALENT}, &GetCategoryFromTalent);
    insert(736, "SurrenderByFaction", R_VOID, {R_INT, R_INT}, &SurrenderByFaction);
    insert(737, "ChangeFactionByFaction", R_VOID, {R_INT, R_INT}, &ChangeFactionByFaction);
    insert(738, "PlayRoomAnimation", R_VOID, {R_STRING, R_INT}, &PlayRoomAnimation);
    insert(739, "ShowGalaxyMap", R_VOID, {R_INT}, &ShowGalaxyMap);
    insert(740, "SetPlanetSelectable", R_VOID, {R_INT, R_INT}, &SetPlanetSelectable);
    insert(741, "GetPlanetSelectable", R_INT, {R_INT}, &GetPlanetSelectable);
    insert(742, "SetPlanetAvailable", R_VOID, {R_INT, R_INT}, &SetPlanetAvailable);
    insert(743, "GetPlanetAvailable", R_INT, {R_INT}, &GetPlanetAvailable);
    insert(744, "GetSelectedPlanet", R_INT, {}, &GetSelectedPlanet);
    insert(745, "SoundObjectFadeAndStop", R_VOID, {R_OBJECT, R_FLOAT}, &SoundObjectFadeAndStop);
    insert(746, "SetAreaFogColor", R_VOID, {R_OBJECT, R_FLOAT, R_FLOAT, R_FLOAT}, &SetAreaFogColor);
    insert(747, "ChangeItemCost", R_VOID, {R_STRING, R_FLOAT}, &ChangeItemCost);
    insert(748, "GetIsLiveContentAvailable", R_INT, {R_INT}, &GetIsLiveContentAvailable);
    insert(749, "ResetDialogState", R_VOID, {}, &ResetDialogState);
    insert(750, "SetGoodEvilValue", R_VOID, {R_OBJECT, R_INT}, &SetGoodEvilValue);
    insert(751, "GetIsPoisoned", R_INT, {R_OBJECT}, &GetIsPoisoned);
    insert(752, "GetSpellTarget", R_OBJECT, {R_OBJECT}, &GetSpellTarget);
    insert(753, "SetSoloMode", R_VOID, {R_INT}, &SetSoloMode);
    insert(757, "CancelPostDialogCharacterSwitch", R_VOID, {}, &CancelPostDialogCharacterSwitch);
    insert(758, "SetMaxHitPoints", R_VOID, {R_OBJECT, R_INT}, &SetMaxHitPoints);
    insert(759, "NoClicksFor", R_VOID, {R_FLOAT}, &NoClicksFor);
    insert(760, "HoldWorldFadeInForDialog", R_VOID, {}, &HoldWorldFadeInForDialog);
    insert(761, "ShipBuild", R_INT, {}, &ShipBuild);
    insert(762, "SurrenderRetainBuffs", R_VOID, {}, &SurrenderRetainBuffs);
    insert(763, "SuppressStatusSummaryEntry", R_VOID, {R_INT}, &SuppressStatusSummaryEntry);
    insert(764, "GetCheatCode", R_INT, {R_INT}, &GetCheatCode);
    insert(765, "SetMusicVolume", R_VOID, {R_FLOAT}, &SetMusicVolume);
    insert(766, "CreateItemOnFloor", R_OBJECT, {R_STRING, R_LOCATION, R_INT}, &CreateItemOnFloor);
    insert(767, "SetAvailableNPCId", R_VOID, {R_INT, R_OBJECT}, &SetAvailableNPCId);
    insert(768, "IsMoviePlaying", R_INT, {}, &IsMoviePlaying);
    insert(769, "QueueMovie", R_VOID, {R_STRING, R_INT}, &QueueMovie);
    insert(770, "PlayMovieQueue", R_VOID, {R_INT}, &PlayMovieQueue);
    insert(771, "YavinHackCloseDoor", R_VOID, {R_OBJECT}, &YavinHackDoorClose);
}

void Routines::registerMainTslRoutines() {
    insert(0, "Random", R_INT, {R_INT}, &Random);
    insert(1, "PrintString", R_VOID, {R_STRING}, &PrintString);
    insert(2, "PrintFloat", R_VOID, {R_FLOAT, R_INT, R_INT}, &PrintFloat);
    insert(3, "FloatToString", R_STRING, {R_FLOAT, R_INT, R_INT}, &FloatToString);
    insert(4, "PrintInteger", R_VOID, {R_INT}, &PrintInteger);
    insert(5, "PrintObject", R_VOID, {R_OBJECT}, &PrintObject);
    insert(6, "AssignCommand", R_VOID, {R_OBJECT, R_ACTION}, &AssignCommand);
    insert(7, "DelayCommand", R_VOID, {R_FLOAT, R_ACTION}, &DelayCommand);
    insert(8, "ExecuteScript", R_VOID, {R_STRING, R_OBJECT, R_INT}, &ExecuteScript);
    insert(9, "ClearAllActions", R_VOID, {}, &ClearAllActions);
    insert(10, "SetFacing", R_VOID, {R_FLOAT}, &SetFacing);
    insert(11, "SwitchPlayerCharacter", R_INT, {R_INT}, &SwitchPlayerCharacter);
    insert(12, "SetTime", R_VOID, {R_INT, R_INT, R_INT, R_INT}, &SetTime);
    insert(13, "SetPartyLeader", R_INT, {R_INT}, &SetPartyLeader);
    insert(14, "SetAreaUnescapable", R_VOID, {R_INT}, &SetAreaUnescapable);
    insert(15, "GetAreaUnescapable", R_INT, {}, &GetAreaUnescapable);
    insert(16, "GetTimeHour", R_INT, {}, &GetTimeHour);
    insert(17, "GetTimeMinute", R_INT, {}, &GetTimeMinute);
    insert(18, "GetTimeSecond", R_INT, {}, &GetTimeSecond);
    insert(19, "GetTimeMillisecond", R_INT, {}, &GetTimeMillisecond);
    insert(24, "GetArea", R_OBJECT, {R_OBJECT}, &GetArea);
    insert(25, "GetEnteringObject", R_OBJECT, {}, &GetEnteringObject);
    insert(26, "GetExitingObject", R_OBJECT, {}, &GetExitingObject);
    insert(27, "GetPosition", R_VECTOR, {R_OBJECT}, &GetPosition);
    insert(28, "GetFacing", R_FLOAT, {R_OBJECT}, &GetFacing);
    insert(29, "GetItemPossessor", R_OBJECT, {R_OBJECT}, &GetItemPossessor);
    insert(30, "GetItemPossessedBy", R_OBJECT, {R_OBJECT, R_STRING}, &GetItemPossessedBy);
    insert(31, "CreateItemOnObject", R_OBJECT, {R_STRING, R_OBJECT, R_INT, R_INT}, &CreateItemOnObject);
    insert(36, "GetLastAttacker", R_OBJECT, {R_OBJECT}, &GetLastAttacker);
    insert(38, "GetNearestCreature", R_OBJECT, {R_INT, R_INT, R_OBJECT, R_INT, R_INT, R_INT, R_INT, R_INT}, &GetNearestCreature);
    insert(41, "GetDistanceToObject", R_FLOAT, {R_OBJECT}, &GetDistanceToObject);
    insert(42, "GetIsObjectValid", R_INT, {R_OBJECT}, &GetIsObjectValid);
    insert(45, "SetCameraFacing", R_VOID, {R_FLOAT}, &SetCameraFacing);
    insert(46, "PlaySound", R_VOID, {R_STRING}, &PlaySound);
    insert(47, "GetSpellTargetObject", R_OBJECT, {}, &GetSpellTargetObject);
    insert(49, "GetCurrentHitPoints", R_INT, {R_OBJECT}, &GetCurrentHitPoints);
    insert(50, "GetMaxHitPoints", R_INT, {R_OBJECT}, &GetMaxHitPoints);
    insert(52, "GetLastItemEquipped", R_OBJECT, {}, &GetLastItemEquipped);
    insert(53, "GetSubScreenID", R_INT, {}, &GetSubScreenID);
    insert(54, "CancelCombat", R_VOID, {R_OBJECT, R_INT}, &CancelCombat);
    insert(55, "GetCurrentForcePoints", R_INT, {R_OBJECT}, &GetCurrentForcePoints);
    insert(56, "GetMaxForcePoints", R_INT, {R_OBJECT}, &GetMaxForcePoints);
    insert(57, "PauseGame", R_VOID, {R_INT}, &PauseGame);
    insert(58, "SetPlayerRestrictMode", R_VOID, {R_INT}, &SetPlayerRestrictMode);
    insert(59, "GetStringLength", R_INT, {R_STRING}, &GetStringLength);
    insert(60, "GetStringUpperCase", R_STRING, {R_STRING}, &GetStringUpperCase);
    insert(61, "GetStringLowerCase", R_STRING, {R_STRING}, &GetStringLowerCase);
    insert(62, "GetStringRight", R_STRING, {R_STRING, R_INT}, &GetStringRight);
    insert(63, "GetStringLeft", R_STRING, {R_STRING, R_INT}, &GetStringLeft);
    insert(64, "InsertString", R_STRING, {R_STRING, R_STRING, R_INT}, &InsertString);
    insert(65, "GetSubString", R_STRING, {R_STRING, R_INT, R_INT}, &GetSubString);
    insert(66, "FindSubString", R_INT, {R_STRING, R_STRING}, &FindSubString);
    insert(67, "fabs", R_FLOAT, {R_FLOAT}, &fabs);
    insert(68, "cos", R_FLOAT, {R_FLOAT}, &cos);
    insert(69, "sin", R_FLOAT, {R_FLOAT}, &sin);
    insert(70, "tan", R_FLOAT, {R_FLOAT}, &tan);
    insert(71, "acos", R_FLOAT, {R_FLOAT}, &acos);
    insert(72, "asin", R_FLOAT, {R_FLOAT}, &asin);
    insert(73, "atan", R_FLOAT, {R_FLOAT}, &atan);
    insert(74, "log", R_FLOAT, {R_FLOAT}, &log);
    insert(75, "pow", R_FLOAT, {R_FLOAT, R_FLOAT}, &pow);
    insert(76, "sqrt", R_FLOAT, {R_FLOAT}, &sqrt);
    insert(77, "abs", R_INT, {R_INT}, &abs);
    insert(83, "GetPlayerRestrictMode", R_INT, {R_OBJECT}, &GetPlayerRestrictMode);
    insert(84, "GetCasterLevel", R_INT, {R_OBJECT}, &GetCasterLevel);
    insert(85, "GetFirstEffect", R_EFFECT, {R_OBJECT}, &GetFirstEffect);
    insert(86, "GetNextEffect", R_EFFECT, {R_OBJECT}, &GetNextEffect);
    insert(87, "RemoveEffect", R_VOID, {R_OBJECT, R_EFFECT}, &RemoveEffect);
    insert(88, "GetIsEffectValid", R_INT, {R_EFFECT}, &GetIsEffectValid);
    insert(89, "GetEffectDurationType", R_INT, {R_EFFECT}, &GetEffectDurationType);
    insert(90, "GetEffectSubType", R_INT, {R_EFFECT}, &GetEffectSubType);
    insert(91, "GetEffectCreator", R_OBJECT, {R_EFFECT}, &GetEffectCreator);
    insert(92, "IntToString", R_STRING, {R_INT}, &IntToString);
    insert(93, "GetFirstObjectInArea", R_OBJECT, {R_OBJECT, R_INT}, &GetFirstObjectInArea);
    insert(94, "GetNextObjectInArea", R_OBJECT, {R_OBJECT, R_INT}, &GetNextObjectInArea);
    insert(95, "d2", R_INT, {R_INT}, &d2);
    insert(96, "d3", R_INT, {R_INT}, &d3);
    insert(97, "d4", R_INT, {R_INT}, &d4);
    insert(98, "d6", R_INT, {R_INT}, &d6);
    insert(99, "d8", R_INT, {R_INT}, &d8);
    insert(100, "d10", R_INT, {R_INT}, &d10);
    insert(101, "d12", R_INT, {R_INT}, &d12);
    insert(102, "d20", R_INT, {R_INT}, &d20);
    insert(103, "d100", R_INT, {R_INT}, &d100);
    insert(104, "VectorMagnitude", R_FLOAT, {R_VECTOR}, &VectorMagnitude);
    insert(105, "GetMetaMagicFeat", R_INT, {}, &GetMetaMagicFeat);
    insert(106, "GetObjectType", R_INT, {R_OBJECT}, &GetObjectType);
    insert(107, "GetRacialType", R_INT, {R_OBJECT}, &GetRacialType);
    insert(108, "FortitudeSave", R_INT, {R_OBJECT, R_INT, R_INT, R_OBJECT}, &FortitudeSave);
    insert(109, "ReflexSave", R_INT, {R_OBJECT, R_INT, R_INT, R_OBJECT}, &ReflexSave);
    insert(110, "WillSave", R_INT, {R_OBJECT, R_INT, R_INT, R_OBJECT}, &WillSave);
    insert(111, "GetSpellSaveDC", R_INT, {}, &GetSpellSaveDC);
    insert(112, "MagicalEffect", R_EFFECT, {R_EFFECT}, &MagicalEffect);
    insert(113, "SupernaturalEffect", R_EFFECT, {R_EFFECT}, &SupernaturalEffect);
    insert(114, "ExtraordinaryEffect", R_EFFECT, {R_EFFECT}, &ExtraordinaryEffect);
    insert(116, "GetAC", R_INT, {R_OBJECT, R_INT}, &GetAC);
    insert(121, "RoundsToSeconds", R_FLOAT, {R_INT}, &RoundsToSeconds);
    insert(122, "HoursToSeconds", R_FLOAT, {R_INT}, &HoursToSeconds);
    insert(123, "TurnsToSeconds", R_FLOAT, {R_INT}, &TurnsToSeconds);
    insert(124, "SoundObjectSetFixedVariance", R_VOID, {R_OBJECT, R_FLOAT}, &SoundObjectSetFixedVariance);
    insert(125, "GetGoodEvilValue", R_INT, {R_OBJECT}, &GetGoodEvilValue);
    insert(126, "GetPartyMemberCount", R_INT, {}, &GetPartyMemberCount);
    insert(127, "GetAlignmentGoodEvil", R_INT, {R_OBJECT}, &GetAlignmentGoodEvil);
    insert(128, "GetFirstObjectInShape", R_OBJECT, {R_INT, R_FLOAT, R_LOCATION, R_INT, R_INT, R_VECTOR}, &GetFirstObjectInShape);
    insert(129, "GetNextObjectInShape", R_OBJECT, {R_INT, R_FLOAT, R_LOCATION, R_INT, R_INT, R_VECTOR}, &GetNextObjectInShape);
    insert(131, "SignalEvent", R_VOID, {R_OBJECT, R_EVENT}, &SignalEvent);
    insert(132, "EventUserDefined", R_EVENT, {R_INT}, &EventUserDefined);
    insert(137, "VectorNormalize", R_VECTOR, {R_VECTOR}, &VectorNormalize);
    insert(138, "GetItemStackSize", R_INT, {R_OBJECT}, &GetItemStackSize);
    insert(139, "GetAbilityScore", R_INT, {R_OBJECT, R_INT}, &GetAbilityScore);
    insert(140, "GetIsDead", R_INT, {R_OBJECT}, &GetIsDead);
    insert(141, "PrintVector", R_VOID, {R_VECTOR, R_INT}, &PrintVector);
    insert(142, "Vector", R_VECTOR, {R_FLOAT, R_FLOAT, R_FLOAT}, &Vector);
    insert(143, "SetFacingPoint", R_VOID, {R_VECTOR}, &SetFacingPoint);
    insert(144, "AngleToVector", R_VECTOR, {R_FLOAT}, &AngleToVector);
    insert(145, "VectorToAngle", R_FLOAT, {R_VECTOR}, &VectorToAngle);
    insert(146, "TouchAttackMelee", R_INT, {R_OBJECT, R_INT}, &TouchAttackMelee);
    insert(147, "TouchAttackRanged", R_INT, {R_OBJECT, R_INT}, &TouchAttackRanged);
    insert(150, "SetItemStackSize", R_VOID, {R_OBJECT, R_INT}, &SetItemStackSize);
    insert(151, "GetDistanceBetween", R_FLOAT, {R_OBJECT, R_OBJECT}, &GetDistanceBetween);
    insert(152, "SetReturnStrref", R_VOID, {R_INT, R_INT, R_INT}, &SetReturnStrref);
    insert(155, "GetItemInSlot", R_OBJECT, {R_INT, R_OBJECT}, &GetItemInSlot);
    insert(160, "SetGlobalString", R_VOID, {R_STRING, R_STRING}, &SetGlobalString);
    insert(162, "SetCommandable", R_VOID, {R_INT, R_OBJECT}, &SetCommandable);
    insert(163, "GetCommandable", R_INT, {R_OBJECT}, &GetCommandable);
    insert(166, "GetHitDice", R_INT, {R_OBJECT}, &GetHitDice);
    insert(168, "GetTag", R_STRING, {R_OBJECT}, &GetTag);
    insert(169, "ResistForce", R_INT, {R_OBJECT, R_OBJECT}, &ResistForce);
    insert(170, "GetEffectType", R_INT, {R_EFFECT}, &GetEffectType);
    insert(172, "GetFactionEqual", R_INT, {R_OBJECT, R_OBJECT}, &GetFactionEqual);
    insert(173, "ChangeFaction", R_VOID, {R_OBJECT, R_OBJECT}, &ChangeFaction);
    insert(174, "GetIsListening", R_INT, {R_OBJECT}, &GetIsListening);
    insert(175, "SetListening", R_VOID, {R_OBJECT, R_INT}, &SetListening);
    insert(176, "SetListenPattern", R_VOID, {R_OBJECT, R_STRING, R_INT}, &SetListenPattern);
    insert(177, "TestStringAgainstPattern", R_INT, {R_STRING, R_STRING}, &TestStringAgainstPattern);
    insert(178, "GetMatchedSubstring", R_STRING, {R_INT}, &GetMatchedSubstring);
    insert(179, "GetMatchedSubstringsCount", R_INT, {}, &GetMatchedSubstringsCount);
    insert(181, "GetFactionWeakestMember", R_OBJECT, {R_OBJECT, R_INT}, &GetFactionWeakestMember);
    insert(182, "GetFactionStrongestMember", R_OBJECT, {R_OBJECT, R_INT}, &GetFactionStrongestMember);
    insert(183, "GetFactionMostDamagedMember", R_OBJECT, {R_OBJECT, R_INT}, &GetFactionMostDamagedMember);
    insert(184, "GetFactionLeastDamagedMember", R_OBJECT, {R_OBJECT, R_INT}, &GetFactionLeastDamagedMember);
    insert(185, "GetFactionGold", R_INT, {R_OBJECT}, &GetFactionGold);
    insert(186, "GetFactionAverageReputation", R_INT, {R_OBJECT, R_OBJECT}, &GetFactionAverageReputation);
    insert(187, "GetFactionAverageGoodEvilAlignment", R_INT, {R_OBJECT}, &GetFactionAverageGoodEvilAlignment);
    insert(188, "SoundObjectGetFixedVariance", R_FLOAT, {R_OBJECT}, &SoundObjectGetFixedVariance);
    insert(189, "GetFactionAverageLevel", R_INT, {R_OBJECT}, &GetFactionAverageLevel);
    insert(190, "GetFactionAverageXP", R_INT, {R_OBJECT}, &GetFactionAverageXP);
    insert(191, "GetFactionMostFrequentClass", R_INT, {R_OBJECT}, &GetFactionMostFrequentClass);
    insert(192, "GetFactionWorstAC", R_OBJECT, {R_OBJECT, R_INT}, &GetFactionWorstAC);
    insert(193, "GetFactionBestAC", R_OBJECT, {R_OBJECT, R_INT}, &GetFactionBestAC);
    insert(194, "GetGlobalString", R_STRING, {R_STRING}, &GetGlobalString);
    insert(195, "GetListenPatternNumber", R_INT, {}, &GetListenPatternNumber);
    insert(197, "GetWaypointByTag", R_OBJECT, {R_STRING}, &GetWaypointByTag);
    insert(198, "GetTransitionTarget", R_OBJECT, {R_OBJECT}, &GetTransitionTarget);
    insert(200, "GetObjectByTag", R_OBJECT, {R_STRING, R_INT}, &GetObjectByTag);
    insert(201, "AdjustAlignment", R_VOID, {R_OBJECT, R_INT, R_INT, R_INT}, &AdjustAlignment);
    insert(203, "SetAreaTransitionBMP", R_VOID, {R_INT, R_STRING}, &SetAreaTransitionBMP);
    insert(208, "GetReputation", R_INT, {R_OBJECT, R_OBJECT}, &GetReputation);
    insert(209, "AdjustReputation", R_VOID, {R_OBJECT, R_OBJECT, R_INT}, &AdjustReputation);
    insert(210, "GetModuleFileName", R_STRING, {}, &GetModuleFileName);
    insert(211, "GetGoingToBeAttackedBy", R_OBJECT, {R_OBJECT}, &GetGoingToBeAttackedBy);
    insert(213, "GetLocation", R_LOCATION, {R_OBJECT}, &GetLocation);
    insert(215, "Location", R_LOCATION, {R_VECTOR, R_FLOAT}, &CreateLocation);
    insert(216, "ApplyEffectAtLocation", R_VOID, {R_INT, R_EFFECT, R_LOCATION, R_FLOAT}, &ApplyEffectAtLocation);
    insert(217, "GetIsPC", R_INT, {R_OBJECT}, &GetIsPC);
    insert(218, "FeetToMeters", R_FLOAT, {R_FLOAT}, &FeetToMeters);
    insert(219, "YardsToMeters", R_FLOAT, {R_FLOAT}, &YardsToMeters);
    insert(220, "ApplyEffectToObject", R_VOID, {R_INT, R_EFFECT, R_OBJECT, R_FLOAT}, &ApplyEffectToObject);
    insert(221, "SpeakString", R_VOID, {R_STRING, R_INT}, &SpeakString);
    insert(222, "GetSpellTargetLocation", R_LOCATION, {}, &GetSpellTargetLocation);
    insert(223, "GetPositionFromLocation", R_VECTOR, {R_LOCATION}, &GetPositionFromLocation);
    insert(225, "GetFacingFromLocation", R_FLOAT, {R_LOCATION}, &GetFacingFromLocation);
    insert(226, "GetNearestCreatureToLocation", R_OBJECT, {R_INT, R_INT, R_LOCATION, R_INT, R_INT, R_INT, R_INT, R_INT}, &GetNearestCreatureToLocation);
    insert(227, "GetNearestObject", R_OBJECT, {R_INT, R_OBJECT, R_INT}, &GetNearestObject);
    insert(228, "GetNearestObjectToLocation", R_OBJECT, {R_INT, R_LOCATION, R_INT}, &GetNearestObjectToLocation);
    insert(229, "GetNearestObjectByTag", R_OBJECT, {R_STRING, R_OBJECT, R_INT}, &GetNearestObjectByTag);
    insert(230, "IntToFloat", R_FLOAT, {R_INT}, &IntToFloat);
    insert(231, "FloatToInt", R_INT, {R_FLOAT}, &FloatToInt);
    insert(232, "StringToInt", R_INT, {R_STRING}, &StringToInt);
    insert(233, "StringToFloat", R_FLOAT, {R_STRING}, &StringToFloat);
    insert(235, "GetIsEnemy", R_INT, {R_OBJECT, R_OBJECT}, &GetIsEnemy);
    insert(236, "GetIsFriend", R_INT, {R_OBJECT, R_OBJECT}, &GetIsFriend);
    insert(237, "GetIsNeutral", R_INT, {R_OBJECT, R_OBJECT}, &GetIsNeutral);
    insert(238, "GetPCSpeaker", R_OBJECT, {}, &GetPCSpeaker);
    insert(239, "GetStringByStrRef", R_STRING, {R_INT}, &GetStringByStrRef);
    insert(241, "DestroyObject", R_VOID, {R_OBJECT, R_FLOAT, R_INT, R_FLOAT, R_INT}, &DestroyObject);
    insert(242, "GetModule", R_OBJECT, {}, &GetModule);
    insert(243, "CreateObject", R_OBJECT, {R_INT, R_STRING, R_LOCATION, R_INT}, &CreateObject);
    insert(244, "EventSpellCastAt", R_EVENT, {R_OBJECT, R_INT, R_INT}, &EventSpellCastAt);
    insert(245, "GetLastSpellCaster", R_OBJECT, {}, &GetLastSpellCaster);
    insert(246, "GetLastSpell", R_INT, {}, &GetLastSpell);
    insert(247, "GetUserDefinedEventNumber", R_INT, {}, &GetUserDefinedEventNumber);
    insert(248, "GetSpellId", R_INT, {}, &GetSpellId);
    insert(249, "RandomName", R_STRING, {}, &RandomName);
    insert(251, "GetLoadFromSaveGame", R_INT, {}, &GetLoadFromSaveGame);
    insert(253, "GetName", R_STRING, {R_OBJECT}, &GetName);
    insert(254, "GetLastSpeaker", R_OBJECT, {}, &GetLastSpeaker);
    insert(255, "BeginConversation", R_INT, {R_STRING, R_OBJECT}, &BeginConversation);
    insert(256, "GetLastPerceived", R_OBJECT, {}, &GetLastPerceived);
    insert(257, "GetLastPerceptionHeard", R_INT, {}, &GetLastPerceptionHeard);
    insert(258, "GetLastPerceptionInaudible", R_INT, {}, &GetLastPerceptionInaudible);
    insert(259, "GetLastPerceptionSeen", R_INT, {}, &GetLastPerceptionSeen);
    insert(260, "GetLastClosedBy", R_OBJECT, {}, &GetLastClosedBy);
    insert(261, "GetLastPerceptionVanished", R_INT, {}, &GetLastPerceptionVanished);
    insert(262, "GetFirstInPersistentObject", R_OBJECT, {R_OBJECT, R_INT, R_INT}, &GetFirstInPersistentObject);
    insert(263, "GetNextInPersistentObject", R_OBJECT, {R_OBJECT, R_INT, R_INT}, &GetNextInPersistentObject);
    insert(264, "GetAreaOfEffectCreator", R_OBJECT, {R_OBJECT}, &GetAreaOfEffectCreator);
    insert(265, "ShowLevelUpGUI", R_INT, {}, &ShowLevelUpGUI);
    insert(266, "SetItemNonEquippable", R_VOID, {R_OBJECT, R_INT}, &SetItemNonEquippable);
    insert(267, "GetButtonMashCheck", R_INT, {}, &GetButtonMashCheck);
    insert(268, "SetButtonMashCheck", R_VOID, {R_INT}, &SetButtonMashCheck);
    insert(271, "GiveItem", R_VOID, {R_OBJECT, R_OBJECT}, &GiveItem);
    insert(272, "ObjectToString", R_STRING, {R_OBJECT}, &ObjectToString);
    insert(274, "GetIsImmune", R_INT, {R_OBJECT, R_INT, R_OBJECT}, &GetIsImmune);
    insert(276, "GetEncounterActive", R_INT, {R_OBJECT}, &GetEncounterActive);
    insert(277, "SetEncounterActive", R_VOID, {R_INT, R_OBJECT}, &SetEncounterActive);
    insert(278, "GetEncounterSpawnsMax", R_INT, {R_OBJECT}, &GetEncounterSpawnsMax);
    insert(279, "SetEncounterSpawnsMax", R_VOID, {R_INT, R_OBJECT}, &SetEncounterSpawnsMax);
    insert(280, "GetEncounterSpawnsCurrent", R_INT, {R_OBJECT}, &GetEncounterSpawnsCurrent);
    insert(281, "SetEncounterSpawnsCurrent", R_VOID, {R_INT, R_OBJECT}, &SetEncounterSpawnsCurrent);
    insert(282, "GetModuleItemAcquired", R_OBJECT, {}, &GetModuleItemAcquired);
    insert(283, "GetModuleItemAcquiredFrom", R_OBJECT, {}, &GetModuleItemAcquiredFrom);
    insert(284, "SetCustomToken", R_VOID, {R_INT, R_STRING}, &SetCustomToken);
    insert(285, "GetHasFeat", R_INT, {R_INT, R_OBJECT}, &GetHasFeat);
    insert(286, "GetHasSkill", R_INT, {R_INT, R_OBJECT}, &GetHasSkill);
    insert(289, "GetObjectSeen", R_INT, {R_OBJECT, R_OBJECT}, &GetObjectSeen);
    insert(290, "GetObjectHeard", R_INT, {R_OBJECT, R_OBJECT}, &GetObjectHeard);
    insert(291, "GetLastPlayerDied", R_OBJECT, {}, &GetLastPlayerDied);
    insert(292, "GetModuleItemLost", R_OBJECT, {}, &GetModuleItemLost);
    insert(293, "GetModuleItemLostBy", R_OBJECT, {}, &GetModuleItemLostBy);
    insert(295, "EventConversation", R_EVENT, {}, &EventConversation);
    insert(296, "SetEncounterDifficulty", R_VOID, {R_INT, R_OBJECT}, &SetEncounterDifficulty);
    insert(297, "GetEncounterDifficulty", R_INT, {R_OBJECT}, &GetEncounterDifficulty);
    insert(298, "GetDistanceBetweenLocations", R_FLOAT, {R_LOCATION, R_LOCATION}, &GetDistanceBetweenLocations);
    insert(299, "GetReflexAdjustedDamage", R_INT, {R_INT, R_OBJECT, R_INT, R_INT, R_OBJECT}, &GetReflexAdjustedDamage);
    insert(300, "PlayAnimation", R_VOID, {R_INT, R_FLOAT, R_FLOAT}, &PlayAnimation);
    insert(301, "TalentSpell", R_TALENT, {R_INT}, &TalentSpell);
    insert(302, "TalentFeat", R_TALENT, {R_INT}, &TalentFeat);
    insert(303, "TalentSkill", R_TALENT, {R_INT}, &TalentSkill);
    insert(304, "GetHasSpellEffect", R_INT, {R_INT, R_OBJECT}, &GetHasSpellEffect);
    insert(305, "GetEffectSpellId", R_INT, {R_EFFECT}, &GetEffectSpellId);
    insert(306, "GetCreatureHasTalent", R_INT, {R_TALENT, R_OBJECT}, &GetCreatureHasTalent);
    insert(307, "GetCreatureTalentRandom", R_TALENT, {R_INT, R_OBJECT, R_INT}, &GetCreatureTalentRandom);
    insert(308, "GetCreatureTalentBest", R_TALENT, {R_INT, R_INT, R_OBJECT, R_INT, R_INT, R_INT}, &GetCreatureTalentBest);
    insert(311, "GetGoldPieceValue", R_INT, {R_OBJECT}, &GetGoldPieceValue);
    insert(312, "GetIsPlayableRacialType", R_INT, {R_OBJECT}, &GetIsPlayableRacialType);
    insert(313, "JumpToLocation", R_VOID, {R_LOCATION}, &JumpToLocation);
    insert(315, "GetSkillRank", R_INT, {R_INT, R_OBJECT}, &GetSkillRank);
    insert(316, "GetAttackTarget", R_OBJECT, {R_OBJECT}, &GetAttackTarget);
    insert(317, "GetLastAttackType", R_INT, {R_OBJECT}, &GetLastAttackType);
    insert(318, "GetLastAttackMode", R_INT, {R_OBJECT}, &GetLastAttackMode);
    insert(319, "GetDistanceBetween2D", R_FLOAT, {R_OBJECT, R_OBJECT}, &GetDistanceBetween2D);
    insert(320, "GetIsInCombat", R_INT, {R_OBJECT, R_INT}, &GetIsInCombat);
    insert(321, "GetLastAssociateCommand", R_INT, {R_OBJECT}, &GetLastAssociateCommand);
    insert(322, "GiveGoldToCreature", R_VOID, {R_OBJECT, R_INT}, &GiveGoldToCreature);
    insert(323, "SetIsDestroyable", R_VOID, {R_INT, R_INT, R_INT}, &SetIsDestroyable);
    insert(324, "SetLocked", R_VOID, {R_OBJECT, R_INT}, &SetLocked);
    insert(325, "GetLocked", R_INT, {R_OBJECT}, &GetLocked);
    insert(326, "GetClickingObject", R_OBJECT, {}, &GetClickingObject);
    insert(327, "SetAssociateListenPatterns", R_VOID, {R_OBJECT}, &SetAssociateListenPatterns);
    insert(328, "GetLastWeaponUsed", R_OBJECT, {R_OBJECT}, &GetLastWeaponUsed);
    insert(330, "GetLastUsedBy", R_OBJECT, {}, &GetLastUsedBy);
    insert(331, "GetAbilityModifier", R_INT, {R_INT, R_OBJECT}, &GetAbilityModifier);
    insert(332, "GetIdentified", R_INT, {R_OBJECT}, &GetIdentified);
    insert(333, "SetIdentified", R_VOID, {R_OBJECT, R_INT}, &SetIdentified);
    insert(334, "GetDistanceBetweenLocations2D", R_FLOAT, {R_LOCATION, R_LOCATION}, &GetDistanceBetweenLocations2D);
    insert(335, "GetDistanceToObject2D", R_FLOAT, {R_OBJECT}, &GetDistanceToObject2D);
    insert(336, "GetBlockingDoor", R_OBJECT, {}, &GetBlockingDoor);
    insert(337, "GetIsDoorActionPossible", R_INT, {R_OBJECT, R_INT}, &GetIsDoorActionPossible);
    insert(338, "DoDoorAction", R_VOID, {R_OBJECT, R_INT}, &DoDoorAction);
    insert(339, "GetFirstItemInInventory", R_OBJECT, {R_OBJECT}, &GetFirstItemInInventory);
    insert(340, "GetNextItemInInventory", R_OBJECT, {R_OBJECT}, &GetNextItemInInventory);
    insert(341, "GetClassByPosition", R_INT, {R_INT, R_OBJECT}, &GetClassByPosition);
    insert(342, "GetLevelByPosition", R_INT, {R_INT, R_OBJECT}, &GetLevelByPosition);
    insert(343, "GetLevelByClass", R_INT, {R_INT, R_OBJECT}, &GetLevelByClass);
    insert(344, "GetDamageDealtByType", R_INT, {R_INT}, &GetDamageDealtByType);
    insert(345, "GetTotalDamageDealt", R_INT, {}, &GetTotalDamageDealt);
    insert(346, "GetLastDamager", R_OBJECT, {}, &GetLastDamager);
    insert(347, "GetLastDisarmed", R_OBJECT, {}, &GetLastDisarmed);
    insert(348, "GetLastDisturbed", R_OBJECT, {}, &GetLastDisturbed);
    insert(349, "GetLastLocked", R_OBJECT, {}, &GetLastLocked);
    insert(350, "GetLastUnlocked", R_OBJECT, {}, &GetLastUnlocked);
    insert(352, "GetInventoryDisturbType", R_INT, {}, &GetInventoryDisturbType);
    insert(353, "GetInventoryDisturbItem", R_OBJECT, {}, &GetInventoryDisturbItem);
    insert(354, "ShowUpgradeScreen", R_VOID, {R_OBJECT, R_OBJECT, R_INT, R_INT, R_STRING}, &ShowUpgradeScreen);
    insert(355, "VersusAlignmentEffect", R_EFFECT, {R_EFFECT, R_INT, R_INT}, &VersusAlignmentEffect);
    insert(356, "VersusRacialTypeEffect", R_EFFECT, {R_EFFECT, R_INT}, &VersusRacialTypeEffect);
    insert(357, "VersusTrapEffect", R_EFFECT, {R_EFFECT}, &VersusTrapEffect);
    insert(358, "GetGender", R_INT, {R_OBJECT}, &GetGender);
    insert(359, "GetIsTalentValid", R_INT, {R_TALENT}, &GetIsTalentValid);
    insert(361, "GetAttemptedAttackTarget", R_OBJECT, {}, &GetAttemptedAttackTarget);
    insert(362, "GetTypeFromTalent", R_INT, {R_TALENT}, &GetTypeFromTalent);
    insert(363, "GetIdFromTalent", R_INT, {R_TALENT}, &GetIdFromTalent);
    insert(364, "PlayPazaak", R_VOID, {R_INT, R_STRING, R_INT, R_INT, R_OBJECT}, &PlayPazaak);
    insert(365, "GetLastPazaakResult", R_INT, {}, &GetLastPazaakResult);
    insert(366, "DisplayFeedBackText", R_VOID, {R_OBJECT, R_INT}, &DisplayFeedBackText);
    insert(367, "AddJournalQuestEntry", R_VOID, {R_STRING, R_INT, R_INT}, &AddJournalQuestEntry);
    insert(368, "RemoveJournalQuestEntry", R_VOID, {R_STRING}, &RemoveJournalQuestEntry);
    insert(369, "GetJournalEntry", R_INT, {R_STRING}, &GetJournalEntry);
    insert(370, "PlayRumblePattern", R_INT, {R_INT}, &PlayRumblePattern);
    insert(371, "StopRumblePattern", R_INT, {R_INT}, &StopRumblePattern);
    insert(374, "SendMessageToPC", R_VOID, {R_OBJECT, R_STRING}, &SendMessageToPC);
    insert(375, "GetAttemptedSpellTarget", R_OBJECT, {}, &GetAttemptedSpellTarget);
    insert(376, "GetLastOpenedBy", R_OBJECT, {}, &GetLastOpenedBy);
    insert(377, "GetHasSpell", R_INT, {R_INT, R_OBJECT}, &GetHasSpell);
    insert(378, "OpenStore", R_VOID, {R_OBJECT, R_OBJECT, R_INT, R_INT}, &OpenStore);
    insert(380, "GetFirstFactionMember", R_OBJECT, {R_OBJECT, R_INT}, &GetFirstFactionMember);
    insert(381, "GetNextFactionMember", R_OBJECT, {R_OBJECT, R_INT}, &GetNextFactionMember);
    insert(384, "GetJournalQuestExperience", R_INT, {R_STRING}, &GetJournalQuestExperience);
    insert(385, "JumpToObject", R_VOID, {R_OBJECT, R_INT}, &JumpToObject);
    insert(386, "SetMapPinEnabled", R_VOID, {R_OBJECT, R_INT}, &SetMapPinEnabled);
    insert(388, "PopUpGUIPanel", R_VOID, {R_OBJECT, R_INT}, &PopUpGUIPanel);
    insert(389, "AddMultiClass", R_VOID, {R_INT, R_OBJECT}, &AddMultiClass);
    insert(390, "GetIsLinkImmune", R_INT, {R_OBJECT, R_EFFECT}, &GetIsLinkImmune);
    insert(393, "GiveXPToCreature", R_VOID, {R_OBJECT, R_INT}, &GiveXPToCreature);
    insert(394, "SetXP", R_VOID, {R_OBJECT, R_INT}, &SetXP);
    insert(395, "GetXP", R_INT, {R_OBJECT}, &GetXP);
    insert(396, "IntToHexString", R_STRING, {R_INT}, &IntToHexString);
    insert(397, "GetBaseItemType", R_INT, {R_OBJECT}, &GetBaseItemType);
    insert(398, "GetItemHasItemProperty", R_INT, {R_OBJECT, R_INT}, &GetItemHasItemProperty);
    insert(401, "GetItemACValue", R_INT, {R_OBJECT}, &GetItemACValue);
    insert(403, "ExploreAreaForPlayer", R_VOID, {R_OBJECT, R_OBJECT}, &ExploreAreaForPlayer);
    insert(405, "GetIsDay", R_INT, {}, &GetIsDay);
    insert(406, "GetIsNight", R_INT, {}, &GetIsNight);
    insert(407, "GetIsDawn", R_INT, {}, &GetIsDawn);
    insert(408, "GetIsDusk", R_INT, {}, &GetIsDusk);
    insert(409, "GetIsEncounterCreature", R_INT, {R_OBJECT}, &GetIsEncounterCreature);
    insert(410, "GetLastPlayerDying", R_OBJECT, {}, &GetLastPlayerDying);
    insert(411, "GetStartingLocation", R_LOCATION, {}, &GetStartingLocation);
    insert(412, "ChangeToStandardFaction", R_VOID, {R_OBJECT, R_INT}, &ChangeToStandardFaction);
    insert(413, "SoundObjectPlay", R_VOID, {R_OBJECT}, &SoundObjectPlay);
    insert(414, "SoundObjectStop", R_VOID, {R_OBJECT}, &SoundObjectStop);
    insert(415, "SoundObjectSetVolume", R_VOID, {R_OBJECT, R_INT}, &SoundObjectSetVolume);
    insert(416, "SoundObjectSetPosition", R_VOID, {R_OBJECT, R_VECTOR}, &SoundObjectSetPosition);
    insert(417, "SpeakOneLinerConversation", R_VOID, {R_STRING, R_OBJECT}, &SpeakOneLinerConversation);
    insert(418, "GetGold", R_INT, {R_OBJECT}, &GetGold);
    insert(419, "GetLastRespawnButtonPresser", R_OBJECT, {}, &GetLastRespawnButtonPresser);
    insert(421, "SetLightsaberPowered", R_VOID, {R_OBJECT, R_INT, R_INT, R_INT}, &SetLightsaberPowered);
    insert(422, "GetIsWeaponEffective", R_INT, {R_OBJECT, R_INT}, &GetIsWeaponEffective);
    insert(423, "GetLastSpellHarmful", R_INT, {}, &GetLastSpellHarmful);
    insert(424, "EventActivateItem", R_EVENT, {R_OBJECT, R_LOCATION, R_OBJECT}, &EventActivateItem);
    insert(425, "MusicBackgroundPlay", R_VOID, {R_OBJECT}, &MusicBackgroundPlay);
    insert(426, "MusicBackgroundStop", R_VOID, {R_OBJECT}, &MusicBackgroundStop);
    insert(427, "MusicBackgroundSetDelay", R_VOID, {R_OBJECT, R_INT}, &MusicBackgroundSetDelay);
    insert(428, "MusicBackgroundChangeDay", R_VOID, {R_OBJECT, R_INT, R_INT}, &MusicBackgroundChangeDay);
    insert(429, "MusicBackgroundChangeNight", R_VOID, {R_OBJECT, R_INT, R_INT}, &MusicBackgroundChangeNight);
    insert(430, "MusicBattlePlay", R_VOID, {R_OBJECT}, &MusicBattlePlay);
    insert(431, "MusicBattleStop", R_VOID, {R_OBJECT}, &MusicBattleStop);
    insert(432, "MusicBattleChange", R_VOID, {R_OBJECT, R_INT}, &MusicBattleChange);
    insert(433, "AmbientSoundPlay", R_VOID, {R_OBJECT}, &AmbientSoundPlay);
    insert(434, "AmbientSoundStop", R_VOID, {R_OBJECT}, &AmbientSoundStop);
    insert(435, "AmbientSoundChangeDay", R_VOID, {R_OBJECT, R_INT}, &AmbientSoundChangeDay);
    insert(436, "AmbientSoundChangeNight", R_VOID, {R_OBJECT, R_INT}, &AmbientSoundChangeNight);
    insert(437, "GetLastKiller", R_OBJECT, {}, &GetLastKiller);
    insert(438, "GetSpellCastItem", R_OBJECT, {}, &GetSpellCastItem);
    insert(439, "GetItemActivated", R_OBJECT, {}, &GetItemActivated);
    insert(440, "GetItemActivator", R_OBJECT, {}, &GetItemActivator);
    insert(441, "GetItemActivatedTargetLocation", R_LOCATION, {}, &GetItemActivatedTargetLocation);
    insert(442, "GetItemActivatedTarget", R_OBJECT, {}, &GetItemActivatedTarget);
    insert(443, "GetIsOpen", R_INT, {R_OBJECT}, &GetIsOpen);
    insert(444, "TakeGoldFromCreature", R_VOID, {R_INT, R_OBJECT, R_INT}, &TakeGoldFromCreature);
    insert(445, "GetIsInConversation", R_INT, {R_OBJECT}, &GetIsInConversation);
    insert(455, "GetPlotFlag", R_INT, {R_OBJECT}, &GetPlotFlag);
    insert(456, "SetPlotFlag", R_VOID, {R_OBJECT, R_INT}, &SetPlotFlag);
    insert(461, "SetDialogPlaceableCamera", R_VOID, {R_INT}, &SetDialogPlaceableCamera);
    insert(462, "GetSoloMode", R_INT, {}, &GetSoloMode);
    insert(464, "GetMaxStealthXP", R_INT, {}, &GetMaxStealthXP);
    insert(468, "SetMaxStealthXP", R_VOID, {R_INT}, &SetMaxStealthXP);
    insert(474, "GetCurrentStealthXP", R_INT, {}, &GetCurrentStealthXP);
    insert(475, "GetNumStackedItems", R_INT, {R_OBJECT}, &GetNumStackedItems);
    insert(476, "SurrenderToEnemies", R_VOID, {}, &SurrenderToEnemies);
    insert(478, "SetCurrentStealthXP", R_VOID, {R_INT}, &SetCurrentStealthXP);
    insert(479, "GetCreatureSize", R_INT, {R_OBJECT}, &GetCreatureSize);
    insert(480, "AwardStealthXP", R_VOID, {R_OBJECT}, &AwardStealthXP);
    insert(481, "GetStealthXPEnabled", R_INT, {}, &GetStealthXPEnabled);
    insert(482, "SetStealthXPEnabled", R_VOID, {R_INT}, &SetStealthXPEnabled);
    insert(486, "GetLastTrapDetected", R_OBJECT, {R_OBJECT}, &GetLastTrapDetected);
    insert(488, "GetNearestTrapToObject", R_OBJECT, {R_OBJECT, R_INT}, &GetNearestTrapToObject);
    insert(489, "GetAttemptedMovementTarget", R_OBJECT, {}, &GetAttemptedMovementTarget);
    insert(490, "GetBlockingCreature", R_OBJECT, {R_OBJECT}, &GetBlockingCreature);
    insert(491, "GetFortitudeSavingThrow", R_INT, {R_OBJECT}, &GetFortitudeSavingThrow);
    insert(492, "GetWillSavingThrow", R_INT, {R_OBJECT}, &GetWillSavingThrow);
    insert(493, "GetReflexSavingThrow", R_INT, {R_OBJECT}, &GetReflexSavingThrow);
    insert(494, "GetChallengeRating", R_FLOAT, {R_OBJECT}, &GetChallengeRating);
    insert(495, "GetFoundEnemyCreature", R_OBJECT, {R_OBJECT}, &GetFoundEnemyCreature);
    insert(496, "GetMovementRate", R_INT, {R_OBJECT}, &GetMovementRate);
    insert(497, "GetSubRace", R_INT, {R_OBJECT}, &GetSubRace);
    insert(498, "GetStealthXPDecrement", R_INT, {}, &GetStealthXPDecrement);
    insert(499, "SetStealthXPDecrement", R_VOID, {R_INT}, &SetStealthXPDecrement);
    insert(500, "DuplicateHeadAppearance", R_VOID, {R_OBJECT, R_OBJECT}, &DuplicateHeadAppearance);
    insert(503, "CutsceneAttack", R_VOID, {R_OBJECT, R_INT, R_INT, R_INT}, &CutsceneAttack);
    insert(504, "SetCameraMode", R_VOID, {R_OBJECT, R_INT}, &SetCameraMode);
    insert(505, "SetLockOrientationInDialog", R_VOID, {R_OBJECT, R_INT}, &SetLockOrientationInDialog);
    insert(506, "SetLockHeadFollowInDialog", R_VOID, {R_OBJECT, R_INT}, &SetLockHeadFollowInDialog);
    insert(507, "CutsceneMove", R_VOID, {R_OBJECT, R_VECTOR, R_INT}, &CutsceneMove);
    insert(508, "EnableVideoEffect", R_VOID, {R_INT}, &EnableVideoEffect);
    insert(509, "StartNewModule", R_VOID, {R_STRING, R_STRING, R_STRING, R_STRING, R_STRING, R_STRING, R_STRING, R_STRING}, &StartNewModule);
    insert(510, "DisableVideoEffect", R_VOID, {}, &DisableVideoEffect);
    insert(511, "GetWeaponRanged", R_INT, {R_OBJECT}, &GetWeaponRanged);
    insert(512, "DoSinglePlayerAutoSave", R_VOID, {}, &DoSinglePlayerAutoSave);
    insert(513, "GetGameDifficulty", R_INT, {}, &GetGameDifficulty);
    insert(514, "GetUserActionsPending", R_INT, {}, &GetUserActionsPending);
    insert(515, "RevealMap", R_VOID, {R_VECTOR, R_INT}, &RevealMap);
    insert(516, "SetTutorialWindowsEnabled", R_VOID, {R_INT}, &SetTutorialWindowsEnabled);
    insert(517, "ShowTutorialWindow", R_VOID, {R_INT}, &ShowTutorialWindow);
    insert(518, "StartCreditSequence", R_VOID, {R_INT, R_STRING}, &StartCreditSequence);
    insert(519, "IsCreditSequenceInProgress", R_INT, {}, &IsCreditSequenceInProgress);
    insert(522, "GetCurrentAction", R_INT, {R_OBJECT}, &GetCurrentAction);
    insert(523, "GetDifficultyModifier", R_FLOAT, {}, &GetDifficultyModifier);
    insert(524, "GetAppearanceType", R_INT, {R_OBJECT}, &GetAppearanceType);
    insert(525, "FloatingTextStrRefOnCreature", R_VOID, {R_INT, R_OBJECT, R_INT}, &FloatingTextStrRefOnCreature);
    insert(526, "FloatingTextStringOnCreature", R_VOID, {R_STRING, R_OBJECT, R_INT}, &FloatingTextStringOnCreature);
    insert(527, "GetTrapDisarmable", R_INT, {R_OBJECT}, &GetTrapDisarmable);
    insert(528, "GetTrapDetectable", R_INT, {R_OBJECT}, &GetTrapDetectable);
    insert(529, "GetTrapDetectedBy", R_INT, {R_OBJECT, R_OBJECT}, &GetTrapDetectedBy);
    insert(530, "GetTrapFlagged", R_INT, {R_OBJECT}, &GetTrapFlagged);
    insert(531, "GetTrapBaseType", R_INT, {R_OBJECT}, &GetTrapBaseType);
    insert(532, "GetTrapOneShot", R_INT, {R_OBJECT}, &GetTrapOneShot);
    insert(533, "GetTrapCreator", R_OBJECT, {R_OBJECT}, &GetTrapCreator);
    insert(534, "GetTrapKeyTag", R_STRING, {R_OBJECT}, &GetTrapKeyTag);
    insert(535, "GetTrapDisarmDC", R_INT, {R_OBJECT}, &GetTrapDisarmDC);
    insert(536, "GetTrapDetectDC", R_INT, {R_OBJECT}, &GetTrapDetectDC);
    insert(537, "GetLockKeyRequired", R_INT, {R_OBJECT}, &GetLockKeyRequired);
    insert(538, "GetLockKeyTag", R_INT, {R_OBJECT}, &GetLockKeyTag);
    insert(539, "GetLockLockable", R_INT, {R_OBJECT}, &GetLockLockable);
    insert(540, "GetLockUnlockDC", R_INT, {R_OBJECT}, &GetLockUnlockDC);
    insert(541, "GetLockLockDC", R_INT, {R_OBJECT}, &GetLockLockDC);
    insert(542, "GetPCLevellingUp", R_OBJECT, {}, &GetPCLevellingUp);
    insert(543, "GetHasFeatEffect", R_INT, {R_INT, R_OBJECT}, &GetHasFeatEffect);
    insert(544, "SetPlaceableIllumination", R_VOID, {R_OBJECT, R_INT}, &SetPlaceableIllumination);
    insert(545, "GetPlaceableIllumination", R_INT, {R_OBJECT}, &GetPlaceableIllumination);
    insert(546, "GetIsPlaceableObjectActionPossible", R_INT, {R_OBJECT, R_INT}, &GetIsPlaceableObjectActionPossible);
    insert(547, "DoPlaceableObjectAction", R_VOID, {R_OBJECT, R_INT}, &DoPlaceableObjectAction);
    insert(548, "GetFirstPC", R_OBJECT, {}, &GetFirstPC);
    insert(549, "GetNextPC", R_OBJECT, {}, &GetNextPC);
    insert(550, "SetTrapDetectedBy", R_INT, {R_OBJECT, R_OBJECT}, &SetTrapDetectedBy);
    insert(551, "GetIsTrapped", R_INT, {R_OBJECT}, &GetIsTrapped);
    insert(552, "SetEffectIcon", R_EFFECT, {R_EFFECT, R_INT}, &SetEffectIcon);
    insert(553, "FaceObjectAwayFromObject", R_VOID, {R_OBJECT, R_OBJECT}, &FaceObjectAwayFromObject);
    insert(554, "PopUpDeathGUIPanel", R_VOID, {R_OBJECT, R_INT, R_INT, R_INT, R_STRING}, &PopUpDeathGUIPanel);
    insert(555, "SetTrapDisabled", R_VOID, {R_OBJECT}, &SetTrapDisabled);
    insert(556, "GetLastHostileActor", R_OBJECT, {R_OBJECT}, &GetLastHostileActor);
    insert(557, "ExportAllCharacters", R_VOID, {}, &ExportAllCharacters);
    insert(558, "MusicBackgroundGetDayTrack", R_INT, {R_OBJECT}, &MusicBackgroundGetDayTrack);
    insert(559, "MusicBackgroundGetNightTrack", R_INT, {R_OBJECT}, &MusicBackgroundGetNightTrack);
    insert(560, "WriteTimestampedLogEntry", R_VOID, {R_STRING}, &WriteTimestampedLogEntry);
    insert(561, "GetModuleName", R_STRING, {}, &GetModuleName);
    insert(562, "GetFactionLeader", R_OBJECT, {R_OBJECT}, &GetFactionLeader);
    insert(564, "EndGame", R_VOID, {R_INT}, &EndGame);
    insert(565, "GetRunScriptVar", R_INT, {}, &GetRunScriptVar);
    insert(566, "GetCreatureMovmentType", R_INT, {R_OBJECT}, &GetCreatureMovmentType);
    insert(567, "AmbientSoundSetDayVolume", R_VOID, {R_OBJECT, R_INT}, &AmbientSoundSetDayVolume);
    insert(568, "AmbientSoundSetNightVolume", R_VOID, {R_OBJECT, R_INT}, &AmbientSoundSetNightVolume);
    insert(569, "MusicBackgroundGetBattleTrack", R_INT, {R_OBJECT}, &MusicBackgroundGetBattleTrack);
    insert(570, "GetHasInventory", R_INT, {R_OBJECT}, &GetHasInventory);
    insert(571, "GetStrRefSoundDuration", R_FLOAT, {R_INT}, &GetStrRefSoundDuration);
    insert(572, "AddToParty", R_VOID, {R_OBJECT, R_OBJECT}, &AddToParty);
    insert(573, "RemoveFromParty", R_VOID, {R_OBJECT}, &RemoveFromParty);
    insert(574, "AddPartyMember", R_INT, {R_INT, R_OBJECT}, &AddPartyMember);
    insert(575, "RemovePartyMember", R_INT, {R_INT}, &RemovePartyMember);
    insert(576, "IsObjectPartyMember", R_INT, {R_OBJECT}, &IsObjectPartyMember);
    insert(577, "GetPartyMemberByIndex", R_OBJECT, {R_INT}, &GetPartyMemberByIndex);
    insert(578, "GetGlobalBoolean", R_INT, {R_STRING}, &GetGlobalBoolean);
    insert(579, "SetGlobalBoolean", R_VOID, {R_STRING, R_INT}, &SetGlobalBoolean);
    insert(580, "GetGlobalNumber", R_INT, {R_STRING}, &GetGlobalNumber);
    insert(581, "SetGlobalNumber", R_VOID, {R_STRING, R_INT}, &SetGlobalNumber);
    insert(582, "AurPostString", R_VOID, {R_STRING, R_INT, R_INT, R_FLOAT}, &AurPostString);
    insert(669, "AddJournalWorldEntry", R_VOID, {R_INT, R_STRING, R_STRING}, &AddJournalWorldEntry);
    insert(670, "AddJournalWorldEntryStrref", R_VOID, {R_INT, R_INT}, &AddJournalWorldEntryStrref);
    insert(671, "BarkString", R_VOID, {R_OBJECT, R_INT, R_INT, R_INT}, &BarkString);
    insert(672, "DeleteJournalWorldAllEntries", R_VOID, {}, &DeleteJournalWorldAllEntries);
    insert(673, "DeleteJournalWorldEntry", R_VOID, {R_INT}, &DeleteJournalWorldEntry);
    insert(674, "DeleteJournalWorldEntryStrref", R_VOID, {R_INT}, &DeleteJournalWorldEntryStrref);
    insert(677, "PlayVisualAreaEffect", R_VOID, {R_INT, R_LOCATION}, &PlayVisualAreaEffect);
    insert(678, "SetJournalQuestEntryPicture", R_VOID, {R_STRING, R_OBJECT, R_INT, R_INT, R_INT}, &SetJournalQuestEntryPicture);
    insert(679, "GetLocalBoolean", R_INT, {R_OBJECT, R_INT}, &GetLocalBoolean);
    insert(680, "SetLocalBoolean", R_VOID, {R_OBJECT, R_INT, R_INT}, &SetLocalBoolean);
    insert(681, "GetLocalNumber", R_INT, {R_OBJECT, R_INT}, &GetLocalNumber);
    insert(682, "SetLocalNumber", R_VOID, {R_OBJECT, R_INT, R_INT}, &SetLocalNumber);
    insert(689, "SoundObjectGetPitchVariance", R_FLOAT, {R_OBJECT}, &SoundObjectGetPitchVariance);
    insert(690, "SoundObjectSetPitchVariance", R_VOID, {R_OBJECT, R_FLOAT}, &SoundObjectSetPitchVariance);
    insert(691, "SoundObjectGetVolume", R_INT, {R_OBJECT}, &SoundObjectGetVolume);
    insert(692, "GetGlobalLocation", R_LOCATION, {R_STRING}, &GetGlobalLocation);
    insert(693, "SetGlobalLocation", R_VOID, {R_STRING, R_LOCATION}, &SetGlobalLocation);
    insert(694, "AddAvailableNPCByObject", R_INT, {R_INT, R_OBJECT}, &AddAvailableNPCByObject);
    insert(695, "RemoveAvailableNPC", R_INT, {R_INT}, &RemoveAvailableNPC);
    insert(696, "IsAvailableCreature", R_INT, {R_INT}, &IsAvailableCreature);
    insert(697, "AddAvailableNPCByTemplate", R_INT, {R_INT, R_STRING}, &AddAvailableNPCByTemplate);
    insert(698, "SpawnAvailableNPC", R_OBJECT, {R_INT, R_LOCATION}, &SpawnAvailableNPC);
    insert(699, "IsNPCPartyMember", R_INT, {R_INT}, &IsNPCPartyMember);
    insert(701, "GetIsConversationActive", R_INT, {}, &GetIsConversationActive);
    insert(704, "GetPartyAIStyle", R_INT, {}, &GetPartyAIStyle);
    insert(705, "GetNPCAIStyle", R_INT, {R_OBJECT}, &GetNPCAIStyle);
    insert(706, "SetPartyAIStyle", R_VOID, {R_INT}, &SetPartyAIStyle);
    insert(707, "SetNPCAIStyle", R_VOID, {R_OBJECT, R_INT}, &SetNPCAIStyle);
    insert(708, "SetNPCSelectability", R_VOID, {R_INT, R_INT}, &SetNPCSelectability);
    insert(709, "GetNPCSelectability", R_INT, {R_INT}, &GetNPCSelectability);
    insert(710, "ClearAllEffects", R_VOID, {}, &ClearAllEffects);
    insert(711, "GetLastConversation", R_STRING, {}, &GetLastConversation);
    insert(712, "ShowPartySelectionGUI", R_VOID, {R_STRING, R_INT, R_INT, R_INT}, &ShowPartySelectionGUI);
    insert(713, "GetStandardFaction", R_INT, {R_OBJECT}, &GetStandardFaction);
    insert(714, "GivePlotXP", R_VOID, {R_STRING, R_INT}, &GivePlotXP);
    insert(715, "GetMinOneHP", R_INT, {R_OBJECT}, &GetMinOneHP);
    insert(716, "SetMinOneHP", R_VOID, {R_OBJECT, R_INT}, &SetMinOneHP);
    insert(719, "SetGlobalFadeIn", R_VOID, {R_FLOAT, R_FLOAT, R_FLOAT, R_FLOAT, R_FLOAT}, &SetGlobalFadeIn);
    insert(720, "SetGlobalFadeOut", R_VOID, {R_FLOAT, R_FLOAT, R_FLOAT, R_FLOAT, R_FLOAT}, &SetGlobalFadeOut);
    insert(721, "GetLastHostileTarget", R_OBJECT, {R_OBJECT}, &GetLastHostileTarget);
    insert(722, "GetLastAttackAction", R_INT, {R_OBJECT}, &GetLastAttackAction);
    insert(723, "GetLastForcePowerUsed", R_INT, {R_OBJECT}, &GetLastForcePowerUsed);
    insert(724, "GetLastCombatFeatUsed", R_INT, {R_OBJECT}, &GetLastCombatFeatUsed);
    insert(725, "GetLastAttackResult", R_INT, {R_OBJECT}, &GetLastAttackResult);
    insert(726, "GetWasForcePowerSuccessful", R_INT, {R_OBJECT}, &GetWasForcePowerSuccessful);
    insert(727, "GetFirstAttacker", R_OBJECT, {R_OBJECT}, &GetFirstAttacker);
    insert(728, "GetNextAttacker", R_OBJECT, {R_OBJECT}, &GetNextAttacker);
    insert(729, "SetFormation", R_VOID, {R_OBJECT, R_OBJECT, R_INT, R_INT}, &SetFormation);
    insert(731, "SetForcePowerUnsuccessful", R_VOID, {R_INT, R_OBJECT}, &SetForcePowerUnsuccessful);
    insert(732, "GetIsDebilitated", R_INT, {R_OBJECT}, &GetIsDebilitated);
    insert(733, "PlayMovie", R_VOID, {R_STRING, R_INT}, &PlayMovie);
    insert(734, "SaveNPCState", R_VOID, {R_INT}, &SaveNPCState);
    insert(735, "GetCategoryFromTalent", R_INT, {R_TALENT}, &GetCategoryFromTalent);
    insert(736, "SurrenderByFaction", R_VOID, {R_INT, R_INT}, &SurrenderByFaction);
    insert(737, "ChangeFactionByFaction", R_VOID, {R_INT, R_INT}, &ChangeFactionByFaction);
    insert(738, "PlayRoomAnimation", R_VOID, {R_STRING, R_INT}, &PlayRoomAnimation);
    insert(739, "ShowGalaxyMap", R_VOID, {R_INT}, &ShowGalaxyMap);
    insert(740, "SetPlanetSelectable", R_VOID, {R_INT, R_INT}, &SetPlanetSelectable);
    insert(741, "GetPlanetSelectable", R_INT, {R_INT}, &GetPlanetSelectable);
    insert(742, "SetPlanetAvailable", R_VOID, {R_INT, R_INT}, &SetPlanetAvailable);
    insert(743, "GetPlanetAvailable", R_INT, {R_INT}, &GetPlanetAvailable);
    insert(744, "GetSelectedPlanet", R_INT, {}, &GetSelectedPlanet);
    insert(745, "SoundObjectFadeAndStop", R_VOID, {R_OBJECT, R_FLOAT}, &SoundObjectFadeAndStop);
    insert(746, "SetAreaFogColor", R_VOID, {R_OBJECT, R_FLOAT, R_FLOAT, R_FLOAT}, &SetAreaFogColor);
    insert(747, "ChangeItemCost", R_VOID, {R_STRING, R_FLOAT}, &ChangeItemCost);
    insert(748, "GetIsLiveContentAvailable", R_INT, {R_INT}, &GetIsLiveContentAvailable);
    insert(749, "ResetDialogState", R_VOID, {}, &ResetDialogState);
    insert(750, "SetGoodEvilValue", R_VOID, {R_OBJECT, R_INT}, &SetGoodEvilValue);
    insert(751, "GetIsPoisoned", R_INT, {R_OBJECT}, &GetIsPoisoned);
    insert(752, "GetSpellTarget", R_OBJECT, {R_OBJECT}, &GetSpellTarget);
    insert(753, "SetSoloMode", R_VOID, {R_INT}, &SetSoloMode);
    insert(757, "CancelPostDialogCharacterSwitch", R_VOID, {}, &CancelPostDialogCharacterSwitch);
    insert(758, "SetMaxHitPoints", R_VOID, {R_OBJECT, R_INT}, &SetMaxHitPoints);
    insert(759, "NoClicksFor", R_VOID, {R_FLOAT}, &NoClicksFor);
    insert(760, "HoldWorldFadeInForDialog", R_VOID, {}, &HoldWorldFadeInForDialog);
    insert(761, "ShipBuild", R_INT, {}, &ShipBuild);
    insert(762, "SurrenderRetainBuffs", R_VOID, {}, &SurrenderRetainBuffs);
    insert(763, "SuppressStatusSummaryEntry", R_VOID, {R_INT}, &SuppressStatusSummaryEntry);
    insert(764, "GetCheatCode", R_INT, {R_INT}, &GetCheatCode);
    insert(765, "SetMusicVolume", R_VOID, {R_FLOAT}, &SetMusicVolume);
    insert(766, "CreateItemOnFloor", R_OBJECT, {R_STRING, R_LOCATION, R_INT}, &CreateItemOnFloor);
    insert(767, "SetAvailableNPCId", R_VOID, {R_INT, R_OBJECT}, &SetAvailableNPCId);
    insert(768, "GetScriptParameter", R_INT, {R_INT}, &GetScriptParameter);
    insert(769, "SetFadeUntilScript", R_VOID, {}, &SetFadeUntilScript);
    insert(771, "GetItemComponent", R_INT, {}, &GetItemComponent);
    insert(772, "GetItemComponentPieceValue", R_INT, {}, &GetItemComponentPieceValue);
    insert(773, "ShowChemicalUpgradeScreen", R_VOID, {R_OBJECT}, &ShowChemicalUpgradeScreen);
    insert(774, "GetChemicals", R_INT, {}, &GetChemicals);
    insert(775, "GetChemicalPieceValue", R_INT, {}, &GetChemicalPieceValue);
    insert(776, "GetSpellForcePointCost", R_INT, {}, &GetSpellForcePointCost);
    insert(783, "GetFeatAcquired", R_INT, {R_INT, R_OBJECT}, &GetFeatAcquired);
    insert(784, "GetSpellAcquired", R_INT, {R_INT, R_OBJECT}, &GetSpellAcquired);
    insert(785, "ShowSwoopUpgradeScreen", R_VOID, {}, &ShowSwoopUpgradeScreen);
    insert(786, "GrantFeat", R_VOID, {R_INT, R_OBJECT}, &GrantFeat);
    insert(787, "GrantSpell", R_VOID, {R_INT, R_OBJECT}, &GrantSpell);
    insert(788, "SpawnMine", R_VOID, {R_INT, R_LOCATION, R_INT, R_INT, R_OBJECT}, &SpawnMine);
    insert(791, "SetFakeCombatState", R_VOID, {R_OBJECT, R_INT}, &SetFakeCombatState);
    insert(793, "GetOwnerDemolitionsSkill", R_INT, {R_OBJECT}, &GetOwnerDemolitionsSkill);
    insert(794, "SetOrientOnClick", R_VOID, {R_OBJECT, R_INT}, &SetOrientOnClick);
    insert(795, "GetInfluence", R_INT, {R_INT}, &GetInfluence);
    insert(796, "SetInfluence", R_VOID, {R_INT, R_INT}, &SetInfluence);
    insert(797, "ModifyInfluence", R_VOID, {R_INT, R_INT}, &ModifyInfluence);
    insert(798, "GetRacialSubType", R_INT, {R_OBJECT}, &GetRacialSubType);
    insert(799, "IncrementGlobalNumber", R_VOID, {R_STRING, R_INT}, &IncrementGlobalNumber);
    insert(800, "DecrementGlobalNumber", R_VOID, {R_STRING, R_INT}, &DecrementGlobalNumber);
    insert(801, "SetBonusForcePoints", R_VOID, {R_OBJECT, R_INT}, &SetBonusForcePoints);
    insert(802, "AddBonusForcePoints", R_VOID, {R_OBJECT, R_INT}, &AddBonusForcePoints);
    insert(803, "GetBonusForcePoints", R_INT, {R_OBJECT}, &GetBonusForcePoints);
    insert(805, "IsMoviePlaying", R_INT, {}, &IsMoviePlaying);
    insert(806, "QueueMovie", R_VOID, {R_STRING, R_INT}, &QueueMovie);
    insert(807, "PlayMovieQueue", R_VOID, {R_INT}, &PlayMovieQueue);
    insert(808, "YavinHackDoorClose", R_VOID, {R_OBJECT}, &YavinHackDoorClose);
    insert(810, "IsStealthed", R_INT, {R_OBJECT}, &IsStealthed);
    insert(811, "IsMeditating", R_INT, {R_OBJECT}, &IsMeditating);
    insert(812, "IsInTotalDefense", R_INT, {R_OBJECT}, &IsInTotalDefense);
    insert(813, "SetHealTarget", R_VOID, {R_OBJECT, R_OBJECT}, &SetHealTarget);
    insert(814, "GetHealTarget", R_OBJECT, {R_OBJECT}, &GetHealTarget);
    insert(815, "GetRandomDestination", R_VECTOR, {R_OBJECT, R_INT}, &GetRandomDestination);
    insert(816, "IsFormActive", R_INT, {R_OBJECT, R_INT}, &IsFormActive);
    insert(817, "GetSpellFormMask", R_INT, {R_INT}, &GetSpellFormMask);
    insert(818, "GetSpellBaseForcePointCost", R_INT, {R_INT}, &GetSpellBaseForcePointCost);
    insert(819, "SetKeepStealthInDialog", R_VOID, {R_INT}, &SetKeepStealthInDialog);
    insert(820, "HasLineOfSight", R_INT, {R_VECTOR, R_VECTOR, R_OBJECT, R_OBJECT}, &HasLineOfSight);
    insert(821, "ShowDemoScreen", R_INT, {R_STRING, R_INT, R_INT, R_INT, R_INT}, &ShowDemoScreen);
    insert(822, "ForceHeartbeat", R_VOID, {R_OBJECT}, &ForceHeartbeat);
    insert(824, "IsRunning", R_INT, {R_OBJECT}, &IsRunning);
    insert(826, "SetForfeitConditions", R_VOID, {R_INT}, &SetForfeitConditions);
    insert(827, "GetLastForfeitViolation", R_INT, {}, &GetLastForfeitViolation);
    insert(828, "ModifyReflexSavingThrowBase", R_VOID, {R_OBJECT, R_INT}, &ModifyReflexSavingThrowBase);
    insert(829, "ModifyFortitudeSavingThrowBase", R_VOID, {R_OBJECT, R_INT}, &ModifyFortitudeSavingThrowBase);
    insert(830, "ModifyWillSavingThrowBase", R_VOID, {R_OBJECT, R_INT}, &ModifyWillSavingThrowBase);
    insert(831, "GetScriptStringParameter", R_STRING, {}, &GetScriptStringParameter);
    insert(832, "GetObjectPersonalSpace", R_FLOAT, {R_OBJECT}, &GetObjectPersonalSpace);
    insert(833, "AdjustCreatureAttributes", R_VOID, {R_OBJECT, R_INT, R_INT}, &AdjustCreatureAttributes);
    insert(834, "SetCreatureAILevel", R_VOID, {R_OBJECT, R_INT}, &SetCreatureAILevel);
    insert(835, "ResetCreatureAILevel", R_VOID, {R_OBJECT}, &ResetCreatureAILevel);
    insert(836, "AddAvailablePUPByTemplate", R_INT, {R_INT, R_STRING}, &AddAvailablePUPByTemplate);
    insert(837, "AddAvailablePUPByObject", R_INT, {R_INT, R_OBJECT}, &AddAvailablePUPByObject);
    insert(838, "AssignPUP", R_INT, {R_INT, R_INT}, &AssignPUP);
    insert(839, "SpawnAvailablePUP", R_OBJECT, {R_INT, R_LOCATION}, &SpawnAvailablePUP);
    insert(840, "AddPartyPuppet", R_INT, {R_INT, R_OBJECT}, &AddPartyPuppet);
    insert(841, "GetPUPOwner", R_OBJECT, {R_OBJECT}, &GetPUPOwner);
    insert(842, "GetIsPuppet", R_INT, {R_OBJECT}, &GetIsPuppet);
    insert(844, "GetIsPartyLeader", R_INT, {R_OBJECT}, &GetIsPartyLeader);
    insert(845, "GetPartyLeader", R_OBJECT, {}, &GetPartyLeader);
    insert(846, "RemoveNPCFromPartyToBase", R_INT, {R_INT}, &RemoveNPCFromPartyToBase);
    insert(847, "CreatureFlourishWeapon", R_VOID, {R_OBJECT}, &CreatureFlourishWeapon);
    insert(850, "ChangeObjectAppearance", R_VOID, {R_OBJECT, R_INT}, &ChangeObjectAppearance);
    insert(851, "GetIsXBox", R_INT, {}, &GetIsXBox);
    insert(854, "PlayOverlayAnimation", R_VOID, {R_OBJECT, R_INT}, &PlayOverlayAnimation);
    insert(855, "UnlockAllSongs", R_VOID, {}, &UnlockAllSongs);
    insert(856, "DisableMap", R_VOID, {R_INT}, &DisableMap);
    insert(857, "DetonateMine", R_VOID, {R_OBJECT}, &DetonateMine);
    insert(858, "DisableHealthRegen", R_VOID, {R_INT}, &DisableHealthRegen);
    insert(859, "SetCurrentForm", R_VOID, {R_OBJECT, R_INT}, &SetCurrentForm);
    insert(860, "SetDisableTransit", R_VOID, {R_INT}, &SetDisableTransit);
    insert(861, "SetInputClass", R_VOID, {R_INT}, &SetInputClass);
    insert(862, "SetForceAlwaysUpdate", R_VOID, {R_OBJECT, R_INT}, &SetForceAlwaysUpdate);
    insert(863, "EnableRain", R_VOID, {R_INT}, &EnableRain);
    insert(864, "DisplayMessageBox", R_VOID, {R_INT, R_STRING}, &DisplayMessageBox);
    insert(865, "DisplayDatapad", R_VOID, {R_OBJECT}, &DisplayDatapad);
    insert(866, "RemoveHeartbeat", R_VOID, {R_OBJECT}, &RemoveHeartbeat);
    insert(867, "RemoveEffectByID", R_VOID, {R_OBJECT, R_INT}, &RemoveEffectByID);
    insert(868, "RemoveEffectByExactMatch", R_VOID, {R_OBJECT, R_EFFECT}, &RemoveEffectByExactMatch);
    insert(869, "AdjustCreatureSkills", R_VOID, {R_OBJECT, R_INT, R_INT}, &AdjustCreatureSkills);
    insert(870, "GetSkillRankBase", R_INT, {R_INT, R_OBJECT}, &GetSkillRankBase);
    insert(871, "EnableRendering", R_VOID, {R_OBJECT, R_INT}, &EnableRendering);
    insert(872, "GetCombatActionsPending", R_INT, {R_OBJECT}, &GetCombatActionsPending);
    insert(873, "SaveNPCByObject", R_VOID, {R_INT, R_OBJECT}, &SaveNPCByObject);
    insert(874, "SavePUPByObject", R_VOID, {R_INT, R_OBJECT}, &SavePUPByObject);
    insert(875, "GetIsPlayerMadeCharacter", R_INT, {R_OBJECT}, &GetIsPlayerMadeCharacter);
    insert(876, "RebuildPartyTable", R_VOID, {}, &RebuildPartyTable);
}

} // namespace game

} // namespace reone
