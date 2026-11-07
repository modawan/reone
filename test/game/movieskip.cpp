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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"
#include "../fixtures/movie.h"
#include "reone/game/game.h"

using namespace reone;
using namespace reone::game;
using namespace testing;

namespace {

class MovieSkip : public TestWithParam<resource::GameID> {};

std::shared_ptr<NiceMock<movie::MockMovie>> playMovie(TestEngine &engine, Game &game) {
    auto movie = std::make_shared<NiceMock<movie::MockMovie>>();
    EXPECT_CALL(engine.resourceModule().movies(), get("skip_movie")).WillOnce(Return(movie));
    // Starting a movie holds other sounds; not what these tests are about.
    EXPECT_CALL(engine.audioModule().mixer(), setSoundsHeldForMovie(_, _)).Times(AnyNumber());
    game.playVideo("skip_movie");
    return movie;
}

} // namespace

TEST_P(MovieSkip, escape_finishes_the_playing_movie) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(GetParam(), "", engine.options(), engine.services(), console);
    auto movie = playMovie(engine, game);

    EXPECT_CALL(*movie, finish()).Times(1);
    EXPECT_TRUE(game.handle(input::Event::newKeyDown({true, input::KeyCode::Escape, 0, false})));
}

TEST_P(MovieSkip, a_held_escape_does_not_finish_it_again) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(GetParam(), "", engine.options(), engine.services(), console);
    auto movie = playMovie(engine, game);

    EXPECT_CALL(*movie, finish()).Times(0);
    game.handle(input::Event::newKeyDown({true, input::KeyCode::Escape, 0, true}));
}

TEST_P(MovieSkip, other_keys_leave_it_playing) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(GetParam(), "", engine.options(), engine.services(), console);
    auto movie = playMovie(engine, game);

    EXPECT_CALL(*movie, finish()).Times(0);
    game.handle(input::Event::newKeyDown({true, input::KeyCode::A, 0, false}));
}

TEST_P(MovieSkip, a_left_click_still_finishes_it) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(GetParam(), "", engine.options(), engine.services(), console);
    auto movie = playMovie(engine, game);

    EXPECT_CALL(*movie, finish()).Times(1);
    game.handle(input::Event::newMouseButtonDown({input::MouseButton::Left, true, 1, 10, 10}));
}

INSTANTIATE_TEST_SUITE_P(
    BothGames,
    MovieSkip,
    Values(resource::GameID::KotOR, resource::GameID::TSL),
    [](const TestParamInfo<resource::GameID> &info) {
        return info.param == resource::GameID::KotOR ? "KotOR" : "TSL";
    });
