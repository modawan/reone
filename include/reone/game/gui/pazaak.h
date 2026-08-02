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

#include "../gui.h"
#include "../pazaaksession.h"

#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"

#include <array>

namespace reone {

namespace game {

const char *pazaakAudioResRef(PazaakPresentationEventType event);

class PazaakWagerGUI : public GameGUI {
public:
    PazaakWagerGUI(Game &game, ServicesView &services);

    void refresh();

private:
    struct Controls {
        std::shared_ptr<gui::Button> less;
        std::shared_ptr<gui::Button> more;
        std::shared_ptr<gui::Button> quit;
        std::shared_ptr<gui::Button> wager;
        std::shared_ptr<gui::Label> maximum;
        std::shared_ptr<gui::Label> title;
        std::shared_ptr<gui::Label> wagerValue;
    };

    Controls _controls;

    void onGUILoaded() override;
};

class PazaakSetupGUI : public GameGUI {
public:
    PazaakSetupGUI(Game &game, ServicesView &services);

    bool handle(const input::Event &event) override;
    void refresh();

private:
    struct Controls {
        std::shared_ptr<gui::Button> accept;
        std::shared_ptr<gui::Button> cancel;
        std::shared_ptr<gui::Button> clear;
        std::shared_ptr<gui::Label> help;
        std::shared_ptr<gui::Label> leftTitle;
        std::shared_ptr<gui::Label> rightTitle;
        std::shared_ptr<gui::Label> title;
        std::array<std::shared_ptr<gui::Button>, 24> availableButtons;
        std::array<std::shared_ptr<gui::Label>, 24> availableLabels;
        std::array<std::shared_ptr<gui::Label>, 24> availableCounts;
        std::array<std::shared_ptr<gui::Button>, pazaak::kSideDeckSize> chosenButtons;
        std::array<std::shared_ptr<gui::Label>, pazaak::kSideDeckSize> chosenLabels;
    };

    Controls _controls;
    std::optional<size_t> _selectedAvailable;

    void onGUILoaded() override;
};

class PazaakBoardGUI : public GameGUI {
public:
    PazaakBoardGUI(Game &game, ServicesView &services);

    bool handle(const input::Event &event) override;
    void refresh();

private:
    struct Controls {
        std::shared_ptr<gui::Button> endTurn;
        std::shared_ptr<gui::Button> stand;
        std::shared_ptr<gui::Button> forfeit;
        // Per-hand-slot sign switch, and its authored legend/icon.
        std::array<std::shared_ptr<gui::Button>, pazaak::kHandSize> flip;
        std::shared_ptr<gui::Label> flipLegend;
        std::shared_ptr<gui::Label> flipIcon;
        // Per-hand-slot KotOR II Value Change switch (optional; absent in KotOR I).
        std::array<std::shared_ptr<gui::Button>, pazaak::kHandSize> change;
        std::shared_ptr<gui::Label> changeLegend;
        std::shared_ptr<gui::Label> changeIcon;
        std::array<std::shared_ptr<gui::Button>, pazaak::kBoardSize> opponentBoardButtons;
        std::array<std::shared_ptr<gui::Label>, pazaak::kBoardSize> opponentBoardLabels;
        std::array<std::shared_ptr<gui::Button>, pazaak::kHandSize> opponentHandButtons;
        std::array<std::shared_ptr<gui::Label>, pazaak::kHandSize> opponentHandLabels;
        std::array<std::shared_ptr<gui::Button>, pazaak::kBoardSize> playerBoardButtons;
        std::array<std::shared_ptr<gui::Label>, pazaak::kBoardSize> playerBoardLabels;
        std::array<std::shared_ptr<gui::Button>, pazaak::kHandSize> playerHandButtons;
        std::array<std::shared_ptr<gui::Label>, pazaak::kHandSize> playerHandLabels;
        std::array<std::shared_ptr<gui::Label>, pazaak::kSetWinsForMatch> opponentScores;
        std::array<std::shared_ptr<gui::Label>, pazaak::kSetWinsForMatch> playerScores;
        std::shared_ptr<gui::Label> opponentName;
        std::shared_ptr<gui::Label> opponentTotal;
        std::shared_ptr<gui::Label> opponentTurn;
        std::shared_ptr<gui::Label> playerName;
        std::shared_ptr<gui::Label> playerTotal;
        std::shared_ptr<gui::Label> playerTurn;
    };

    Controls _controls;

    void onGUILoaded() override;
    void refreshAfterCommand(pazaak::ActionError error);
    void playPendingAudio();
    void playAudio(const std::string &resRef);
};

} // namespace game

} // namespace reone
