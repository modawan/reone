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

#include "reone/game/gui/ingame/options.h"

#include <array>
#include <tuple>

#include "reone/audio/mixer.h"
#include "reone/game/di/services.h"
#include "reone/game/difficultyoptions.h"
#include "reone/game/game.h"
#include "reone/gui/control/button.h"
#include "reone/gui/guis.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/strings.h"

using namespace reone::audio;
using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

// Descriptions of the front page's buttons.
static constexpr int kLoadGameDescriptionStrRef = 42300;
static constexpr int kSaveGameDescriptionStrRef = 42301;
static constexpr int kExitGameDescriptionStrRef = 42302;
static constexpr int kGameplayDescriptionStrRef = 48006;
static constexpr int kGameplayDescriptionStrRefTSL = 42273;
static constexpr int kFeedbackDescriptionStrRef = 48537;
static constexpr int kFeedbackDescriptionStrRefTSL = 136314;
static constexpr int kAutoPauseDescriptionStrRef = 48572;
static constexpr int kAutoPauseDescriptionStrRefTSL = 42275;
static constexpr int kGraphicsDescriptionStrRef = 48010;
static constexpr int kGraphicsDescriptionStrRefTSL = 48687;
static constexpr int kSoundDescriptionStrRef = 48012;
static constexpr int kSoundDescriptionStrRefTSL = 48688;

// Exit Game asks this before it ends the game.
static constexpr int kExitGameQuestionStrRef = 42348;

void OptionsMenu::showCheckBoxes(IGUI &panel) {
    for (auto &child : panel.rootControl().children()) {
        showCheckBox(child.get());
    }
}

void OptionsMenu::showCheckBox(Control &control) {
    if (auto toggle = dynamic_cast<ToggleButton *>(&control)) {
        toggle->showAsCheckBox(_baseColor, _hilightColor);
    }
}

void OptionsMenu::describe(ListBox &box, int strRef) {
    box.clearItems();
    box.addTextLinesAsItems(_services.resource.strings.getText(strRef));
}

void OptionsMenu::usePageTitle(const std::shared_ptr<Label> &title) {
    if (_inGame && _game.isTSL()) {
        title->setVisible(false);
    }
}

// Each options menu has pages of its own: the main menu's and the in-game
// menu's bind the same layouts to themselves, and the in-game one goes when
// the game returns to the main menu.
std::shared_ptr<IGUI> OptionsMenu::loadPage(const std::string &name) {
    auto gui = _services.gui.guis.load(guiResRef(name), [this](IGUI &gui) { preload(gui); });
    if (!gui) {
        throw ResourceNotFoundException("GUI not found: " + guiResRef(name));
    }
    return gui;
}

void OptionsMenu::onGUILoaded() {
    loadBackground(BackgroundType::Menu);
    bindControls();
    loadFrontPage();
    loadAutoPausePanel();
    loadFeedbackPanel();
    loadGameplayPanel();
    loadGraphicsPanel();
    loadSoundPanel();
    if (!_game.isTSL()) {
        loadAdvancedSoundPanel();
    }
    loadMousePanel();
}

