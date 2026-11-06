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

// Message box layout, in authored units. An icon sits kIconTop from the top,
// centred, and makes the box kIconSize taller with the message below it.
// Without Cancel the box loses the Cancel row and the gap above it,
// kButtonGap. A button starts kButtonMinWidth wide and grows by
// kButtonWidthStep until its label fits on one line inside its border. OK
// sits kOkGap below the message and Cancel kButtonGap below OK, both centred.
static constexpr int kIconSize = 32;
static constexpr int kIconTop = 10;
static constexpr int kButtonGap = 2;
static constexpr int kButtonMinWidth = 100;
static constexpr int kButtonWidthStep = 10;
static constexpr int kOkGap = 4;

// Message box growth, in authored units. A message that overflows widens the
// box by kGrowWidth, while the box is at least kWidenMinHeight tall and
// narrower than kMaxWidth, and heightens it by a line while it is lower than
// kMaxHeight. The scroll bar is kScrollBarWidth wide.
static constexpr int kGrowWidth = 40;
static constexpr int kWidenMinHeight = 161;
static constexpr int kMaxWidth = 440;
static constexpr int kMaxHeight = 280;
static constexpr int kScrollBarWidth = 16;

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
    // The message is one block of text that scrolls a line at a time.
    _controls.LB_MESSAGE->setTextLineRows(true);

    // The box as authored, centred in the authored canvas so the Scaled
    // layout lands it mid-screen at any resolution. Every message is laid
    // out from it.
    centerRootInCanvas(kAuthoredCanvasWidth, kAuthoredCanvasHeight);

    const int iconSize = static_cast<int>(std::lround(kIconSize * _gui->scale()));
    auto icon = _gui->newControl(ControlType::Label, kIconControlTag);
    Control::Extent iconExtent;
    iconExtent.width = iconSize;
    iconExtent.height = iconSize;
    icon->setExtent(std::move(iconExtent));
    icon->setSharpenBorderFillAlpha(true);
    icon->setVisible(false);
    _icon = std::shared_ptr<Control>(std::move(icon));
    _gui->addControlToFront(_icon, IGUI::ControlCoordinates::Screen);

    _rootExtent = _gui->rootControl().extent();
    _listExtent = _controls.LB_MESSAGE->extent();
    _messageExtent = _controls.LB_MESSAGE->protoItem().extent();
    _okExtent = _controls.BTN_OK->extent();
    _cancelExtent = _controls.BTN_CANCEL->extent();
    if (auto scrollBar = _controls.LB_MESSAGE->scrollBarOrNull()) {
        _scrollBarExtent = scrollBar->extent();
    }
}

void ConfirmPopup::show(const std::string &message, std::shared_ptr<graphics::Texture> icon,
                        std::function<void()> onConfirm) {
    open(message, std::move(icon), false);
    _onConfirm = std::move(onConfirm);
}

void ConfirmPopup::open(const std::string &message, std::shared_ptr<graphics::Texture> icon, bool allowCancel) {
    _onConfirm = {};
    _onCancel = {};
    _controls.BTN_CANCEL->setVisible(allowCancel);
    _controls.BTN_OK->setTextMessage(_defaultConfirmText);
    _hasIcon = static_cast<bool>(icon);
    if (_hasIcon) {
        _icon->setBorderFill(std::move(icon));
    }
    _icon->setVisible(_hasIcon);

    // A message box resolves the tokens of whatever message it is given.
    _message = _game.substituteCustomTokens(message);
    layoutMessage();

    _visible = true;
}

int ConfirmPopup::buttonWidth(const Button &button) const {
    const float scale = _gui->scale();
    const auto &text = button.text();
    const int border = button.border().dimension;
    const float textWidth = text.font->measure(text.text, button.scale());
    int width = static_cast<int>(std::lround(kButtonMinWidth * scale));
    const int step = static_cast<int>(std::lround(kButtonWidthStep * scale));
    while (width - 2 * border < textWidth) {
        width += step;
    }
    return width + 2 * border;
}

