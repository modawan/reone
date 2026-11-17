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

#include "reone/game/contextorder.h"

#include "reone/game/action/attackobject.h"
#include "reone/game/action/castspellatobject.h"
#include "reone/game/action/doorsaber.h"
#include "reone/game/action/unlockobject.h"
#include "reone/game/action/usefeat.h"
#include "reone/game/action/useskill.h"
#include "reone/game/castspell.h"
#include "reone/game/combat.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/party.h"
#include "reone/resource/di/services.h"
#include "reone/resource/strings.h"

namespace reone {

namespace game {

/** The message shown when the area holds as many player mines as it allows. */
static constexpr int kMineLimitStrRef = 47861;
/** The tutorial windows that the first combat feat, grenade and mine bring. */
static constexpr int kCombatFeatTutorial = 0;
static constexpr int kGrenadeTutorial = 1;
static constexpr int kSetMineTutorial = 2;

ContextOrderOutcome giveContextOrder(
    Game &game,
    ServicesView &services,
    const std::shared_ptr<Creature> &leader,
    const std::shared_ptr<Object> &selectedObject,
    ContextAction &ctxAction,
    bool selectedHostile,
    const std::function<void()> &accepted) {
    std::shared_ptr<Area> area(game.module()->area());
    if (ctxAction.spell && !ctxAction.item) {
        ctxAction.availability = leader->powerMenuStatus(*ctxAction.spell, *selectedObject);
    }
    if (!ctxAction.availability.available()) {
        return {false, ctxAction.availability.reason};
    }

    // Every entry runs inside the menu's wrapper. Out of combat mode it first
    // drops the leader's pending round entries and, for a hostile creature or
    // a bash, posts the combat message. When the entry is done and combat
    // mode is still off, it enters combat mode for a hostile creature and
    // clears the leader's actions, before the entry's order arrives.
    const bool hostileCreature = selectedHostile && selectedObject->type() == ObjectType::Creature;
    const bool bash = ctxAction.type == ActionType::AttackObject && selectedObject->type() != ObjectType::Creature;
    const bool inCombatMode = leader->partyCombatMode();
    if (!inCombatMode) {
        game.combat().removeAllScheduled(*leader);
        if (hostileCreature || bash) {
            game.party().setCombatMessage(*leader, game.party().idleCombatMessage());
        }
    } else if (game.isAutoPaused()) {
        // In combat mode an entry taken during an autopause shows its own
        // pause reason.
        game.setPaused(true, PauseReason::CombatOrder);
    }
    if (accepted) {
        accepted();
    }

    // The first time an action is taken, its tutorial window may take it
    // instead, and gives it back when dismissed.
    auto intercepted = [&](int tutorial, uint32_t param = 0) {
        return game.requestTutorialWindow(tutorial, leader->id(), selectedObject->id(), param);
    };
    std::function<void()> order;
    // A leader that cannot be commanded takes no skill use or power; its
    // attacks are refused as they are taken up.
    auto queue = [&](std::shared_ptr<Action> action) {
        order = [leader, action]() {
            if (!leader->isCommandable()) {
                return;
            }
            action->setUserAction(true);
            leader->addAction(action);
        };
    };
    switch (ctxAction.type) {
    case ActionType::AttackObject:
        if (bash ? game.prepareMenuBash(*leader, selectedObject) : game.prepareMenuAttack(*leader, selectedObject)) {
            order = [&game, leader, selectedObject]() { game.sendAttack(*leader, selectedObject); };
        }
        break;
    case ActionType::UseFeat:
        if (intercepted(kCombatFeatTutorial, static_cast<uint32_t>(ctxAction.feat))) {
            break;
        }
        order = [&game, leader, selectedObject, feat = ctxAction.feat]() { game.sendAttack(*leader, selectedObject, feat); };
        break;
    case ActionType::UseSkill:
        if (ctxAction.skill == SkillType::Security) {
            // Security, alone or with a spike, first clears the leader's
            // orders on a door, and with a spike on any lock; Security alone
            // on a door also delays the next autopause. A leader that can be
            // commanded then clears its actions unless busy and, when the
            // object is still locked, works at the lock, or is told it cannot
            // use the skill.
            const bool door = selectedObject->type() == ObjectType::Door;
            if (ctxAction.item || door) {
                game.combat().clearAllOrders(*leader);
            }
            if (door && !ctxAction.item) {
                game.delayNextEnemyAutoPause();
            }
            order = [&game, leader, selectedObject, item = ctxAction.item]() {
                if (!leader->isCommandable()) {
                    return;
                }
                leader->clearOrdersUnlessBusy();
                const auto lock = selectedObject->findLock();
                if (!lock || !lock->get().isLocked() || !UnlockObjectAction::admits(*leader)) {
                    return;
                }
                auto action = game.newAction<UnlockObjectAction>(selectedObject, item);
                action->setUserAction(true);
                leader->addAction(action);
            };
            break;
        }
        if (ctxAction.item) {
            // Setting a mine on a door or placeable.
            if (!area->playerCanSetMines()) {
                game.showMessagePopup(services.resource.strings.getText(kMineLimitStrRef));
                return {};
            }
            // The first time, the tutorial window takes the action instead.
            if (game.requestTutorialWindow(kSetMineTutorial, leader->id(), ctxAction.item->id())) {
                break;
            }
        }
        // Setting a mine is the use of its kit, which clears the leader's
        // actions unless the leader is busy, then follows them. The other mine
        // work is a use of the skill.
        if (ctxAction.item) {
            queue(game.newAction<UseSkillAction>(ctxAction.skill, selectedObject, ctxAction.subSkill, ctxAction.item));
            order = [leader, queued = std::move(order)]() {
                leader->clearOrdersUnlessBusy();
                queued();
            };
        } else if (auto action = leader->useSkill(ctxAction.skill, selectedObject, ctxAction.subSkill)) {
            queue(std::move(action));
        }
        break;
    case ActionType::DoorSaber: {
        // The door saber first clears the leader's orders, as the player's
        // controls clear them, and is taken up only by a door still locked.
        game.combat().clearAllOrders(*leader);
        auto door = std::static_pointer_cast<Door>(selectedObject);
        queue(game.newAction<DoorSaberAction>(door));
        order = [door, queued = std::move(order)]() {
            if (door->isLocked()) {
                queued();
            }
        };
        break;
    }
    case ActionType::CastSpellAtObject: {
        if (ctxAction.equipmentPower) {
            // A droid's item power enters combat mode as a power does, then
            // goes out as the use of the item at the target.
            leader->setPartyCombatMode(true);
            order = [&game, leader, item = ctxAction.item, selectedObject]() {
                game.useMenuItem(*leader, item, item->firstUseProperty(), selectedObject,
                                  selectedObject->position());
            };
            break;
        }
        if (ctxAction.containerUse) {
            // A plot item used on a locked container goes out as the use of
            // its first power on the container.
            order = [&game, leader, item = ctxAction.item, selectedObject]() {
                game.useMenuItem(*leader, item, item->firstUseProperty(), selectedObject,
                                  menuItemLocation(*leader, *selectedObject));
            };
            break;
        }
        if (ctxAction.item) {
            // Grenades, and TSL's rockets, have their own tutorial window.
            const int itemType = ctxAction.item->itemType();
            const bool grenade = itemType == Item::kGrenadeItemType ||
                                 (game.isTSL() && itemType == Item::kRocketItemType);
            if (grenade && intercepted(kGrenadeTutorial, ctxAction.item->id())) {
                break;
            }
        } else {
            // A power from the target menu puts the leader into combat mode
            // whether or not it is cast. A caster without a Jedi class casts
            // nothing.
            leader->setPartyCombatMode(true);
            if (!game.canMenuCast(*leader, *ctxAction.spell)) {
                order = [&game, leader]() { game.refuseMenuCast(*leader); };
                break;
            }
            if (auto tutorial = game.forcePowerTutorial(*ctxAction.spell);
                tutorial && intercepted(*tutorial, static_cast<uint32_t>(ctxAction.spell->type)))
                break;
        }
        // An item goes out as its use at the target, a power as its cast.
        if (auto item = ctxAction.item) {
            order = [&game, leader, item, spell = ctxAction.spell, selectedObject]() {
                game.useMenuItem(*leader, item, item->spellProperty(spell->type), selectedObject,
                                  menuItemLocation(*leader, *selectedObject));
            };
        } else {
            order = [&game, leader, spell = ctxAction.spell, selectedObject]() {
                game.sendMenuCast(*leader, spell, selectedObject);
            };
        }
        debug(str(boost::format("Spell selected: actor=%u spell=%d target=%u") % leader->id() % static_cast<int>(ctxAction.spell->type) % selectedObject->id()), LogChannel::Combat);
        break;
    }
    default:
        break;
    }
    if (!leader->partyCombatMode()) {
        if (!inCombatMode && hostileCreature) {
            leader->setPartyCombatMode(true);
        }
        game.combat().clearActions(*leader);
    }
    if (order) {
        order();
    }
    return {};
}

} // namespace game

} // namespace reone
