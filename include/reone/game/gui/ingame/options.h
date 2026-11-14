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

#include "reone/graphics/types.h"
#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"
#include "reone/gui/control/listbox.h"
#include "reone/gui/control/slider.h"
#include "reone/gui/control/togglebutton.h"

#include "../../gui.h"
#include "../../menupresentation.h"

namespace reone {

namespace game {

/**
 * The options: a front page whose buttons open the option pages. The in-game
 * options are a tab of the in-game menu and add Load Game, Save Game and Exit
 * Game; the main menu's options are a screen of their own. A page opens over
 * the page that opened it and takes the input until it closes.
 */
class OptionsMenu : public GameGUI {
public:
    OptionsMenu(Game &game, ServicesView &services, bool inGame) :
        GameGUI(game, services),
        _inGame(inGame) {
        _resRef = guiResRef(inGame ? "optionsingame" : "optionsmain");
    }

    bool handle(const input::Event &event) override;
    void update(float dt) override;
    void render() override;
    /** Leaving the options writes them to the configuration. */
    void clearSelection() override;
    /** An open page has its own title and buttons where the in-game menu's navigation and footer are. */
    bool coversInGameMenu() const override;

private:
    struct Controls {
        std::shared_ptr<gui::Button> BTN_AUTOPAUSE;
        std::shared_ptr<gui::Button> BTN_FEEDBACK;
        std::shared_ptr<gui::Button> BTN_GAMEPLAY;
        std::shared_ptr<gui::Button> BTN_GRAPHICS;
        std::shared_ptr<gui::Button> BTN_SOUND;
        std::shared_ptr<gui::Label> LBL_BAR1;
        std::shared_ptr<gui::Label> LBL_BAR2;
        std::shared_ptr<gui::Label> LBL_TITLE;
        std::shared_ptr<gui::ListBox> LB_DESC;
    };

    /** The buttons only the in-game options have. */
    struct InGameControls {
        std::shared_ptr<gui::Button> BTN_EXIT;
        std::shared_ptr<gui::Button> BTN_LOADGAME;
        std::shared_ptr<gui::Button> BTN_QUIT;
        std::shared_ptr<gui::Button> BTN_SAVEGAME;
    };

    /** An open page, and what leaving it by Close or Escape does. */
    struct Page {
        gui::IGUI *gui {nullptr};
        std::function<void()> leave;
    };

    /** The autopause options page, opened from BTN_AUTOPAUSE. */
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

    /** The feedback options page, opened from BTN_FEEDBACK. Its rows are built in code, one per option. */
    struct FeedbackControls {
        std::shared_ptr<gui::Button> BTN_BACK;
        std::shared_ptr<gui::Button> BTN_DEFAULT;
        std::shared_ptr<gui::Label> LBL_BAR1;
        std::shared_ptr<gui::Label> LBL_BAR2;
        std::shared_ptr<gui::Label> LBL_TITLE;
        std::shared_ptr<gui::ListBox> LB_DESC;
        std::shared_ptr<gui::ListBox> LB_OPTIONS;
    };

    /** A row of the feedback options page: the option it flips, its name and its description. */
    struct FeedbackRow {
        bool FeedbackOptions::*option {nullptr};
        int nameStrRef {0};
        int descriptionStrRef {0};
    };

    /**
     * The gameplay options page, opened from BTN_GAMEPLAY. The difficulty
     * level, mouse look and combat movement are handled, and BTN_MOUSE opens
     * the mouse settings; the other options are drawn as laid out.
     */
    struct GameplayControls {
        std::shared_ptr<gui::Button> BTN_BACK;
        std::shared_ptr<gui::Button> BTN_DEFAULT;
        std::shared_ptr<gui::Button> BTN_DIFFICULTY;
        std::shared_ptr<gui::Button> BTN_DIFFLEFT;
        std::shared_ptr<gui::Button> BTN_DIFFRIGHT;
        std::shared_ptr<gui::Button> BTN_MOUSE;
        std::shared_ptr<gui::ToggleButton> CB_DISABLEMOVE;
        std::shared_ptr<gui::ToggleButton> CB_INVERTCAM;
        std::shared_ptr<gui::Label> LBL_TITLE;
        std::shared_ptr<gui::ListBox> LB_DESC;
    };

