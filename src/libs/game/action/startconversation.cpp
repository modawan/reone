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

#include "reone/game/action/startconversation.h"

#include "reone/system/logutil.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/party.h"
#include "reone/game/savedruntime.h"

namespace reone {

namespace game {

static constexpr float kMaxConversationDistance = 4.0f;

void StartConversationAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    // A queued ActionStartConversation that comes up while a dialogue is
    // already running is discarded, not honored and not deferred.
    //
    // Scripts queue a conversation on a creature as a recovery measure, to be
    // taken only once whatever is on screen has finished - K2 103PER reply
    // scripts do this, and reaching one mid-scene must not restart the scene.
    // Honoring the action would replace the running conversation, which also
    // robs it of its EndConversation script and of the globals that script
    // latches, so the replacement can pick the same opening entry and loop
    // forever. Deferring it until the conversation ends is equally wrong: the
    // recovery call would then fire the moment the dialogue it was guarding
    // against completed, and the speaker would immediately re-greet the player.
    //
    // Dropping it matches retail, where the action fails outright while the
    // engine is in dialogue mode.
    if (_game.isConversationActive()) {
        debug("Discarding StartConversation, a conversation is already active",
              LogChannel::Conversation);
        complete();
        return;
    }

    auto actorPtr = _game.getObjectById(actor.id());

    // A creature walks up to its conversation partner before talking. If the
    // target object is invalid - a script can pass an object that was never
    // created (e.g. GetObjectByTag on a tag with no instance) or one that has
    // been destroyed - there is nobody to approach, so the action is dropped
    // rather than starting a partnerless dialog. This mirrors retail/KotOR.js,
    // where the creature path requires a valid target (it navigates to and
    // references the target) and the action otherwise fails. It also prevents a
    // null dereference below and stops an NPC told to converse with a missing
    // placeholder from starting a stray dialog.
    if (auto creatureActor = dyn_cast<Creature>(actorPtr)) {
        if (!_objectToConverse) {
            complete();
            return;
        }
        bool reached =
            _ignoreStartRange ||
            creatureActor->navigateTo(_objectToConverse->position(), true, kMaxConversationDistance, dt);

        if (!reached) {
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
        complete();
        return;
    }

    _game.module()->area()->startDialog(dialogOwner, _dialogResRef);
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

    SavedActionRecord result =
        originalSavedAction().value_or(SavedActionRecord {});
    result.actionId = 24;
    result.declaredParameterCount = 3;
    result.parameters = {
        SavedActionParameter {
            static_cast<uint32_t>(SavedActionParameterType::Object),
            SavedObjectReference::fromRuntimeId(_objectToConverse->id())},
        SavedActionParameter {
            static_cast<uint32_t>(SavedActionParameterType::String),
            _dialogResRef},
        SavedActionParameter {
            static_cast<uint32_t>(SavedActionParameterType::Integer),
            static_cast<int32_t>(_privateConversation ? 1 : 0)},
    };
    return result;
}

} // namespace game

} // namespace reone
