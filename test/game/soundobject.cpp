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

#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "reone/game/game.h"
#include "reone/game/object/sound.h"
#include "reone/game/script/routines.h"
#include "reone/resource/types.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;

namespace {

class SoundObjectFixture : boost::noncopyable {
public:
    SoundObjectFixture() :
        _game(GameID::KotOR, "", testEngine().options(), testEngine().services(), _console),
        _routines(GameID::KotOR, &_game, &testEngine().services()) {

        _routines.init();
    }

    std::shared_ptr<Sound> playingSound() {
        auto sound = _game.newSound();
        sound->setActive(true);
        return sound;
    }

    void call(const std::string &name, std::vector<Variable> args) {
        Routine &routine = _routines.get(_routines.getIndexByName(name));
        ExecutionContext execution;
        execution.routines = &_routines;
        routine.invoke(args, execution);
    }

private:
    StubConsole _console;
    Game _game;
    Routines _routines;
};

} // namespace

// 415: "nVolume: 0-127".
TEST(SoundObject, volume_is_clamped_to_0_127) {
    SoundObjectFixture fixture;
    auto sound = fixture.playingSound();

    sound->setVolume(90);
    EXPECT_EQ(90, sound->volume());
    sound->setVolume(300);
    EXPECT_EQ(127, sound->volume());
    sound->setVolume(-5);
    EXPECT_EQ(0, sound->volume());
}

// 745: "Fades a sound object for 'fSeconds' and then stops it."
TEST(SoundObject, fade_and_stop_lowers_the_volume_then_stops) {
    SoundObjectFixture fixture;
    auto sound = fixture.playingSound();

    sound->fadeAndStop(2.0f);
    ASSERT_TRUE(sound->isFading());
    EXPECT_FLOAT_EQ(1.0f, sound->fadeScale());

    sound->advanceFade(0.5f);
    EXPECT_TRUE(sound->isActive());
    EXPECT_FLOAT_EQ(0.75f, sound->fadeScale());

    sound->advanceFade(1.0f);
    EXPECT_TRUE(sound->isActive());
    EXPECT_FLOAT_EQ(0.25f, sound->fadeScale());

    sound->advanceFade(0.5f);
    EXPECT_FALSE(sound->isActive());
    EXPECT_FALSE(sound->isFading());
    EXPECT_FLOAT_EQ(1.0f, sound->fadeScale());
}

TEST(SoundObject, fade_of_no_length_stops_at_once) {
    SoundObjectFixture fixture;
    auto sound = fixture.playingSound();

    sound->fadeAndStop(0.0f);

    EXPECT_FALSE(sound->isActive());
    EXPECT_FALSE(sound->isFading());
}

TEST(SoundObject, fading_a_stopped_sound_leaves_it_stopped) {
    SoundObjectFixture fixture;
    auto sound = fixture.playingSound();
    sound->setActive(false);

    sound->fadeAndStop(3.0f);

    EXPECT_FALSE(sound->isActive());
    EXPECT_FALSE(sound->isFading());
}

// A stop part way through ends the fade, so playing again starts at full volume.
TEST(SoundObject, stopping_ends_the_fade) {
    SoundObjectFixture fixture;
    auto sound = fixture.playingSound();
    sound->fadeAndStop(4.0f);
    sound->advanceFade(1.0f);

    sound->setActive(false);
    sound->setActive(true);

    EXPECT_FALSE(sound->isFading());
    EXPECT_FLOAT_EQ(1.0f, sound->fadeScale());
}

TEST(SoundObject, routines_set_the_volume_and_fade) {
    SoundObjectFixture fixture;
    auto sound = fixture.playingSound();

    fixture.call("SoundObjectSetVolume", {Variable::ofObject(sound->id()), Variable::ofInt(64)});
    EXPECT_EQ(64, sound->volume());

    fixture.call("SoundObjectFadeAndStop", {Variable::ofObject(sound->id()), Variable::ofFloat(1.5f)});
    EXPECT_TRUE(sound->isFading());
    sound->advanceFade(1.5f);
    EXPECT_FALSE(sound->isActive());
}
