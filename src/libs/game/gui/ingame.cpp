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

#include "reone/game/gui/ingame.h"

#include <algorithm>
#include <array>

#include "reone/game/d20/classes.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/party.h"
#include "reone/game/types.h"

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

static void tintK2TopNavigationIcon(const std::shared_ptr<ImageButton> &button, const glm::vec3 &baseColor) {
    if (!button) {
        return;
    }
    button->setBorderColor(baseColor);
    button->setTintBorderFill(true);
}

static void configureTopNavigationIcon(const std::shared_ptr<ImageButton> &button) {
    if (!button) {
        return;
    }
    button->setSelectable(false);
}

void InGameMenu::preload(IGUI &gui) {
    if (_game.isTSL()) {
        gui.setResolution(800, 600);
    }
}

void InGameMenu::onGUILoaded() {
    bindControls();

    configureTopNavigationIcon(_controls.LBLH_EQU);
    configureTopNavigationIcon(_controls.LBLH_INV);
    configureTopNavigationIcon(_controls.LBLH_CHA);
    configureTopNavigationIcon(_controls.LBLH_ABI);
    configureTopNavigationIcon(_controls.LBLH_MSG);
    configureTopNavigationIcon(_controls.LBLH_JOU);
    configureTopNavigationIcon(_controls.LBLH_MAP);
    configureTopNavigationIcon(_controls.LBLH_OPT);

    if (_game.isTSL()) {
        tintK2TopNavigationIcon(_controls.LBLH_EQU, _baseColor);
        tintK2TopNavigationIcon(_controls.LBLH_INV, _baseColor);
        tintK2TopNavigationIcon(_controls.LBLH_CHA, _baseColor);
        tintK2TopNavigationIcon(_controls.LBLH_ABI, _baseColor);
        tintK2TopNavigationIcon(_controls.LBLH_MSG, _baseColor);
        tintK2TopNavigationIcon(_controls.LBLH_JOU, _baseColor);
        tintK2TopNavigationIcon(_controls.LBLH_MAP, _baseColor);
        tintK2TopNavigationIcon(_controls.LBLH_OPT, _baseColor);
        refreshK2Footer();
    }

    // _controls.BTN_EQU->setVisible(false);
    // _controls.BTN_INV->setVisible(false);
    // _controls.BTN_CHAR->setVisible(false);
    // _controls.BTN_ABI->setVisible(false);
    // _controls.BTN_MSG->setVisible(false);
    // _controls.BTN_JOU->setVisible(false);
    // _controls.BTN_MAP->setVisible(false);
    // _controls.BTN_OPT->setVisible(false);

    _controls.BTN_EQU->setOnClick([this]() {
        openEquipment();
    });
    _controls.BTN_INV->setOnClick([this]() {
        openInventory();
    });
    _controls.BTN_CHAR->setOnClick([this]() {
        openCharacter();
    });
    _controls.BTN_ABI->setOnClick([this]() {
        openAbilities();
    });
    _controls.BTN_MSG->setOnClick([this]() {
        if (_game.isTSL()) {
            openPartySelection();
        } else {
            openMessages();
        }
    });
    _controls.BTN_JOU->setOnClick([this]() {
        openJournal();
    });
    _controls.BTN_MAP->setOnClick([this]() {
        openMap();
    });
    _controls.BTN_OPT->setOnClick([this]() {
        openOptions();
    });

    loadEquipment();
    loadInventory();
    loadCharacter();
    loadAbilities();
    loadPartySelection();
    loadMessages();
    loadJournal();
    loadMap();
    loadOptions();
}

void InGameMenu::loadEquipment() {
    _equip = std::make_unique<Equipment>(_game, *this, _services);
    _equip->init();
}

void InGameMenu::loadInventory() {
    _inventory = std::make_unique<InventoryMenu>(_game, _services);
    _inventory->init();
}

void InGameMenu::loadCharacter() {
    _character = std::make_unique<CharacterMenu>(_game, *this, _services);
    _character->init();
}

void InGameMenu::loadAbilities() {
    _abilities = std::make_unique<AbilitiesMenu>(_game, _services);
    _abilities->init();
}

void InGameMenu::loadPartySelection() {
    if (!_game.isTSL()) {
        return;
    }
    _partySelect = std::make_unique<PartySelection>(_game, _services);
    _partySelect->init();
}

