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

class Font;
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
        ConfirmPopup(game, services, false) {}

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
    /**
     * Shows an OK box with pages, in the colours it has. OK, and closing the
     * box as its key would, first turn to the next page, which replaces the
     * message while the box stays open; the box closes and confirms only when
     * no page follows.
     *
     * @param message the first page
     * @param icon the icon shown above every page, if any
     * @param nextPage turns to the next page; false when none follows
     * @param onConfirm called when the box closes
     */
    void showPages(const std::string &message, std::shared_ptr<graphics::Texture> icon,
                   std::function<bool()> nextPage, std::function<void()> onConfirm = {});
    /**
     * Shows an OK box with a datapad's text, in the colours it has, aligned
     * left in the datapad font. The message takes the box's own font and
     * alignment again when the box closes.
     */
    void showDatapad(const std::string &message, std::function<void()> onConfirm);
    /** Replaces the message of the box shown, which stays open; its tokens are resolved. */
    void setMessage(const std::string &message);
    /** Shows \p icon above the message, in place of any icon shown; the box is laid out again. */
    void setIcon(std::shared_ptr<graphics::Texture> icon);
    /** Sets the font the message is drawn in, from the next message laid out; the box's own font returns when it closes. */
    void setMessageFont(std::shared_ptr<graphics::Font> font);
    /**
     * Closes the box; in TSL a box shown closes in the interface green and the
     * highlight colour. The message takes the box's own font and alignment again.
     */
    void hide();
    /** Close as its key would: a box with pages turns to its next page; otherwise cancelled when it offers Cancel, or confirmed. */
    void close();
    /**
     * The box shown gives way to another: one offering Cancel is cancelled,
     * as its question goes unanswered; an OK box closes without confirming.
     */
    void withdraw();
    /** Relabel OK until the next show; the box is laid out again for the new label. */
    void setConfirmText(const std::string &text);

    bool isVisible() const { return _visible; }

protected:
    /** @param datapad whether the box is TSL's datapad box, on the datapad's layout */
    ConfirmPopup(Game &game, ServicesView &services, bool datapad) :
        GameGUI(game, services),
        _datapad(datapad) {
        _resRef = guiResRef(datapad ? "datapad" : "confirm");
    }

private:
    struct Controls {
        std::shared_ptr<gui::Button> BTN_CANCEL;
        std::shared_ptr<gui::Button> BTN_OK;
        std::shared_ptr<gui::ListBox> LB_MESSAGE;
    };

    Controls _controls;
    bool _datapad {false};
    bool _visible {false};
    std::function<void()> _onConfirm;
    std::function<void()> _onCancel;
    /** Turns a box with pages to its next page; false when none follows. */
    std::function<bool()> _nextPage;
    std::string _defaultConfirmText;

    std::shared_ptr<gui::Control> _icon;
    /** The message shown, its tokens resolved. */
    std::string _message;
    /** The message's font and alignment as the box was loaded. */
    std::shared_ptr<graphics::Font> _messageFont;
    gui::Control::TextAlign _messageAlign {gui::Control::TextAlign::CenterTop};
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
     * Lays the message out in the datapad box, which is of a set size,
     * centred across the screen with its top where its layout puts it; the
     * message and OK keep set places in it. Only a message that does not fit
     * shows the scroll bar, its text wrapped clear of it.
     */
    void layoutDatapadMessage();

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

/**
 * TSL's datapad box: a message box on the datapad's layout, its frame drawn
 * over the datapad's background, which TSL shows a datapad's text in.
 */
class DatapadBox : public ConfirmPopup {
public:
    DatapadBox(Game &game, ServicesView &services) :
        ConfirmPopup(game, services, true) {}
};

} // namespace game

} // namespace reone
