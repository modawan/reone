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

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/party.h"

namespace reone {

namespace game {

static constexpr float kMaxConversationDistance = 4.0f;

void StartConversationAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto actorPtr = _game.getObjectById(actor.id());

    // Creatures should move to the target before starting the dialog.
    // Triggers and other objects can start the dialog immediately
    if (auto creatureActor = dyn_cast<Creature>(actorPtr)) {
        bool reached =
            _ignoreStartRange ||
            creatureActor->navigateTo(_objectToConverse->position(), true, kMaxConversationDistance, dt);

        if (!reached) {
            return;
        }
    }

    // When scripts do not specify DialogResRef, it takes a default
    // value of caller->conversation() before StartConversationAction
    // object is constructed.
    //
    // When the player initiates a conversation, DialogResRef should
    // be empty.
    //
    // Therefore, if DialogResRef is empty, we must always select
    // _objectToConverse and use its conversation() as the dialog.
    //
    // Otherwise, if DialogResRef is set, select Actor as the dialog
    // owner.

    std::shared_ptr<Object> dialogOwner = _dialogResRef.empty() ? _objectToConverse : actorPtr;
    _game.module()->area()->startDialog(dialogOwner, _dialogResRef);
    complete();
}

} // namespace game

} // namespace reone