    /**
     * The graphics options page, opened from BTN_GRAPHICS. Shadows and grass
     * are handled; the brightness slider is shown but cannot be moved, and
     * the other controls are drawn as laid out.
     */
    struct GraphicsControls {
        std::shared_ptr<gui::Button> BTN_ADVANCED;
        std::shared_ptr<gui::Button> BTN_BACK;
        std::shared_ptr<gui::Button> BTN_DEFAULT;
        std::shared_ptr<gui::Button> BTN_RESOLUTION;
        std::shared_ptr<gui::ToggleButton> CB_GRASS;
        std::shared_ptr<gui::ToggleButton> CB_SHADOWS;
        std::shared_ptr<gui::ListBox> LB_DESC;
        std::shared_ptr<gui::Slider> SLI_GAMMA;
    };

    /**
     * The screen resolution list, opened from the graphics page's
     * BTN_RESOLUTION: the display modes the game can start in. OK keeps the
     * chosen one for the next start; Cancel and Escape leave the list.
     */
    struct ResolutionControls {
        std::shared_ptr<gui::Button> BTN_CANCEL;
        std::shared_ptr<gui::Button> BTN_OK;
        std::shared_ptr<gui::ListBox> LB_RESOLUTIONS;
    };

    /**
     * The advanced graphics options page, opened from the graphics page's
     * BTN_ADVANCED, with an Interface Size slider below its own rows. Its
     * changes wait for OK (BTN_BACK); Cancel and Escape drop them. OK applies
     * Anti-aliasing, Soft Shadows and V-Sync at once, and keeps Texture
     * Quality, Anisotropy and Interface Size for the next start. Frame Buffer
     * Effects is drawn as laid out.
     */
    struct AdvancedGraphicsControls {
        std::shared_ptr<gui::Button> BTN_ANISOTROPY;
        std::shared_ptr<gui::Button> BTN_ANISOTROPYLEFT;
        std::shared_ptr<gui::Button> BTN_ANISOTROPYRIGHT;
        std::shared_ptr<gui::Button> BTN_ANTIALIAS;
        std::shared_ptr<gui::Button> BTN_ANTIALIASLEFT;
        std::shared_ptr<gui::Button> BTN_ANTIALIASRIGHT;
        std::shared_ptr<gui::Button> BTN_BACK;
        std::shared_ptr<gui::Button> BTN_CANCEL;
        std::shared_ptr<gui::Button> BTN_DEFAULT;
        std::shared_ptr<gui::Button> BTN_TEXQUAL;
        std::shared_ptr<gui::Button> BTN_TEXQUALLEFT;
        std::shared_ptr<gui::Button> BTN_TEXQUALRIGHT;
        std::shared_ptr<gui::ToggleButton> CB_SOFTSHADOWS;
        std::shared_ptr<gui::ToggleButton> CB_VSYNC;
        std::shared_ptr<gui::ListBox> LB_DESC;
        /** The interface size label and slider, which the page's layout lacks. */
        std::shared_ptr<gui::Label> LBL_GUISCALE;
        std::shared_ptr<gui::Slider> SLI_GUISCALE;
    };

    /** The advanced graphics page's choices as it shows them, until OK keeps them. */
    struct AdvancedGraphicsChoices {
        bool fxaa {false};
        bool softShadows {false};
        bool vsync {false};
        /** Texture Quality, Anisotropy and Interface Size, with the display mode the next start keeps. */
        StartGraphicsOptions start;
    };

    /** The sound options page, opened from BTN_SOUND: a volume slider for each kind of audio. */
    struct SoundControls {
        std::shared_ptr<gui::Button> BTN_ADVANCED;
        std::shared_ptr<gui::Button> BTN_BACK;
        std::shared_ptr<gui::Button> BTN_DEFAULT;
        std::shared_ptr<gui::ListBox> LB_DESC;
        std::shared_ptr<gui::Slider> SLI_FX;
        std::shared_ptr<gui::Slider> SLI_MOVIE;
        std::shared_ptr<gui::Slider> SLI_MUSIC;
        std::shared_ptr<gui::Slider> SLI_VO;
    };

