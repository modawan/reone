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
#include "reone/game/messagelog.h"
#include "reone/gui/control/button.h"
#include "reone/resource/strings.h"

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

static constexpr int kStrRefMessages = 1563;
static constexpr int kStrRefDialog = 371;
static constexpr int kStrRefFeedback = 42167;
static constexpr int kStrRefShowFeedback = 42142;
static constexpr int kStrRefShowDialog = 42143;

static const glm::vec3 kFeedbackColor(0.0f, 0.66f, 0.98f);
static const glm::vec3 kCombatColor(0.74f, 0.11f, 0.0f);

void MessagesMenu::onGUILoaded() {
    loadBackground(BackgroundType::Menu);
    bindControls();

    _controls.BTN_EXIT->setOnClick([this]() {
        if (_game.isTSL()) {
            _game.openInGameMenu(InGameMenuTab::Journal);
        } else {
            _game.openInGame();
        }
    });
    if (_game.isTSL()) {
        _controls.BTN_FEEDBACK->setOnClick([this]() {
            showFeedbackMessages();
        });
    } else {
        _controls.BTN_SHOW->setOnClick([this]() {
            toggleMessages();
        });
    }

    _controls.LB_MESSAGES->setItemsInteractive(false);
    _controls.LB_MESSAGES->setProtoMatchContent(true);
}

void MessagesMenu::refresh() {
    _controls.LB_MESSAGES->clearItems();

    for (const MessageLog::Entry &entry : _game.messageLog().entries()) {
        if ((entry.type & MessageLog::kFeedbackMessageType) == 0) {
            continue;
        }

        gui::ListBox::Item item;
        item.text = entry.text;
        if (!_game.isTSL()) {
            item.textColor = entry.style == MessageLog::Style::Red
                                 ? kCombatColor
                                 : kFeedbackColor;
        }
        _controls.LB_MESSAGES->addItem(std::move(item));
    }
    _controls.LB_MESSAGES->scrollToBottom();

    if (_showingFeedback) {
        showFeedbackMessages();
    } else {
        showDialogMessages();
    }
}

void MessagesMenu::showDialogMessages() {
    if (_game.isTSL()) {
        return;
    }

    _controls.LB_MESSAGES->setVisible(false);
    _controls.LB_DIALOG->setVisible(true);
    _controls.LBL_MESSAGES->setTextMessage(
        _services.resource.strings.getText(kStrRefMessages) + " - " +
        _services.resource.strings.getText(kStrRefDialog));
    _controls.BTN_SHOW->setTextMessage(
        _services.resource.strings.getText(kStrRefShowFeedback));
    _showingFeedback = false;
}

void MessagesMenu::showFeedbackMessages() {
    if (!_game.isTSL()) {
        _controls.LB_DIALOG->setVisible(false);
        _controls.LB_MESSAGES->setVisible(true);
        _controls.LBL_MESSAGES->setTextMessage(
            _services.resource.strings.getText(kStrRefMessages) + " - " +
            _services.resource.strings.getText(kStrRefFeedback));
        _controls.BTN_SHOW->setTextMessage(
            _services.resource.strings.getText(kStrRefShowDialog));
        _showingFeedback = true;
        return;
    }

    _controls.LB_COMBAT->setVisible(false);
    _controls.LB_DIALOG->setVisible(false);
    _controls.LB_EFFECTS_BAD->setVisible(false);
    _controls.LB_EFFECTS_GOOD->setVisible(false);
    _controls.LB_MESSAGES->setVisible(true);

    _controls.LBL_EFFECTS_BAD->setVisible(false);
    _controls.LBL_EFFECTS_GOOD->setVisible(false);
    _controls.LBL_MESSAGES->setVisible(true);

    _controls.BTN_COMBAT->setSelected(false);
    _controls.BTN_DIALOG->setSelected(false);
    _controls.BTN_EFFECTS->setSelected(false);
    _controls.BTN_FEEDBACK->setSelected(true);
    _showingFeedback = true;
}

void MessagesMenu::toggleMessages() {
    if (_showingFeedback) {
        showDialogMessages();
    } else {
        showFeedbackMessages();
    }
}

} // namespace game

} // namespace reone
