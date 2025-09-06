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

#include <gtest/gtest.h>

#include "reone/game/messagebus.h"

using namespace reone;
using namespace reone::game;

struct Msg {
    uint32_t speakerId;
    uint32_t listenerId;
    int32_t number;
    TalkVolume volume;

    bool operator==(const struct Msg &m) const {
        return speakerId == m.speakerId && listenerId == m.listenerId && number == m.number && volume == m.volume;
    }
};

TEST(MessageBus, test_basic) {
    MessageBus bus;
    bus.addListener(10, "foo", 1);
    bus.addListener(11, "foo", 1);
    bus.addListener(11, "bar", 2);

    bus.addMessage(20, "foo", TalkVolume::Shout);
    bus.addMessage(20, "bar", TalkVolume::Shout);

    std::vector<Msg> expected = {
        {20, 10, 1, TalkVolume::Shout},
        {20, 11, 1, TalkVolume::Shout},
        {20, 11, 2, TalkVolume::Shout}};

    std::vector<Msg> got;

    bus.update([&](uint32_t speakerId, uint32_t listenerId, int32_t number, TalkVolume volume) {
        got.push_back({speakerId, listenerId, number, volume});
    });

    ASSERT_EQ(expected.size(), got.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(expected[i], got[i]);
    }
}

TEST(MessageBus, test_update_number) {
    MessageBus bus;
    bus.addListener(10, "foo", 1);
    bus.addListener(10, "foo", 2);

    bus.addMessage(20, "foo", TalkVolume::Shout);

    std::vector<Msg> expected = {
        {20, 10, 2, TalkVolume::Shout},
    };

    std::vector<Msg> got;

    bus.update([&](uint32_t speakerId, uint32_t listenerId, int32_t number, TalkVolume volume) {
        got.push_back({speakerId, listenerId, number, volume});
    });

    ASSERT_EQ(expected.size(), got.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(expected[i], got[i]);
    }
}
