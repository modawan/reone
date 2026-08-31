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

#include "../fixtures/engine.h"
#include "../fixtures/game.h"
#include "../fixtures/scene.h"

#include "reone/game/game.h"
#include "reone/game/messagebus.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

class MessageBusHarness {
public:
    MessageBusHarness() {
        engine.init();
        ON_CALL(engine.sceneModule().graphs(), get(_))
            .WillByDefault(ReturnRef(sceneGraph));
        game = std::make_unique<Game>(
            GameID::KotOR,
            "",
            engine.options(),
            engine.services(),
            console);
    }

    std::shared_ptr<Creature> creature() {
        return game->newCreature();
    }

    TestEngine engine;
    NiceMock<scene::MockSceneGraph> sceneGraph;
    StubConsole console;
    std::unique_ptr<Game> game;
};

struct Msg {
    uint32_t speakerId;
    uint32_t listenerId;
    int32_t number;
    TalkVolume volume;

    bool operator==(const struct Msg &m) const {
        return speakerId == m.speakerId && listenerId == m.listenerId && number == m.number && volume == m.volume;
    }
};

} // namespace

TEST(MessageBus, test_basic) {
    MessageBusHarness harness;
    MessageBus bus;
    auto first = harness.creature();
    auto second = harness.creature();
    bus.addListener(first, "foo", 1);
    bus.addListener(second, "foo", 1);
    bus.addListener(second, "bar", 2);

    bus.addMessage(20, "foo", TalkVolume::Shout);
    bus.addMessage(20, "bar", TalkVolume::Shout);

    std::vector<Msg> expected = {
        {20, first->id(), 1, TalkVolume::Shout},
        {20, second->id(), 1, TalkVolume::Shout},
        {20, second->id(), 2, TalkVolume::Shout}};

    std::vector<Msg> got;

    bus.update([&](uint32_t speakerId, const auto &listener,
                   int32_t number, TalkVolume volume) {
        got.push_back({speakerId, listener->id(), number, volume});
    });

    ASSERT_EQ(expected.size(), got.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(expected[i], got[i]);
    }
}

TEST(MessageBus, test_update_number) {
    MessageBusHarness harness;
    MessageBus bus;
    auto listener = harness.creature();
    bus.addListener(listener, "foo", 1);
    bus.addListener(listener, "foo", 2);

    bus.addMessage(20, "foo", TalkVolume::Shout);

    std::vector<Msg> expected = {
        {20, listener->id(), 2, TalkVolume::Shout},
    };

    std::vector<Msg> got;

    bus.update([&](uint32_t speakerId, const auto &resolved,
                   int32_t number, TalkVolume volume) {
        got.push_back({speakerId, resolved->id(), number, volume});
    });

    ASSERT_EQ(expected.size(), got.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(expected[i], got[i]);
    }
}

TEST(MessageBus, dead_listener_is_pruned_without_dispatch) {
    MessageBusHarness harness;
    MessageBus bus;
    auto listener = harness.creature();
    bus.addListener(listener, "foo", 15);
    ASSERT_EQ(1u, bus.listenerCount());
    harness.game->destroyRuntimeObjectGraph(listener);
    bus.addMessage(10, "foo", TalkVolume::Talk);
    int dispatches = 0;

    bus.update([&](uint32_t, const auto &, int32_t, TalkVolume) {
        ++dispatches;
    });

    EXPECT_EQ(0, dispatches);
    EXPECT_EQ(0u, bus.listenerCount());
}

TEST(MessageBus, maintenance_prunes_dead_listener_without_a_message) {
    MessageBusHarness harness;
    MessageBus bus;
    auto listener = harness.creature();
    bus.addListener(listener, "unused", 1);
    harness.game->destroyRuntimeObjectGraph(listener);

    bus.update([](uint32_t, const auto &, int32_t, TalkVolume) {});

    EXPECT_EQ(0u, bus.listenerCount());
}

TEST(MessageBus, one_dead_listener_does_not_disturb_a_live_listener) {
    MessageBusHarness harness;
    MessageBus bus;
    auto dead = harness.creature();
    auto live = harness.creature();
    bus.addListener(dead, "foo", 1);
    bus.addListener(live, "foo", 2);
    harness.game->destroyRuntimeObjectGraph(dead);
    bus.addMessage(20, "foo", TalkVolume::Shout);
    std::vector<uint32_t> listeners;

    bus.update([&](uint32_t, const auto &listener, int32_t, TalkVolume) {
        listeners.push_back(listener->id());
    });

    EXPECT_EQ(std::vector<uint32_t>({live->id()}), listeners);
    EXPECT_EQ(1u, bus.listenerCount());
}