void OptionsMenu::loadFrontPage() {
    const bool tsl = _game.isTSL();
    if (tsl) {
        fillK2SectionStrip(_controls.LBL_BAR1, _controls.LBL_BAR2);
        // The in-game menu's title bar names the tab.
        if (_inGame) {
            useK2ShellTitle(_controls.LBL_TITLE);
        }
    }

    std::vector<std::pair<std::shared_ptr<Button>, int>> buttons {
        {_controls.BTN_GAMEPLAY, tsl ? kGameplayDescriptionStrRefTSL : kGameplayDescriptionStrRef},
        {_controls.BTN_FEEDBACK, tsl ? kFeedbackDescriptionStrRefTSL : kFeedbackDescriptionStrRef},
        {_controls.BTN_AUTOPAUSE, tsl ? kAutoPauseDescriptionStrRefTSL : kAutoPauseDescriptionStrRef},
        {_controls.BTN_GRAPHICS, tsl ? kGraphicsDescriptionStrRefTSL : kGraphicsDescriptionStrRef},
        {_controls.BTN_SOUND, tsl ? kSoundDescriptionStrRefTSL : kSoundDescriptionStrRef},
    };
    if (_inGame) {
        buttons.push_back({_inGameControls.BTN_LOADGAME, kLoadGameDescriptionStrRef});
        buttons.push_back({_inGameControls.BTN_SAVEGAME, kSaveGameDescriptionStrRef});
        buttons.push_back({_inGameControls.BTN_QUIT, kExitGameDescriptionStrRef});
    }
    for (const auto &[button, strRef] : buttons) {
        enableK2ButtonBodyFill(button);
        button->setOnSelectionChanged([this, strRef = strRef](bool selected) {
            if (selected) {
                describe(*_controls.LB_DESC, strRef);
            }
        });
    }

    _controls.BTN_GAMEPLAY->setOnClick([this]() {
        openGameplayPanel();
    });
    _controls.BTN_FEEDBACK->setOnClick([this]() {
        openFeedbackPanel();
    });
    _controls.BTN_AUTOPAUSE->setOnClick([this]() {
        openAutoPausePanel();
    });
    _controls.BTN_GRAPHICS->setOnClick([this]() {
        openGraphicsPanel();
    });
    _controls.BTN_SOUND->setOnClick([this]() {
        openSoundPanel();
    });

    if (!_inGame) {
        enableK2ButtonBodyFill(_mainMenuBack);
        _mainMenuBack->setOnClick([this]() {
            _game.closeMainMenuOptions();
        });
        return;
    }
    enableK2ButtonBodyFill(_inGameControls.BTN_EXIT);
    _inGameControls.BTN_LOADGAME->setOnClick([this]() {
        _game.openSaveLoad(SaveLoadMode::LoadFromInGame);
    });
    _inGameControls.BTN_SAVEGAME->setOnClick([this]() {
        _game.openSaveLoad(SaveLoadMode::Save);
    });
    _inGameControls.BTN_EXIT->setOnClick([this]() {
        _game.openInGame();
    });
    // Exit Game writes the options, then asks before it ends the game and
    // returns to the main menu.
    _inGameControls.BTN_QUIT->setOnClick([this]() {
        saveOptions();
        _game.showConfirmPopup(
            _services.resource.strings.getText(kExitGameQuestionStrRef),
            [this]() { _game.requestEndGame(); });
    });
}

bool OptionsMenu::handle(const input::Event &event) {
    const bool escape = event.type == input::EventType::KeyDown && event.key.code == input::KeyCode::Escape;
    if (_pages.empty()) {
        // Escape leaves the main menu's options as Close does; the in-game
        // menu handles it for its tabs.
        if (escape && !_inGame) {
            _game.closeMainMenuOptions();
            return true;
        }
        return GameGUI::handle(event);
    }
    // The top page is modal; Escape leaves it as Close (or Cancel) does.
    if (escape) {
        closePage();
        return true;
    }
    _pages.back().gui->handle(event);
    return true;
}

void OptionsMenu::update(float dt) {
    GameGUI::update(dt);
    if (_pages.empty()) {
        return;
    }
    auto &top = *_pages.back().gui;
    top.update(dt);
    if (&top == _feedbackGUI.get()) {
        // A row describes itself when it gains focus.
        const int row = _feedbackControls.LB_OPTIONS->selectedItemIndex();
        if (row >= 0 && row != _feedbackDescribedRow) {
            showFeedbackDescription(row);
        }
    }
}

void OptionsMenu::render() {
    GameGUI::render();
    for (auto &page : _pages) {
        page.gui->render();
    }
}

void OptionsMenu::openPage(IGUI &gui, std::function<void()> leave) {
    _pages.push_back({&gui, std::move(leave)});
}

void OptionsMenu::closePage() {
    auto page = std::move(_pages.back());
    _pages.pop_back();
    page.leave();
    page.gui->clearSelection();
}

void OptionsMenu::clearSelection() {
    while (!_pages.empty()) {
        closePage();
    }
    GameGUI::clearSelection();
    saveOptions();
}

void OptionsMenu::saveOptions() {
    _game.saveAutoPauseOptions();
    _game.saveFeedbackOptions();
    _game.saveDifficultyLevel();
    _game.saveMouseOptions();
    _game.saveGameplayOptions();
    _game.saveKeybindOptions();
    _game.saveSoundOptions();
    _game.saveGraphicsOptions();
}

// Feedback

