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

#include <optional>

#include "../gui.h"

#include "reone/gui/control/label.h"
#include "reone/script/types.h"
#include "reone/system/timer.h"

namespace reone {

namespace game {

class BarkBubble : public GameGUI {
public:
    BarkBubble(Game &game, ServicesView &services) :
        GameGUI(game, services) {
        _resRef = guiResRef("barkbubble");
    }

    void update(float dt) override;

    /** Overrides the authored top edge, in the GUI's authored canvas units. */
    void setAuthoredTop(int top) { _authoredTopOverride = top; }
    /**
     * Shows \p text for \p duration seconds, barked by \p speaker, or with
     * no speaker. A bark with a speaker ends once the party leader stands
     * 6 m or more from it, unless \p ignoresDistance, or once the speaker is
     * gone. Empty text hides the bubble.
     */
    void setBarkText(const std::string &text, float duration, uint32_t speaker = script::kObjectInvalid,
                     bool ignoresDistance = false);
    /**
     * TSL: places the bubble's left and top edges at \p x and \p y, in the
     * GUI's authored canvas units, each unless -1. The place holds for every
     * bark until it is set again.
     */
    void setPlace(int x, int y);

private:
    std::optional<int> _authoredTopOverride;
    struct Controls {
        std::shared_ptr<gui::Label> LBL_BARKTEXT;
    };

    Controls _controls;

    std::string _barkText;
    Timer _timer;
    /** A bark is up and its text shows. */
    bool _barking {false};
    /** Who barked; invalid for a bark with no speaker. */
    uint32_t _speaker {script::kObjectInvalid};
    /** The bark shows whatever the leader's distance from its speaker. */
    bool _ignoresDistance {false};
    /** TSL's left and top edges for the bubble, in authored canvas units; -1 for the layout's. */
    int _placeX {-1};
    int _placeY {-1};

    void preload(gui::IGUI &gui) override;
    void onGUILoaded() override;
    /** Places the bubble and its text from their authored extents at the GUI's scale. */
    void place();
    /** Whether the bark's speaker is close enough to the party leader for the bark to show. */
    bool speakerInRange() const;
    /** Ends a bark whose speaker is out of range, and shows the bubble while a bark is up. */
    void updateShown();

    void bindControls() {
        _controls.LBL_BARKTEXT = findControl<gui::Label>("LBL_BARKTEXT");
    }
};

} // namespace game

} // namespace reone
