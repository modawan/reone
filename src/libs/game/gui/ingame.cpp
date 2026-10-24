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
#include "reone/game/gui/ingame/itembacking.h"
#include "reone/game/object/creature.h"
#include "reone/game/party.h"
#include "reone/game/types.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/strings.h"

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

/** The tutorial windows that the in-game menu's screens bring. */
static constexpr int kEquipSlotTutorial = 11;
static constexpr int kMessagesTutorial = 12;
static constexpr int kMapTutorial = 13;
static constexpr int kInventoryTutorial = 20;
static constexpr int kSkillsViewTutorial = 36;

void InGameMenu::init() {
    _host = std::make_unique<InGameMenuHost>(_game.gameId(), _game.options().graphics,
        _presentation, [this](InGameMenuTab tab) { navigate(tab); }, [this]() { return footer(); });
    _host->init();
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
    _equip = std::make_shared<Equipment>(_game.gameId(), _game.options().graphics,
        _presentation, _services.resource.strings, newEquipmentMenuBacking(_game, _services),
        [this]() { _game.openInGame(); });
    _equip->setOnSlotOpened([this]() { _game.requestTutorialWindow(kEquipSlotTutorial); });
    _equip->setOnMessage([this](const std::string &message) { _game.showMessagePopup(message); });
    _equip->init();
    _host->registerScreen(InGameMenuTab::Equipment, _equip);
}

void InGameMenu::loadInventory() {
    _inventory = std::make_shared<InventoryMenu>(_game.gameId(), _game.options().graphics,
        _presentation, newInventoryMenuBacking(_game, _services),
        [this]() { _game.openInGame(); });
    _inventory->setOnMessage([this](int strRef) { _game.showMessagePopup(_services.resource.strings.getText(strRef)); });
    _inventory->init();
    _host->registerScreen(InGameMenuTab::Inventory, _inventory);
}

void InGameMenu::loadCharacter() {
    _character = std::make_shared<CharacterMenu>(_game, *this, _services);
    _character->init();
    _host->registerScreen(InGameMenuTab::Character, _character);
}

void InGameMenu::loadAbilities() {
    _abilities = std::make_shared<AbilitiesMenu>(_game, _services);
    _abilities->init();
    _host->registerScreen(InGameMenuTab::Abilities, _abilities);
}

void InGameMenu::loadPartySelection() {
    if (!_game.isTSL()) {
        return;
    }
    _partySelect = std::make_shared<PartySelection>(_game, _services);
    _partySelect->init();
    _host->registerScreen(InGameMenuTab::Party, _partySelect);
}

void InGameMenu::loadMessages() {
    _messages = std::make_shared<MessagesMenu>(_game, _services);
    _messages->init();
    _host->registerScreen(InGameMenuTab::Messages, _messages);
}

void InGameMenu::loadJournal() {
    _journal = std::make_shared<JournalMenu>(_game, _services);
    _journal->init();
    _host->registerScreen(InGameMenuTab::Journal, _journal);
}

void InGameMenu::loadMap() {
    _map = std::make_shared<MapMenu>(_game, _services);
    _map->init();
    _host->registerScreen(InGameMenuTab::Map, _map);
}

void InGameMenu::loadOptions() {
    _options = std::make_shared<OptionsMenu>(_game, _services);
    _options->init();
    _host->registerScreen(InGameMenuTab::Options, _options);
}

void InGameMenu::closeEquipment() {
    if (_equipmentOpen) {
        _equip->endSession();
    }
    _equipmentOpen = false;
    _inventoryCharacter = -1;
}

std::shared_ptr<Creature> InGameMenu::equipmentCharacter() const {
    if (!_equipmentOpen || !_equip->browsingRoster()) {
        return _game.party().getLeader();
    }
    return _inventoryCharacter == -1 ? _game.party().player() : _game.party().getAvailableMember(_inventoryCharacter);
}

void InGameMenu::openEquipment() {
    if (!_equipmentOpen) {
        _equip->beginSession();
        _equipmentOpen = true;
    }
    _equip->update();
    _host->changeTab(InGameMenuTab::Equipment);
}

void InGameMenu::openEquipmentItems() {
    if (!_equipmentOpen) {
        _equip->beginSession();
        _equipmentOpen = true;
    }
    _equip->openItems();
    _host->changeTab(InGameMenuTab::Equipment);
}

