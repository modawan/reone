/* Copyright (c) 2026 The reone project contributors */

#include <gtest/gtest.h>

#include "reone/audio/arbiter.h"

using namespace reone::audio;

namespace {

// Groups by row: 0 is the most important. Each test sets the rows it uses.
PriorityGroup group(int priority, int maxPlaying = 255, bool interrupt = true) {
    PriorityGroup row;
    row.priority = priority;
    row.maxPlaying = maxPlaying;
    row.interrupt = interrupt;
    return row;
}

ArbitedSource oneShot(int group, bool positional = true) {
    ArbitedSource source;
    source.group = group;
    source.positional = positional;
    return source;
}

ArbitedSource loop(int group, bool positional = true) {
    ArbitedSource source = oneShot(group, positional);
    source.looping = true;
    return source;
}

ArbitedSource stream(int group, bool looping) {
    ArbitedSource source;
    source.group = group;
    source.looping = looping;
    source.streamed = true;
    return source;
}

bool hasChange(const std::vector<SlotChange> &changes, uint32_t id, SlotChange::Kind kind) {
    return std::any_of(changes.begin(), changes.end(), [&](const SlotChange &change) {
        return change.id == id && change.kind == kind;
    });
}

} // namespace

TEST(SourceArbiter, full_group_without_interrupt_drops_one_shot_and_holds_loop) {
    SourceArbiter arbiter;
    arbiter.setPriorityGroups({group(0, 1, false)});

    auto first = arbiter.admit(oneShot(0), true);
    ASSERT_TRUE(first.id);
    EXPECT_EQ(SourceSlot::Playing, first.slot);

    EXPECT_FALSE(arbiter.admit(oneShot(0), true).id);

    auto waiting = arbiter.admit(loop(0), true);
    ASSERT_TRUE(waiting.id);
    EXPECT_EQ(SourceSlot::Waiting, waiting.slot);
    EXPECT_TRUE(arbiter.update().empty());

    arbiter.remove(*first.id);
    auto changes = arbiter.update();
    EXPECT_TRUE(hasChange(changes, *waiting.id, SlotChange::Kind::Start));
    EXPECT_EQ(SourceSlot::Playing, arbiter.slot(*waiting.id));
}

TEST(SourceArbiter, interrupt_stops_newest_owned_source_and_parks_newest_loop) {
    SourceArbiter arbiter;
    arbiter.setPriorityGroups({group(0, 2)});

    auto older = arbiter.admit(oneShot(0), true);
    auto newer = arbiter.admit(oneShot(0), true);
    arbiter.setOwned(*older.id, true);
    arbiter.setOwned(*newer.id, true);

    auto third = arbiter.admit(oneShot(0), true);
    ASSERT_TRUE(third.id);
    EXPECT_TRUE(hasChange(third.changes, *newer.id, SlotChange::Kind::Stop));
    EXPECT_FALSE(arbiter.slot(*newer.id));
    EXPECT_EQ(SourceSlot::Playing, arbiter.slot(*older.id));
    arbiter.setOwned(*third.id, true);

    auto loopSource = arbiter.admit(loop(0), true);
    EXPECT_TRUE(hasChange(loopSource.changes, *third.id, SlotChange::Kind::Stop));
    auto last = arbiter.admit(oneShot(0), true);
    EXPECT_TRUE(hasChange(last.changes, *loopSource.id, SlotChange::Kind::Wait));
    EXPECT_EQ(SourceSlot::Waiting, arbiter.slot(*loopSource.id));
}

TEST(SourceArbiter, interrupt_lets_fire_and_forget_one_shot_play_out) {
    SourceArbiter arbiter;
    arbiter.setPriorityGroups({group(0, 1)});

    auto fired = arbiter.admit(oneShot(0), true);
    ASSERT_TRUE(fired.id);

    // The newest is fire-and-forget: nothing is stopped, and the new source
    // plays over the group's maximum.
    auto next = arbiter.admit(oneShot(0), true);
    ASSERT_TRUE(next.id);
    EXPECT_TRUE(next.changes.empty());
    EXPECT_EQ(SourceSlot::Playing, arbiter.slot(*fired.id));
    EXPECT_EQ(SourceSlot::Playing, arbiter.slot(*next.id));
}

TEST(SourceArbiter, interrupt_weighs_only_sources_of_the_same_kind) {
    SourceArbiter arbiter;
    arbiter.setPriorityGroups({group(0, 1)});

    auto unstreamed = arbiter.admit(loop(0), true);
    auto streamed = arbiter.admit(stream(0, true), true);
    ASSERT_TRUE(streamed.id);
    EXPECT_TRUE(streamed.changes.empty());
    EXPECT_EQ(SourceSlot::Playing, arbiter.slot(*unstreamed.id));
}

TEST(SourceArbiter, waiting_loop_retries_never_interrupt) {
    SourceArbiter arbiter;
    arbiter.setPriorityGroups({group(0, 1)});

    auto first = arbiter.admit(loop(0), true);
    auto second = arbiter.admit(loop(0), true);
    EXPECT_TRUE(hasChange(second.changes, *first.id, SlotChange::Kind::Wait));

    auto changes = arbiter.update();
    EXPECT_TRUE(changes.empty());
    EXPECT_EQ(SourceSlot::Waiting, arbiter.slot(*first.id));
    EXPECT_EQ(SourceSlot::Playing, arbiter.slot(*second.id));
}

