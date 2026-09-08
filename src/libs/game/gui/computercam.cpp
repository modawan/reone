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
#include "reone/gui/control/label.h"

namespace reone {

namespace game {

ComputerCamGUI::ComputerCamGUI(Game &game, ServicesView &services, std::function<void()> onReturn) :
    GameGUI(game, services),
    _onReturn(std::move(onReturn)) {
    _resRef = game.isTSL() ? guiResRef("computercam") : "computercamera";
}

void ComputerCamGUI::preload(gui::IGUI &gui) {
    GameGUI::preload(gui);
    // Both games author the camera overlay on a 640x480 canvas.
    gui.setResolution(640, 480);
}

void ComputerCamGUI::onGUILoaded() {
    auto returnControl = findControl<gui::Label>("LBL_RETURN");
    returnControl->setSelectable(true);
    returnControl->setOnClick(_onReturn);
}

bool ComputerCamGUI::handle(const input::Event &event) {
    if (event.type == input::EventType::KeyUp &&
        (event.key.code == input::KeyCode::Escape || event.key.code == input::KeyCode::Return)) {
        _onReturn();
        return true;
    }
    return GameGUI::handle(event);
}

} // namespace game

} // namespace reone
