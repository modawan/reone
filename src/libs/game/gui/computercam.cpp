/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "reone/game/gui/computercam.h"

#include "reone/game/game.h"

namespace reone {

namespace game {

ComputerCamGUI::ComputerCamGUI(Game &game, ServicesView &services) :
    GameGUI(game, services) {
    _resRef = game.isTSL() ? guiResRef("computercam") : "computercamera";
}

void ComputerCamGUI::preload(gui::IGUI &gui) {
    GameGUI::preload(gui);
    // Both games author the camera overlay on a 640x480 canvas.
    gui.setResolution(640, 480);
}

} // namespace game

} // namespace reone
