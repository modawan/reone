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

    _rootExtent = root.extent();
    _listExtent = _controls.LB_MESSAGE->extent();
    _okExtent = _controls.BTN_OK->extent();
    _cancelExtent = _controls.BTN_CANCEL->extent();
    if (auto scrollBar = _controls.LB_MESSAGE->scrollBarOrNull()) {
        _scrollBarExtent = scrollBar->extent();
    }
    _iconExtent = _icon->extent();
}

void ConfirmPopup::show(const std::string &message, std::shared_ptr<graphics::Texture> icon,
                        std::function<void()> onConfirm) {
    _onConfirm = std::move(onConfirm);
    _onCancel = {};
    _controls.BTN_CANCEL->setVisible(false);
    _controls.BTN_OK->setTextMessage(_defaultConfirmText);
    bool hasIcon = static_cast<bool>(icon);
    if (hasIcon) {
        _icon->setBorderFill(std::move(icon));
    }
    _icon->setVisible(hasIcon);

    // A message box resolves the tokens of whatever message it is given.
    layoutMessage(_game.substituteCustomTokens(message), hasIcon);

    _visible = true;
}

void ConfirmPopup::layoutMessage(const std::string &text, bool hasIcon) {
    auto &root = _gui->rootControl();
    auto &list = *_controls.LB_MESSAGE;
    auto scrollBar = list.scrollBarOrNull();
    const float scale = _gui->scale();
    const auto scaled = [scale](int value) { return static_cast<int>(std::lround(value * scale)); };

    auto rootExtent = _rootExtent;
    auto listExtent = _listExtent;
    auto okExtent = _okExtent;
    auto cancelExtent = _cancelExtent;
    auto iconExtent = _iconExtent;
    // The scroll bar keeps its right edge at its own width.
    auto barExtent = _scrollBarExtent;
    const int barWidth = scaled(kScrollBarWidth);
    barExtent.left += barExtent.width - barWidth;
    barExtent.width = barWidth;
    // A gutter for the icon, if any, is kept left of the text.
    const int gutter = hasIcon ? _iconSize + _iconPadding : 0;
    auto textExtent = _messageExtent;
    textExtent.left += gutter;

    // The message is laid out a line to a row across the list's inner width,
    // less the scroll bar while it is enabled.
    const auto wrap = [&]() {
        root.setExtent(rootExtent);
        list.setExtent(listExtent);
        if (scrollBar) {
            scrollBar->setExtent(barExtent);
        }
        textExtent.width = std::max(0, list.getItemWidth() - gutter);
        list.protoItem().setExtent(textExtent);
        list.clearItems();
        list.addTextLinesAsItems(text);
    };
    const auto overflows = [&list]() { return list.getItemCount() > list.visibleItemCount(); };

    // The text is first wrapped clear of the scroll bar, which is then hidden
    // without wrapping the text again.
    list.setScrollBarEnabled(true);
    wrap();
    list.setScrollBarEnabled(false);
    if (overflows()) {
        const auto &proto = list.protoItem();
        const int lineHeight = static_cast<int>(std::lround(proto.text().font->height() * proto.scale()));
        const int widen = scaled(kGrowWidth);
        const int maxWidth = scaled(kMaxWidth);
        const int maxHeight = scaled(kMaxHeight);
        while (overflows() && (rootExtent.width < maxWidth || rootExtent.height < maxHeight)) {
            if (rootExtent.height >= scaled(kWidenMinHeight) && rootExtent.width < maxWidth) {
                // The box widens about its centre.
                rootExtent.left -= widen / 2;
                rootExtent.width += widen;
                listExtent.left -= widen / 2;
                listExtent.width += widen;
                textExtent.left -= widen / 2;
                iconExtent.left -= widen / 2;
                barExtent.left += widen / 2;
                wrap();
                if (!overflows()) {
                    break;
                }
            }
            if (rootExtent.height < maxHeight) {
                // The box grows a line about its centre; the buttons stay
                // below the list.
                rootExtent.top -= lineHeight / 2;
                rootExtent.height += lineHeight;
                listExtent.top -= lineHeight / 2;
                listExtent.height += lineHeight;
                textExtent.top -= lineHeight / 2;
                iconExtent.top -= lineHeight / 2;
                barExtent.top -= lineHeight / 2;
                barExtent.height += lineHeight;
                okExtent.top += lineHeight - lineHeight / 2;
                cancelExtent.top += lineHeight - lineHeight / 2;
                wrap();
            }
        }
        // A message the largest box cannot hold scrolls, wrapped clear of the bar.
        if (overflows()) {
            list.setScrollBarEnabled(true);
            wrap();
        }
    }
    _icon->setExtent(std::move(iconExtent));
    _controls.BTN_OK->setExtent(std::move(okExtent));
    _controls.BTN_CANCEL->setExtent(std::move(cancelExtent));
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
