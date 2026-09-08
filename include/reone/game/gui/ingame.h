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
#include "reone/gui/control/progressbar.h"
#include "reone/gui/control/togglebutton.h"

#include "../gui.h"
#include "ingamehost.h"
#include "partyselect.h"

#include "ingame/abilities.h"
#include "ingame/character.h"
#include "ingame/equip.h"
#include "ingame/inventory.h"
#include "ingame/journal.h"
#include "ingame/map.h"
#include "ingame/messages.h"
#include "ingame/options.h"

namespace reone {

namespace game {

class InGameMenu : public GameGUI {
public:
    InGameMenu(Game &game, ServicesView &services) :
        GameGUI(game, services) {
        _resRef = guiResRef("top");
    }

    void init() override;
    void clearSelection() override;

    bool handle(const input::Event &event) override;
    void update(float dt) override;
    void render() override;

    void openEquipment();
    void openEquipmentItems();
    void openInventory();
    void openCharacter();
    void openAbilities();
    void openPartySelection();
    void openMessages();
    void openJournal();
    void openMap();
    void openOptions();

    std::shared_ptr<gui::Button> getBtnChange2();
    std::shared_ptr<gui::Button> getBtnChange3();

private:
    std::unique_ptr<InGameMenuHost> _host;
    std::shared_ptr<CharacterMenu> _character;
    std::shared_ptr<Equipment> _equip;
    std::shared_ptr<InventoryMenu> _inventory;
    std::shared_ptr<AbilitiesMenu> _abilities;
    std::shared_ptr<PartySelection> _partySelect;
    std::shared_ptr<MessagesMenu> _messages;
    std::shared_ptr<JournalMenu> _journal;
    std::shared_ptr<MapMenu> _map;
    std::shared_ptr<OptionsMenu> _options;

    InGameMenuFooter footer() const;
    void navigate(InGameMenuTab tab);

    void loadCharacter();
    void loadEquipment();
    void loadInventory();
    void loadAbilities();
    void loadPartySelection();
    void loadMessages();
    void loadJournal();
    void loadMap();
    void loadOptions();

};

} // namespace game

} // namespace reone