void InGameMenu::loadMessages() {
    _messages = std::make_unique<MessagesMenu>(_game, _services);
    _messages->init();
}

void InGameMenu::loadJournal() {
    _journal = std::make_unique<JournalMenu>(_game, _services);
    _journal->init();
}

void InGameMenu::loadMap() {
    _map = std::make_unique<MapMenu>(_game, _services);
    _map->init();
}

void InGameMenu::loadOptions() {
    _options = std::make_unique<OptionsMenu>(_game, _services);
    _options->init();
}

bool InGameMenu::handle(const input::Event &event) {
    auto tabGui = getActiveTabGUI();
    if (tabGui && tabGui->handle(event))
        return true;

    if (_gui->handle(event))
        return true;

    return false;
}

GameGUI *InGameMenu::getActiveTabGUI() const {
    switch (_tab) {
    case InGameMenuTab::Equipment:
        return _equip.get();
    case InGameMenuTab::Inventory:
        return _inventory.get();
    case InGameMenuTab::Character:
        return _character.get();
    case InGameMenuTab::Abilities:
        return _abilities.get();
    case InGameMenuTab::Party:
        return _partySelect.get();
    case InGameMenuTab::Messages:
        return _messages.get();
    case InGameMenuTab::Journal:
        return _journal.get();
    case InGameMenuTab::Map:
        return _map.get();
    case InGameMenuTab::Options:
        return _options.get();
    default:
        return nullptr;
    }
}

void InGameMenu::update(float dt) {
    GameGUI::update(dt);

    refreshK2Footer();

    auto tabGui = getActiveTabGUI();
    if (tabGui) {
        tabGui->update(dt);
    }
}

void InGameMenu::render() {
    auto tabGui = getActiveTabGUI();
    if (tabGui) {
        tabGui->render();
    }
    GameGUI::render();
}

void InGameMenu::openEquipment() {
    _equip->update();
    changeTab(InGameMenuTab::Equipment);
}

void InGameMenu::changeTab(InGameMenuTab tab) {
    auto gui = getActiveTabGUI();
    if (gui) {
        gui->clearSelection();
    }
    _tab = tab;
    updateTabButtons();
    refreshK2Footer();
}

void InGameMenu::refreshK2Footer() {
    if (!_game.isTSL()) {
        return;
    }

    auto hide = [](const auto &control) {
        if (control) {
            control->setVisible(false);
        }
    };

    hide(_controls.BTN_CHANGE2);
    hide(_controls.BTN_CHANGE3);
    hide(_controls.LBL_LEFT_ARROW);
    hide(_controls.LBL_RIGHT_ARROW);
    hide(_controls.LBL_CMBTEFCTINC1);
    hide(_controls.LBL_CMBTEFCTINC2);
    hide(_controls.LBL_CMBTEFCTINC3);
    hide(_controls.LBL_CMBTEFCTRED1);
    hide(_controls.LBL_CMBTEFCTRED2);
    hide(_controls.LBL_CMBTEFCTRED3);
    hide(_controls.LBL_DEBILATATED1);
    hide(_controls.LBL_DEBILATATED2);
    hide(_controls.LBL_DEBILATATED3);
    hide(_controls.LBL_DISABLE1);
    hide(_controls.LBL_DISABLE2);
    hide(_controls.LBL_DISABLE3);
    hide(_controls.PB_FORCE1);

    Party &party = _game.party();
    std::array<std::shared_ptr<Label>, 3> backLabels {
        _controls.LBL_BACK1,
        _controls.LBL_BACK2,
        _controls.LBL_BACK3};
    std::array<std::shared_ptr<Label>, 3> portraitLabels {
        _controls.LBL_CHAR1,
        _controls.LBL_CHAR2,
        _controls.LBL_CHAR3};
    std::array<std::shared_ptr<Label>, 3> levelUpLabels {
        _controls.LBL_LEVELUP1,
        _controls.LBL_LEVELUP2,
        _controls.LBL_LEVELUP3};

    for (int i = 0; i < 3; ++i) {
        auto member = party.getMember(i);
        if (!member) {
            hide(backLabels[i]);
            hide(portraitLabels[i]);
            hide(levelUpLabels[i]);
            continue;
        }

        backLabels[i]->setVisible(true);
        portraitLabels[i]->setVisible(true);
        portraitLabels[i]->setBorderFill(member->portrait());
        levelUpLabels[i]->setVisible(member->isLevelUpPending());
    }

    auto leader = party.getLeader();
    if (!leader) {
        hide(_controls.LBL_CHARNAME);
        hide(_controls.LBL_TOP_CLASS1);
        hide(_controls.LBL_TOP_CLASS1LEVEL);
        hide(_controls.LBL_TOP_CLASS2);
        hide(_controls.LBL_TOP_CLASS2LEVEL);
        hide(_controls.PB_VIT1);
        return;
    }

    _controls.LBL_CHARNAME->setVisible(true);
    _controls.LBL_CHARNAME->setTextMessage(leader->name());

    auto &attributes = leader->attributes();
    auto describeClass = [this](ClassType clazz) {
        return clazz == ClassType::Invalid ? std::string() : _services.game.classes.get(clazz)->name();
    };
    auto describeLevel = [](int level) {
        return level == 0 ? std::string() : std::to_string(level);
    };

    _controls.LBL_TOP_CLASS1->setVisible(true);
    _controls.LBL_TOP_CLASS1->setTextMessage(describeClass(attributes.getClassByPosition(1)));
    _controls.LBL_TOP_CLASS1LEVEL->setVisible(true);
    _controls.LBL_TOP_CLASS1LEVEL->setTextMessage(describeLevel(attributes.getLevelByPosition(1)));
    _controls.LBL_TOP_CLASS2->setVisible(true);
    _controls.LBL_TOP_CLASS2->setTextMessage(describeClass(attributes.getClassByPosition(2)));
    _controls.LBL_TOP_CLASS2LEVEL->setVisible(true);
    _controls.LBL_TOP_CLASS2LEVEL->setTextMessage(describeLevel(attributes.getLevelByPosition(2)));

    int hitPoints = leader->hitPoints();
    int vitalityPercent = hitPoints > 0
        ? std::clamp(100 * leader->currentHitPoints() / hitPoints, 0, 100)
        : 0;
    _controls.PB_VIT1->setVisible(true);
    _controls.PB_VIT1->setValue(vitalityPercent);
}