void ConfirmPopup::layoutMessage() {
    auto &root = _gui->rootControl();
    auto &list = *_controls.LB_MESSAGE;
    auto &ok = *_controls.BTN_OK;
    auto &cancel = *_controls.BTN_CANCEL;
    auto scrollBar = list.scrollBarOrNull();
    const bool allowCancel = cancel.isVisible();
    const float scale = _gui->scale();
    const auto scaled = [scale](int value) { return static_cast<int>(std::lround(value * scale)); };

    // The box and its controls are laid out from the box's top-left corner,
    // where its controls were authored, and the box is centred where the
    // authored box was centred.
    int centreX = 0;
    int centreY = 0;
    _rootExtent.getCenter(centreX, centreY);
    Control::Extent panel {0, 0, _rootExtent.width, _rootExtent.height};
    auto listExtent = _listExtent;
    auto okExtent = _okExtent;
    auto cancelExtent = _cancelExtent;

    // An icon makes the box taller and moves the message below it. Without
    // Cancel its row goes.
    if (_hasIcon) {
        panel.height += scaled(kIconSize);
        listExtent.top += scaled(kIconSize);
    }
    if (!allowCancel) {
        panel.height -= _cancelExtent.height + scaled(kButtonGap);
    }

    // Each shown button is as wide as its label needs, and the box is at
    // least as wide; the message widens with the box. TSL gives both buttons
    // the wider width.
    okExtent.width = buttonWidth(ok);
    panel.width = std::max(panel.width, okExtent.width - scaled(kButtonGap));
    if (allowCancel) {
        cancelExtent.width = buttonWidth(cancel);
        panel.width = std::max(panel.width, cancelExtent.width - scaled(kButtonGap));
        if (_game.isTSL()) {
            // The narrower takes the wider width; a wider OK gives Cancel its
            // height too.
            if (okExtent.width > cancelExtent.width) {
                cancelExtent.width = okExtent.width;
                cancelExtent.height = okExtent.height;
            } else {
                okExtent.width = cancelExtent.width;
            }
        }
    }
    listExtent.width += panel.width - _rootExtent.width;

    // The scroll bar keeps its place at the message's right edge, at its own
    // width.
    const int barWidth = scaled(kScrollBarWidth);
    const int barRightInset = (_listExtent.left + _listExtent.width) - (_scrollBarExtent.left + _scrollBarExtent.width);
    const int barTopOffset = _scrollBarExtent.top - _listExtent.top;
    const int barHeightOffset = _scrollBarExtent.height - _listExtent.height;
    const auto &proto = list.protoItem();
    const int lineHeight = static_cast<int>(std::lround(proto.text().font->height() * proto.scale()));

    // Place the box and its message on screen and lay the message out across
    // the list's inner width, less the scroll bar while it is enabled, a line
    // to a row; the message overflows when its lines and the list's padding
    // are taller than the list inside its border. The box's controls are
    // drawn from the corner the box had when it was loaded, so they move by
    // as much as the box does.
    const auto onScreen = [&](Control::Extent extent) {
        extent.left += centreX - panel.width / 2;
        extent.top += centreY - panel.height / 2;
        return extent;
    };
    const auto inBox = [&](Control::Extent extent) {
        extent.left += centreX - panel.width / 2 - _rootExtent.left;
        extent.top += centreY - panel.height / 2 - _rootExtent.top;
        return extent;
    };
    const auto wrap = [&]() {
        root.setExtent(onScreen(panel));
        const auto listInBox = inBox(listExtent);
        list.setExtent(listInBox);
        if (scrollBar) {
            Control::Extent bar;
            bar.left = listInBox.left + listInBox.width - barRightInset - barWidth;
            bar.top = listInBox.top + barTopOffset;
            bar.width = barWidth;
            bar.height = listInBox.height + barHeightOffset;
            scrollBar->setExtent(std::move(bar));
        }
        // The list places the lines itself; the row is a line high and as
        // wide as the room.
        auto textExtent = _messageExtent;
        textExtent.left = listInBox.left;
        textExtent.top = listInBox.top;
        textExtent.width = std::max(0, list.getItemWidth());
        textExtent.height = lineHeight;
        list.protoItem().setExtent(std::move(textExtent));
        list.clearItems();
        list.addTextLinesAsItems(_message);
    };
    const auto overflows = [&list]() { return list.isOverflowing(); };

    // The text is first wrapped clear of the scroll bar, which is then hidden
    // without wrapping the text again.
    list.setScrollBarEnabled(true);
    wrap();
    list.setScrollBarEnabled(false);
    if (overflows()) {
        const int widen = scaled(kGrowWidth);
        const int maxWidth = scaled(kMaxWidth);
        const int maxHeight = scaled(kMaxHeight);
        while (overflows() && (panel.width < maxWidth || panel.height < maxHeight)) {
            if (panel.height >= scaled(kWidenMinHeight) && panel.width < maxWidth) {
                // The box widens about its centre.
                panel.width += widen;
                listExtent.width += widen;
                wrap();
                if (!overflows()) {
                    break;
                }
            }
            if (panel.height < maxHeight) {
                // The box grows a line about its centre.
                panel.height += lineHeight;
                listExtent.height += lineHeight;
                wrap();
            }
        }
        // A message the largest box cannot hold scrolls, wrapped clear of the bar.
        if (overflows()) {
            list.setScrollBarEnabled(true);
            wrap();
        }
    }

    // The icon is centred at the top; OK sits under the message and Cancel
    // under OK, both centred.
    Control::Extent iconExtent = _icon->extent();
    iconExtent.left = panel.width / 2 - iconExtent.width / 2;
    iconExtent.top = scaled(kIconTop);
    _icon->setExtent(inBox(iconExtent));
    okExtent.top = listExtent.top + listExtent.height + scaled(kOkGap);
    okExtent.left = panel.width / 2 - okExtent.width / 2;
    ok.setExtent(inBox(okExtent));
    cancelExtent.top = okExtent.top + okExtent.height + scaled(kButtonGap);
    cancelExtent.left = panel.width / 2 - cancelExtent.width / 2;
    cancel.setExtent(inBox(cancelExtent));
}

void ConfirmPopup::showConfirm(
    const std::string &message,
    std::function<void()> onConfirm,
    std::function<void()> onCancel) {
    open(message, nullptr, true);
    _onConfirm = std::move(onConfirm);
    _onCancel = std::move(onCancel);
}

// OK is sized to its label, so the box is laid out again.
void ConfirmPopup::setConfirmText(const std::string &text) {
    _controls.BTN_OK->setTextMessage(text);
    layoutMessage();
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
