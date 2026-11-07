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

#include <map>

#include "../fixtures/engine.h"
#include "../fixtures/movie.h"
#include "reone/game/game.h"
#include "reone/game/script/routines.h"
#include "reone/input/event.h"
#include "reone/resource/types.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace {

/** A movie that plays until finish() is called, as a click or its end does. */
std::shared_ptr<NiceMock<movie::MockMovie>> makeMovie() {
    auto movie = std::make_shared<NiceMock<movie::MockMovie>>();
    auto finished = std::make_shared<bool>(false);
    ON_CALL(*movie, finish()).WillByDefault([finished]() { *finished = true; });
    ON_CALL(*movie, isFinished()).WillByDefault([finished]() { return *finished; });
    return movie;
}

class MovieQueueHarness : boost::noncopyable {
public:
    MovieQueueHarness() :
        _game(GameID::KotOR, "", testEngine().options(), testEngine().services(), _console),
        _routines(GameID::KotOR, &_game, &testEngine().services()) {

        _routines.init();
        ON_CALL(movies(), get(_)).WillByDefault([this](const std::string &name) -> std::shared_ptr<movie::IMovie> {
            _requested.push_back(name);
            auto found = _movies.find(name);
            return found != _movies.end() ? found->second : nullptr;
        });
    }

    // The engine's movies outlive the harness, so its default action goes with it.
    ~MovieQueueHarness() {
        testing::Mock::VerifyAndClear(&movies());
    }

    void call(const std::string &name, std::vector<Variable> args) {
        Routine &routine = _routines.get(_routines.getIndexByName(name));
        ExecutionContext execution;
        execution.routines = &_routines;
        routine.invoke(args, execution);
    }

    void queue(const std::string &name, bool skippable) {
        call("QueueMovie", {Variable::ofString(name), Variable::ofInt(skippable ? 1 : 0)});
    }

    void play(bool allowSeparateSkips) {
        call("PlayMovieQueue", {Variable::ofInt(allowSeparateSkips ? 1 : 0)});
    }

    std::shared_ptr<NiceMock<movie::MockMovie>> addMovie(const std::string &name) {
        auto movie = makeMovie();
        _movies[name] = movie;
        return movie;
    }

    void click() {
        _game.handle(input::Event::newMouseButtonDown({input::MouseButton::Left, true, 1, 100, 100}));
    }

    void update() { _game.update(0.1f); }

    Game &game() { return _game; }
    const std::vector<std::string> &requested() const { return _requested; }

private:
    StubConsole _console;
    Game _game;
    Routines _routines;
    std::map<std::string, std::shared_ptr<NiceMock<movie::MockMovie>>> _movies;
    std::vector<std::string> _requested;

    static MockMovies &movies() { return testEngine().resourceModule().movies(); }
};

using Names = std::vector<std::string>;

} // namespace

// 769, 770: k_stunt_end queues the "55" movie and plays the queue.
TEST(MovieQueue, queued_movies_play_in_order_once_the_queue_is_played) {
    MovieQueueHarness harness;
    auto first = harness.addMovie("55");
    auto second = harness.addMovie("56");

    harness.queue("55", true);
    harness.queue("56", true);
    EXPECT_TRUE(harness.requested().empty());
    EXPECT_FALSE(harness.game().movie());

    harness.play(true);
    EXPECT_EQ(Names({"55"}), harness.requested());
    EXPECT_EQ(first, harness.game().movie());

    first->finish();
    harness.update();
    EXPECT_EQ(Names({"55", "56"}), harness.requested());
    EXPECT_EQ(second, harness.game().movie());

    second->finish();
    harness.update();
    EXPECT_FALSE(harness.game().movie());
}

TEST(MovieQueue, playing_the_queue_empties_it) {
    MovieQueueHarness harness;
    auto movie = harness.addMovie("55");

    harness.queue("55", true);
    harness.play(true);
    movie->finish();
    harness.update();

    harness.play(true);
    EXPECT_EQ(Names({"55"}), harness.requested());
    EXPECT_FALSE(harness.game().movie());
}

TEST(MovieQueue, names_are_matched_in_lower_case_and_missing_movies_are_passed_over) {
    MovieQueueHarness harness;
    auto movie = harness.addMovie("leclogo");

    harness.queue("missing", true);
    harness.queue("LecLogo", true);
    harness.play(true);

    EXPECT_EQ(Names({"missing", "leclogo"}), harness.requested());
    EXPECT_EQ(movie, harness.game().movie());
}

TEST(MovieQueue, a_movie_that_is_not_skippable_plays_through_a_click) {
    MovieQueueHarness harness;
    auto movie = harness.addMovie("55");

    harness.queue("55", false);
    harness.play(true);
    harness.click();
    harness.update();

    EXPECT_FALSE(movie->isFinished());
    EXPECT_EQ(movie, harness.game().movie());
}

TEST(MovieQueue, with_separate_skips_a_click_skips_only_the_movie_playing) {
    MovieQueueHarness harness;
    harness.addMovie("55");
    auto second = harness.addMovie("56");

    harness.queue("55", true);
    harness.queue("56", true);
    harness.play(true);
    harness.click();
    harness.update();

    EXPECT_EQ(second, harness.game().movie());
}

TEST(MovieQueue, without_separate_skips_a_click_skips_the_rest_of_the_queue) {
    MovieQueueHarness harness;
    harness.addMovie("55");
    harness.addMovie("56");

    harness.queue("55", true);
    harness.queue("56", true);
    harness.play(false);
    harness.click();
    harness.update();

    EXPECT_EQ(Names({"55"}), harness.requested());
    EXPECT_FALSE(harness.game().movie());
}

TEST(MovieQueue, the_queue_waits_for_the_movie_playing) {
    MovieQueueHarness harness;
    auto single = harness.addMovie("permov");
    auto queued = harness.addMovie("55");

    harness.call("PlayMovie", {Variable::ofString("permov")});
    harness.queue("55", true);
    harness.play(true);
    EXPECT_EQ(single, harness.game().movie());

    single->finish();
    harness.update();
    EXPECT_EQ(queued, harness.game().movie());
}
