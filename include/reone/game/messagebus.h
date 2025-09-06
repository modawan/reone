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

#pragma once

#include "reone/game/types.h"

namespace reone {

namespace game {

/**
 * MessageBus implements a queue of messages for SpeakString and
 * SetListenPattern.
 */
class MessageBus {
public:
    /**
     * Adds a listener object for a pattern. The same object may listen for
     * multiple patterns, as long as it uses a different number for each.
     */
    void addListener(uint32_t listenerId, std::string pattern, int32_t number);

    void addMessage(uint32_t speakerId, std::string msg, TalkVolume volume);

    using OnMessage = std::function<void(uint32_t speakerId, uint32_t listenerId,
                                         int32_t number, TalkVolume volume)>;

    /**
     * Process accumulated messages and call onMessage for each "matched"
     * listener.
     */
    void update(OnMessage onMessage);

private:
    struct Message {
        uint32_t speakerId;
        std::string str;
        TalkVolume volume;
    };

    struct Listener {
        uint32_t id;
        int32_t number;
    };

    using ListenerVec = std::vector<Listener>;

private:
    std::queue<Message> _pendingMessages;
    std::unordered_map<std::string, ListenerVec> _listeners;
};

} // namespace game

} // namespace reone
