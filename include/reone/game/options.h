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
#include "reone/input/event.h"
#include "menupresentation.h"

namespace reone {

namespace game {

struct Keybind {
    reone::input::KeyCode keyCode;
};
struct KeybindOptions {
    Keybind moveForward {reone::input::KeyCode::W};
    Keybind moveBackward {reone::input::KeyCode::S};
    Keybind moveLeft {reone::input::KeyCode::Z};
    Keybind moveRight {reone::input::KeyCode::C};
    Keybind flourish {reone::input::KeyCode::X};
    Keybind walk {reone::input::KeyCode::B};
    Keybind cameraRotateLeft {reone::input::KeyCode::A};
    Keybind cameraRotateRight {reone::input::KeyCode::D};
    Keybind cycleTargetLeft {reone::input::KeyCode::Q};
    Keybind cycleTargetRight {reone::input::KeyCode::E};
    Keybind toggleStealth {reone::input::KeyCode::G};
    Keybind clearOneAction {reone::input::KeyCode::Y};
    Keybind cancelCombat {reone::input::KeyCode::F};
    Keybind defaultAction {reone::input::KeyCode::R};
    Keybind pause {reone::input::KeyCode::Space};
};

struct GameOptions {
    std::filesystem::path path;
    bool developer {false};
    bool neo {false};
    FeedbackOptions feedback;
    int clientDifficulty {1}; /**< Easy=0, Normal=1, Difficult=2, Default=3 */
    MenuPresentation menuPresentation;
    AutoPauseOptions autoPause;
    /** How the mouse turns the camera. */
    MouseOptions mouse;
    KeybindOptions keybinds;
    std::filesystem::path configurationPath; // Empty for embedded/test hosts.
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