void OptionsMenu::loadFeedbackPanel() {
    _feedbackGUI = loadPage("optfeedback");
    auto find = [this](const std::string &tag) { return _feedbackGUI->findControl(tag); };
    auto &controls = _feedbackControls;
    controls.BTN_BACK = std::static_pointer_cast<Button>(find("BTN_BACK"));
    controls.BTN_DEFAULT = std::static_pointer_cast<Button>(find("BTN_DEFAULT"));
    controls.LBL_TITLE = std::static_pointer_cast<Label>(find("LBL_TITLE"));
    controls.LB_DESC = std::static_pointer_cast<ListBox>(find("LB_DESC"));
    controls.LB_OPTIONS = std::static_pointer_cast<ListBox>(find("LB_OPTIONS"));
    showCheckBox(controls.LB_OPTIONS->protoItem());

    const bool tsl = _game.isTSL();
    usePageTitle(controls.LBL_TITLE);
    if (tsl) {
        controls.LBL_BAR1 = std::static_pointer_cast<Label>(find("LBL_BAR1"));
        controls.LBL_BAR2 = std::static_pointer_cast<Label>(find("LBL_BAR2"));
        fillK2SectionStrip(controls.LBL_BAR1, controls.LBL_BAR2);
        enableK2ButtonBodyFill(controls.BTN_BACK);
        enableK2ButtonBodyFill(controls.BTN_DEFAULT);
    }

    // Row order and names: TSL has eight rows, KotOR adds small fonts before tooltips.
    _feedbackRows = {
        {&FeedbackOptions::hideUnequippable, 42279, 42286},
        {&FeedbackOptions::tutorialPopups, 42280, 42287},
        {&FeedbackOptions::subtitles, 42281, 42288},
        {&FeedbackOptions::miniMap, 42283, 42289},
        {&FeedbackOptions::floatingNumbers, 42285, 42291},
        {&FeedbackOptions::statusSummary, 42450, 42451},
        {&FeedbackOptions::hideInGameGui, tsl ? 48693 : 49061, tsl ? 48700 : 49065},
    };
    if (!tsl) {
        _feedbackRows.push_back({&FeedbackOptions::smallFonts, 49062, 49066});
    }
    _feedbackRows.push_back({&FeedbackOptions::tooltips, tsl ? 48696 : 49115, tsl ? 48703 : 49114});

    // A click flips its option at once; the configuration is written when the options close.
    controls.LB_OPTIONS->setOnItemClick([this](const std::string &tag) {
        const int index = std::stoi(tag);
        const auto &row = _feedbackRows.at(static_cast<size_t>(index));
        auto options = _game.feedbackOptions();
        options.*row.option = !(options.*row.option);
        _game.setFeedbackOptions(options);
        _feedbackControls.LB_OPTIONS->setItemOn(index, options.*row.option);
    });
    controls.BTN_DEFAULT->setOnClick([this]() {
        const FeedbackOptions defaults;
        _game.setFeedbackOptions(defaults);
        for (size_t i = 0; i < _feedbackRows.size(); ++i) {
            _feedbackControls.LB_OPTIONS->setItemOn(static_cast<int>(i), defaults.*_feedbackRows[i].option);
        }
    });
    controls.BTN_BACK->setOnClick([this]() {
        closePage();
    });
}

void OptionsMenu::openFeedbackPanel() {
    showFeedbackOptions();
    // The page opens with its list holding the keyboard, which selects and
    // describes the first row.
    _feedbackGUI->clearActiveControl();
    _feedbackGUI->setActiveControl(*_feedbackControls.LB_OPTIONS);
    openPage(*_feedbackGUI, [] {});
}

void OptionsMenu::showFeedbackOptions() {
    auto &list = *_feedbackControls.LB_OPTIONS;
    list.clearItems();
    const auto &options = _game.feedbackOptions();
    for (size_t i = 0; i < _feedbackRows.size(); ++i) {
        ListBox::Item item;
        item.tag = std::to_string(i);
        item.text = _services.resource.strings.getText(_feedbackRows[i].nameStrRef);
        item.on = options.*_feedbackRows[i].option;
        list.addItem(std::move(item));
    }
}

void OptionsMenu::showFeedbackDescription(int row) {
    _feedbackDescribedRow = row;
    if (row < 0 || row >= static_cast<int>(_feedbackRows.size())) {
        return;
    }
    describe(*_feedbackControls.LB_DESC, _feedbackRows[static_cast<size_t>(row)].descriptionStrRef);
}

// Gameplay

// The difficulty level's description.
static constexpr int kDifficultyDescriptionStrRef = 42265;
// The level the Default button restores.
static constexpr int kDefaultDifficultyLevel = 1;
static constexpr int kMouseLookDescriptionStrRef = 48014;
static constexpr int kMouseLookDescriptionStrRefTSL = 48697;
static constexpr int kCombatMovementDescriptionStrRef = 42484;
static constexpr int kMouseSettingsDescriptionStrRef = 49071;
static constexpr int kMouseSettingsDescriptionStrRefTSL = 48720;

