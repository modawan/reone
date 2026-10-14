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

#include "reone/game/action/barkstring.h"
#include "reone/game/action/pauseconversation.h"
#include "reone/game/action/resumeconversation.h"
#include "reone/game/action/startconversation.h"
#include "reone/game/combat.h"
#include "reone/game/di/services.h"
#include "reone/game/effect/resurrection.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/party.h"
#include "reone/game/savedruntime.h"
#include "reone/system/logutil.h"

namespace reone {

namespace game {

// A partner is talked to from within its use range and a metre more; once the
// speaker has set off, from anywhere within ten metres of it when the walk
// ends.
static constexpr float kConversationUseRangeExtra = 1.0f;
static constexpr float kConversationStartDistance = 10.0f;
static constexpr float kHandOffFadeLength = 0.75f;
// Followers at least this far (squared metres) from the handing-off companion
// are brought to their formation spots, placed within this radius.
static constexpr float kHandOffGatherDistance2 = 900.0f;
static constexpr float kHandOffPlacementRadius = 10.0f;

static bool isPartyInteract(const Object &object) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        return creature->isPartyInteract();
    }
    if (auto *placeable = dyn_cast<Placeable>(&object)) {
        return placeable->isPartyInteract();
    }
    return false;
}

// The speaker first looks for the partner within use range, a metre further.
// Out of it, the speaker runs to the partner's use point until that close, or
// to a door's or placeable's use range, and then starts the conversation if
// the partner is within ten metres, or sets off again.
bool StartConversationAction::approach(Creature &speaker, float dt) {
    if (!_approaching) {
        if (speaker.isInUseRange(*_objectToConverse, kConversationUseRangeExtra)) {
            return true;
        }
        _approaching = true;
    }
    const Creature::UseRange use = speaker.useRange(*_objectToConverse);
    const ObjectType partnerType = _objectToConverse->type();
    const float range = partnerType == ObjectType::Door || partnerType == ObjectType::Placeable
                            ? use.range
                            : use.range + kConversationUseRangeExtra;
    if (!speaker.navigateTo(use.point, true, range, dt)) {
        return false;
    }
    const glm::vec3 offset {_objectToConverse->position() - speaker.position()};
    return glm::dot(offset, offset) <= kConversationStartDistance * kConversationStartDistance;
}

// A companion that reaches someone with a conversation of their own hands
// the conversation to the player character, unless the target is the
// player character or asks for party interaction.
bool StartConversationAction::handsOffToPlayer(const std::shared_ptr<Object> &actor) const {
    auto &party = _game.party();
    auto player = party.player();
    auto *creature = dyn_cast<Creature>(actor.get());
    return !_transferredFrom && creature && player && actor != player && party.isMember(*creature) &&
           _objectToConverse && _objectToConverse != player && !isPartyInteract(*_objectToConverse) &&
           !_objectToConverse->conversation().empty() && party.isMember(*player);
}

