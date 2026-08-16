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

#pragma once

#include "reone/audio/options.h"
#include "reone/system/types.h"
#include "reone/graphics/options.h"

namespace reone {

namespace game {

struct GameOptions {
    std::filesystem::path path;
    bool developer {false};
    bool neo {false};

    // Client options used by difficulty scaling and AddFloatyText.
    uint8_t clientDifficulty {1}; // Easy=0, Normal=1, Difficult=2, Default=3
    uint8_t feedbackOptions {0xBE};
};

struct OptionsView {
    GameOptions &game;
    graphics::GraphicsOptions &graphics;
    audio::AudioOptions &audio;

    OptionsView(
        GameOptions &game,
        graphics::GraphicsOptions &graphics,
        audio::AudioOptions &audio) :
        game(game),
        graphics(graphics),
        audio(audio) {
    }
};

} // namespace game

} // namespace reone