void OptionsMenu::loadGameplayPanel() {
    _gameplayGUI = loadPage("optgameplay");
    auto find = [this](const std::string &tag) { return _gameplayGUI->findControl(tag); };
    auto &controls = _gameplayControls;
    controls.BTN_BACK = std::static_pointer_cast<Button>(find("BTN_BACK"));
    controls.BTN_DEFAULT = std::static_pointer_cast<Button>(find("BTN_DEFAULT"));
    controls.BTN_DIFFICULTY = std::static_pointer_cast<Button>(find("BTN_DIFFICULTY"));
    controls.BTN_DIFFLEFT = std::static_pointer_cast<Button>(find("BTN_DIFFLEFT"));
    controls.BTN_DIFFRIGHT = std::static_pointer_cast<Button>(find("BTN_DIFFRIGHT"));
    controls.BTN_MOUSE = std::static_pointer_cast<Button>(find("BTN_MOUSE"));
    controls.CB_DISABLEMOVE = std::static_pointer_cast<ToggleButton>(find("CB_DISABLEMOVE"));
    controls.CB_INVERTCAM = std::static_pointer_cast<ToggleButton>(find("CB_INVERTCAM"));
    controls.LBL_TITLE = std::static_pointer_cast<Label>(find("LBL_TITLE"));
    controls.LB_DESC = std::static_pointer_cast<ListBox>(find("LB_DESC"));
    showCheckBoxes(*_gameplayGUI);

    usePageTitle(controls.LBL_TITLE);
    enableK2ButtonBodyFill(controls.BTN_BACK);
    enableK2ButtonBodyFill(controls.BTN_DEFAULT);

    // The level changes at once; the configuration is written when the options close.
    controls.BTN_DIFFLEFT->setOnClick([this]() {
        lowerDifficulty();
    });
    controls.BTN_DIFFRIGHT->setOnClick([this]() {
        raiseDifficulty();
    });
    controls.BTN_DIFFICULTY->setOnSelectionChanged([this](bool selected) {
        if (!selected) {
            return;
        }
        showDifficulty();
        describe(*_gameplayControls.LB_DESC, kDifficultyDescriptionStrRef);
    });
    // The mouse look and combat movement boxes change their options at once
    // and describe themselves when they gain focus.
    controls.CB_INVERTCAM->setOnClick([this]() {
        _gameplayControls.CB_INVERTCAM->toggle();
        _game.setMouseLook(_gameplayControls.CB_INVERTCAM->isOn());
    });
    controls.CB_INVERTCAM->setOnSelectionChanged([this](bool selected) {
        if (selected) {
            describe(*_gameplayControls.LB_DESC, _game.isTSL() ? kMouseLookDescriptionStrRefTSL : kMouseLookDescriptionStrRef);
        }
    });
    controls.BTN_MOUSE->setOnClick([this]() {
        openMousePanel();
    });
    controls.BTN_MOUSE->setOnSelectionChanged([this](bool selected) {
        if (selected) {
            describe(*_gameplayControls.LB_DESC, _game.isTSL() ? kMouseSettingsDescriptionStrRefTSL : kMouseSettingsDescriptionStrRef);
        }
    });
    controls.CB_DISABLEMOVE->setOnClick([this]() {
        _gameplayControls.CB_DISABLEMOVE->toggle();
        _game.setCombatMovement(_gameplayControls.CB_DISABLEMOVE->isOn());
    });
    controls.CB_DISABLEMOVE->setOnSelectionChanged([this](bool selected) {
        if (!selected) {
            return;
        }
        _gameplayControls.LB_DESC->clearItems();
        _gameplayControls.LB_DESC->addTextLinesAsItems(
            _services.resource.strings.getText(kCombatMovementDescriptionStrRef));
    });
    // The defaults are the normal difficulty, mouse look off and combat
    // movement on.
    controls.BTN_DEFAULT->setOnClick([this]() {
        _game.setDifficultyLevel(kDefaultDifficultyLevel);
        _game.setMouseLook(false);
        _game.setCombatMovement(true);
        showDifficulty();
        _gameplayControls.CB_INVERTCAM->setOn(false);
        _gameplayControls.CB_DISABLEMOVE->setOn(true);
    });
    controls.BTN_BACK->setOnClick([this]() {
        closePage();
    });
}

void OptionsMenu::openGameplayPanel() {
    showDifficulty();
    _gameplayControls.CB_INVERTCAM->setOn(_game.mouseLook());
    _gameplayControls.CB_DISABLEMOVE->setOn(_game.combatMovement());
    _gameplayControls.LB_DESC->clearItems();
    openPage(*_gameplayGUI, [] {});
}

// The last row of the difficulty table is not a level the player can pick.
static int highestPickableDifficulty(const IDifficultyOptions &options) {
    return options.count() - 2;
}

void OptionsMenu::lowerDifficulty() {
    const int level = _game.difficultyLevel();
    if (level <= 0) {
        return;
    }
    _game.setDifficultyLevel(level - 1);
    showDifficulty();
}

void OptionsMenu::raiseDifficulty() {
    const int level = _game.difficultyLevel();
    if (level >= highestPickableDifficulty(_services.game.difficultyOptions)) {
        return;
    }
    _game.setDifficultyLevel(level + 1);
    showDifficulty();
}

