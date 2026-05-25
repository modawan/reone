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

#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"
#include "reone/gui/control/listbox.h"

#include "../../d20/spells.h"
#include "../../gui.h"

namespace reone {

namespace gui {

class Button;
class IconChain;

} // namespace gui

namespace game {

class CharacterGeneration;

class CharGenPowers : public GameGUI {
public:
    CharGenPowers(
        CharacterGeneration &charGen,
        Game &game,
        ServicesView &services) :
        GameGUI(game, services),
        _charGen(charGen) {
        _resRef = guiResRef("pwrlvlup");
    }

    void reset();

private:
    struct Controls {
        std::shared_ptr<gui::Button> ACCEPT_BTN;
        std::shared_ptr<gui::Button> BACK_BTN;
        std::shared_ptr<gui::Label> DESC_LBL;
        std::shared_ptr<gui::Label> LBL_BAR1;
        std::shared_ptr<gui::Label> LBL_BAR2;
        std::shared_ptr<gui::Label> LBL_POWER;
        std::shared_ptr<gui::IconChain> ICONCHAIN_POWERS;
        std::shared_ptr<gui::ListBox> LB_DESC;
        std::shared_ptr<gui::ListBox> LB_POWERS;
        std::shared_ptr<gui::Label> MAIN_TITLE_LBL;
        std::shared_ptr<gui::Button> RECOMMENDED_BTN;
        std::shared_ptr<gui::Label> REMAINING_SELECTIONS_LBL;
        std::shared_ptr<gui::Label> SELECTIONS_REMAINING_LBL;
        std::shared_ptr<gui::Button> SELECT_BTN;
        std::shared_ptr<gui::Label> SUB_TITLE_LBL;
    };

    Controls _controls;

    CharacterGeneration &_charGen;
    std::vector<SpellDisplayEntry> _displayEntries;
    std::set<SpellType> _selectedPowers;
    std::string _defaultPowerNameText;
    int _powerGain {0};

    void onGUILoaded() override;

    void bindControls() {
        _controls.ACCEPT_BTN = findControl<gui::Button>("ACCEPT_BTN");
        _controls.BACK_BTN = findControl<gui::Button>("BACK_BTN");
        _controls.DESC_LBL = findControl<gui::Label>("DESC_LBL");
        _controls.LBL_BAR1 = findControl<gui::Label>("LBL_BAR1");
        _controls.LBL_BAR2 = findControl<gui::Label>("LBL_BAR2");
        _controls.LBL_POWER = findControl<gui::Label>("LBL_POWER");
        _controls.LB_DESC = findControl<gui::ListBox>("LB_DESC");
        _controls.LB_POWERS = findControl<gui::ListBox>("LB_POWERS");
        _controls.MAIN_TITLE_LBL = findControl<gui::Label>("MAIN_TITLE_LBL");
        _controls.RECOMMENDED_BTN = findControl<gui::Button>("RECOMMENDED_BTN");
        _controls.REMAINING_SELECTIONS_LBL = findControl<gui::Label>("REMAINING_SELECTIONS_LBL");
        _controls.SELECTIONS_REMAINING_LBL = findControl<gui::Label>("SELECTIONS_REMAINING_LBL");
        _controls.SELECT_BTN = findControl<gui::Button>("SELECT_BTN");
        _controls.SUB_TITLE_LBL = findControl<gui::Label>("SUB_TITLE_LBL");
    }

    void loadDisplayEntries();
    void refreshControls();
    void refreshSelectionControls();
    void refreshIconChain();
    void refreshIconChainSelection();
    void refreshIconChainLinks(const std::map<SpellType, glm::ivec2> &positions);
    void updateCharacter();
    void toggleSelectedPower(SpellType power);
    void removeInvalidSelectedPowers();
    void showPowerDescription(SpellType type);
    void resetFocusedPowerName();
    void onPowerFocused(const std::string &power);
    void onPowerActivated(const std::string &power);
    void activateFocusedPower();
};

} // namespace game

} // namespace reone
