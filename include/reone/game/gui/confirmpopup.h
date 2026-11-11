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

#pragma once

#include <optional>
#include "reone/gui/control/button.h"
#include "reone/gui/control/listbox.h"

#include "../gui.h"

namespace reone {

namespace graphics {

class Texture;

}

namespace game {

/** The colours a message box is drawn in: its frame and button borders, and its message and button text. */
struct PopupColors {
    /** The frame's and the buttons' borders. */
    glm::vec3 border {1.0f};
    /** The message's and the buttons' text. */
    glm::vec3 text {1.0f};
};

/**
 * Centered in-game message popup laid out on the confirmation dialog's GUI,
 * dismissed with the OK button. An optional icon is shown above the message
 * text.
 */
class ConfirmPopup : public GameGUI {
public:
    ConfirmPopup(Game &game, ServicesView &services) :
        GameGUI(game, services) {
        _resRef = guiResRef("confirm");
    }

    /** Shows an OK box, in the given colours or in those it has. */
    void show(const std::string &message, std::shared_ptr<graphics::Texture> icon = nullptr,
              std::function<void()> onConfirm = {},
              const std::optional<PopupColors> &colors = std::nullopt);
    /** Shows an OK/Cancel box, in the given colours or in those it has. */
    void showConfirm(
        const std::string &message,
        std::function<void()> onConfirm,
        std::function<void()> onCancel = {},
        const std::optional<PopupColors> &colors = std::nullopt);
    /** Closes the box; in TSL a box shown closes in the interface green and the highlight colour. */
    void hide();
    /** Close as its key would: cancelled when it offers Cancel, otherwise confirmed. */
    void close();
    /** Relabel OK until the next show; the box is laid out again for the new label. */
    void setConfirmText(const std::string &text);

    bool isVisible() const { return _visible; }

private:
    struct Controls {
        std::shared_ptr<gui::Button> BTN_CANCEL;
        std::shared_ptr<gui::Button> BTN_OK;
        std::shared_ptr<gui::ListBox> LB_MESSAGE;
    };

    Controls _controls;
    bool _visible {false};
    std::function<void()> _onConfirm;
    std::function<void()> _onCancel;
    std::string _defaultConfirmText;

    std::shared_ptr<gui::Control> _icon;
    /** The message shown, its tokens resolved. */
    std::string _message;
    /** Whether an icon is shown above the message. */
    bool _hasIcon {false};

    // The box as loaded; every message is laid out again from it.
    gui::Control::Extent _rootExtent;
    gui::Control::Extent _listExtent;
    gui::Control::Extent _messageExtent;
    gui::Control::Extent _okExtent;
    gui::Control::Extent _cancelExtent;
    gui::Control::Extent _scrollBarExtent;

    void preload(gui::IGUI &gui) override;
    void onGUILoaded() override;
    /** Draws the frame and the buttons' borders, and the message's and the buttons' text, in the given colours. */
    void applyColors(const PopupColors &colors);

    /**
     * Shows a message, with OK and, when asked for, Cancel.
     *
     * @param message the message; its tokens are resolved
     * @param icon the icon shown above the message, if any
     * @param allowCancel whether Cancel is offered
     * @param colors the colours the box is drawn in; none for those it has
     */
    void open(const std::string &message, std::shared_ptr<graphics::Texture> icon, bool allowCancel,
              const std::optional<PopupColors> &colors);

    /**
     * Lays the message out from the box as loaded. An icon makes the box
     * taller and the box loses the Cancel row when Cancel is not offered. The
     * buttons are sized to their labels and the box widened to hold them. A
     * message that does not fit grows the box, wider and taller, up to a
     * limit; only a message that still does not fit then shows the scroll
     * bar, its text wrapped clear of it. The icon, OK and Cancel are then
     * stacked, centred, from the top and below the message.
     */
    void layoutMessage();

    /**
     * The width a message box button takes for its label: a minimum width
     * grown in steps until the label fits on one line inside the border, plus
     * the border on both sides.
     */
    int buttonWidth(const gui::Button &button) const;

    void bindControls() {
        _controls.BTN_CANCEL = findControl<gui::Button>("BTN_CANCEL");
        _controls.BTN_OK = findControl<gui::Button>("BTN_OK");
        _controls.LB_MESSAGE = findControl<gui::ListBox>("LB_MESSAGE");
    }
};

} // namespace game

} // namespace reone