void InGameMenu::openInventory() {
    closeEquipment();
    _inventory->refreshPortraits();
    _inventory->refreshItems();
    _host->changeTab(InGameMenuTab::Inventory);
    _game.requestTutorialWindow(kInventoryTutorial);
}

void InGameMenu::openCharacter() {
    closeEquipment();
    _character->refreshControls();
    _host->changeTab(InGameMenuTab::Character);
}

void InGameMenu::openAbilities() {
    closeEquipment();
    _abilities->refreshControls();
    _host->changeTab(InGameMenuTab::Abilities);
    // TSL opens abilities on the skills view.
    if (_game.isTSL()) {
        _game.requestTutorialWindow(kSkillsViewTutorial);
    }
}

void InGameMenu::openPartySelection() {
    closeEquipment();
    _partySelect->prepare(PartySelectionContext());
    _host->changeTab(InGameMenuTab::Party);
}

void InGameMenu::openMessages() {
    closeEquipment();
    _messages->refresh();
    _messages->resetFilter();
    _host->changeTab(InGameMenuTab::Messages);
    _game.requestTutorialWindow(kMessagesTutorial);
}

void InGameMenu::openJournal() {
    closeEquipment();
    _journal->refresh();
    _host->changeTab(InGameMenuTab::Journal);
}

void InGameMenu::openMap() {
    closeEquipment();
    _map->refreshControls();
    _host->changeTab(InGameMenuTab::Map);
    _game.requestTutorialWindow(kMapTutorial);
}

void InGameMenu::openOptions() {
    closeEquipment();
    _host->changeTab(InGameMenuTab::Options);
}

void InGameMenu::clearSelection() { if (_host) _host->clearSelection(); }

bool InGameMenu::handle(const input::Event &event) { return _host->handle(event); }
void InGameMenu::update(float dt) { _host->update(dt); }
void InGameMenu::render() { _host->render(); }
std::shared_ptr<Button> InGameMenu::getBtnChange2() { return _host->getBtnChange2(); }
std::shared_ptr<Button> InGameMenu::getBtnChange3() { return _host->getBtnChange3(); }

void InGameMenu::navigate(InGameMenuTab tab) {
    switch (tab) {
    case InGameMenuTab::Equipment: openEquipment(); break;
    case InGameMenuTab::Inventory: openInventory(); break;
    case InGameMenuTab::Character: openCharacter(); break;
    case InGameMenuTab::Abilities: openAbilities(); break;
    case InGameMenuTab::Party: openPartySelection(); break;
    case InGameMenuTab::Messages: openMessages(); break;
    case InGameMenuTab::Journal: openJournal(); break;
    case InGameMenuTab::Map: openMap(); break;
    case InGameMenuTab::Options: openOptions(); break;
    default: break;
    }
}

InGameMenuFooter InGameMenu::footer() const {
    InGameMenuFooter view;
    for (int i = 0; i < 3; ++i) {
        auto member = _game.party().getMember(i);
        if (member) view.members[i] = {true, member->portrait(), member->isLevelUpPending()};
    }
    auto leader = equipmentCharacter();
    if (_equipmentOpen && _equip->browsingRoster()) {
        view.members = {};
        if (leader) {
            view.members[0] = {true, leader->portrait(), leader->isLevelUpPending()};
        }
    }
    if (!leader) return view;
    view.subjectPresent = true;
    view.name = leader->name();
    // TSL shows the character's name with its actions hidden, resolved for
    // that character.
    if (_game.isTSL()) {
        view.name = _game.substituteLogTokens(std::move(view.name), *leader);
    }
    auto &attributes = leader->attributes();
    for (int i = 0; i < 2; ++i) {
        auto clazz = attributes.getClassByPosition(i + 1);
        auto level = attributes.getLevelByPosition(i + 1);
        view.classes[i] = clazz == ClassType::Invalid ? std::string() : _services.game.classes.get(clazz)->name();
        view.levels[i] = level == 0 ? std::string() : std::to_string(level);
    }
    int hp = leader->maxHitPoints();
    view.vitalityPercent = hp > 0 ? std::clamp(100 * leader->currentHitPoints() / hp, 0, 100) : 0;
    return view;
}

} // namespace game
} // namespace reone
