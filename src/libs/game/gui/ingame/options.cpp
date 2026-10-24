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

// The description of the first autopause option, shown when the panel opens.
static constexpr int kStrRefEndOfRoundDescription = 42445;

void OptionsMenu::onGUILoaded() {
    loadBackground(BackgroundType::Menu);
    bindControls();

    if (_game.isTSL()) {
        fillK2SectionStrip(_controls.LBL_BAR1, _controls.LBL_BAR2);
        _controls.LB_DESC->setTintBorderFill(true);
        useK2ShellTitle(_controls.LBL_TITLE);
        for (auto &button : {
                 _controls.BTN_SAVEGAME,
                 _controls.BTN_LOADGAME,
                 _controls.BTN_GAMEPLAY,
                 _controls.BTN_FEEDBACK,
                 _controls.BTN_AUTOPAUSE,
                 _controls.BTN_GRAPHICS,
                 _controls.BTN_SOUND,
                 _controls.BTN_QUIT,
                 _controls.BTN_EXIT}) {
            enableK2ButtonBodyFill(button);
        }
    }
    _controls.BTN_LOADGAME->setOnClick([this]() {
        _game.openSaveLoad(SaveLoadMode::LoadFromInGame);
    });
    _controls.BTN_SAVEGAME->setOnClick([this]() {
        _game.openSaveLoad(SaveLoadMode::Save);
    });
    _controls.BTN_EXIT->setOnClick([this]() {
        _game.openInGame();
    });
    _controls.BTN_AUTOPAUSE->setOnClick([this]() {
        openAutoPausePanel();
    });
    _controls.BTN_FEEDBACK->setOnClick([this]() {
        openFeedbackPanel();
    });
    loadAutoPausePanel();
    loadFeedbackPanel();
}

bool OptionsMenu::handle(const input::Event &event) {
    if (!_autoPauseOpen && !_feedbackOpen) {
        return GameGUI::handle(event);
    }
    // The panels are modal; Escape leaves them the same way as Back.
    if (event.type == input::EventType::KeyDown && event.key.code == input::KeyCode::Escape) {
        if (_autoPauseOpen) {
            closeAutoPausePanel();
        }
        if (_feedbackOpen) {
            closeFeedbackPanel();
        }
        return true;
    }
    if (_autoPauseOpen) {
        _autoPauseGUI->handle(event);
    }
    if (_feedbackOpen) {
        _feedbackGUI->handle(event);
    }
    return true;
}

void OptionsMenu::update(float dt) {
    GameGUI::update(dt);
    if (_autoPauseOpen) {
        _autoPauseGUI->update(dt);
    }
    if (_feedbackOpen) {
        _feedbackGUI->update(dt);
        // A row describes itself when it gains focus.
        const int row = _feedbackControls.LB_OPTIONS->selectedItemIndex();
        if (row >= 0 && row != _feedbackDescribedRow) {
            showFeedbackDescription(row);
        }
    }
}

void OptionsMenu::render() {
    GameGUI::render();
    if (_autoPauseOpen) {
        _autoPauseGUI->render();
    }
    if (_feedbackOpen) {
        _feedbackGUI->render();
    }
}

void OptionsMenu::clearSelection() {
    if (_autoPauseOpen) {
        closeAutoPausePanel();
    }
    if (_feedbackOpen) {
        closeFeedbackPanel();
    }
    GameGUI::clearSelection();
    _game.saveAutoPauseOptions();
    _game.saveFeedbackOptions();
}