void InGameMenu::updateTabButtons() {
    _controls.LBLH_EQU->setSelected(_tab == InGameMenuTab::Equipment);
    _controls.LBLH_INV->setSelected(_tab == InGameMenuTab::Inventory);
    _controls.LBLH_CHA->setSelected(_tab == InGameMenuTab::Character);
    _controls.LBLH_ABI->setSelected(_tab == InGameMenuTab::Abilities);
    _controls.LBLH_MSG->setSelected(_tab == (_game.isTSL() ? InGameMenuTab::Party : InGameMenuTab::Messages));
    _controls.LBLH_JOU->setSelected(_tab == InGameMenuTab::Journal || (_game.isTSL() && _tab == InGameMenuTab::Messages));
    _controls.LBLH_MAP->setSelected(_tab == InGameMenuTab::Map);
    _controls.LBLH_OPT->setSelected(_tab == InGameMenuTab::Options);
}

void InGameMenu::openInventory() {
    _inventory->refreshPortraits();
    _inventory->refreshItems();
    changeTab(InGameMenuTab::Inventory);
}

void InGameMenu::openCharacter() {
    _character->refreshControls();
    changeTab(InGameMenuTab::Character);
}

void InGameMenu::openAbilities() {
    _abilities->refreshControls();
    changeTab(InGameMenuTab::Abilities);
}

void InGameMenu::openPartySelection() {
    _partySelect->prepare(PartySelectionContext());
    changeTab(InGameMenuTab::Party);
}

void InGameMenu::openMessages() {
    changeTab(InGameMenuTab::Messages);
}

void InGameMenu::openJournal() {
    changeTab(InGameMenuTab::Journal);
}

void InGameMenu::openMap() {
    _map->refreshControls();
    changeTab(InGameMenuTab::Map);
}

void InGameMenu::openOptions() {
    changeTab(InGameMenuTab::Options);
}

std::shared_ptr<Button> InGameMenu::getBtnChange2() {
    return _game.isTSL() ? findControl<Button>("BTN_CHANGE2") : nullptr;
}

std::shared_ptr<Button> InGameMenu::getBtnChange3() {
    return _game.isTSL() ? findControl<Button>("BTN_CHANGE3") : nullptr;
}

} // namespace game

} // namespace reone
