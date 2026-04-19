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
#include "reone/system/logutil.h"

namespace reone {

namespace game {

static constexpr float kMaxConversationDistance = 4.0f;

static bool containsTraskTraceToken(const std::string &value) {
    return value.find("trask") != std::string::npos;
}

static bool isTraskTraceObject(const std::shared_ptr<Object> &object) {
    return object &&
           (containsTraskTraceToken(object->tag()) ||
            containsTraskTraceToken(object->blueprintResRef()) ||
            containsTraskTraceToken(object->conversation()));
}

static std::string describeTraskTraceObject(const std::shared_ptr<Object> &object) {
    if (!object) {
        return "null";
    }
    return "#" + std::to_string(object->id()) +
           " tag='" + object->tag() +
           "' blueprint='" + object->blueprintResRef() +
           "' conv='" + object->conversation() + "'";
}

void StartConversationAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto actorPtr = _game.getObjectById(actor.id());
    auto creatureActor = std::static_pointer_cast<Creature>(actorPtr);

    bool reached =
        actorPtr->type() != ObjectType::Creature ||
        _ignoreStartRange ||
        creatureActor->navigateTo(_objectToConverse->position(), true, kMaxConversationDistance, dt);

    bool traskTrace = isTraskTraceObject(actorPtr) || isTraskTraceObject(_objectToConverse);
    if (traskTrace) {
        info("reone trask autodialog trace: StartConversationAction execute reached=" + std::to_string(static_cast<int>(reached)) +
             " actor=" + describeTraskTraceObject(actorPtr) +
             " target=" + describeTraskTraceObject(_objectToConverse) +
             " dialogResRef='" + _dialogResRef + "'" +
             " ignoreStartRange=" + std::to_string(static_cast<int>(_ignoreStartRange)));
    }

    if (reached) {
        bool isActorLeader = _game.party().getLeader() == actorPtr;
        _game.module()->area()->startDialog(isActorLeader ? _objectToConverse : actorPtr, _dialogResRef);
        complete();
    }
}

} // namespace game

} // namespace reone
