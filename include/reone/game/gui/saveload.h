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
#include "reone/gui/textinput.h"

#include "confirmpopup.h"
#include "../gui.h"
#include "../savedgame.h"

namespace reone {

namespace game {

class SaveLoad : public GameGUI {
public:
    SaveLoad(Game &game, ServicesView &services) :
        GameGUI(game, services) {
        _resRef = guiResRef("saveload");
    }

    void refresh();

    void setMode(SaveLoadMode mode);

    bool handle(const input::Event &event) override;
    void update(float dt) override;
    void render() override;

private:
    struct Controls {
        std::shared_ptr<gui::Button> BTN_BACK;
        std::shared_ptr<gui::Button> BTN_DELETE;
        std::shared_ptr<gui::Button> BTN_FILTER;
        std::shared_ptr<gui::Button> BTN_SAVELOAD;
        std::shared_ptr<gui::ToggleButton> CB_CLOUDSAVE;
        std::shared_ptr<gui::Label> LBL_AREANAME;
        std::shared_ptr<gui::Label> LBL_BAR2;
        std::shared_ptr<gui::Label> LBL_BAR3;
        std::shared_ptr<gui::Label> LBL_BAR4;
        std::shared_ptr<gui::Label> LBL_PANELNAME;
        std::shared_ptr<gui::Label> LBL_PCNAME;
        std::shared_ptr<gui::Label> LBL_PLANETNAME;
        std::shared_ptr<gui::Label> LBL_PM1;
        std::shared_ptr<gui::Label> LBL_PM2;
        std::shared_ptr<gui::Label> LBL_PM3;
        std::shared_ptr<gui::Label> LBL_SCREENSHOT;
        std::shared_ptr<gui::Label> LBL_TIMEPLAYED;
        std::shared_ptr<gui::ListBox> LB_GAMES;
    };

    Controls _controls;

    SaveLoadMode _mode {SaveLoadMode::Save};
    std::vector<SavedGame> _saves;
    std::optional<uint32_t> _selectedSaveSlot;
    std::optional<uint64_t> _pendingRequestId;
    std::unique_ptr<ConfirmPopup> _confirmation;

    struct SaveNameControls {
        std::shared_ptr<gui::Button> BTN_CANCEL;
        std::shared_ptr<gui::Button> BTN_OK;
        std::shared_ptr<gui::Label> EDITBOX;
        std::shared_ptr<gui::Label> LBL_TITLE;
    };
    std::shared_ptr<gui::IGUI> _saveNameGUI;
    SaveNameControls _saveNameControls;
    TextBuffer _saveNameBuffer;
    gui::TextInput _saveNameInput {
        _saveNameBuffer, gui::TextInputFlags::console};
    bool _saveNameVisible {false};
    uint32_t _saveNameSlot {0};

    void onGUILoaded() override;

    void bindControls() {
        _controls.BTN_BACK = findControl<gui::Button>("BTN_BACK");
        _controls.BTN_DELETE = findControl<gui::Button>("BTN_DELETE");
        _controls.BTN_FILTER = findControl<gui::Button>("BTN_FILTER");
        _controls.BTN_SAVELOAD = findControl<gui::Button>("BTN_SAVELOAD");
        _controls.CB_CLOUDSAVE = findControl<gui::ToggleButton>("CB_CLOUDSAVE");
        _controls.LBL_AREANAME = findControl<gui::Label>("LBL_AREANAME");
        _controls.LBL_BAR2 = findControl<gui::Label>("LBL_BAR2");
        _controls.LBL_BAR3 = findControl<gui::Label>("LBL_BAR3");
        _controls.LBL_BAR4 = findControl<gui::Label>("LBL_BAR4");
        _controls.LBL_PANELNAME = findControl<gui::Label>("LBL_PANELNAME");
        _controls.LBL_PCNAME = findControl<gui::Label>("LBL_PCNAME");
        _controls.LBL_PLANETNAME = findControl<gui::Label>("LBL_PLANETNAME");
        _controls.LBL_PM1 = findControl<gui::Label>("LBL_PM1");
        _controls.LBL_PM2 = findControl<gui::Label>("LBL_PM2");
        _controls.LBL_PM3 = findControl<gui::Label>("LBL_PM3");
        _controls.LBL_SCREENSHOT = findControl<gui::Label>("LBL_SCREENSHOT");
        _controls.LBL_TIMEPLAYED = findControl<gui::Label>("LBL_TIMEPLAYED");
        _controls.LB_GAMES = findControl<gui::ListBox>("LB_GAMES");
    }

    void refreshSavedGames();
    void refreshSelection();
    void showStatus(std::string message);
    void showSaveName(uint32_t slot, std::string name);
    void hideSaveName();
    void confirmSaveName();
    void saveGame(uint32_t number, std::string name);
    void loadGame(uint32_t number);
    void deleteGame(uint32_t number);

    int getSelectedSaveNumber() const;
    uint32_t getNewSaveNumber() const;
    const SavedGame *findSave(uint32_t number) const;
};

} // namespace game

} // namespace reone