void OptionsMenu::showDifficulty() {
    const auto &controls = _gameplayControls;
    const int level = _game.difficultyLevel();
    // A level whose name cell is blank leaves the button's text as it was.
    const int nameStrRef = _services.game.difficultyOptions.get(level).nameStrRef;
    if (nameStrRef != -1) {
        controls.BTN_DIFFICULTY->setTextMessage(_services.resource.strings.getText(nameStrRef));
    }
    controls.BTN_DIFFLEFT->setVisible(level > 0);
    controls.BTN_DIFFRIGHT->setVisible(level < highestPickableDifficulty(_services.game.difficultyOptions));
}

// Auto-pause

// The description of the first autopause option, shown when the page opens.
static constexpr int kStrRefEndOfRoundDescription = 42445;

void OptionsMenu::loadAutoPausePanel() {
    _autoPauseGUI = loadPage("optautopause");
    auto find = [this](const std::string &tag) { return _autoPauseGUI->findControl(tag); };
    auto &controls = _autoPauseControls;
    controls.BTN_BACK = std::static_pointer_cast<Button>(find("BTN_BACK"));
    controls.BTN_DEFAULT = std::static_pointer_cast<Button>(find("BTN_DEFAULT"));
    controls.CB_ACTIONMENU = std::static_pointer_cast<ToggleButton>(find("CB_ACTIONMENU"));
    controls.CB_ENDROUND = std::static_pointer_cast<ToggleButton>(find("CB_ENDROUND"));
    controls.CB_ENEMYSIGHTED = std::static_pointer_cast<ToggleButton>(find("CB_ENEMYSIGHTED"));
    controls.CB_MINESIGHTED = std::static_pointer_cast<ToggleButton>(find("CB_MINESIGHTED"));
    controls.CB_PARTYKILLED = std::static_pointer_cast<ToggleButton>(find("CB_PARTYKILLED"));
    controls.CB_TRIGGERS = std::static_pointer_cast<ToggleButton>(find("CB_TRIGGERS"));
    controls.LB_DETAILS = std::static_pointer_cast<ListBox>(find("LB_DETAILS"));
    showCheckBoxes(*_autoPauseGUI);

    enableK2ButtonBodyFill(controls.BTN_BACK);
    enableK2ButtonBodyFill(controls.BTN_DEFAULT);

    // Each checkbox describes itself when it gains focus; a click only flips it.
    const bool tsl = _game.isTSL();
    const std::array<std::pair<std::shared_ptr<ToggleButton>, int>, 6> checkboxes {{
        {controls.CB_ENDROUND, kStrRefEndOfRoundDescription},
        {controls.CB_ENEMYSIGHTED, 42446},
        {controls.CB_MINESIGHTED, tsl ? 48704 : 49117},
        {controls.CB_PARTYKILLED, 42447},
        {controls.CB_ACTIONMENU, tsl ? 48705 : 48416},
        {controls.CB_TRIGGERS, tsl ? 48706 : 48214},
    }};
    for (const auto &checkbox : checkboxes) {
        auto *control = checkbox.first.get();
        const int strRef = checkbox.second;
        control->setOnClick([control]() { control->toggle(); });
        control->setOnSelectionChanged([this, strRef](bool selected) {
            if (selected) {
                describe(*_autoPauseControls.LB_DETAILS, strRef);
            }
        });
    }
    controls.BTN_DEFAULT->setOnClick([this]() {
        _game.setAutoPauseOptions(AutoPauseOptions::defaults(_game.isTSL()));
        showAutoPauseOptions(_game.autoPauseOptions());
    });
    controls.BTN_BACK->setOnClick([this]() {
        closePage();
    });
}

void OptionsMenu::openAutoPausePanel() {
    showAutoPauseOptions(_game.autoPauseOptions());
    // The page opens with the first option's description.
    describe(*_autoPauseControls.LB_DETAILS, kStrRefEndOfRoundDescription);
    openPage(*_autoPauseGUI, [this] { keepAutoPauseOptions(); });
}

void OptionsMenu::keepAutoPauseOptions() {
    const auto &controls = _autoPauseControls;
    AutoPauseOptions options;
    options.endOfCombatRound = controls.CB_ENDROUND->isOn();
    options.enemySighted = controls.CB_ENEMYSIGHTED->isOn();
    options.mineSighted = controls.CB_MINESIGHTED->isOn();
    options.partyKilled = controls.CB_PARTYKILLED->isOn();
    options.actionMenu = controls.CB_ACTIONMENU->isOn();
    options.newTargetSelected = controls.CB_TRIGGERS->isOn();
    _game.setAutoPauseOptions(options);
}

