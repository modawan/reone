/*
 * Copyright (c) 2025 The reone project contributors
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

#include "reone/game/messagebus.h"

#include "reone/game/object/creature.h"

namespace reone {
namespace game {

void MessageBus::addListener(uint32_t listenerId, std::string pattern, int32_t number) {
    ListenerVec &vec = _listeners[pattern];
    for (Listener &listener : vec) {
        if (listenerId == listener.id) {
            listener.number = number;
            return;
        }
    }
    vec.push_back({listenerId, number});
}

void MessageBus::addMessage(uint32_t speakerId, std::string pattern, TalkVolume volume) {
    _pendingMessages.push({speakerId, std::move(pattern), volume});
}

void MessageBus::update(OnMessage onMessage) {
    while (!_pendingMessages.empty()) {
        Message &msg = _pendingMessages.front();

        // Pattern may be a regexp (** for a sequence of any characters, *n for numbers, etc.)
        // KOTOR does not seem to have these yet, so we only match the whole string.

        ListenerVec &vec = _listeners[msg.str];

        for (Listener &listener : vec) {
            onMessage(msg.speakerId, listener.id, listener.number, msg.volume);
        }
        _pendingMessages.pop();
    }
}

} // namespace game
} // namespace reone
