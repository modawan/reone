/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "reone/game/gui/chargen/classselect.h"
#include "reone/resource/2da.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

std::shared_ptr<TwoDA> soundSetTable(int rows) {
    TwoDA::Builder builder;
    builder.columns({"label"});
    for (int row = 0; row < rows; ++row) {
        builder.row({"voice"});
    }
    return builder.build();
}

} // namespace

TEST(PlayerSoundSet, a_new_character_takes_the_player_voice_of_its_gender) {
    TestEngine engine;
    engine.init();
    // A table long enough for both player rows.
    ON_CALL(engine.resourceModule().twoDas(), get("soundset")).WillByDefault(Return(soundSetTable(90)));

    EXPECT_EQ(85, playerSoundSet(Gender::Male, engine.resourceModule().twoDas()));
    EXPECT_EQ(83, playerSoundSet(Gender::Female, engine.resourceModule().twoDas()));
}

TEST(PlayerSoundSet, a_table_without_the_player_rows_leaves_the_character_silent) {
    TestEngine engine;
    engine.init();
    // A table that ends before either player row.
    ON_CALL(engine.resourceModule().twoDas(), get("soundset")).WillByDefault(Return(soundSetTable(66)));

    EXPECT_FALSE(playerSoundSet(Gender::Male, engine.resourceModule().twoDas()));
    EXPECT_FALSE(playerSoundSet(Gender::Female, engine.resourceModule().twoDas()));

    ON_CALL(engine.resourceModule().twoDas(), get("soundset")).WillByDefault(Return(nullptr));
    EXPECT_FALSE(playerSoundSet(Gender::Male, engine.resourceModule().twoDas()));
}