    /**
     * KotOR's advanced sound options page, opened from the sound page's
     * BTN_ADVANCED. Its changes wait for OK (BTN_BACK); Cancel and Escape
     * drop them.
     */
    struct AdvancedSoundControls {
        std::shared_ptr<gui::Button> BTN_BACK;
        std::shared_ptr<gui::Button> BTN_CANCEL;
        std::shared_ptr<gui::Button> BTN_DEFAULT;
        std::shared_ptr<gui::Button> BTN_EAX;
        std::shared_ptr<gui::ToggleButton> CB_FORCESOFTWARE;
        std::shared_ptr<gui::ListBox> LB_DESC;
    };

    /**
     * The mouse settings page, opened from the gameplay page's BTN_MOUSE.
     * The sensitivity is handled; reversing the buttons is drawn as laid out.
     */
    struct MouseControls {
        std::shared_ptr<gui::Button> BTN_BACK;
        std::shared_ptr<gui::Button> BTN_DEFAULT;
        std::shared_ptr<gui::ListBox> LB_DESC;
        std::shared_ptr<gui::Slider> SLI_MOUSESEN;
    };

    bool _inGame;
    Controls _controls;
    InGameControls _inGameControls;
    /** The main menu's options' Close button. */
    std::shared_ptr<gui::Button> _mainMenuBack;

    /** The open pages, the one taking the input last. */
    std::vector<Page> _pages;

    std::shared_ptr<gui::IGUI> _autoPauseGUI;
    AutoPauseControls _autoPauseControls;

    std::shared_ptr<gui::IGUI> _feedbackGUI;
    FeedbackControls _feedbackControls;
    std::vector<FeedbackRow> _feedbackRows;
    /** The row whose description the page shows, or -1 before any. */
    int _feedbackDescribedRow {-1};

    std::shared_ptr<gui::IGUI> _gameplayGUI;
    GameplayControls _gameplayControls;

    std::shared_ptr<gui::IGUI> _graphicsGUI;
    GraphicsControls _graphicsControls;

    std::shared_ptr<gui::IGUI> _resolutionGUI;
    ResolutionControls _resolutionControls;
    /** The modes the resolution list offers, by row. */
    std::vector<graphics::DisplayMode> _displayModes;

    std::shared_ptr<gui::IGUI> _advancedGraphicsGUI;
    AdvancedGraphicsControls _advancedGraphicsControls;
    AdvancedGraphicsChoices _advancedGraphics;

    std::shared_ptr<gui::IGUI> _soundGUI;
    SoundControls _soundControls;

    std::shared_ptr<gui::IGUI> _advancedSoundGUI;
    AdvancedSoundControls _advancedSoundControls;
    /** Force Software as the advanced sound page shows it, until OK keeps it. */
    bool _forceSoftware {false};

    std::shared_ptr<gui::IGUI> _mouseGUI;
    MouseControls _mouseControls;

    void onGUILoaded() override;

    /** Bind the front page's buttons and show each one's description as it gains focus. */
    void loadFrontPage();
    /** Load an option page's layout, failing when the game has none. */
    std::shared_ptr<gui::IGUI> loadPage(const std::string &name);
    /** Open \p gui over the top page; \p leave runs when it is left by Close or Escape. */
    void openPage(gui::IGUI &gui, std::function<void()> leave);
    /** Leave the top page by Close or Escape, back to the page beneath. */
    void closePage();
    /** Show \p strRef in a page's description box. */
    void describe(gui::ListBox &box, int strRef);
    void describe(gui::ListBox &box, const std::string &text);
    /** Show \p strRef in a page's description box, saying the option takes effect at the next start. */
    void describeForNextStart(gui::ListBox &box, int strRef);

    /** Load the feedback options page and wire its rows and buttons. */
    void loadFeedbackPanel();
    /** Open the feedback page on the current options, describing its first row. */
    void openFeedbackPanel();
    /** List one row per feedback option, checked when the option is on. */
    void showFeedbackOptions();
    void showFeedbackDescription(int row);

