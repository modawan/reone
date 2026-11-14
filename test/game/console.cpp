/* Copyright (c) 2026 The reone project contributors */

#include <gtest/gtest.h>

#include "reone/game/console.h"

using namespace reone::game;

TEST(ConsoleArgs, reads_floating_point_and_integer_arguments_whole) {
    ConsoleArgs args({"warp", "1.5", "-2", "1.5x", "12", "12a", "+1.5"});

    EXPECT_FLOAT_EQ(args.get<float>(1).value(), 1.5f);
    EXPECT_FLOAT_EQ(args.get<float>(2).value(), -2.0f);
    EXPECT_FALSE(args.get<float>(3));
    EXPECT_EQ(args.get<int>(4).value(), 12);
    EXPECT_FALSE(args.get<int>(5));
    EXPECT_FALSE(args.get<float>(6));
    EXPECT_FALSE(args.get<float>(7));
}
