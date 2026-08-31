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

#include <algorithm>

#include "reone/game/object/creature.h"

namespace reone {
namespace game {

void MessageBus::addListener(
    const std::shared_ptr<Creature> &listener,
    std::string pattern,
    int32_t number) {
    if (!listener || !listener->isRuntimeLive()) {
        return;
    }

    pruneDeadListeners();
    ListenerVec &vec = _listeners[pattern];
    for (Listener &entry : vec) {
        if (entry.object.resolve() == listener) {
            entry.number = number;
            return;
        }
    }
    vec.push_back({RuntimeObjectRef<Creature>(listener), number});
}

void MessageBus::addMessage(uint32_t speakerId, std::string pattern, TalkVolume volume) {
    _pendingMessages.push({speakerId, std::move(pattern), volume});
}

void MessageBus::update(OnMessage onMessage) {
    pruneDeadListeners();
    while (!_pendingMessages.empty()) {
        Message msg = std::move(_pendingMessages.front());
        _pendingMessages.pop();

        // Pattern may be a regexp (** for a sequence of any characters, *n for numbers, etc.)
        // KOTOR does not seem to have these yet, so we only match the whole string.

        auto found = _listeners.find(msg.str);
        if (found == _listeners.end()) {
            continue;
        }

        // Scripts may add listeners or destroy this or another listener while
        // handling a message. Iterate a snapshot so map/vector mutation cannot
        // invalidate dispatch, and resolve every entry immediately before use.
        const ListenerVec listeners = found->second;
        for (const Listener &entry : listeners) {
            auto listener = entry.object.resolve();
            if (!listener) {
                continue;
            }
            onMessage(msg.speakerId, listener, entry.number, msg.volume);
        }
        pruneDeadListeners();
    }
}

size_t MessageBus::listenerCount() const {
    size_t result = 0;
    for (const auto &[_, listeners] : _listeners) {
        result += listeners.size();
    }
    return result;
}

void MessageBus::pruneDeadListeners() {
    for (auto found = _listeners.begin(); found != _listeners.end();) {
        auto &listeners = found->second;
        listeners.erase(
            std::remove_if(
                listeners.begin(), listeners.end(),
                [](const Listener &entry) {
                    return entry.object.resolve() == nullptr;
                }),
            listeners.end());
        if (listeners.empty()) {
            found = _listeners.erase(found);
        } else {
            ++found;
        }
    }
}

} // namespace game
} // namespace reone