void OptionsMenu::showAutoPauseOptions(const AutoPauseOptions &options) {
    const auto &controls = _autoPauseControls;
    controls.CB_ENDROUND->setOn(options.endOfCombatRound);
    controls.CB_ENEMYSIGHTED->setOn(options.enemySighted);
    controls.CB_MINESIGHTED->setOn(options.mineSighted);
    controls.CB_PARTYKILLED->setOn(options.partyKilled);
    controls.CB_ACTIONMENU->setOn(options.actionMenu);
    controls.CB_TRIGGERS->setOn(options.newTargetSelected);
}

// Graphics

static constexpr int kShadowsDescriptionStrRef = 47951;
static constexpr int kShadowsDescriptionStrRefTSL = 48708;
static constexpr int kGrassDescriptionStrRef = 47955;
static constexpr int kGrassDescriptionStrRefTSL = 48709;

void OptionsMenu::loadGraphicsPanel() {
    _graphicsGUI = loadPage("optgraphics");
    auto find = [this](const std::string &tag) { return _graphicsGUI->findControl(tag); };
    auto &controls = _graphicsControls;
    controls.BTN_BACK = std::static_pointer_cast<Button>(find("BTN_BACK"));
    controls.BTN_DEFAULT = std::static_pointer_cast<Button>(find("BTN_DEFAULT"));
    controls.CB_GRASS = std::static_pointer_cast<ToggleButton>(find("CB_GRASS"));
    controls.CB_SHADOWS = std::static_pointer_cast<ToggleButton>(find("CB_SHADOWS"));
    controls.LBL_TITLE = std::static_pointer_cast<Label>(find("LBL_TITLE"));
    controls.LB_DESC = std::static_pointer_cast<ListBox>(find("LB_DESC"));
    controls.SLI_GAMMA = std::static_pointer_cast<Slider>(find("SLI_GAMMA"));
    showCheckBoxes(*_graphicsGUI);

    usePageTitle(controls.LBL_TITLE);
    enableK2ButtonBodyFill(controls.BTN_BACK);
    enableK2ButtonBodyFill(controls.BTN_DEFAULT);

    // The display's brightness cannot be changed.
    controls.SLI_GAMMA->setDisabled(true);

    // Shadows and grass change at once: the next frame is drawn with them or
    // without. The configuration is written when the options close.
    const bool tsl = _game.isTSL();
    controls.CB_SHADOWS->setOnClick([this]() {
        _graphicsControls.CB_SHADOWS->toggle();
        _game.options().graphics.shadows = _graphicsControls.CB_SHADOWS->isOn();
    });
    controls.CB_SHADOWS->setOnSelectionChanged([this, tsl](bool selected) {
        if (selected) {
            describe(*_graphicsControls.LB_DESC, tsl ? kShadowsDescriptionStrRefTSL : kShadowsDescriptionStrRef);
        }
    });
    controls.CB_GRASS->setOnClick([this]() {
        _graphicsControls.CB_GRASS->toggle();
        _game.options().graphics.grass = _graphicsControls.CB_GRASS->isOn();
    });
    controls.CB_GRASS->setOnSelectionChanged([this, tsl](bool selected) {
        if (selected) {
            describe(*_graphicsControls.LB_DESC, tsl ? kGrassDescriptionStrRefTSL : kGrassDescriptionStrRef);
        }
    });
    // The defaults show shadows and grass.
    controls.BTN_DEFAULT->setOnClick([this]() {
        const GraphicsOptions defaults;
        auto &options = _game.options().graphics;
        options.shadows = defaults.shadows;
        options.grass = defaults.grass;
        showGraphicsOptions();
    });
    controls.BTN_BACK->setOnClick([this]() {
        closePage();
    });
}

void OptionsMenu::openGraphicsPanel() {
    showGraphicsOptions();
    _graphicsControls.LB_DESC->clearItems();
    openPage(*_graphicsGUI, [] {});
}

void OptionsMenu::showGraphicsOptions() {
    const auto &options = _game.options().graphics;
    _graphicsControls.CB_SHADOWS->setOn(options.shadows);
    _graphicsControls.CB_GRASS->setOn(options.grass);
}

// Sound

static constexpr int kMusicVolumeDescriptionStrRef = 42311;
static constexpr int kVoiceVolumeDescriptionStrRef = 42312;
static constexpr int kSoundVolumeDescriptionStrRef = 42313;
static constexpr int kMovieVolumeDescriptionStrRef = 47921;
static constexpr int kAdvancedSoundDescriptionStrRef = 49073;

