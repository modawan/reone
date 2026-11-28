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

#include <algorithm>
#include <array>
#include <tuple>

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/itemdescription.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/party.h"
#include "reone/gui/guis.h"
#include "reone/resource/2da.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/strings.h"

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

void JournalMenu::onGUILoaded() {
    loadBackground(BackgroundType::Menu);
    bindControls();

    if (_game.isTSL()) {
        fillK2SectionStrip(_controls.LBL_BAR1, _controls.LBL_BAR2);
        useK2ShellTitle(_controls.LBL_TITLE);
        _controls.BTN_MESSAGES->setOnClick([this]() {
            _game.openInGameMenu(InGameMenuTab::Messages);
        });
        _controls.BTN_FILTER_PRIORITY->setOnClick([this]() {
            setSort(Sort::Priority);
        });
        _controls.BTN_FILTER_PLANET->setOnClick([this]() {
            setSort(Sort::Planet);
        });
        _controls.BTN_FILTER_NAME->setOnClick([this]() {
            setSort(Sort::Name);
        });
        _controls.BTN_FILTER_TIME->setOnClick([this]() {
            setSort(Sort::OrderReceived);
        });
    }
    _controls.BTN_EXIT->setOnClick([this]() {
        _game.openInGame();
    });
    _controls.BTN_SWAPTEXT->setOnClick([this]() {
        swapQuests();
    });

    // KotOR's sort button steps to the next sort, from the last back to the
    // first, and its quest items button opens the quest items panel.
    if (!_game.isTSL()) {
        loadQuestItemsPanel();
        _controls.BTN_QUESTITEMS->setOnClick([this]() {
            openQuestItemsPanel();
        });
        _controls.BTN_SORT->setOnClick([this]() {
            setSort(static_cast<Sort>((static_cast<int>(_sort) + 1) % (static_cast<int>(Sort::Planet) + 1)));
        });
    }
    updateListControls();

    useK1ListEntryColors(_controls.LB_ITEMS->protoItem());
    // A quest's row pulses its hilight frame.
    _controls.LB_ITEMS->protoItem().setHilightPulsing(true, false);
    _controls.LB_ITEMS->setSelectionMode(ListBox::SelectionMode::OnClick);
    _controls.LB_ITEMS->setOnItemClick([this](const std::string &plotId) {
        refreshEntryText(plotId);
    });
    // The quest list holds the keyboard until the pointer gives it to another control.
    _gui->setActiveControl(*_controls.LB_ITEMS);
}

bool JournalMenu::handle(const input::Event &event) {
    if (!_questItemsOpen) {
        return GameGUI::handle(event);
    }
    // The panel is modal; Escape leaves it the same way as Back, with a
    // click, and its repeats do nothing.
    if (event.type == input::EventType::KeyDown && event.key.code == input::KeyCode::Escape) {
        if (!event.key.repeat) {
            onClick(_questItemsControls.BTN_BACK->tag());
            closeQuestItemsPanel();
        }
        return true;
    }
    _questItemsGUI->handle(event);
    return true;
}

void JournalMenu::update(float dt) {
    GameGUI::update(dt);
    if (_questItemsOpen) {
        _questItemsGUI->update(dt);
    }
}

void JournalMenu::render() {
    GameGUI::render();
    if (_questItemsOpen) {
        _questItemsGUI->render();
    }
}

void JournalMenu::clearSelection() {
    if (_questItemsOpen) {
        closeQuestItemsPanel();
    }
    GameGUI::clearSelection();
}

void JournalMenu::loadQuestItemsPanel() {
    _questItemsGUI = _services.gui.guis.get(guiResRef("questitem"), [this](IGUI &gui) { preload(gui); });
    if (!_questItemsGUI) {
        throw ResourceNotFoundException("GUI not found: " + guiResRef("questitem"));
    }
    // The panel's controls sound as the journal's do.
    _questItemsGUI->setEventListener(*this);
    auto find = [this](const std::string &tag) { return _questItemsGUI->findControl(tag); };
    auto &controls = _questItemsControls;
    controls.BTN_BACK = std::static_pointer_cast<Button>(find("BTN_BACK"));
    controls.LBL_TITLE = std::static_pointer_cast<Label>(find("LBL_TITLE"));
    controls.LB_ITEMS = std::static_pointer_cast<ListBox>(find("LB_ITEMS"));
    controls.LB_ITEM_DESCRIPTION = std::static_pointer_cast<ListBox>(find("LB_ITEM_DESCRIPTION"));

    // The rows are item entries, as in the inventory.
    auto &list = *controls.LB_ITEMS;
    list.setRenderItemIconsForButtonProto(true);
    useItemEntryLayout(list);
    useK1ListEntryColors(list.protoItem());
    list.setSelectionMode(ListBox::SelectionMode::OnClick);
    // An item describes itself as it is chosen, by the pointer or the keys.
    list.setOnItemClick([this](const std::string &) {
        showQuestItemDescription(_questItemsControls.LB_ITEMS->selectedItemIndex());
    });
    controls.LB_ITEM_DESCRIPTION->setProtoMatchContent(true);
    // Back never takes the keyboard from the item list.
    controls.BTN_BACK->setTakesKeyboardOnHover(false);
    controls.BTN_BACK->setOnClick([this]() {
        closeQuestItemsPanel();
    });
}

