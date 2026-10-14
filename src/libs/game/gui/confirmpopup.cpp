/*
 * Copyright (c) 2026 The reone project contributors
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

#include "reone/game/gui/confirmpopup.h"

#include "reone/game/game.h"

#include <algorithm>
#include <cmath>

using namespace reone::gui;

namespace reone {

namespace game {

static const char kIconControlTag[] = "LBL_POPUPICON";

static constexpr int kAuthoredCanvasWidth = 640;
static constexpr int kAuthoredCanvasHeight = 480;
static constexpr int kIconSize = 32;
static constexpr int kIconPadding = 6;

void ConfirmPopup::preload(IGUI &gui) {
    GameGUI::preload(gui);

    // The confirmation dialog is authored for 640x480 in both games. It
    // scales and centers with the game-wide Scaled default from the base
    // preload; an explicit Center here kept it unscaled.
    gui.setResolution(kAuthoredCanvasWidth, kAuthoredCanvasHeight);
}

void ConfirmPopup::onGUILoaded() {
    bindControls();

    _controls.BTN_CANCEL->setOnClick([this]() {
        auto callback = std::move(_onCancel);
        hide();
        if (callback) callback();
    });
    _controls.BTN_OK->setOnClick([this]() {
        auto callback = std::move(_onConfirm);
        hide();
        if (callback) callback();
    });
    _defaultConfirmText = _controls.BTN_OK->text().text;
    _controls.LB_MESSAGE->setItemsInteractive(false);

    // The authored confirmation panel reserves a second button row. This
    // popup only exposes OK, so retain the top inset below that button instead
    // of leaving the unused row as a large empty footer, and center the
    // trimmed panel in the authored canvas so the Scaled layout lands it
    // mid-screen at any resolution.
    auto &root = _gui->rootControl();
    auto rootAuthored = root.authoredExtent();
    const auto &okAuthored = _controls.BTN_OK->authoredExtent();
    const auto &messageAuthored = _controls.LB_MESSAGE->authoredExtent();
    int topInset = std::max(0, std::min(messageAuthored.top, okAuthored.top));
    rootAuthored.height = std::min(rootAuthored.height, okAuthored.top + okAuthored.height + topInset);
    root.setAuthoredExtent(std::move(rootAuthored));
    centerRootInCanvas(kAuthoredCanvasWidth, kAuthoredCanvasHeight);

    _messageExtent = _controls.LB_MESSAGE->protoItem().extent();
    _iconSize = std::max(1, static_cast<int>(std::lround(kIconSize * _gui->scale())));
    _iconPadding = std::max(1, static_cast<int>(std::lround(kIconPadding * _gui->scale())));

    auto icon = _gui->newControl(ControlType::Label, kIconControlTag);
    Control::Extent iconExtent;
    iconExtent.left = _messageExtent.left;
    iconExtent.top = _messageExtent.top;
    iconExtent.width = _iconSize;
    iconExtent.height = _iconSize;
    icon->setExtent(std::move(iconExtent));
    icon->setSharpenBorderFillAlpha(true);
    icon->setVisible(false);
    _icon = std::shared_ptr<Control>(std::move(icon));
    _gui->addControlToFront(_icon, IGUI::ControlCoordinates::Screen);
}

void ConfirmPopup::show(const std::string &message, std::shared_ptr<graphics::Texture> icon,
                        std::function<void()> onConfirm) {
    _onConfirm = std::move(onConfirm);
    _onCancel = {};
    _controls.BTN_CANCEL->setVisible(false);
    _controls.BTN_OK->setTextMessage(_defaultConfirmText);
    // Reserve a gutter for the icon, if any, before the message is broken
    // into lines.
    bool hasIcon = static_cast<bool>(icon);
    Control::Extent textExtent(_messageExtent);
    if (hasIcon) {
        int gutter = _iconSize + _iconPadding;
        textExtent.left += gutter;
        textExtent.width = std::max(0, textExtent.width - gutter);
        _icon->setBorderFill(std::move(icon));
    }
    _icon->setVisible(hasIcon);
    _controls.LB_MESSAGE->protoItem().setExtent(std::move(textExtent));

    // A message box resolves the tokens of whatever message it is given.
    // The message is laid out a line to a row, and scrolls when it runs
    // past the list.
    _controls.LB_MESSAGE->clearItems();
    _controls.LB_MESSAGE->addTextLinesAsItems(_game.substituteCustomTokens(message));

    _visible = true;
}

void ConfirmPopup::showConfirm(
    const std::string &message,
    std::function<void()> onConfirm,
    std::function<void()> onCancel) {
    show(message);
    _onConfirm = std::move(onConfirm);
    _onCancel = std::move(onCancel);
    _controls.BTN_CANCEL->setVisible(true);
}

void ConfirmPopup::setConfirmText(const std::string &text) {
    _controls.BTN_OK->setTextMessage(text);
}

void ConfirmPopup::hide() {
    _visible = false;
    _onConfirm = {};
    _onCancel = {};
}

void ConfirmPopup::close() {
    auto callback = _controls.BTN_CANCEL->isVisible() ? std::move(_onCancel) : std::move(_onConfirm);
    hide();
    if (callback) {
        callback();
    }
}

} // namespace game

} // namespace reone