void OptionsMenu::loadSoundPanel() {
    _soundGUI = loadPage("optsound");
    auto find = [this](const std::string &tag) { return _soundGUI->findControl(tag); };
    auto &controls = _soundControls;
    controls.BTN_ADVANCED = std::static_pointer_cast<Button>(find("BTN_ADVANCED"));
    controls.BTN_BACK = std::static_pointer_cast<Button>(find("BTN_BACK"));
    controls.BTN_DEFAULT = std::static_pointer_cast<Button>(find("BTN_DEFAULT"));
    controls.LBL_TITLE = std::static_pointer_cast<Label>(find("LBL_TITLE"));
    controls.LB_DESC = std::static_pointer_cast<ListBox>(find("LB_DESC"));
    controls.SLI_FX = std::static_pointer_cast<Slider>(find("SLI_FX"));
    controls.SLI_MOVIE = std::static_pointer_cast<Slider>(find("SLI_MOVIE"));
    controls.SLI_MUSIC = std::static_pointer_cast<Slider>(find("SLI_MUSIC"));
    controls.SLI_VO = std::static_pointer_cast<Slider>(find("SLI_VO"));

    usePageTitle(controls.LBL_TITLE);
    enableK2ButtonBodyFill(controls.BTN_BACK);
    enableK2ButtonBodyFill(controls.BTN_DEFAULT);

    // A volume follows its slider as it moves, sounds already playing
    // included, and describes itself when the slider gains focus.
    const std::array<std::tuple<std::shared_ptr<Slider>, AudioType, int>, 4> sliders {{
        {controls.SLI_MUSIC, AudioType::Music, kMusicVolumeDescriptionStrRef},
        {controls.SLI_VO, AudioType::Voice, kVoiceVolumeDescriptionStrRef},
        {controls.SLI_FX, AudioType::Sound, kSoundVolumeDescriptionStrRef},
        {controls.SLI_MOVIE, AudioType::Movie, kMovieVolumeDescriptionStrRef},
    }};
    for (const auto &[slider, type, strRef] : sliders) {
        slider->setOnValueChanged([this, type = type](int value) {
            _services.audio.mixer.setVolume(type, value);
        });
        slider->setOnSelectionChanged([this, strRef = strRef](bool selected) {
            if (selected) {
                describe(*_soundControls.LB_DESC, strRef);
            }
        });
    }

    // Only KotOR shows the advanced sound options.
    if (_game.isTSL()) {
        controls.BTN_ADVANCED->setVisible(false);
    } else {
        controls.BTN_ADVANCED->setOnClick([this]() {
            openAdvancedSoundPanel();
        });
        controls.BTN_ADVANCED->setOnSelectionChanged([this](bool selected) {
            if (selected) {
                describe(*_soundControls.LB_DESC, kAdvancedSoundDescriptionStrRef);
            }
        });
    }
    controls.BTN_DEFAULT->setOnClick([this]() {
        const AudioOptions defaults;
        auto &mixer = _services.audio.mixer;
        mixer.setVolume(AudioType::Music, defaults.musicVolume);
        mixer.setVolume(AudioType::Voice, defaults.voiceVolume);
        mixer.setVolume(AudioType::Sound, defaults.soundVolume);
        mixer.setVolume(AudioType::Movie, defaults.movieVolume);
        showVolumes();
    });
    controls.BTN_BACK->setOnClick([this]() {
        closePage();
    });
}

void OptionsMenu::openSoundPanel() {
    showVolumes();
    // The page opens on the music volume.
    describe(*_soundControls.LB_DESC, kMusicVolumeDescriptionStrRef);
    openPage(*_soundGUI, [] {});
}

void OptionsMenu::showVolumes() {
    const auto &options = _game.options().audio;
    _soundControls.SLI_MUSIC->setValue(options.musicVolume);
    _soundControls.SLI_VO->setValue(options.voiceVolume);
    _soundControls.SLI_FX->setValue(options.soundVolume);
    _soundControls.SLI_MOVIE->setValue(options.movieVolume);
}

// Advanced sound

static constexpr int kForceSoftwareDescriptionStrRef = 48577;
static constexpr int kEAXDescriptionStrRef = 48578;

