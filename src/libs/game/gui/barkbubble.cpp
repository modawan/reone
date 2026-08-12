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

#include "reone/game/gui/barkbubble.h"

#include <algorithm>
#include <cmath>

#include "reone/game/game.h"

using namespace reone::audio;
using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

void BarkBubble::preload(IGUI &gui) {
    GameGUI::preload(gui);
    gui.setScaling(GUI::ScalingMode::PositionRelativeToCenter);
}

void BarkBubble::onGUILoaded() {
    bindControls();

    // Unlike the HUD, the bubble's text extent is authored relative to its
    // panel. PositionRelativeToCenter otherwise anchors both controls as
    // independent screen-space objects and separates them after scaling.
    float scale = _gui->scale();
    auto &root = _gui->rootControl();
    auto rootAuthored = root.authoredExtent();
    Control::Extent rootExtent {
        static_cast<int>(std::lround(rootAuthored.left * scale)),
        static_cast<int>(std::lround(rootAuthored.top * scale)),
        static_cast<int>(std::lround(rootAuthored.width * scale)),
        static_cast<int>(std::lround(rootAuthored.height * scale))};
    root.setExtent(rootExtent);
    root.setPresentationScale(scale);

    auto labelAuthored = _controls.LBL_BARKTEXT->authoredExtent();
    _controls.LBL_BARKTEXT->setExtent({
        rootExtent.left + static_cast<int>(std::lround(labelAuthored.left * scale)),
        rootExtent.top + static_cast<int>(std::lround(labelAuthored.top * scale)),
        static_cast<int>(std::lround(labelAuthored.width * scale)),
        static_cast<int>(std::lround(labelAuthored.height * scale))});
    _controls.LBL_BARKTEXT->setPresentationScale(scale);

    root.setVisible(false);
    _controls.LBL_BARKTEXT->setVisible(false);
}

void BarkBubble::update(float dt) {
    _timer.update(dt);
    if (_timer.elapsed()) {
        setBarkText("", 0.0f);
    }
}

void BarkBubble::setBarkText(const std::string &text, float duration) {
    if (text.empty()) {
        _gui->rootControl().setVisible(false);
        _controls.LBL_BARKTEXT->setVisible(false);
    } else {
        _controls.LBL_BARKTEXT->setTextMessage(text);

        int lineCount = std::max(1, static_cast<int>(_controls.LBL_BARKTEXT->textLines().size()));
        int lineHeight = std::max(1, static_cast<int>(std::lround(
                                         _controls.LBL_BARKTEXT->text().font->height() *
                                         _controls.LBL_BARKTEXT->scale())));
        int padding = std::max(0, static_cast<int>(std::lround(
                                      _controls.LBL_BARKTEXT->authoredExtent().left * _gui->scale())));

        _gui->rootControl().setVisible(true);
        _gui->rootControl().setExtentHeight(lineCount * lineHeight + 2 * padding);

        _controls.LBL_BARKTEXT->setExtentHeight(lineCount * lineHeight);
        _controls.LBL_BARKTEXT->setVisible(true);
    }

    if (duration > 0.0f) {
        _timer.reset(duration);
    }
}

} // namespace game

} // namespace reone
