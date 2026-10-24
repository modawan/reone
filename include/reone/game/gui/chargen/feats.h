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

#include "../../d20/feats.h"
#include "../../types.h"
#include "../../gui.h"

namespace reone {

namespace gui {

class Button;
class IconChain;

}

namespace game {

class CharacterGeneration;

class CharGenFeats : public GameGUI {
public:
    /** A feat's place on the screen. */
    enum class FeatStatus {
        Available,
        Chosen,
        Granted,
        Owned,
        Unavailable
    };

    CharGenFeats(
        CharacterGeneration &charGen,
        Game &game,
        ServicesView &services) :
        GameGUI(game, services),
        _charGen(charGen) {
        _resRef = guiResRef("ftchrgen");
    }

    /**
     * Opens the screen's choice of feats.
     *
     * @param levelUp true for a level-up, false for a new character
     * @param bonus true for the choice of the level's bonus feats, false for
     *        that of its normal feats
     * @param originalFeats the feats the character held before the screen
     *        granted the level's feats
     * @param grantedFeats the feats the screen's grant lists as granted;
     *        those among the original feats do not count as granted
     */
    void reset(bool levelUp, bool bonus, std::set<FeatType> originalFeats, const std::vector<FeatType> &grantedFeats);

    /** @return the feats still to choose */
    int remainingPicks() const { return _remaining; }

    /**
     * Accepts the chosen feats, unless feats are still to choose and one can
     * be chosen. After the normal feats, a level with bonus feats goes on to
     * their choice.
     */
    void accept();

    /** Selects the first selectable grid entry, for scripted captures. */
    void selectFirstEntryForCapture();

private:
    struct Controls {
        std::shared_ptr<gui::Button> BTN_ACCEPT;
        std::shared_ptr<gui::Button> BTN_BACK;
        std::shared_ptr<gui::Button> BTN_RECOMMENDED;
        std::shared_ptr<gui::Button> BTN_SELECT;
        std::shared_ptr<gui::Label> DESC_LBL;
        std::shared_ptr<gui::Label> LBL_BAR1;
        std::shared_ptr<gui::Label> LBL_BAR2;
        std::shared_ptr<gui::Label> LBL_NAME;
        std::shared_ptr<gui::IconChain> ICONCHAIN_FEATS;
        std::shared_ptr<gui::ListBox> LB_DESC;
        std::shared_ptr<gui::ListBox> LB_FEATS;
        std::shared_ptr<gui::Label> MAIN_TITLE_LBL;
        std::shared_ptr<gui::Label> STD_REMAINING_SELECTIONS_LBL;
        std::shared_ptr<gui::Label> STD_SELECTIONS_REMAINING_LBL;
        std::shared_ptr<gui::Label> SUB_TITLE_LBL;
    };

    Controls _controls;

    CharacterGeneration &_charGen;
    std::vector<FeatDisplayEntry> _displayEntries;
    /** The feats chosen on the screen. */
    std::set<FeatType> _selectedFeats;
    /** The feats held before the screen granted the level's feats; TSL leaves out the feats no character chooses. */
    std::set<FeatType> _originalFeats;
    /** The feats the screen's grant listed that are not among the original feats. */
    std::set<FeatType> _grantedFeats;
    /** The feats the character's effects give it. */
    std::set<FeatType> _effectFeats;
    /** The feats that can be chosen; a choice adds to them and a removal rebuilds them. */
    std::set<FeatType> _availableFeats;
    std::string _defaultFeatNameText;
    std::string _defaultMainTitleText;
    std::string _defaultSubTitleText;
    /** The level's normal and bonus feats. */
    int _normalFeats {0};
    int _bonusFeats {0};
    /** The feats the screen offers to choose: the normal or the bonus feats. */
    int _points {0};
    /** The feats still to choose. */
    int _remaining {0};
    bool _levelUp {false};
    bool _bonus {false};

    void onGUILoaded() override;

    void bindControls() {
        _controls.BTN_ACCEPT = findControl<gui::Button>("BTN_ACCEPT");
        _controls.BTN_BACK = findControl<gui::Button>("BTN_BACK");
        _controls.BTN_RECOMMENDED = findControl<gui::Button>("BTN_RECOMMENDED");
        _controls.BTN_SELECT = findControl<gui::Button>("BTN_SELECT");
        _controls.DESC_LBL = findControl<gui::Label>("DESC_LBL");
        _controls.LBL_BAR1 = findControl<gui::Label>("LBL_BAR1");
        _controls.LBL_BAR2 = findControl<gui::Label>("LBL_BAR2");
        _controls.LBL_NAME = findControl<gui::Label>("LBL_NAME");
        _controls.LB_DESC = findControl<gui::ListBox>("LB_DESC");
        _controls.LB_FEATS = findControl<gui::ListBox>("LB_FEATS");
        _controls.MAIN_TITLE_LBL = findControl<gui::Label>("MAIN_TITLE_LBL");
        _controls.STD_REMAINING_SELECTIONS_LBL = findControl<gui::Label>("STD_REMAINING_SELECTIONS_LBL");
        _controls.STD_SELECTIONS_REMAINING_LBL = findControl<gui::Label>("STD_SELECTIONS_REMAINING_LBL");
        _controls.SUB_TITLE_LBL = findControl<gui::Label>("SUB_TITLE_LBL");
    }

    void loadDisplayEntries();
    /** @return the character as the feats screen counts it: with its coming level on a level-up */
    CreatureAttributes choiceAttributes() const;
    /** Adds the feats that can now be chosen to those that could. */
    void buildAvailableFeats();
    /** @return the feat's place on the screen */
    FeatStatus getFeatStatus(FeatType feat) const;
    /** Responds to a pick of the feat: chooses it, takes it back or says why it cannot be chosen. */
    void pickFeat(FeatType feat);
    /** Chooses an available feat, using a pick. */
    void addChosenFeat(FeatType feat);
    /** Takes the feat back, and with it every chosen feat left without its prerequisites. */
    void removeChosenFeat(FeatType feat);
    /** Shows the message in a message box. */
    void showMessage(int strRef);
    void refreshControls();
    void refreshSelectionControls();
    void refreshIconChain();
    /** Updates the icon chain's states and selection after a pick, keeping its focus. */
    void refreshIconChainStates();
    void refreshIconChainLinks();
    void refreshListBox();
    void updateCharacter();
    void showFeatDescription(FeatType feat);
    void resetFocusedFeatName();
    void onFeatFocused(const std::string &feat);
    void onFeatActivated(const std::string &feat);
    void activateFocusedFeat();
};

} // namespace game

} // namespace reone