void OptionsMenu::loadAdvancedSoundPanel() {
    _advancedSoundGUI = loadPage("optsoundadv");
    auto find = [this](const std::string &tag) { return _advancedSoundGUI->findControl(tag); };
    auto &controls = _advancedSoundControls;
    controls.BTN_BACK = std::static_pointer_cast<Button>(find("BTN_BACK"));
    controls.BTN_CANCEL = std::static_pointer_cast<Button>(find("BTN_CANCEL"));
    controls.BTN_DEFAULT = std::static_pointer_cast<Button>(find("BTN_DEFAULT"));
    controls.BTN_EAX = std::static_pointer_cast<Button>(find("BTN_EAX"));
    controls.CB_FORCESOFTWARE = std::static_pointer_cast<ToggleButton>(find("CB_FORCESOFTWARE"));
    controls.LB_DESC = std::static_pointer_cast<ListBox>(find("LB_DESC"));
    showCheckBoxes(*_advancedSoundGUI);

    // The environmental audio level is drawn as laid out; nothing sets it here.
    controls.BTN_EAX->setOnSelectionChanged([this](bool selected) {
        if (selected) {
            describe(*_advancedSoundControls.LB_DESC, kEAXDescriptionStrRef);
        }
    });

    controls.CB_FORCESOFTWARE->setOnClick([this]() {
        _forceSoftware = !_forceSoftware;
        showForceSoftware();
    });
    controls.CB_FORCESOFTWARE->setOnSelectionChanged([this](bool selected) {
        if (selected) {
            describe(*_advancedSoundControls.LB_DESC, kForceSoftwareDescriptionStrRef);
        }
    });
    controls.BTN_DEFAULT->setOnClick([this]() {
        _forceSoftware = AudioOptions().forceSoftware;
        showForceSoftware();
    });
    // OK keeps the page's Force Software; Cancel, like Escape, drops it.
    controls.BTN_BACK->setOnClick([this]() {
        _game.options().audio.forceSoftware = _forceSoftware;
        closePage();
    });
    controls.BTN_CANCEL->setOnClick([this]() {
        closePage();
    });
}

void OptionsMenu::openAdvancedSoundPanel() {
    _forceSoftware = _game.options().audio.forceSoftware;
    showForceSoftware();
    describe(*_advancedSoundControls.LB_DESC, kForceSoftwareDescriptionStrRef);
    openPage(*_advancedSoundGUI, [] {});
}

void OptionsMenu::showForceSoftware() {
    const auto &controls = _advancedSoundControls;
    controls.CB_FORCESOFTWARE->setOn(_forceSoftware);
    controls.BTN_EAX->setDisabled(_forceSoftware);
    controls.BTN_EAX->setTextColor(_forceSoftware ? kK1ColorDisabledText : _baseColor);
}

// Mouse

static constexpr int kMouseSensitivityDescriptionStrRef = 48013;
static constexpr int kMouseSensitivityDescriptionStrRefTSL = 48721;

void OptionsMenu::loadMousePanel() {
    _mouseGUI = loadPage("optmouse");
    auto find = [this](const std::string &tag) { return _mouseGUI->findControl(tag); };
    auto &controls = _mouseControls;
    controls.BTN_BACK = std::static_pointer_cast<Button>(find("BTN_BACK"));
    controls.BTN_DEFAULT = std::static_pointer_cast<Button>(find("BTN_DEFAULT"));
    controls.LBL_TITLE = std::static_pointer_cast<Label>(find("LBL_TITLE"));
    controls.LB_DESC = std::static_pointer_cast<ListBox>(find("LB_DESC"));
    controls.SLI_MOUSESEN = std::static_pointer_cast<Slider>(find("SLI_MOUSESEN"));
    showCheckBoxes(*_mouseGUI);

    usePageTitle(controls.LBL_TITLE);
    enableK2ButtonBodyFill(controls.BTN_BACK);
    enableK2ButtonBodyFill(controls.BTN_DEFAULT);

    // The sensitivity changes at once; the configuration is written when the
    // options close.
    controls.SLI_MOUSESEN->setOnValueChanged([this](int value) {
        _game.options().game.mouse.sensitivity = value;
    });
    const int descriptionStrRef = _game.isTSL() ? kMouseSensitivityDescriptionStrRefTSL : kMouseSensitivityDescriptionStrRef;
    controls.SLI_MOUSESEN->setOnSelectionChanged([this, descriptionStrRef](bool selected) {
        if (selected) {
            describe(*_mouseControls.LB_DESC, descriptionStrRef);
        }
    });
    controls.BTN_DEFAULT->setOnClick([this]() {
        const int sensitivity = MouseOptions().sensitivity;
        _game.options().game.mouse.sensitivity = sensitivity;
        _mouseControls.SLI_MOUSESEN->setValue(sensitivity);
    });
    controls.BTN_BACK->setOnClick([this]() {
        closePage();
    });
}

void OptionsMenu::openMousePanel() {
    _mouseControls.SLI_MOUSESEN->setValue(_game.options().game.mouse.sensitivity);
    describe(*_mouseControls.LB_DESC, _game.isTSL() ? kMouseSensitivityDescriptionStrRefTSL : kMouseSensitivityDescriptionStrRef);
    openPage(*_mouseGUI, [] {});
}

} // namespace game

} // namespace reone
