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

using namespace reone::gui;

namespace reone {

namespace game {

static const char kIconControlTag[] = "LBL_POPUPICON";

static constexpr int kIconSize = 32;
static constexpr int kIconPadding = 6;

void ConfirmPopup::preload(IGUI &gui) {
    GameGUI::preload(gui);

    // The confirmation dialog is authored for 640x480 in both games. Center
    // it on the screen.
    gui.setResolution(640, 480);
    gui.setScaling(GUI::ScalingMode::Center);
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
    _controls.LB_MESSAGE->setItemsInteractive(false);

    _messageExtent = _controls.LB_MESSAGE->protoItem().extent();

    auto icon = _gui->newControl(ControlType::Label, kIconControlTag);
    Control::Extent iconExtent;
    iconExtent.left = _messageExtent.left;
    iconExtent.top = _messageExtent.top + (_messageExtent.height - kIconSize) / 2;
    iconExtent.width = kIconSize;
    iconExtent.height = kIconSize;
    icon->setExtent(std::move(iconExtent));
    icon->setVisible(false);
    _icon = std::shared_ptr<Control>(std::move(icon));
    _gui->addControlToFront(_icon);
}

void ConfirmPopup::show(const std::string &message, std::shared_ptr<graphics::Texture> icon) {
    _onConfirm = {};
    _onCancel = {};
    _controls.BTN_CANCEL->setVisible(false);
    // Reserve a gutter for the icon, if any, before the message is broken
    // into lines.
    bool hasIcon = static_cast<bool>(icon);
    Control::Extent textExtent(_messageExtent);
    if (hasIcon) {
        int gutter = kIconSize + kIconPadding;
        textExtent.left += gutter;
        textExtent.width = std::max(0, textExtent.width - gutter);
        _icon->setBorderFill(std::move(icon));
    }
    _icon->setVisible(hasIcon);
    _controls.LB_MESSAGE->protoItem().setExtent(std::move(textExtent));

    _controls.LB_MESSAGE->clearItems();
    _controls.LB_MESSAGE->addTextLinesAsItems(message);

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

void ConfirmPopup::hide() {
    _visible = false;
    _onConfirm = {};
    _onCancel = {};
}

} // namespace game

} // namespace reone
