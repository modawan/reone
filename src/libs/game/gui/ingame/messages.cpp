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

#include "reone/game/gui/ingame/messages.h"

#include "reone/game/game.h"
#include "reone/gui/control/button.h"

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

void MessagesMenu::onGUILoaded() {
    loadBackground(BackgroundType::Menu);
    bindControls();
    tintK2InGameFooter();

    _controls.BTN_EXIT->setOnClick([this]() {
        if (_game.isTSL()) {
            _game.openInGameMenu(InGameMenuTab::Journal);
        } else {
            _game.openInGame();
        }
    });

    if (!_game.isTSL()) {
        _controls.BTN_SHOW->setDisabled(true);
        return;
    }

    _controls.LB_DIALOG->setTintBorderFill(true);
    _controls.LB_MESSAGES->setTintBorderFill(true);
    _controls.LB_COMBAT->setTintBorderFill(true);
    _controls.LB_EFFECTS_GOOD->setTintBorderFill(true);
    _controls.LB_EFFECTS_BAD->setTintBorderFill(true);
    _controls.LBL_EFFECTS_GOOD->setTintBorderFill(true);
    _controls.LBL_EFFECTS_BAD->setTintBorderFill(true);

    _controls.BTN_DIALOG->setOnClick([this]() {
        setFilter(Filter::Dialog);
    });
    _controls.BTN_FEEDBACK->setOnClick([this]() {
        setFilter(Filter::Feedback);
    });
    _controls.BTN_COMBAT->setOnClick([this]() {
        setFilter(Filter::Combat);
    });
    _controls.BTN_EFFECTS->setOnClick([this]() {
        setFilter(Filter::Effects);
    });

    resetFilter();
}

void MessagesMenu::resetFilter() {
    if (!_game.isTSL()) {
        return;
    }

    setFilter(Filter::Dialog);
}

void MessagesMenu::setFilter(Filter filter) {
    _filter = filter;
    refreshFilterVisibility();
}

void MessagesMenu::refreshFilterVisibility() {
    bool dialog = _filter == Filter::Dialog;
    bool feedback = _filter == Filter::Feedback;
    bool combat = _filter == Filter::Combat;
    bool effects = _filter == Filter::Effects;

    _controls.BTN_DIALOG->setSelected(dialog);
    _controls.BTN_FEEDBACK->setSelected(feedback);
    _controls.BTN_COMBAT->setSelected(combat);
    _controls.BTN_EFFECTS->setSelected(effects);

    _controls.LB_DIALOG->setVisible(dialog);
    _controls.LB_MESSAGES->setVisible(feedback);
    _controls.LB_COMBAT->setVisible(combat);
    _controls.LBL_EFFECTS_GOOD->setVisible(effects);
    _controls.LBL_EFFECTS_BAD->setVisible(effects);
    _controls.LB_EFFECTS_GOOD->setVisible(effects);
    _controls.LB_EFFECTS_BAD->setVisible(effects);
}

} // namespace game

} // namespace reone
