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

/** A feedback line's colour: KotOR's and TSL's; a highlighted line's in both. */
static const glm::vec3 kFeedbackColor(0.0f, 0.66f, 0.98f);
static const glm::vec3 kTSLFeedbackColor(0.1f, 0.7f, 0.55f);
static const glm::vec3 kCombatColor(0.74f, 0.11f, 0.0f);

// In TSL a feedback or combat line, or a conversation line's speaker name,
// holding a token or a note is parsed again when the list shows it, and loses
// its trailing spaces. K1 lists every line as it was logged.
static std::string getTslMessageLine(const Game &game, std::string text) {
    if (text.find_first_of("<{") == std::string::npos) {
        return text;
    }
    text = game.substituteLogTokens(std::move(text));
    text.erase(text.find_last_not_of(' ') + 1);
    return text;
}

void MessagesMenu::onGUILoaded() {
    loadBackground(BackgroundType::Menu);
    bindControls();
    tintK2InGameFooter();
    tintK2InGameHeader();

    _controls.BTN_EXIT->setOnClick([this]() {
        if (_game.isTSL()) {
            _game.openInGameMenu(InGameMenuTab::Journal);
        } else {
            _game.openInGame();
        }
    });
    if (!_game.isTSL()) {
        _controls.BTN_SHOW->setOnClick([this]() {
            toggleMessages();
        });
        for (const auto &list : {_controls.LB_MESSAGES, _controls.LB_DIALOG}) {
            list->setItemsInteractive(false);
            list->setProtoMatchContent(true);
        }
        return;
    }

    useK2ShellTitle(_controls.LBL_MESSAGES);
    enableK2ButtonBodyFill(_controls.BTN_EXIT);
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

    for (const auto &list : {_controls.LB_MESSAGES, _controls.LB_COMBAT, _controls.LB_DIALOG}) {
        list->setItemsInteractive(false);
        list->setProtoMatchContent(true);
    }
    resetFilter();
}

void MessagesMenu::refresh() {
    _controls.LB_DIALOG->clearItems();
    _controls.LB_MESSAGES->clearItems();
    if (_game.isTSL()) {
        _controls.LB_COMBAT->clearItems();
    }

    // A conversation line shows as "speaker: text", or the text alone. The
    // text is shown as it was logged; in TSL the speaker's name is the part
    // parsed as a message line.
    for (const MessageLog::DialogEntry &entry : _game.messageLog().dialogEntries()) {
        gui::ListBox::Item item;
        const std::string speaker = _game.isTSL() ? getTslMessageLine(_game, entry.speaker) : entry.speaker;
        item.text = speaker.empty() ? entry.text : speaker + ": " + entry.text;
        _controls.LB_DIALOG->addItem(std::move(item));
    }
    _controls.LB_DIALOG->scrollToBottom();

    for (const MessageLog::Entry &entry : _game.messageLog().entries()) {
        if ((entry.type & MessageLog::kFeedbackMessageType) == 0) {
            continue;
        }

        gui::ListBox::Item item;
        // Lines are resolved when they are written; K1 lists them as written.
        item.text = _game.isTSL() ? getTslMessageLine(_game, entry.text) : entry.text;
        item.textColor = entry.style == MessageLog::Style::Combat
                             ? kCombatColor
                             : (_game.isTSL() ? kTSLFeedbackColor : kFeedbackColor);
        // The list follows the line's buffer; the colour follows its highlight flag.
        if (_game.isTSL() && entry.buffer == MessageLog::Buffer::Combat) {
            _controls.LB_COMBAT->addItem(std::move(item));
        } else {
            _controls.LB_MESSAGES->addItem(std::move(item));
        }
    }
    _controls.LB_MESSAGES->scrollToBottom();
    if (_game.isTSL()) {
        _controls.LB_COMBAT->scrollToBottom();
        refreshFilterVisibility();
        return;
    }

    if (_showingFeedback) {
        showFeedbackMessages();
    } else {
        showDialogMessages();
    }
}

void MessagesMenu::showDialogMessages() {
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
    _controls.LB_DIALOG->setVisible(false);
    _controls.LB_MESSAGES->setVisible(true);
    _controls.LBL_MESSAGES->setTextMessage(
        _services.resource.strings.getText(kStrRefMessages) + " - " +
        _services.resource.strings.getText(kStrRefFeedback));
    _controls.BTN_SHOW->setTextMessage(
        _services.resource.strings.getText(kStrRefShowDialog));
    _showingFeedback = true;
}

void MessagesMenu::toggleMessages() {
    if (_showingFeedback) {
        showDialogMessages();
    } else {
        showFeedbackMessages();
    }
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

    updateK2FilterButton(_controls.BTN_DIALOG, dialog);
    updateK2FilterButton(_controls.BTN_FEEDBACK, feedback);
    updateK2FilterButton(_controls.BTN_COMBAT, combat);
    updateK2FilterButton(_controls.BTN_EFFECTS, effects);

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