    /** Load the gameplay options page and wire its buttons. */
    void loadGameplayPanel();
    /** Open the gameplay page on the current difficulty and Mouse Look. */
    void openGameplayPanel();
    /** Step the difficulty level down or up, within the levels the player can pick. */
    void lowerDifficulty();
    void raiseDifficulty();
    /** Show the difficulty level's name, with an arrow on each side that has a level to step to. */
    void showDifficulty();

    /** Load the autopause options page and wire its checkboxes and buttons. */
    void loadAutoPausePanel();
    /** Open the autopause page on the current options, describing its first checkbox. */
    void openAutoPausePanel();
    /** The autopause checkbox states become the current options. */
    void keepAutoPauseOptions();
    void showAutoPauseOptions(const AutoPauseOptions &options);

    /** Load the graphics options page and wire its checkboxes and buttons. */
    void loadGraphicsPanel();
    /** Open the graphics page on the current options. */
    void openGraphicsPanel();
    void showGraphicsOptions();

    /** Load the screen resolution list and wire its buttons. */
    void loadResolutionPanel();
    /** Open the list on the display's modes, the one the game starts in chosen. */
    void openResolutionPanel();

    /** Load the advanced graphics options page and wire its controls. */
    void loadAdvancedGraphicsPanel();
    /** Add the Interface Size label and slider below the page's last row, drawn as the graphics page's Brightness. */
    void addInterfaceSizeSlider();
    /** Open the advanced graphics page on the current options, describing Texture Quality. */
    void openAdvancedGraphicsPanel();
    /** Show the page's choices: each level's name between its arrows, the checkboxes and the slider. */
    void showAdvancedGraphics();

    /** Load the sound options page and wire its sliders and buttons. */
    void loadSoundPanel();
    /** Open the sound page on the current volumes, describing the music volume. */
    void openSoundPanel();
    void showVolumes();

    /** Load KotOR's advanced sound options page and wire its controls. */
    void loadAdvancedSoundPanel();
    /** Open the advanced sound page on the current options, describing Force Software. */
    void openAdvancedSoundPanel();
    /** Show Force Software as the page holds it; while it is on, the EAX controls are disabled. */
    void showForceSoftware();

    /** Load the mouse settings page and wire its slider and buttons. */
    void loadMousePanel();
    /** Open the mouse page on the current sensitivity, describing it. */
    void openMousePanel();

    /** Every toggle on an options page, and every row of its option list, is a checkbox drawn in the GUI's colours. */
    void showCheckBoxes(gui::IGUI &panel);
    void showCheckBox(gui::Control &control);

    /** Write every option the pages change to the configuration. */
    void saveOptions();

    void bindControls() {
        _controls.BTN_AUTOPAUSE = findControl<gui::Button>("BTN_AUTOPAUSE");
        _controls.BTN_FEEDBACK = findControl<gui::Button>("BTN_FEEDBACK");
        _controls.BTN_GAMEPLAY = findControl<gui::Button>("BTN_GAMEPLAY");
        _controls.BTN_GRAPHICS = findControl<gui::Button>("BTN_GRAPHICS");
        _controls.BTN_SOUND = findControl<gui::Button>("BTN_SOUND");
        _controls.LBL_BAR1 = findControl<gui::Label>("LBL_BAR1");
        _controls.LBL_BAR2 = findControl<gui::Label>("LBL_BAR2");
        _controls.LBL_TITLE = findControl<gui::Label>("LBL_TITLE");
        _controls.LB_DESC = findControl<gui::ListBox>("LB_DESC");
        if (_inGame) {
            _inGameControls.BTN_EXIT = findControl<gui::Button>("BTN_EXIT");
            _inGameControls.BTN_LOADGAME = findControl<gui::Button>("BTN_LOADGAME");
            _inGameControls.BTN_QUIT = findControl<gui::Button>("BTN_QUIT");
            _inGameControls.BTN_SAVEGAME = findControl<gui::Button>("BTN_SAVEGAME");
        } else {
            _mainMenuBack = findControl<gui::Button>("BTN_BACK");
        }
    }
};

} // namespace game

} // namespace reone