void OptionsMenu::loadFeedbackPanel() {
    _feedbackGUI = _services.gui.guis.get(guiResRef("optfeedback"), [this](IGUI &gui) { preload(gui); });
    if (!_feedbackGUI) {
        throw ResourceNotFoundException("GUI not found: " + guiResRef("optfeedback"));
    }
    auto find = [this](const std::string &tag) { return _feedbackGUI->findControl(tag); };
    auto &controls = _feedbackControls;
    controls.BTN_BACK = std::static_pointer_cast<Button>(find("BTN_BACK"));
    controls.BTN_DEFAULT = std::static_pointer_cast<Button>(find("BTN_DEFAULT"));
    controls.LBL_TITLE = std::static_pointer_cast<Label>(find("LBL_TITLE"));
    controls.LB_DESC = std::static_pointer_cast<ListBox>(find("LB_DESC"));
    controls.LB_OPTIONS = std::static_pointer_cast<ListBox>(find("LB_OPTIONS"));

    const bool tsl = _game.isTSL();
    if (tsl) {
        controls.LBL_BAR1 = std::static_pointer_cast<Label>(find("LBL_BAR1"));
        controls.LBL_BAR2 = std::static_pointer_cast<Label>(find("LBL_BAR2"));
        fillK2SectionStrip(controls.LBL_BAR1, controls.LBL_BAR2);
        useK2ShellTitle(controls.LBL_TITLE);
        enableK2ButtonBodyFill(controls.BTN_BACK);
        enableK2ButtonBodyFill(controls.BTN_DEFAULT);
        controls.LB_DESC->setTintBorderFill(true);
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
        closeFeedbackPanel();
    });
}

void OptionsMenu::openFeedbackPanel() {
    showFeedbackOptions();
    // The panel opens on its first row.
    showFeedbackDescription(0);
    _feedbackOpen = true;
}

void OptionsMenu::closeFeedbackPanel() {
    _feedbackGUI->clearSelection();
    _feedbackOpen = false;
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
    _feedbackControls.LB_DESC->clearItems();
    _feedbackControls.LB_DESC->addTextLinesAsItems(
        _services.resource.strings.getText(_feedbackRows[static_cast<size_t>(row)].descriptionStrRef));
}

void OptionsMenu::loadAutoPausePanel() {
    _autoPauseGUI = _services.gui.guis.get(guiResRef("optautopause"), [this](IGUI &gui) { preload(gui); });
    if (!_autoPauseGUI) {
        throw ResourceNotFoundException("GUI not found: " + guiResRef("optautopause"));
    }
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

    if (_game.isTSL()) {
        enableK2ButtonBodyFill(controls.BTN_BACK);
        enableK2ButtonBodyFill(controls.BTN_DEFAULT);
        controls.LB_DETAILS->setTintBorderFill(true);
    }

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
                showAutoPauseDescription(strRef);
            }
        });
    }
    controls.BTN_DEFAULT->setOnClick([this]() {
        _game.setAutoPauseOptions(AutoPauseOptions::defaults(_game.isTSL()));
        showAutoPauseOptions(_game.autoPauseOptions());
    });
    controls.BTN_BACK->setOnClick([this]() {
        closeAutoPausePanel();
    });
}

void OptionsMenu::openAutoPausePanel() {
    showAutoPauseOptions(_game.autoPauseOptions());
    // The panel opens with the first option's description.
    showAutoPauseDescription(kStrRefEndOfRoundDescription);
    _autoPauseOpen = true;
}

// Leaving the panel keeps the checkbox states as the current options.
void OptionsMenu::closeAutoPausePanel() {
    const auto &controls = _autoPauseControls;
    AutoPauseOptions options;
    options.endOfCombatRound = controls.CB_ENDROUND->isOn();
    options.enemySighted = controls.CB_ENEMYSIGHTED->isOn();
    options.mineSighted = controls.CB_MINESIGHTED->isOn();
    options.partyKilled = controls.CB_PARTYKILLED->isOn();
    options.actionMenu = controls.CB_ACTIONMENU->isOn();
    options.newTargetSelected = controls.CB_TRIGGERS->isOn();
    _game.setAutoPauseOptions(options);
    _autoPauseGUI->clearSelection();
    _autoPauseOpen = false;
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

void OptionsMenu::showAutoPauseDescription(int strRef) {
    _autoPauseControls.LB_DETAILS->clearItems();
    _autoPauseControls.LB_DETAILS->addTextLinesAsItems(_services.resource.strings.getText(strRef));
}

} // namespace game

} // namespace reone