TEST(SourceArbiter, loop_that_began_waiting_last_retries_first) {
    SourceArbiter arbiter;
    arbiter.setPriorityGroups({group(0, 1, false)});

    auto playing = arbiter.admit(loop(0), true);
    auto earlier = arbiter.admit(loop(0), true);
    auto later = arbiter.admit(loop(0), true);
    ASSERT_EQ(SourceSlot::Waiting, earlier.slot);
    ASSERT_EQ(SourceSlot::Waiting, later.slot);

    arbiter.remove(*playing.id);
    arbiter.update();
    EXPECT_EQ(SourceSlot::Playing, arbiter.slot(*later.id));
    EXPECT_EQ(SourceSlot::Waiting, arbiter.slot(*earlier.id));
}

TEST(SourceArbiter, restarted_loop_counts_as_newest) {
    SourceArbiter arbiter;
    arbiter.setPriorityGroups({group(0, 2)});

    auto restarted = arbiter.admit(loop(0), true);
    auto other = arbiter.admit(loop(0), true);
    arbiter.leaveRange(*restarted.id);
    arbiter.enterRange(*restarted.id);
    arbiter.update();
    ASSERT_EQ(SourceSlot::Playing, arbiter.slot(*restarted.id));

    auto next = arbiter.admit(loop(0), true);
    EXPECT_TRUE(hasChange(next.changes, *restarted.id, SlotChange::Kind::Wait));
    EXPECT_EQ(SourceSlot::Playing, arbiter.slot(*other.id));
}

TEST(SourceArbiter, loop_out_of_range_takes_no_room) {
    SourceArbiter arbiter;
    arbiter.setPriorityGroups({group(0, 1, false)});

    auto away = arbiter.admit(loop(0), false);
    ASSERT_TRUE(away.id);
    EXPECT_EQ(SourceSlot::Away, away.slot);
    EXPECT_FALSE(arbiter.admit(oneShot(0), false).id);

    auto playing = arbiter.admit(loop(0), true);
    EXPECT_EQ(SourceSlot::Playing, playing.slot);

    arbiter.enterRange(*away.id);
    EXPECT_EQ(SourceSlot::Waiting, arbiter.slot(*away.id));
    arbiter.leaveRange(*playing.id);
    arbiter.update();
    EXPECT_EQ(SourceSlot::Playing, arbiter.slot(*away.id));
}

TEST(SourceArbiter, full_pool_gives_voice_of_lowest_priority_group) {
    SourceArbiter arbiter;
    arbiter.setPriorityGroups({group(5), group(10), group(20)});

    std::vector<uint32_t> ids;
    for (int i = 0; i < SourceArbiter::kMinVoices3D - 1; ++i) {
        ids.push_back(*arbiter.admit(oneShot(1), true).id);
    }
    auto lowest = arbiter.admit(loop(2), true);
    ASSERT_EQ(SourceSlot::Playing, lowest.slot);

    // A source of the same priority as the weakest playing takes nothing.
    auto sameAsMost = arbiter.admit(oneShot(1), true);
    EXPECT_TRUE(hasChange(sameAsMost.changes, *lowest.id, SlotChange::Kind::Wait));
    auto equal = arbiter.admit(oneShot(1), true);
    EXPECT_FALSE(equal.id);
    auto equalLoop = arbiter.admit(loop(1), true);
    EXPECT_EQ(SourceSlot::Waiting, equalLoop.slot);

    // A more important source takes the voice that has played longest.
    auto important = arbiter.admit(oneShot(0), true);
    ASSERT_TRUE(important.id);
    EXPECT_TRUE(hasChange(important.changes, ids.front(), SlotChange::Kind::Stop));
}

TEST(SourceArbiter, pools_are_apart) {
    SourceArbiter arbiter;
    arbiter.setPriorityGroups({group(0)});

    for (int i = 0; i < SourceArbiter::kMinVoices3D; ++i) {
        ASSERT_TRUE(arbiter.admit(oneShot(0), true).id);
    }
    EXPECT_FALSE(arbiter.admit(oneShot(0), true).id);
    for (int i = 0; i < SourceArbiter::kMinVoices2D / 2; ++i) {
        ASSERT_TRUE(arbiter.admit(oneShot(0, false), true).id);
    }
    EXPECT_FALSE(arbiter.admit(oneShot(0, false), true).id);
    for (int i = 0; i < SourceArbiter::kMinVoices2D - SourceArbiter::kMinVoices2D / 2; ++i) {
        ASSERT_TRUE(arbiter.admit(stream(0, false), true).id);
    }
    EXPECT_FALSE(arbiter.admit(stream(0, false), true).id);
}

TEST(SourceArbiter, only_looping_streams_give_up_streamed_voices) {
    SourceArbiter arbiter;
    arbiter.setPriorityGroups({group(0), group(10)});

    const int streams = SourceArbiter::kMinVoices2D - SourceArbiter::kMinVoices2D / 2;
    for (int i = 0; i < streams; ++i) {
        ASSERT_TRUE(arbiter.admit(stream(1, false), true).id);
    }
    EXPECT_FALSE(arbiter.admit(stream(0, false), true).id);
    auto waiting = arbiter.admit(stream(0, true), true);
    EXPECT_EQ(SourceSlot::Waiting, waiting.slot);
}

TEST(SourceArbiter, voice_counts_are_kept_within_the_game_bounds) {
    SourceArbiter arbiter;
    arbiter.setPriorityGroups({group(0)});
    arbiter.setVoiceCounts(0, 0);

    for (int i = 0; i < SourceArbiter::kMinVoices3D; ++i) {
        ASSERT_TRUE(arbiter.admit(oneShot(0), true).id);
    }
    EXPECT_FALSE(arbiter.admit(oneShot(0), true).id);
}
