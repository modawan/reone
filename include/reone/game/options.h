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
#include "menupresentation.h"

namespace reone {

namespace game {

struct GameOptions {
    std::filesystem::path path;
    bool developer {false};
    bool neo {false};
    /** The mouse wheel moves the follow camera in and out. */
    bool wheelZoom {false};
    FeedbackOptions feedback;
    int clientDifficulty {1}; /**< Easy=0, Normal=1, Difficult=2, Default=3 */
    int activeKeybindsIndex {0};
    MenuPresentation menuPresentation;
    AutoPauseOptions autoPause;
    /** How the mouse turns the camera. */
    MouseOptions mouse;
    /** The other switches of the Gameplay panel. */
    GameplayOptions gameplay;
    /** The movies the main menu's movie list offers beyond those it always shows. */
    MoviesShown moviesShown;
    /** The planet groups of the main menu's music list that are open, one bit each (TSL). */
    int unlockedPlanetSongs {0};
    std::array<KeybindOptions, 1> keybinds;
    std::filesystem::path configurationPath; // Empty for embedded/test hosts.

    KeybindOptions &activeKeybinds() {
        return keybinds.at(activeKeybindsIndex);
    }
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
