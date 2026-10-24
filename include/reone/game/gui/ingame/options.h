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

#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"
#include "reone/gui/control/listbox.h"
#include "reone/gui/control/togglebutton.h"

#include "../../gui.h"
#include "../../menupresentation.h"

namespace reone {

namespace gui {

class Button;

}

namespace game {

class OptionsMenu : public GameGUI {
public:
    OptionsMenu(Game &game, ServicesView &services) :
        GameGUI(game, services) {
        _resRef = guiResRef("optionsingame");
    }

    bool handle(const input::Event &event) override;
    void update(float dt) override;
    void render() override;
    /** Leaving the options writes them to the configuration. */
    void clearSelection() override;

private:
    struct Controls {
        std::shared_ptr<gui::Button> BTN_AUTOPAUSE;
        std::shared_ptr<gui::Button> BTN_EXIT;
        std::shared_ptr<gui::Button> BTN_FEEDBACK;
        std::shared_ptr<gui::Button> BTN_GAMEPLAY;
        std::shared_ptr<gui::Button> BTN_GRAPHICS;
        std::shared_ptr<gui::Button> BTN_LOADGAME;
        std::shared_ptr<gui::Button> BTN_QUIT;
        std::shared_ptr<gui::Button> BTN_SAVEGAME;
        std::shared_ptr<gui::Button> BTN_SOUND;
        std::shared_ptr<gui::Label> LBL_BAR1;
        std::shared_ptr<gui::Label> LBL_BAR2;
        std::shared_ptr<gui::Label> LBL_BAR3;
        std::shared_ptr<gui::Label> LBL_BAR4;
        std::shared_ptr<gui::Label> LBL_BAR5;
        std::shared_ptr<gui::Label> LBL_TITLE;
        std::shared_ptr<gui::ListBox> LB_DESC;
    };

    /** The autopause options panel, opened over the menu from BTN_AUTOPAUSE. */
    struct AutoPauseControls {
        std::shared_ptr<gui::Button> BTN_BACK;
        std::shared_ptr<gui::Button> BTN_DEFAULT;
        std::shared_ptr<gui::ToggleButton> CB_ACTIONMENU;
        std::shared_ptr<gui::ToggleButton> CB_ENDROUND;
        std::shared_ptr<gui::ToggleButton> CB_ENEMYSIGHTED;
        std::shared_ptr<gui::ToggleButton> CB_MINESIGHTED;
        std::shared_ptr<gui::ToggleButton> CB_PARTYKILLED;
        std::shared_ptr<gui::ToggleButton> CB_TRIGGERS;
        std::shared_ptr<gui::ListBox> LB_DETAILS;
    };

    /** The feedback options panel, opened over the menu from BTN_FEEDBACK. Its rows are built in code, one per option. */
    struct FeedbackControls {
        std::shared_ptr<gui::Button> BTN_BACK;
        std::shared_ptr<gui::Button> BTN_DEFAULT;
        std::shared_ptr<gui::Label> LBL_BAR1;
        std::shared_ptr<gui::Label> LBL_BAR2;
        std::shared_ptr<gui::Label> LBL_TITLE;
        std::shared_ptr<gui::ListBox> LB_DESC;
        std::shared_ptr<gui::ListBox> LB_OPTIONS;
    };

    /** A row of the feedback options panel: the option it flips, its name and its description. */
    struct FeedbackRow {
        bool FeedbackOptions::*option {nullptr};
        int nameStrRef {0};
        int descriptionStrRef {0};
    };

    Controls _controls;

    std::shared_ptr<gui::IGUI> _autoPauseGUI;
    AutoPauseControls _autoPauseControls;
    bool _autoPauseOpen {false};

    std::shared_ptr<gui::IGUI> _feedbackGUI;
    FeedbackControls _feedbackControls;
    std::vector<FeedbackRow> _feedbackRows;
    bool _feedbackOpen {false};
    /** The row whose description the panel shows, or -1 before any. */
    int _feedbackDescribedRow {-1};

    void onGUILoaded() override;

    /** Load the feedback options panel and wire its rows and buttons. */
    void loadFeedbackPanel();
    /** Open the feedback panel on the current options, describing its first row. */
    void openFeedbackPanel();
    void closeFeedbackPanel();
    /** List one row per feedback option, checked when the option is on. */
    void showFeedbackOptions();
    void showFeedbackDescription(int row);

    /** Load the autopause options panel and wire its checkboxes and buttons. */
    void loadAutoPausePanel();
    /** Open the autopause panel on the current options, describing its first checkbox. */
    void openAutoPausePanel();
    /** Close the autopause panel; its checkbox states become the current options. */
    void closeAutoPausePanel();
    void showAutoPauseOptions(const AutoPauseOptions &options);
    void showAutoPauseDescription(int strRef);

    void bindControls() {
        _controls.BTN_AUTOPAUSE = findControl<gui::Button>("BTN_AUTOPAUSE");
        _controls.BTN_EXIT = findControl<gui::Button>("BTN_EXIT");
        _controls.BTN_FEEDBACK = findControl<gui::Button>("BTN_FEEDBACK");
        _controls.BTN_GAMEPLAY = findControl<gui::Button>("BTN_GAMEPLAY");
        _controls.BTN_GRAPHICS = findControl<gui::Button>("BTN_GRAPHICS");
        _controls.BTN_LOADGAME = findControl<gui::Button>("BTN_LOADGAME");
        _controls.BTN_QUIT = findControl<gui::Button>("BTN_QUIT");
        _controls.BTN_SAVEGAME = findControl<gui::Button>("BTN_SAVEGAME");
        _controls.BTN_SOUND = findControl<gui::Button>("BTN_SOUND");
        _controls.LBL_BAR1 = findControl<gui::Label>("LBL_BAR1");
        _controls.LBL_BAR2 = findControl<gui::Label>("LBL_BAR2");
        _controls.LBL_BAR3 = findControl<gui::Label>("LBL_BAR3");
        _controls.LBL_BAR4 = findControl<gui::Label>("LBL_BAR4");
        _controls.LBL_BAR5 = findControl<gui::Label>("LBL_BAR5");
        _controls.LBL_TITLE = findControl<gui::Label>("LBL_TITLE");
        _controls.LB_DESC = findControl<gui::ListBox>("LB_DESC");
    }
};

} // namespace game

} // namespace reone