// The party stops, followers far from the companion are brought to their
// formation spots, the screen fades out with the player's input shut off, and
// the player character takes the conversation over, revived if it is down.
void StartConversationAction::handOffToPlayer(std::shared_ptr<Action> self, Object &actor) {
    auto &party = _game.party();
    auto player = party.player();
    auto handOff = _game.newAction<StartConversationAction>(_objectToConverse, _dialogResRef, _privateConversation,
                                                            _conversationType, _ignoreStartRange, _namesToIgnore, _useLeader, _barkX, _barkY, _dontClearAllActions);
    handOff->_transferredFrom = std::dynamic_pointer_cast<Creature>(_game.getObjectById(actor.id()));
    handOff->_admitted = true;
    handOff->_fadeDialog = std::move(_fadeDialog);
    // Each member's actions are cleared, forced, and so are its orders as the
    // player's controls clear them.
    for (int i = 0; i < party.getSize(); ++i) {
        if (auto member = party.getMember(i)) {
            member->clearAllActions(true);
            _game.combat().clearAllOrders(*member);
        }
    }
    const glm::vec3 leaderPosition {party.getLeader()->position()};
    auto &area = *_game.module()->area();
    for (int slot = 1; slot < kPartyFollowSlots && slot < party.getSize(); ++slot) {
        auto member = party.getMember(slot);
        if (!member) {
            continue;
        }
        const glm::vec3 offset {member->position() - leaderPosition};
        if (glm::dot(offset, offset) < kHandOffGatherDistance2) {
            continue;
        }
        const glm::vec3 spot {party.formationSpot(slot)};
        member->setPosition(area.computeSafeLocation(spot, kHandOffPlacementRadius, *member, false).value_or(spot));
        area.determineObjectRoom(*member);
    }
    _game.globalFade().request(GlobalFade::Direction::Out, 0.0f, kHandOffFadeLength);
    _game.setPlayerInputBlocked(true);
    _game.globalFade().holdForDialog();
    if (player->currentHitPoints() <= 0) {
        player->applyEffect(_game.newEffect<ResurrectionEffect>(0), DurationType::Instant);
    }
    player->addActionOnTop(std::move(handOff));
    complete();
}

// Once the fade is done the player character takes the companion's place
// facing the target, takes control, gets the player's input back, and hands
// control back after the conversation.
bool StartConversationAction::takeOverFromCompanion(Object &actor) {
    if (_game.globalFade().fading()) {
        return false;
    }
    auto companion = std::move(_transferredFrom);
    _transferredFrom.reset();
    if (!companion || !companion->isRuntimeLive()) {
        return true;
    }
    const glm::vec3 own = actor.position();
    actor.setPosition(companion->position());
    if (_objectToConverse) {
        actor.face(*_objectToConverse);
    } else {
        actor.setFacing(companion->getFacing());
    }
    companion->setPosition(own);
    auto &party = _game.party();
    for (int i = 1; i < party.getSize(); ++i) {
        if (party.getMember(i).get() == &actor) {
            party.setPartyLeaderByIndex(i);
            break;
        }
    }
    _game.setPostDialogCharacterSwitch(companion);
    _game.setPlayerInputBlocked(false);
    return true;
}

void StartConversationAction::admit() {
    if (_admitted) {
        return;
    }
    _admitted = true;
    if (!_game.isConversationActive()) {
        _fadeDialog = _game.globalFade().admitDialog();
    }
    if (!_fadeDialog) {
        complete();
    }
}

bool StartConversationAction::cancel(std::shared_ptr<Action> self, Object &actor) {
    _game.globalFade().finishDialog(_fadeDialog);
    _fadeDialog.reset();
    return true;
}

void StartConversationAction::onQueued(Object &actor) {
    admit();
}

void StartConversationAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    // Direct executors and restored queues use the same admission path.
    admit();
    if (!_game.globalFade().isCurrentDialog(_fadeDialog)) {
        complete();
        return;
    }
    // Discard a queued conversation if a dialogue is already running. Starting
    // it now would replace the current scene; deferring it would cause an
    // immediate extra greeting when the current dialogue ends.
    if (_game.isConversationActive()) {
        debug("Discarding StartConversation, a conversation is already active",
              LogChannel::Conversation);
        cancel(self, actor);
        complete();
        return;
    }

    auto actorPtr = _game.getObjectById(actor.id());

    // A creature must have a valid conversation partner to approach. Drop the
    // action if its target is missing or destroyed instead of starting a
    // partnerless dialogue.
    if (_transferredFrom) {
        if (!takeOverFromCompanion(actor)) {
            return;
        }
    } else if (auto creatureActor = dyn_cast<Creature>(actorPtr)) {
        if (!_objectToConverse) {
            cancel(self, actor);
            complete();
            return;
        }
        bool reached = _ignoreStartRange || approach(*creatureActor, dt);

        if (!reached) {
            return;
        }
        if (handsOffToPlayer(actorPtr)) {
            // The hand-off is refused, silently, while the player character is in direct combat.
            auto player = _game.party().player();
            if (player->isInCombat() && player->combatActivationType() == CombatActivation::Direct) {
                cancel(self, actor);
                complete();
                return;
            }
            handOffToPlayer(self, actor);
            return;
        }
    }

    // Dialog owner selection - i.e. which participant's Conversation/DLG plays:
    //
    //  - Explicit DialogResRef: the named dialog is caller-owned (PR #150).
    //
    //  - Blank DialogResRef: a party member is always the listener, never the
    //    dialog owner, so the OTHER participant owns the conversation:
    //      * the target owns it when the target is not a party member and has
    //        its own Conversation. This covers the player clicking an NPC or a
    //        placeable, and a script pointing the caller at an invisible dialog
    //        anchor whose Conversation is the scene's dialogue (an NPC told to
    //        converse with such a placeable).
    //      * otherwise the caller owns it when the caller has a Conversation.
    //        This is the common NPC-starts-dialogue-with-the-PC shape, where the
    //        target is the party member.
    //      * otherwise fall back to the target's Conversation if it has one.
    //
    // Keyed only on party membership and Conversation presence, so it is generic
    // (it does not depend on whether the PC/leader happens to have a
    // Conversation field of its own).
    std::shared_ptr<Object> dialogOwner;
    if (!_dialogResRef.empty()) {
        dialogOwner = actorPtr;
    } else {
        bool targetHasConversation =
            _objectToConverse && !_objectToConverse->conversation().empty();
        bool targetIsPartyMember =
            _objectToConverse && _game.party().isMember(*_objectToConverse);
        bool callerHasConversation = actorPtr && !actorPtr->conversation().empty();

        if (targetHasConversation && !targetIsPartyMember) {
            dialogOwner = _objectToConverse;
        } else if (callerHasConversation) {
            dialogOwner = actorPtr;
        } else if (targetHasConversation) {
            dialogOwner = _objectToConverse;
        }
    }

    // If no valid owner can be resolved there is no dialogue to start (e.g. an
    // invalid target combined with an empty resref). Complete the action so it
    // does not block the queue, but do not dereference a null owner -
    // Area::startDialog reads owner->conversation() for an empty resref.
    if (!dialogOwner) {
        cancel(self, actor);
        complete();
        return;
    }

    const auto listener = dialogOwner == actorPtr ? _objectToConverse : actorPtr;
    _game.startDialog(dialogOwner, _dialogResRef.empty() ? dialogOwner->conversation() : _dialogResRef,
                      _fadeDialog, listener);
    _fadeDialog.reset(); // successful startup is now owned by Conversation
    complete();
}

std::optional<SavedActionRecord> StartConversationAction::saveFacingState() const {
    bool ignoredNamesAreEmpty = std::all_of(
        _namesToIgnore.begin(), _namesToIgnore.end(),
        [](const std::string &name) { return name.empty(); });
    if (!_objectToConverse || _dialogResRef.size() > 16 ||
        _conversationType != resource::ConversationType::Cinematic ||
        _ignoreStartRange || !ignoredNamesAreEmpty || _useLeader ||
        _barkX != -1 || _barkY != -1 || _dontClearAllActions) {
        return std::nullopt;
    }

    SavedActionRecord result;
    result.actionId = static_cast<uint32_t>(SavedActionType::StartConversation);
    result.declaredParameterCount = 3;
    result.parameters = {
        SavedActionParameter {
            kSavedObjectParameter,
            SavedObjectReference::fromRuntimeId(_objectToConverse->id())},
        SavedActionParameter {
            static_cast<uint32_t>(SavedActionParameterType::String),
            _dialogResRef},
        SavedActionParameter {
            kSavedIntegerParameter,
            static_cast<int32_t>(_privateConversation ? 1 : 0)},
    };
    return result;
}

void PauseConversationAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    _game.pauseConversationBy(actor);
    complete();
}

void ResumeConversationAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    _game.resumeConversationBy(actor);
    complete();
}

void BarkStringAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    // TODO: implement

    complete();
}

} // namespace game

} // namespace reone
