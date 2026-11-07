/*
 * Copyright (c) 2026 The reone project contributors
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
#include "reone/game/game.h"
#include "reone/game/script/routines.h"
#include "reone/input/event.h"
#include "reone/movie/movie.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;

using testing::_;
using testing::AnyNumber;
using testing::Return;

namespace {

class FakeMovie : public movie::IMovie {
public:
    void update(float dt) override {}
    void render() override {}
    void finish() override { _finished = true; }
    bool isFinished() const override { return _finished; }

private:
    bool _finished {false};
};

class MovieQueue : public testing::Test {
protected:
    void SetUp() override {
        _engine.init();
        // Retiring the session clears the scene.
        ON_CALL(_engine.sceneModule().graphs(), get(_)).WillByDefault(testing::ReturnRef(_sceneGraph));
        EXPECT_CALL(_engine.resourceModule().movies(), get(_)).Times(AnyNumber());
        for (const char *name : {"a", "b", "c", "55", "01a"}) {
            auto movie = std::make_shared<FakeMovie>();
            _movies[name] = movie;
            ON_CALL(_engine.resourceModule().movies(), get(std::string(name))).WillByDefault(Return(movie));
        }
        _game = std::make_unique<Game>(GameID::KotOR, "", _engine.options(), _engine.services(), _console);
        _routines = std::make_unique<Routines>(GameID::KotOR, _game.get(), &_engine.services());
        _routines->init();
    }

    Variable call(const std::string &name, std::vector<Variable> args) {
        Routine &routine = _routines->get(_routines->getIndexByName(name));
        ExecutionContext execution;
        execution.routines = _routines.get();
        return routine.invoke(args, execution);
    }

    void queue(const std::string &movie, int skippable) {
        call("QueueMovie", {Variable::ofString(movie), Variable::ofInt(skippable)});
    }

    void play(int allowSeparateSkips) {
        call("PlayMovieQueue", {Variable::ofInt(allowSeparateSkips)});
    }

    int isMoviePlaying() {
        return call("IsMoviePlaying", {}).intValue;
    }

    std::shared_ptr<movie::IMovie> movie(const std::string &name) {
        return _movies.at(name);
    }

    void pressEscape() {
        input::Event event {input::EventType::KeyDown};
        event.key.pressed = true;
        event.key.code = input::KeyCode::Escape;
        event.key.mod = 0;
        event.key.repeat = false;
        _game->handle(event);
        _game->update(0.1f);
    }

    void click() {
        input::Event event {input::EventType::MouseButtonDown};
        event.button.button = input::MouseButton::Left;
        event.button.pressed = true;
        event.button.x = 0;
        event.button.y = 0;
        event.button.clicks = 1;
        _game->handle(event);
        _game->update(0.1f);
    }

    void finishCurrent() {
        auto current = _game->movie();
        ASSERT_TRUE(current);
        current->finish();
        _game->update(0.1f);
    }

    TestEngine _engine;
    testing::NiceMock<scene::MockSceneGraph> _sceneGraph;
    StubConsole _console;
    std::unique_ptr<Game> _game;
    std::unique_ptr<Routines> _routines;
    std::map<std::string, std::shared_ptr<FakeMovie>> _movies;
};

} // namespace

// 769: queuing plays nothing until the queue is played.
TEST_F(MovieQueue, queued_movie_does_not_play) {
    queue("55", 1);

    EXPECT_EQ(0, isMoviePlaying());
    ASSERT_EQ(1u, _game->queuedMovies().size());
    EXPECT_EQ("55", _game->queuedMovies()[0].name);
    EXPECT_TRUE(_game->queuedMovies()[0].skippable);
}

TEST_F(MovieQueue, movies_queue_in_call_order_with_their_flags) {
    queue("A", 0);
    queue("b", 1);

    ASSERT_EQ(2u, _game->queuedMovies().size());
    EXPECT_EQ("a", _game->queuedMovies()[0].name);
    EXPECT_FALSE(_game->queuedMovies()[0].skippable);
    EXPECT_EQ("b", _game->queuedMovies()[1].name);
    EXPECT_TRUE(_game->queuedMovies()[1].skippable);
}

TEST_F(MovieQueue, any_non_zero_flag_makes_a_movie_skippable) {
    queue("55", 7);

    ASSERT_EQ(1u, _game->queuedMovies().size());
    EXPECT_TRUE(_game->queuedMovies()[0].skippable);
}

TEST_F(MovieQueue, play_movie_leaves_the_queue_alone) {
    queue("55", 1);
    call("PlayMovie", {Variable::ofString("01a")});

    EXPECT_EQ(movie("01a"), _game->movie());
    ASSERT_EQ(1u, _game->queuedMovies().size());
    EXPECT_EQ("55", _game->queuedMovies()[0].name);
}

// 770: the queue plays from its first movie and is emptied.
TEST_F(MovieQueue, single_movie_plays_and_empties_the_queue) {
    queue("a", 1);
    play(1);

    EXPECT_EQ(movie("a"), _game->movie());
    EXPECT_TRUE(_game->queuedMovies().empty());
    finishCurrent();
    EXPECT_FALSE(_game->movie());
}

TEST_F(MovieQueue, movies_play_one_after_another) {
    queue("a", 1);
    queue("b", 1);
    play(1);

    EXPECT_EQ(movie("a"), _game->movie());
    EXPECT_EQ(1, isMoviePlaying());
    finishCurrent();
    EXPECT_EQ(movie("b"), _game->movie());
    EXPECT_EQ(1, isMoviePlaying());
    finishCurrent();
    EXPECT_FALSE(_game->movie());
    EXPECT_EQ(0, isMoviePlaying());
}

TEST_F(MovieQueue, skip_ends_a_single_skippable_movie) {
    queue("a", 1);
    play(1);
    EXPECT_EQ(movie("a"), _game->movie());
    pressEscape();
    EXPECT_FALSE(_game->movie());

    queue("b", 1);
    play(0);
    EXPECT_EQ(movie("b"), _game->movie());
    pressEscape();
    EXPECT_FALSE(_game->movie());
}

TEST_F(MovieQueue, unskippable_movie_ignores_skips) {
    queue("a", 0);
    queue("b", 1);
    play(0);

    pressEscape();
    click();
    EXPECT_EQ(movie("a"), _game->movie());
    finishCurrent();
    EXPECT_EQ(movie("b"), _game->movie());
}

TEST_F(MovieQueue, skip_without_separate_skips_ends_the_whole_queue) {
    queue("a", 1);
    queue("b", 0);
    play(0);

    EXPECT_EQ(movie("a"), _game->movie());
    pressEscape();
    EXPECT_FALSE(_game->movie());
    EXPECT_EQ(0, isMoviePlaying());
}

TEST_F(MovieQueue, skip_with_separate_skips_ends_only_the_current_movie) {
    queue("a", 1);
    queue("b", 0);
    play(1);

    pressEscape();
    EXPECT_EQ(movie("b"), _game->movie());
    pressEscape();
    EXPECT_EQ(movie("b"), _game->movie());
    finishCurrent();
    EXPECT_FALSE(_game->movie());
}

TEST_F(MovieQueue, click_skips_as_escape_does) {
    queue("a", 1);
    queue("b", 1);
    play(1);

    click();
    EXPECT_EQ(movie("b"), _game->movie());
}

TEST_F(MovieQueue, missing_movie_is_passed_over) {
    queue("missing", 1);
    queue("a", 1);
    queue("missing", 1);
    queue("b", 1);
    play(1);

    EXPECT_EQ(movie("a"), _game->movie());
    finishCurrent();
    EXPECT_EQ(movie("b"), _game->movie());
}

TEST_F(MovieQueue, queue_of_missing_movies_plays_nothing) {
    queue("missing", 1);
    play(1);
    EXPECT_FALSE(_game->movie());
    EXPECT_EQ(0, isMoviePlaying());

    queue("a", 1);
    play(1);
    EXPECT_EQ(movie("a"), _game->movie());
}

TEST_F(MovieQueue, empty_queue_plays_nothing) {
    play(1);
    EXPECT_FALSE(_game->movie());

    queue("a", 1);
    play(1);
    EXPECT_EQ(movie("a"), _game->movie());
}

TEST_F(MovieQueue, played_queue_does_not_play_again) {
    queue("a", 1);
    play(1);
    finishCurrent();

    play(1);
    EXPECT_FALSE(_game->movie());
    queue("b", 1);
    play(1);
    EXPECT_EQ(movie("b"), _game->movie());
    finishCurrent();
    EXPECT_FALSE(_game->movie());
}

TEST_F(MovieQueue, queue_played_during_a_movie_follows_it) {
    call("PlayMovie", {Variable::ofString("01a")});
    queue("a", 1);
    play(1);

    EXPECT_EQ(movie("01a"), _game->movie());
    finishCurrent();
    EXPECT_EQ(movie("a"), _game->movie());
}

// The end of the game: the credits follow movie 55.
TEST_F(MovieQueue, end_game_movie_plays_then_lets_the_game_go_on) {
    queue("55", 1);
    play(1);

    EXPECT_EQ(1, isMoviePlaying());
    pressEscape();
    EXPECT_EQ(0, isMoviePlaying());
}

// The movies of a module change play through before a queue the script
// played while the first of them ran.
TEST_F(MovieQueue, transition_movies_play_before_a_later_queue) {
    _game->scheduleModuleTransitionWithMovies("next", "", {"b", "c"});
    queue("a", 1);
    play(1);

    EXPECT_EQ(movie("b"), _game->movie());
    finishCurrent();
    EXPECT_EQ(movie("c"), _game->movie());
    finishCurrent();
    EXPECT_EQ(movie("a"), _game->movie());
}

// A movie the script plays after its queue comes first; the queue follows it
// whole, each movie as it was queued.
TEST_F(MovieQueue, play_movie_after_play_movie_queue_goes_first) {
    queue("a", 1);
    queue("b", 0);
    play(0);
    call("PlayMovie", {Variable::ofString("01a")});

    EXPECT_EQ(movie("01a"), _game->movie());
    finishCurrent();
    EXPECT_EQ(movie("a"), _game->movie());
    pressEscape();
    EXPECT_FALSE(_game->movie());
}

TEST_F(MovieQueue, missing_play_movie_leaves_the_queue_playing) {
    queue("a", 1);
    play(1);
    call("PlayMovie", {Variable::ofString("missing")});

    EXPECT_EQ(movie("a"), _game->movie());
}

// Skipping without separate skips ends the rest of the movie's own queue,
// not a queue played after it.
TEST_F(MovieQueue, skip_without_separate_skips_leaves_a_later_queue) {
    queue("a", 1);
    queue("b", 1);
    play(0);
    queue("c", 1);
    play(1);

    EXPECT_EQ(movie("a"), _game->movie());
    pressEscape();
    EXPECT_EQ(movie("c"), _game->movie());
}

// The skipmovie console command ends the movie whatever its flag, and goes
// on as a skip does.
TEST_F(MovieQueue, console_skip_ends_the_movie_as_a_skip_does) {
    TestGameModule::initConsole(*_game);
    queue("a", 0);
    queue("b", 1);
    play(0);
    queue("c", 1);
    play(1);

    _console.execute("skipmovie", {});
    _game->update(0.1f);
    EXPECT_EQ(movie("c"), _game->movie());
    _console.execute("skipmovie", {});
    _game->update(0.1f);
    EXPECT_FALSE(_game->movie());
}

TEST_F(MovieQueue, retired_session_drops_every_queue) {
    queue("a", 1);
    queue("b", 1);
    play(1);
    queue("c", 1);

    _game->retireRuntimeSession();
    EXPECT_FALSE(_game->movie());
    EXPECT_TRUE(_game->queuedMovies().empty());
    play(1);
    EXPECT_FALSE(_game->movie());
}
