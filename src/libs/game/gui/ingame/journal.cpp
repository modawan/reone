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

#include "reone/game/gui/ingame/journal.h"

#include "reone/game/game.h"

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

void JournalMenu::onGUILoaded() {
    loadBackground(BackgroundType::Menu);
    bindControls();
    tintK2InGameFooter();
    tintK2InGameHeader();

    if (_game.isTSL()) {
        _controls.LB_ITEMS->setTintBorderFill(true);
        _controls.LBL_ITEM_DESCRIPTION->setTintBorderFill(true);
        _controls.BTN_MESSAGES->setOnClick([this]() {
            _game.openInGameMenu(InGameMenuTab::Messages);
        });
        _controls.BTN_FILTER_PRIORITY->setOnClick([this]() {
            setFilter(Filter::Priority);
        });
        _controls.BTN_FILTER_PLANET->setOnClick([this]() {
            setFilter(Filter::Planet);
        });
        _controls.BTN_FILTER_NAME->setOnClick([this]() {
            setFilter(Filter::Name);
        });
        _controls.BTN_FILTER_TIME->setOnClick([this]() {
            setFilter(Filter::Time);
        });
        updateFilterControls();
    }
    _controls.BTN_EXIT->setOnClick([this]() {
        _game.openInGame();
    });
    _controls.BTN_SWAPTEXT->setDisabled(true);

    if (!_game.isTSL()) {
        _controls.BTN_QUESTITEMS->setDisabled(true);
        _controls.BTN_SORT->setDisabled(true);
    }

    _controls.LB_ITEMS->setSelectionMode(ListBox::SelectionMode::OnClick);
    _controls.LB_ITEMS->setOnItemClick([this](const std::string &plotId) {
        refreshEntryText(plotId);
    });
}

void JournalMenu::refresh() {
    _controls.LB_ITEMS->clearItems();
    _controls.LB_ITEMS->clearSelection();
    _controls.LBL_ITEM_DESCRIPTION->clearItems();

    Journal &journal = _game.journal();
    for (const auto &quest : journal.quests()) {
        std::string name(journal.getQuestName(quest.plotId));
        if (name.empty()) {
            name = quest.plotId;
        }
        ListBox::Item item;
        item.tag = quest.plotId;
        item.text = std::move(name);
        _controls.LB_ITEMS->addItem(std::move(item));
    }
}

void JournalMenu::refreshEntryText(const std::string &plotId) {
    _controls.LBL_ITEM_DESCRIPTION->clearItems();

    Journal &journal = _game.journal();
    std::string text(journal.getEntryText(plotId, journal.getEntryState(plotId)));
    if (!text.empty()) {
        _controls.LBL_ITEM_DESCRIPTION->addTextLinesAsItems(text);
    }
}

void JournalMenu::setFilter(Filter filter) {
    _filter = filter;
    updateFilterControls();
}

void JournalMenu::updateFilterControls() {
    updateK2FilterButton(_controls.BTN_FILTER_PRIORITY, _filter == Filter::Priority);
    updateK2FilterButton(_controls.BTN_FILTER_PLANET, _filter == Filter::Planet);
    updateK2FilterButton(_controls.BTN_FILTER_NAME, _filter == Filter::Name);
    updateK2FilterButton(_controls.BTN_FILTER_TIME, _filter == Filter::Time);
}

} // namespace game

} // namespace reone