TEST(MessageBus, destruction_during_dispatch_is_iteration_safe) {
    MessageBusHarness harness;
    MessageBus bus;
    auto first = harness.creature();
    auto second = harness.creature();
    bus.addListener(first, "foo", 1);
    bus.addListener(second, "foo", 2);
    bus.addMessage(20, "foo", TalkVolume::Shout);
    std::vector<uint32_t> listeners;

    bus.update([&](uint32_t, const auto &listener, int32_t, TalkVolume) {
        listeners.push_back(listener->id());
        harness.game->destroyRuntimeObjectGraph(first);
        harness.game->destroyRuntimeObjectGraph(second);
    });

    EXPECT_EQ(std::vector<uint32_t>({first->id()}), listeners);
    EXPECT_EQ(0u, bus.listenerCount());
}

TEST(MessageBus, listener_added_during_dispatch_starts_with_the_next_message) {
    MessageBusHarness harness;
    MessageBus bus;
    auto first = harness.creature();
    auto second = harness.creature();
    bus.addListener(first, "foo", 1);
    bus.addMessage(20, "foo", TalkVolume::Shout);
    std::vector<uint32_t> listeners;

    bus.update([&](uint32_t, const auto &listener, int32_t, TalkVolume) {
        listeners.push_back(listener->id());
        bus.addListener(second, "foo", 2);
    });
    EXPECT_EQ(std::vector<uint32_t>({first->id()}), listeners);

    listeners.clear();
    bus.addMessage(20, "foo", TalkVolume::Shout);
    bus.update([&](uint32_t, const auto &listener, int32_t, TalkVolume) {
        listeners.push_back(listener->id());
    });

    EXPECT_EQ(
        std::vector<uint32_t>({first->id(), second->id()}),
        listeners);
}

TEST(MessageBus, newer_runtime_object_does_not_inherit_a_dead_registration) {
    MessageBusHarness harness;
    MessageBus bus;
    auto stale = harness.creature();
    const uint32_t staleId = stale->id();
    bus.addListener(stale, "foo", 1);
    harness.game->destroyRuntimeObjectGraph(stale);
    auto replacement = harness.creature();
    ASSERT_NE(staleId, replacement->id());
    bus.addListener(replacement, "foo", 2);
    bus.addMessage(20, "foo", TalkVolume::Shout);
    std::vector<uint32_t> listeners;

    bus.update([&](uint32_t, const auto &listener, int32_t, TalkVolume) {
        listeners.push_back(listener->id());
    });

    EXPECT_EQ(std::vector<uint32_t>({replacement->id()}), listeners);
    EXPECT_EQ(1u, bus.listenerCount());
}

TEST(MessageBus, full_session_retirement_invalidates_listener_registration) {
    MessageBusHarness harness;
    MessageBus bus;
    auto listener = harness.creature();
    bus.addListener(listener, "foo", 1);

    harness.game->retireRuntimeSession();
    bus.addMessage(20, "foo", TalkVolume::Talk);
    int dispatches = 0;
    bus.update([&](uint32_t, const auto &, int32_t, TalkVolume) {
        ++dispatches;
    });

    EXPECT_FALSE(harness.game->isRuntimeObjectLive(*listener));
    EXPECT_EQ(0, dispatches);
    EXPECT_EQ(0u, bus.listenerCount());
}

TEST(MessageBus, area_dispatch_prunes_the_destroyed_listener_in_endar_shape) {
    MessageBusHarness harness;
    auto area = harness.game->newArea();
    auto listener = harness.creature();
    auto speaker = harness.creature();
    area->messageBus().addListener(listener, "foo", 15);
    area->messageBus().addMessage(
        speaker->id(), "foo", TalkVolume::Talk);
    harness.game->destroyRuntimeObjectGraph(listener);

    EXPECT_NO_THROW(area->updateMessageBus());
    EXPECT_EQ(0u, area->messageBus().listenerCount());
    EXPECT_TRUE(harness.game->isRuntimeObjectLive(*speaker));
}
