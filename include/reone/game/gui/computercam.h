/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include "../gui.h"

namespace reone {

namespace game {

// Transparent presentation only; the owning computer conversation drives it.
class ComputerCamGUI : public GameGUI {
public:
    ComputerCamGUI(Game &game, ServicesView &services, std::function<void()> onReturn);

    bool handle(const input::Event &event) override;

private:
    std::function<void()> _onReturn;

    void preload(gui::IGUI &gui) override;
    void onGUILoaded() override;
};

} // namespace game

} // namespace reone
