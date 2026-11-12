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

#include <glm/geometric.hpp>

#include "reone/game/game.h"
#include "reone/game/object/creature.h"

using namespace reone::audio;
using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

/** The bark shows while the party leader is within 6 m of its speaker: the distance squared. */
static constexpr float kBarkRangeSquared = 36.0f;

void BarkBubble::preload(IGUI &gui) {
    GameGUI::preload(gui);
    gui.setScaling(GUI::ScalingMode::PositionRelativeToCenter);
}

void BarkBubble::onGUILoaded() {
    bindControls();
    place();
    _gui->rootControl().setVisible(false);
    _controls.LBL_BARKTEXT->setVisible(false);
}

void BarkBubble::place() {
    // Unlike the HUD, the bubble's text extent is authored relative to its
    // panel. PositionRelativeToCenter otherwise anchors both controls as
    // independent screen-space objects and separates them after scaling.
    float scale = _gui->scale();
    auto &root = _gui->rootControl();
    auto rootAuthored = root.authoredExtent();
    if (_authoredTopOverride) {
        rootAuthored.top = *_authoredTopOverride;
    }
    Control::Extent rootExtent {
        static_cast<int>(std::lround(rootAuthored.left * scale)),
        static_cast<int>(std::lround(rootAuthored.top * scale)),
        static_cast<int>(std::lround(rootAuthored.width * scale)),
        static_cast<int>(std::lround(rootAuthored.height * scale))};
    // TSL puts the bubble's edges where a script last placed them, at the
    // GUI's scale as the layout's are.
    if (_game.isTSL()) {
        if (_placeX != -1) {
            rootExtent.left = static_cast<int>(std::lround(_placeX * scale));
        }
        if (_placeY != -1) {
            rootExtent.top = static_cast<int>(std::lround(_placeY * scale));
        }
    }
    root.setExtent(rootExtent);
    root.setPresentationScale(scale);

    auto labelAuthored = _controls.LBL_BARKTEXT->authoredExtent();
    _controls.LBL_BARKTEXT->setExtent({
        rootExtent.left + static_cast<int>(std::lround(labelAuthored.left * scale)),
        rootExtent.top + static_cast<int>(std::lround(labelAuthored.top * scale)),
        static_cast<int>(std::lround(labelAuthored.width * scale)),
        static_cast<int>(std::lround(labelAuthored.height * scale))});
    _controls.LBL_BARKTEXT->setPresentationScale(scale);
}

void BarkBubble::setPlace(int x, int y) {
    if (_placeX == x && _placeY == y) {
        return;
    }
    _placeX = x;
    _placeY = y;
    if (!_controls.LBL_BARKTEXT) {
        return;
    }
    place();
    // A bark under way is measured again in its new place.
    if (_barking) {
        setBarkText(_barkText, 0.0f, _speaker, _ignoresDistance);
    }
}

// A bark ends when its time is up, and the bubble hides once.
void BarkBubble::update(float dt) {
    _timer.update(dt);
    if (!_barking) {
        return;
    }
    if (_timer.elapsed()) {
        setBarkText("", 0.0f);
    } else {
        updateShown();
    }
}

// A bark with no speaker, or with no leader to measure from, shows. One whose
// speaker is gone doesn't; otherwise the leader must stand within range,
// unless the bark ignores the distance.
bool BarkBubble::speakerInRange() const {
    if (_speaker == script::kObjectInvalid) {
        return true;
    }
    const auto leader = _game.party().getLeader();
    if (!leader) {
        return true;
    }
    const auto speaker = _game.getObjectById(_speaker);
    if (!speaker) {
        return false;
    }
    if (_ignoresDistance || speaker->type() == ObjectType::Module || speaker->type() == ObjectType::Area) {
        return true;
    }
    const glm::vec3 offset = leader->position() - speaker->position();
    return glm::dot(offset, offset) < kBarkRangeSquared;
}

// A bark whose speaker is gone or out of range ends, and doesn't come back
// when the speaker is in range again; its sound plays on.
void BarkBubble::updateShown() {
    if (_barking && !speakerInRange()) {
        _barkText.clear();
        _barking = false;
        _speaker = script::kObjectInvalid;
        _ignoresDistance = false;
    }
    _gui->rootControl().setVisible(_barking);
    _controls.LBL_BARKTEXT->setVisible(_barking);
}

void BarkBubble::setBarkText(const std::string &text, float duration, uint32_t speaker, bool ignoresDistance) {
    if (text.empty()) {
        _barkText.clear();
        _barking = false;
        _speaker = script::kObjectInvalid;
        _ignoresDistance = false;
        updateShown();
    } else {
        _barkText = text;
        _controls.LBL_BARKTEXT->setTextMessage(text);

        int lineCount = std::max(1, static_cast<int>(_controls.LBL_BARKTEXT->textLines().size()));
        int lineHeight = std::max(1, static_cast<int>(std::lround(
                                         _controls.LBL_BARKTEXT->text().font->height() *
                                         _controls.LBL_BARKTEXT->scale())));
        // Baked into the control extents by place.
        int padding = std::max(0, _controls.LBL_BARKTEXT->extent().left - _gui->rootControl().extent().left);

        _gui->rootControl().setExtentHeight(lineCount * lineHeight + 2 * padding);

        _controls.LBL_BARKTEXT->setExtentHeight(lineCount * lineHeight);
        _barking = true;
        _speaker = speaker;
        _ignoresDistance = ignoresDistance;
        updateShown();
    }

    if (duration > 0.0f) {
        _timer.reset(duration);
    }
}

} // namespace game

} // namespace reone
