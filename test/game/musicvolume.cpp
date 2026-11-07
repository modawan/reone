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

#include <limits>

#include "../fixtures/engine.h"
#include "reone/audio/types.h"
#include "reone/game/game.h"
#include "reone/game/script/routines.h"
#include "reone/resource/types.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::audio;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;

namespace {

class MusicVolumeHarness : boost::noncopyable {
public:
    MusicVolumeHarness() :
        _game(GameID::KotOR, "", testEngine().options(), testEngine().services(), _console),
        _routines(GameID::KotOR, &_game, &testEngine().services()) {

        _routines.init();
    }

    ~MusicVolumeHarness() {
        testing::Mock::VerifyAndClearExpectations(&testEngine().audioModule().mixer());
    }

    void setMusicVolume(std::vector<Variable> args) {
        Routine &routine = _routines.get(_routines.getIndexByName("SetMusicVolume"));
        ExecutionContext execution;
        execution.routines = &_routines;
        routine.invoke(args, execution);
    }

private:
    StubConsole _console;
    Game _game;
    Routines _routines;
};

void expectMusicVolume(int volume) {
    EXPECT_CALL(testEngine().audioModule().mixer(), setVolume(AudioType::Music, volume)).Times(1);
}

} // namespace

// 765: k_psta_egg, the Star Forge easter egg, puts the music back to full.
TEST(MusicVolume, sets_the_music_volume) {
    MusicVolumeHarness harness;

    expectMusicVolume(100);
    harness.setMusicVolume({Variable::ofFloat(1.0f)});
}

TEST(MusicVolume, a_fraction_is_that_share_of_full_volume) {
    MusicVolumeHarness harness;

    expectMusicVolume(25);
    harness.setMusicVolume({Variable::ofFloat(0.25f)});
}

TEST(MusicVolume, the_volume_defaults_to_full) {
    MusicVolumeHarness harness;

    expectMusicVolume(100);
    harness.setMusicVolume({});
}

// The original keeps the volume from 0 to 1; one that is not a number is 1.
TEST(MusicVolume, volumes_outside_zero_to_one_are_clamped) {
    MusicVolumeHarness harness;

    {
        testing::InSequence order;
        expectMusicVolume(0);
        expectMusicVolume(100);
        expectMusicVolume(100);
    }
    harness.setMusicVolume({Variable::ofFloat(-0.5f)});
    harness.setMusicVolume({Variable::ofFloat(3.0f)});
    harness.setMusicVolume({Variable::ofFloat(std::numeric_limits<float>::quiet_NaN())});
}