// The list holds the plot items the player character carries, in the order
// they were taken.
void JournalMenu::openQuestItemsPanel() {
    auto &list = *_questItemsControls.LB_ITEMS;
    list.clearItems();
    list.clearSelection();
    _questItemsControls.LB_ITEM_DESCRIPTION->clearItems();
    _questItemDescriptions.clear();
    if (auto player = _game.party().actualPlayer()) {
        for (const auto &item : player->items()) {
            if (!item->plotFlag() || item->stackSize() <= 0) {
                continue;
            }
            ListBox::Item row;
            row.tag = std::to_string(list.getItemCount());
            row.text = item->localizedName();
            row.iconTexture = item->icon();
            row.iconFrame = itemFrameTexture(item->stackSize());
            if (item->stackSize() > 1) {
                row.iconText = std::to_string(item->stackSize());
            }
            list.addItem(std::move(row));
            _questItemDescriptions.push_back(buildItemDescription(*item, _game, _services));
        }
    }
    if (list.getItemCount() > 0) {
        list.setSelectedItemIndex(0);
        showQuestItemDescription(0);
    }
    // The item list holds the keyboard until the pointer gives it to another control.
    _questItemsGUI->setActiveControl(list);
    _questItemsOpen = true;
}

void JournalMenu::closeQuestItemsPanel() {
    _questItemsGUI->clearSelection();
    _questItemsOpen = false;
}

void JournalMenu::showQuestItemDescription(int row) {
    auto &description = *_questItemsControls.LB_ITEM_DESCRIPTION;
    description.clearItems();
    if (row >= 0 && row < static_cast<int>(_questItemDescriptions.size())) {
        description.addTextLinesAsItems(_questItemDescriptions[static_cast<size_t>(row)]);
    }
}

// The list holds the active quests or the completed ones, in the chosen sort;
// quests that sort alike keep the newest first.
void JournalMenu::refresh() {
    _controls.LB_ITEMS->clearItems();
    _controls.LB_ITEMS->clearSelection();
    _controls.LBL_ITEM_DESCRIPTION->clearItems();

    Journal &journal = _game.journal();
    std::vector<Row> rows;
    const auto &quests = journal.quests();
    for (auto it = quests.rbegin(); it != quests.rend(); ++it) {
        if (journal.isCompleted(it->plotId) != _showCompleted) {
            continue;
        }
        std::string name(journal.getQuestName(it->plotId));
        if (name.empty()) {
            name = it->plotId;
        }
        // TSL resolves the tokens of quest names and entries as it lists them.
        if (_game.isTSL()) {
            name = _game.substituteLogTokens(std::move(name));
        }
        rows.push_back({&*it, journal.findCategory(it->plotId), std::move(name)});
    }
    sortRows(rows, _sort);

    for (auto &row : rows) {
        ListBox::Item item;
        item.tag = row.quest->plotId;
        item.text = std::move(row.name);
        if (row.quest->unread) {
            useK1NewEntryStyle(item);
        }
        _controls.LB_ITEMS->addItem(std::move(item));
    }
    _shownQuest.clear();
    // The first quest is selected and described.
    if (_controls.LB_ITEMS->getItemCount() > 0) {
        _controls.LB_ITEMS->setSelectedItemIndex(0);
        refreshEntryText(_controls.LB_ITEMS->getItemAt(0).tag);
    }
}

void JournalMenu::sortRows(std::vector<Row> &rows, Sort sort) {
    const auto newer = [](const Row &a, const Row &b) {
        return std::tie(a.quest->date, a.quest->time) > std::tie(b.quest->date, b.quest->time);
    };
    std::stable_sort(rows.begin(), rows.end(), newer);
    switch (sort) {
    case Sort::Name:
        std::stable_sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
            return a.name < b.name;
        });
        break;
    case Sort::Priority:
        std::stable_sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
            return (a.category ? a.category->priority : 0) < (b.category ? b.category->priority : 0);
        });
        break;
    case Sort::Planet:
        std::stable_sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
            return (a.category ? a.category->planetId : -1) > (b.category ? b.category->planetId : -1);
        });
        break;
    default:
        break;
    }
}

void JournalMenu::setSort(Sort sort) {
    _sort = sort;
    updateListControls();
    refresh();
}

// The quests read so far stop being new as the list changes.
void JournalMenu::swapQuests() {
    markReadQuestsSeen();
    _showCompleted = !_showCompleted;
    updateListControls();
    refresh();
}

static constexpr int kCompletedQuestsStrRef = 32177;
static constexpr int kActiveQuestsStrRef = 32178;
static constexpr int kActiveQuestsByStrRef = 49145;
static constexpr int kCompletedQuestsByStrRef = 49146;
static constexpr int kSortStrRef = 42566;
static constexpr std::array<int, 4> kSortStrRefs {32173, 32174, 32175, 32176};

// KotOR titles the list with its sort and offers the other list and the next
// sort on its buttons; the sequel names the list shown beside its sort
// buttons, the chosen one in white.
void JournalMenu::updateListControls() {
    auto &strings = _services.resource.strings;
    if (_game.isTSL()) {
        _controls.BTN_SWAPTEXT->setTextMessage(strings.getText(_showCompleted ? kCompletedQuestsByStrRef : kActiveQuestsByStrRef));
        updateK2FilterButton(_controls.BTN_FILTER_TIME, _sort == Sort::OrderReceived);
        updateK2FilterButton(_controls.BTN_FILTER_NAME, _sort == Sort::Name);
        updateK2FilterButton(_controls.BTN_FILTER_PRIORITY, _sort == Sort::Priority);
        updateK2FilterButton(_controls.BTN_FILTER_PLANET, _sort == Sort::Planet);
        return;
    }
    const auto sortName = [&strings](Sort sort) {
        return strings.getText(kSortStrRefs[static_cast<int>(sort)]);
    };
    const auto next = static_cast<Sort>((static_cast<int>(_sort) + 1) % static_cast<int>(kSortStrRefs.size()));
    _controls.LBL_TITLE->setTextMessage(
        strings.getText(_showCompleted ? kCompletedQuestsStrRef : kActiveQuestsStrRef) + " - " + sortName(_sort));
    _controls.BTN_SWAPTEXT->setTextMessage(strings.getText(_showCompleted ? kActiveQuestsStrRef : kCompletedQuestsStrRef));
    _controls.BTN_SORT->setTextMessage(strings.getText(kSortStrRef) + " " + sortName(next));
}

// A new quest read in the journal shows as new until another is read, and
// stops being new once the journal is left.
void JournalMenu::markReadQuestsSeen() {
    Journal &journal = _game.journal();
    for (const auto &plotId : _readQuests) {
        journal.markRead(plotId);
    }
    _readQuests.clear();
}

void JournalMenu::refreshEntryText(const std::string &plotId) {
    _controls.LBL_ITEM_DESCRIPTION->clearItems();

    if (!_shownQuest.empty() && _shownQuest != plotId) {
        for (int i = 0; i < _controls.LB_ITEMS->getItemCount(); ++i) {
            if (_controls.LB_ITEMS->getItemAt(i).tag == _shownQuest) {
                _controls.LB_ITEMS->clearItemFrameStyle(i);
            }
        }
    }
    _shownQuest = plotId;
    if (std::find(_readQuests.begin(), _readQuests.end(), plotId) == _readQuests.end()) {
        _readQuests.push_back(plotId);
    }

    // A quest on a planet names the planet above its entry.
    Journal &journal = _game.journal();
    std::string text(journal.getEntryText(plotId, journal.getEntryState(plotId)));
    const auto *category = journal.findCategory(plotId);
    if (category && category->planetId != -1) {
        auto planets = _services.resource.twoDas.get("planetary");
        text = _services.resource.strings.getText(planets->getInt(category->planetId, "name")) + ":\n" + text;
    }
    if (_game.isTSL()) {
        text = _game.substituteLogTokens(std::move(text));
    }
    if (!text.empty()) {
        _controls.LBL_ITEM_DESCRIPTION->addTextLinesAsItems(text);
    }
}

} // namespace game

} // namespace reone
