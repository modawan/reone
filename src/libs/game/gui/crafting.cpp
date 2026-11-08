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

#include "reone/game/gui/crafting.h"

#include <algorithm>
#include <array>

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>

#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/game/d20/skills.h"
#include "reone/game/di/services.h"
#include "reone/game/equipmentrules.h"
#include "reone/game/game.h"
#include "reone/game/gui/sounds.h"
#include "reone/game/itemdescription.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/party.h"
#include "reone/game/twodautil.h"
#include "reone/gui/sceneinitializer.h"
#include "reone/input/event.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/models.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/strings.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/camera.h"
#include "reone/scene/node/model.h"

using namespace reone::audio;
using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;
using namespace reone::scene;

namespace reone {

namespace game {

// A row is listed up to this many ranks above the leader's, and made only at
// its rank or below.
static constexpr int kListedRankMargin = 8;
// A make worth more than this many times the leader's level, or a breakdown
// yielding more (up to the cap), is asked about first.
static constexpr int kConfirmValuePerLevel = 50;
static constexpr int kConfirmBreakdownCap = 250;
static constexpr int kRequirementHeaderStrRef = 115429;
static constexpr int kInstalledUpgradeStrRef = 124119;
static constexpr int kCancelStrRef = 219;
static constexpr int kCloseStrRef = 1582;
// The breakdown factor is the skill's share of this many ranks, at most all.
static constexpr float kFullBreakdownRanks = 20.0f;
static constexpr int kExcludedChemicalGroup = 5;
static constexpr int kDatapadItemType = 24;

// The views' strings: their titles, cost captions and buttons.
static constexpr int kCreateTitleStrRef = 111574;
static constexpr int kComponentCostStrRef = 48376;
static constexpr int kChemicalCostStrRef = 48434;
static constexpr int kCreateButtonStrRef = 48379;
static constexpr int kBreakdownViewButtonStrRef = 48373;
static constexpr int kBreakdownTitleStrRef = 115425;
static constexpr int kComponentYieldStrRef = 115427;
static constexpr int kChemicalYieldStrRef = 115432;
static constexpr int kBreakdownButtonStrRef = 115426;
static constexpr int kCreateViewButtonStrRef = 48375;

// The stations' messages.
static constexpr int kCannotCreateStrRef = 116292;
static constexpr int kTooFewComponentsStrRef = 116297;
static constexpr int kTooFewChemicalsStrRef = 116298;
static constexpr int kConfirmComponentCreateStrRef = 124113;
static constexpr int kConfirmChemicalCreateStrRef = 124114;
static constexpr int kConfirmComponentBreakdownStrRef = 124115;
static constexpr int kConfirmChemicalBreakdownStrRef = 124116;

/** TSL's crafting message boxes take the upgrade colours. */
static const PopupColors kCraftingPopupColors {kTSLColorUpgradeBorder, kTSLColorNewHighlight};

// CraftingPanel

CraftingPanel::CraftingPanel(Game &game, ServicesView &services, HeldCount count, SkillType breakdownSkill) :
    GameGUI(game, services),
    _count(count),
    _breakdownSkill(breakdownSkill) {
}

void CraftingPanel::onGUILoaded() {
    _controls.BTN_Accept = findControl<Button>("BTN_Accept");
    _controls.BTN_Cancel = findControl<Button>("BTN_Cancel");
    _controls.BTN_Examine = findControl<Button>("BTN_Examine");
    _controls.LBL_BAR2 = findControl<Label>("LBL_BAR2");
    _controls.LBL_BAR3 = findControl<Label>("LBL_BAR3");
    _controls.LBL_COST = findControl<Label>("LBL_COST");
    _controls.LBL_COST_VALUE = findControl<Label>("LBL_COST_VALUE");
    _controls.LBL_CREDITS_VALUE = findControl<Label>("LBL_CREDITS_VALUE");
    _controls.LBL_RELATEDSKILL = findControl<Label>("LBL_RELATEDSKILL");
    _controls.LBL_STOCK_VALUE = findControl<Label>("LBL_STOCK_VALUE");
    _controls.LBL_TITLE = findControl<Label>("LBL_TITLE");
    _controls.LB_DESCRIPTION = findControl<ListBox>("LB_DESCRIPTION");
    _controls.LB_DESCRIPTIONINV = findControl<ListBox>("LB_DESCRIPTIONINV");
    _controls.LB_INVITEMS = findControl<ListBox>("LB_INVITEMS");
    _controls.LB_SHOPITEMS = findControl<ListBox>("LB_SHOPITEMS");

    // A click selects an entry and a double click chooses it; pointing at an
    // entry does neither.
    for (const auto &list : {_controls.LB_SHOPITEMS, _controls.LB_INVITEMS}) {
        list->setSelectionMode(ListBox::SelectionMode::OnClick);
        auto &protoItem = list->protoItem();
        protoItem.setBorderColor(kTSLColorUpgradeBorder);
        protoItem.setHilightColor(kTSLColorNewHighlight);
        // A create entry's text takes the colour of its state, and one no
        // table row names keeps the layout's.
        if (list == _controls.LB_INVITEMS) {
            protoItem.setTextColor(kTSLColorUpgradeText);
        }
        Control::Text text(protoItem.text());
        text.align = Control::TextAlign::LeftCenter;
        protoItem.setText(text);
        list->setOnItemDoubleClick([this, &chosen = *list](const std::string &tag) {
            choose(chosen, std::stoi(tag));
        });
    }
    _controls.BTN_Accept->setOnClick([this]() {
        // Nothing is chosen while the list is empty.
        const int row = currentList().selectedItemIndex();
        if (row >= 0) {
            act(row);
        }
    });
    _controls.BTN_Cancel->setOnClick([this]() {
        close(false);
    });
    _controls.BTN_Examine->setOnClick([this]() {
        showView(_view == View::Create ? View::Breakdown : View::Create);
    });
    for (size_t group = 0; group < _createFilters.size(); ++group) {
        _createFilters[group]->setOnClick([this, group]() {
            _createGroup = static_cast<int>(group);
            colourFilters();
            // A group whose rows reach a blank cell is stocked only up to that
            // cell, and the list goes on showing the previous group's entries.
            // Those entries no longer stand for anything: pointing at one
            // describes nothing and choosing one does nothing, until the create
            // view is shown again from the stock.
            if (restockCreatable()) {
                showCreateList();
            } else {
                _createListStale = true;
            }
        });
    }
    for (const auto &[filter, button] : _breakdownFilters) {
        button->setOnClick([this, filter = filter]() {
            _breakdownFilter = filter;
            colourFilters();
            fillBreakdownList(0);
        });
    }
}

// The keys of a panel whose list holds the keyboard: Return or keypad Enter
// chooses the selected row, and Up and Down move the selection, also while
// held, sounding as it moves. Whether the key was one of them.
static bool handleListKey(ServicesView &services, const input::KeyEvent &key, ListBox &list, const std::function<void(int)> &choose) {
    if (!key.repeat && (key.code == input::KeyCode::Return || key.code == input::KeyCode::KeypadEnter)) {
        const int row = list.selectedItemIndex();
        if (row >= 0) {
            choose(row);
        }
        return true;
    }
    if (key.code == input::KeyCode::Up || key.code == input::KeyCode::Down) {
        if (list.selectAdjacentItem(key.code == input::KeyCode::Down)) {
            services.audio.mixer.play(services.game.guiSounds.getOnEnter(), AudioType::Sound);
        }
        return true;
    }
    return false;
}

bool CraftingPanel::handle(const input::Event &event) {
    if (event.type != input::EventType::KeyDown) {
        return GameGUI::handle(event);
    }
    if (!event.key.repeat && event.key.code == input::KeyCode::Escape) {
        close(true);
        return true;
    }
    auto &list = currentList();
    if (handleListKey(_services, event.key, list, [this, &list](int row) { choose(list, row); })) {
        return true;
    }
    return GameGUI::handle(event);
}

// Choosing a row of the create list clicks; the breakdown list is silent.
void CraftingPanel::choose(const ListBox &list, int row) {
    if (&list == _controls.LB_SHOPITEMS.get()) {
        _services.audio.mixer.play(_services.game.guiSounds.getOnClick(), AudioType::Sound);
    }
    act(row);
}

// Clicking the lists is silent: a click selecting another entry sounds as
// the selection moves, and a create entry clicks as it is chosen.
void CraftingPanel::onClick(const std::string &control) {
    if (control == _controls.LB_SHOPITEMS->tag() ||
        control == _controls.LB_INVITEMS->tag() ||
        control == _controls.LB_DESCRIPTION->tag() ||
        control == _controls.LB_DESCRIPTIONINV->tag()) {
        return;
    }
    GameGUI::onClick(control);
}

void CraftingPanel::update(float dt) {
    GameGUI::update(dt);
    // The selected row is described.
    const int row = currentList().selectedItemIndex();
    if (row >= 0 && _describedRow != row && !(_view == View::Create && _createListStale)) {
        describe(row);
    }
}

void CraftingPanel::openFor(const std::shared_ptr<Creature> &character, float breakdownFactor) {
    _character = character;
    _breakdownFactor = breakdownFactor;
    // The station works with the character's count, which is the party's for
    // a party member, and with the inventory its breakdown list comes from.
    auto &party = _game.party();
    if (party.creatureCount(*character, _count) < 0) {
        party.setCreatureCount(*character, _count, 0);
    }
    _createSlots.clear();
    setShownCount(party.creatureCount(*character, _count));
    colourFilters();
    showView(View::Create);
}

float CraftingPanel::breakdownFactorOf(const Creature &character) const {
    return std::min(1.0f, static_cast<float>(character.getUnopposedSkillRank(_breakdownSkill)) / kFullBreakdownRanks);
}

const std::vector<std::shared_ptr<Item>> &CraftingPanel::inventoryOf(const std::shared_ptr<Creature> &creature) const {
    return _game.party().sharedInventoryReceiver(creature)->items();
}

ListBox &CraftingPanel::currentList() const {
    return _view == View::Create ? *_controls.LB_SHOPITEMS : *_controls.LB_INVITEMS;
}

void CraftingPanel::showView(View view) {
    _view = view;
    const bool create = view == View::Create;
    const auto &text = create ? _createText : _breakdownText;
    auto &strings = _services.resource.strings;
    _controls.LBL_TITLE->setTextMessage(strings.getText(text.title));
    _controls.LBL_COST->setTextMessage(strings.getText(text.cost));
    _controls.BTN_Accept->setTextMessage(strings.getText(text.accept));
    _controls.BTN_Examine->setTextMessage(strings.getText(text.examine));

    _controls.LB_SHOPITEMS->setVisible(create);
    _controls.LBL_RELATEDSKILL->setVisible(create);
    _controls.LB_DESCRIPTION->setVisible(create);
    _controls.LBL_BAR2->setVisible(create);
    _controls.LBL_BAR3->setVisible(create);
    for (const auto &filter : _createFilters) {
        filter->setVisible(create);
    }
    _controls.LB_INVITEMS->setVisible(!create);
    _controls.LB_DESCRIPTIONINV->setVisible(!create);
    for (const auto &[filter, button] : _breakdownFilters) {
        button->setVisible(!create);
    }

    if (create) {
        _controls.LBL_RELATEDSKILL->setTextMessage("");
        restockCreatable();
        showCreateList();
    } else {
        fillBreakdownList(0);
    }
}

static glm::vec3 filterColour(bool current) {
    return current ? kTSLColorNewHighlight : kTSLColorUpgradeText;
}

void CraftingPanel::colourFilters() {
    for (size_t group = 0; group < _createFilters.size(); ++group) {
        _createFilters[group]->setTextColor(filterColour(static_cast<int>(group) == _createGroup));
    }
    for (const auto &[filter, button] : _breakdownFilters) {
        button->setTextColor(filterColour(filter == _breakdownFilter));
    }
}

// Lists

static ListBox::Item listEntry(Game &game, const Item &item, int index, std::shared_ptr<graphics::Texture> frame) {
    ListBox::Item entry;
    entry.tag = std::to_string(index);
    entry.text = game.substituteLogTokens(item.localizedName());
    entry.iconTexture = item.icon();
    entry.iconFrame = std::move(frame);
    if (item.stackSize() > 1) {
        entry.iconText = std::to_string(item.stackSize());
    }
    return entry;
}

bool CraftingPanel::restockCreatable() {
    _stock.clear();
    bool complete = true;
    const auto leader = _game.party().getLeader();
    // A table a script names that does not exist lists nothing.
    const auto table = createTable();
    if (table) {
        const int level = leader->attributes().getAggregateLevel();
        const int band = static_cast<int>(leader->alignment());
        for (int row = 0; row < table->getRowCount(); ++row) {
            const auto group = table->getIntOpt(row, "group");
            const auto skill = table->getIntOpt(row, skillColumn());
            const auto baseSkill = table->getIntOpt(row, "base_skill");
            std::optional<int> rowLevel {0};
            std::optional<int> rowAlign {0};
            if (checksLevelAndAlignment()) {
                rowLevel = table->getIntOpt(row, "level");
                rowAlign = table->getIntOpt(row, "align");
            }
            // A blank cell ends the table.
            if (!group || !skill || !baseSkill || !rowLevel || !rowAlign) {
                complete = false;
                break;
            }
            if (*group != _createGroup ||
                *skill > leader->getUnopposedSkillRank(static_cast<SkillType>(*baseSkill)) + kListedRankMargin ||
                (*rowLevel != 0 && *rowLevel > level) ||
                (*rowAlign != 0 && *rowAlign != band)) {
                continue;
            }
            auto item = _game.newPresentationItem();
            if (item->loadFromBlueprint(table->getString(row, "label"))) {
                addCreatable(item);
            } else if (stocksMissingTemplates()) {
                item->loadBlank();
                addCreatable(item);
            }
        }
    }
    return complete;
}

void CraftingPanel::showCreateList() {
    _createListStale = false;
    _creatable.clear();
    _controls.LB_SHOPITEMS->clearItems();
    // Each stocked item answers to the first row naming it; one whose row has
    // no skill is not listed, though the position it would have taken takes
    // that row.
    const auto leader = _game.party().getLeader();
    const auto table = createTable();
    for (const auto &item : _stock) {
        Creatable creatable {item};
        const bool kept = matchRow(creatable, *table, *leader);
        const size_t i = _creatable.size();
        if (_createSlots.size() <= i) {
            _createSlots.resize(i + 1);
        }
        auto &slot = _createSlots[i];
        if (creatable.row) {
            slot.row = creatable.row;
            if (kept) {
                slot.available = creatable.available;
            }
        }
        if (!kept) {
            continue;
        }
        auto entry = listEntry(_game, *creatable.item, static_cast<int>(i), itemFrameTexture(creatable.item->stackSize()));
        // An entry the leader can make lights in the upgrade text colour, one
        // the leader can't make is greyed, and one no row names is framed
        // dark gold; the count is in the entry's text colour.
        if (!creatable.row) {
            entry.borderColor = kTSLColorDarkGold;
            entry.iconTextColor = _controls.LB_SHOPITEMS->protoItem().text().color;
        } else if (creatable.available) {
            entry.textColor = kTSLColorUpgradeText;
            entry.hilightColor = kTSLColorUpgradeText;
            entry.iconTextColor = kTSLColorUpgradeText;
        } else {
            entry.textColor = kTSLColorDisabledText;
            entry.borderColor = kTSLColorDisabledText;
            entry.iconTextColor = kTSLColorDisabledText;
        }
        _controls.LB_SHOPITEMS->addItem(std::move(entry));
        _creatable.push_back(std::move(creatable));
    }
    showList(0);
}

void CraftingPanel::addCreatable(const std::shared_ptr<Item> &item) {
    if (item->plotFlag()) {
        return;
    }
    item->setIdentified(true);
    // The latest stocked item with the item's tag takes one more when identical.
    for (auto it = _stock.rbegin(); it != _stock.rend(); ++it) {
        auto &held = **it;
        if (held.tag() != item->tag()) {
            continue;
        }
        if (held.isIdenticalTo(*item)) {
            held.setStackSize(held.stackSize() + 1);
            return;
        }
        break;
    }
    // Otherwise identical stocked items, latest first, take what their stacks
    // have room for.
    for (auto it = _stock.rbegin(); it != _stock.rend(); ++it) {
        auto &held = **it;
        if (!held.isIdenticalTo(*item)) {
            continue;
        }
        const int joined = held.stackSize() + item->stackSize();
        if (joined <= held.maxStackSize()) {
            held.setStackSize(joined);
            return;
        }
        held.setStackSize(held.maxStackSize());
        item->setStackSize(joined - held.maxStackSize());
    }
    _stock.push_back(item);
}

bool CraftingPanel::matchRow(Creatable &creatable, const TwoDA &table, const Creature &leader) const {
    for (int row = 0; row < table.getRowCount(); ++row) {
        const auto tag = rowTag(table, row);
        if (tag.empty()) {
            return false;
        }
        if (!boost::iequals(tag, creatable.item->tag())) {
            continue;
        }
        creatable.row = row;
        const auto skill = table.getIntOpt(row, skillColumn());
        const auto baseSkill = table.getIntOpt(row, "base_skill");
        if (!skill || !baseSkill) {
            return false;
        }
        creatable.available = *skill <= leader.getUnopposedSkillRank(static_cast<SkillType>(*baseSkill));
        return true;
    }
    // An item no row names is shown, with its list position's row and
    // availability.
    return true;
}

void CraftingPanel::fillBreakdownList(int keepRow) {
    _breakable.clear();
    _controls.LB_INVITEMS->clearItems();
    for (const auto &item : listBreakable(_character.resolve())) {
        const int index = static_cast<int>(_breakable.size());
        auto entry = listEntry(_game, *item, index, itemFrameTexture(item->stackSize()));
        entry.iconTextColor = kTSLColorUpgradeText;
        _controls.LB_INVITEMS->addItem(std::move(entry));
        _breakable.emplace_back(item);
    }
    showList(keepRow);
}

void CraftingPanel::showList(int row) {
    auto &list = currentList();
    const int count = list.getItemCount();
    if (count == 0) {
        list.setSelectedItemIndex(-1);
        _describedRow.reset();
        clearFocus();
        return;
    }
    const int selected = std::clamp(row, 0, count - 1);
    list.setSelectedItemIndex(selected);
    describe(selected);
}

void CraftingPanel::clearFocus() {
    _controls.LBL_COST_VALUE->setTextMessage("");
    _controls.LBL_STOCK_VALUE->setTextMessage("");
    _controls.LB_DESCRIPTION->clearItems();
    _controls.LB_DESCRIPTIONINV->clearItems();
}

// END Lists

// Focus

void CraftingPanel::describe(int row) {
    _describedRow = row;
    if (_view == View::Create) {
        describeCreatable(_creatable[row], _createSlots[row]);
    } else {
        describeBreakable(*_breakable[row].resolve());
    }
}

static std::string describedItem(Game &game, ServicesView &services, const Item &item) {
    return game.substituteLogTokens(buildItemDescription(item, game, services));
}

void CraftingPanel::describeCreatable(const Creatable &creatable, const CreateSlot &slot) {
    _controls.LBL_COST_VALUE->setTextMessage(std::to_string(itemValue(*creatable.item)));
    // Without a row the rest is left as it was.
    if (!slot.row) {
        return;
    }
    const int row = *slot.row;
    const auto table = createTable();
    // A row with a blank skill leaves the rest as it was.
    const auto baseSkill = table->getIntOpt(row, "base_skill");
    if (!baseSkill) {
        return;
    }
    // A skill the skills table has no row for has no name.
    const auto skill = _services.game.skills.get(static_cast<SkillType>(*baseSkill));
    const auto skillName = skill ? skill->name : std::string();
    const int rank = _game.party().getLeader()->getUnopposedSkillRank(static_cast<SkillType>(*baseSkill));
    _controls.LBL_RELATEDSKILL->setTextMessage(skillName + " (" + std::to_string(rank) + ")");
    _controls.LBL_RELATEDSKILL->setTextColor(slot.available ? kTSLColorUpgradeText : kTSLColorRed);
    _controls.LBL_STOCK_VALUE->setTextMessage(std::to_string(stockOf(*creatable.item)));
    // A row with a blank requirement leaves the description as it was.
    const auto requirement = table->getIntOpt(row, skillColumn());
    if (!requirement) {
        return;
    }
    const auto description = _services.resource.strings.getText(kRequirementHeaderStrRef) + "\n" +
                             skillName + " (" + std::to_string(*requirement) + ")\n" +
                             describedItem(_game, _services, *creatable.item);
    _controls.LB_DESCRIPTION->clearItems();
    _controls.LB_DESCRIPTION->addTextLinesAsItems(description);
}

void CraftingPanel::describeBreakable(const Item &item) {
    _controls.LBL_COST_VALUE->setTextMessage(std::to_string(breakdownGain(item)));
    _controls.LBL_STOCK_VALUE->setTextMessage(std::to_string(item.stackSize()));
    _controls.LB_DESCRIPTIONINV->clearItems();
    _controls.LB_DESCRIPTIONINV->addTextLinesAsItems(describedItem(_game, _services, item));
}

// What the party holds of an item: the first matching stack it carries, and
// one for each matching item the first three members wear.
int CraftingPanel::stockOf(const Item &item) const {
    auto &party = _game.party();
    int stock = 0;
    for (const auto &carried : inventoryOf(party.getLeader())) {
        if (carried->isIdenticalTo(item)) {
            stock = carried->stackSize();
            break;
        }
    }
    for (int i = 0; i < 3; ++i) {
        const auto member = party.getMember(i);
        if (!member) {
            continue;
        }
        for (const auto &[slot, worn] : member->equipment()) {
            const bool creatureSlot = slot >= InventorySlots::cWeaponL && slot <= InventorySlots::cArmour;
            if (!creatureSlot && worn->isIdenticalTo(item)) {
                ++stock;
            }
        }
    }
    return stock;
}

int CraftingPanel::breakdownGain(const Item &item) const {
    return std::max(1, static_cast<int>(static_cast<float>(itemValue(item)) * _breakdownFactor));
}

// END Focus

// Acting

void CraftingPanel::act(int row) {
    if (_view == View::Create) {
        create(row);
    } else {
        breakDown(row);
    }
}

void CraftingPanel::create(int row) {
    auto &strings = _services.resource.strings;
    if (!_createSlots[row].available) {
        _game.showMessagePopup(strings.getText(_messages.unavailable), nullptr, {}, kCraftingPopupColors);
        return;
    }
    if (_createListStale) {
        return;
    }
    const auto &creatable = _creatable[row];
    const int cost = itemValue(*creatable.item);
    if (_shownCount < cost) {
        _game.showMessagePopup(strings.getText(_messages.tooFew), nullptr, {}, kCraftingPopupColors);
        return;
    }
    const int threshold = kConfirmValuePerLevel * _game.party().getLeader()->attributes().getAggregateLevel();
    if (cost > threshold) {
        _game.setCustomToken(0, std::to_string(threshold));
        _game.showConfirmPopup(
            strings.getText(_messages.confirmCreate),
            [this, row]() {
                finishCreate(row);
            },
            {}, kCraftingPopupColors);
        return;
    }
    finishCreate(row);
}

void CraftingPanel::finishCreate(int row) {
    const auto &creatable = _creatable[row];
    const auto character = _character.resolve();
    _services.audio.mixer.play(_services.game.guiSounds.getInventoryAdd(), AudioType::Sound);
    setShownCount(_shownCount - itemValue(*creatable.item));
    _game.party().setCreatureCount(*character, _count, _shownCount);
    _game.party().sharedInventoryReceiver(character)->addItem(_game.newItemClone(*creatable.item));
    showList(row);
}

void CraftingPanel::breakDown(int row) {
    const auto item = _breakable[row].resolve();
    auto &strings = _services.resource.strings;
    if (const auto refusal = breakdownRefusal(*item)) {
        _game.showMessagePopup(strings.getText(*refusal), nullptr, {}, kCraftingPopupColors);
        return;
    }
    const int threshold = std::min(kConfirmBreakdownCap,
                                   kConfirmValuePerLevel * _game.party().getLeader()->attributes().getAggregateLevel());
    if (static_cast<float>(itemValue(*item)) * _breakdownFactor > static_cast<float>(threshold)) {
        _game.setCustomToken(0, std::to_string(threshold));
        _game.showConfirmPopup(
            strings.getText(_messages.confirmBreakdown),
            [this, row]() {
                finishBreakdown(row);
            },
            {}, kCraftingPopupColors);
        return;
    }
    finishBreakdown(row);
}

void CraftingPanel::finishBreakdown(int row) {
    const auto item = _breakable[row].resolve();
    const auto character = _character.resolve();
    const int gain = breakdownGain(*item);
    // One of a stack is split off; a single item goes from the inventory it
    // was listed from. Nothing is gained for an item that could not be taken.
    if (item->stackSize() >= 2) {
        item->setStackSize(item->stackSize() - 1);
    } else {
        bool last = false;
        if (!_game.party().sharedInventoryReceiver(character)->removeItem(item, last)) {
            fillBreakdownList(row);
            return;
        }
        if (last) {
            _game.destroyRuntimeObjectGraph(item);
        }
    }
    _services.audio.mixer.play(_services.game.guiSounds.getInventoryDrop(), AudioType::Sound);
    setShownCount(_shownCount + gain);
    _game.party().setCreatureCount(*character, _count, _shownCount);
    fillBreakdownList(row);
}

void CraftingPanel::setShownCount(int count) {
    _shownCount = count;
    _controls.LBL_CREDITS_VALUE->setTextMessage(std::to_string(count));
}

// END Acting

// END CraftingPanel

// ItemCreationPanel

ItemCreationPanel::ItemCreationPanel(Game &game, ServicesView &services) :
    CraftingPanel(game, services, HeldCount::Components, SkillType::Repair) {
    _resRef = guiResRef("component");
    _createText = {kCreateTitleStrRef, kComponentCostStrRef, kCreateButtonStrRef, kBreakdownViewButtonStrRef};
    _breakdownText = {kBreakdownTitleStrRef, kComponentYieldStrRef, kBreakdownButtonStrRef, kCreateViewButtonStrRef};
    _messages = {kCannotCreateStrRef, kTooFewComponentsStrRef, kConfirmComponentCreateStrRef, kConfirmComponentBreakdownStrRef};
}

void ItemCreationPanel::onGUILoaded() {
    for (const char *tag : {"BTN_CREATE_ARMOR", "BTN_CREATE_RANGED", "BTN_CREATE_MELEE", "BTN_CREATE_LIGHTSABER", "BTN_CREATE_MISC"}) {
        _createFilters.push_back(findControl<Button>(tag));
    }
    _breakdownFilters = {
        {BreakdownFilter::All, findControl<Button>("BTN_BREAK_ALL")},
        {BreakdownFilter::Weapons, findControl<Button>("BTN_BREAK_WEAPONS")},
        {BreakdownFilter::Armor, findControl<Button>("BTN_BREAK_ARMOR")},
        {BreakdownFilter::Usable, findControl<Button>("BTN_BREAK_USEABLE")},
        {BreakdownFilter::Misc, findControl<Button>("BTN_BREAK_MISC")}};
    _itemCreate = getRequiredTwoDA(_services.resource.twoDas, "itemcreate");
    CraftingPanel::onGUILoaded();
}

void ItemCreationPanel::open(const std::shared_ptr<Creature> &character,
                             float breakdownFactor,
                             std::string itemCreateTable,
                             bool disableUpgrade,
                             std::function<void(bool)> onClose) {
    // A table a script names is read for this opening, and with it the
    // tutorial's list of what the lab station makes.
    if (itemCreateTable.empty()) {
        _createTable = _itemCreate;
        _excludedTable = _game.chemicalCreate();
    } else {
        _createTable = _services.resource.twoDas.get(itemCreateTable);
        _excludedTable = _services.resource.twoDas.get("tutchemcreate");
    }
    _onClose = std::move(onClose);
    _controls.BTN_Cancel->setTextMessage(_services.resource.strings.getText(disableUpgrade ? kCloseStrRef : kCancelStrRef));
    // MISC each time it opens; the breakdown filter as the workbench was opened.
    _createGroup = static_cast<int>(CreateGroup::Misc);
    openFor(character, breakdownFactor);
}

void ItemCreationPanel::resetBreakdownFilter() {
    _breakdownFilter = BreakdownFilter::All;
}

std::shared_ptr<TwoDA> ItemCreationPanel::createTable() const {
    return _createTable;
}

std::string ItemCreationPanel::rowTag(const TwoDA &table, int row) const {
    auto tag = table.getString(row, "OverrideTag");
    return tag.empty() ? table.getString(row, "label") : tag;
}

int ItemCreationPanel::itemValue(const Item &item) const {
    return item.componentValue(_game.baseItemCostMultiplier(item.baseItemType()));
}

std::vector<std::shared_ptr<Item>> ItemCreationPanel::listBreakable(const std::shared_ptr<Creature> &character) const {
    // What the lab station makes is not broken down here; a missing table
    // leaves out nothing.
    std::vector<std::string> excluded;
    if (_excludedTable) {
        for (int row = 0; row < _excludedTable->getRowCount(); ++row) {
            excluded.push_back(_excludedTable->getString(row, "label"));
        }
    }
    std::vector<std::shared_ptr<Item>> breakable;
    for (const auto &item : inventoryOf(character)) {
        if (std::find(excluded.begin(), excluded.end(), item->tag()) != excluded.end() ||
            item->plotFlag() ||
            !passesBreakdownFilter(*character, *item) ||
            item->cost(_game.baseItemCostMultiplier(item->baseItemType())) == 0) {
            continue;
        }
        breakable.push_back(item);
    }
    return breakable;
}

bool ItemCreationPanel::passesBreakdownFilter(const Creature &character, const Item &item) const {
    const auto usable = [&]() {
        return isLeaderUsableItem(character, item) && canUseItem(character, item, true);
    };
    switch (_breakdownFilter) {
    case BreakdownFilter::All:
        break;
    case BreakdownFilter::Weapons:
        return item.isEquippable(InventorySlots::rightWeapon) || item.isEquippable(InventorySlots::leftWeapon);
    case BreakdownFilter::Armor:
        for (int slot : {InventorySlots::head, InventorySlots::body, InventorySlots::hands, InventorySlots::leftArm,
                         InventorySlots::rightArm, InventorySlots::implant, InventorySlots::belt}) {
            if (item.isEquippable(slot)) {
                return true;
            }
        }
        return false;
    case BreakdownFilter::Usable:
        return usable();
    case BreakdownFilter::Misc:
        return !item.isEquippable() && item.itemType() != kDatapadItemType && !usable();
    }
    // Everything passes ALL.
    return true;
}

std::optional<int> ItemCreationPanel::breakdownRefusal(const Item &item) const {
    if (item.hasInstalledUpgrade()) {
        return kInstalledUpgradeStrRef;
    }
    return std::nullopt;
}

void ItemCreationPanel::close(bool escape) {
    _onClose(escape);
}

// END ItemCreationPanel

// LabStationPanel

LabStationPanel::LabStationPanel(Game &game, ServicesView &services) :
    CraftingPanel(game, services, HeldCount::Chemicals, SkillType::TreatInjury) {
    _resRef = guiResRef("chemical");
    _createText = {kCreateTitleStrRef, kChemicalCostStrRef, kCreateButtonStrRef, kBreakdownViewButtonStrRef};
    _breakdownText = {kBreakdownTitleStrRef, kChemicalYieldStrRef, kBreakdownButtonStrRef, kCreateViewButtonStrRef};
    _messages = {kCannotCreateStrRef, kTooFewChemicalsStrRef, kConfirmChemicalCreateStrRef, kConfirmChemicalBreakdownStrRef};
}

void LabStationPanel::onGUILoaded() {
    for (const char *tag : {"BTN_CREATE_HEALTH", "BTN_CREATE_STIMS", "BTN_CREATE_MINES", "BTN_CREATE_GRENADES"}) {
        _createFilters.push_back(findControl<Button>(tag));
    }
    CraftingPanel::onGUILoaded();
}

void LabStationPanel::open(const std::shared_ptr<Creature> &character) {
    _createGroup = static_cast<int>(CreateGroup::Health);
    openFor(character, breakdownFactorOf(*character));
}

std::shared_ptr<TwoDA> LabStationPanel::createTable() const {
    return _game.chemicalCreate();
}

std::string LabStationPanel::rowTag(const TwoDA &table, int row) const {
    return table.getString(row, "label");
}

int LabStationPanel::itemValue(const Item &item) const {
    return item.chemicalValue(_game.baseItemCostMultiplier(item.baseItemType()));
}

std::vector<std::shared_ptr<Item>> LabStationPanel::listBreakable(const std::shared_ptr<Creature> &character) const {
    // Only what the station makes is broken down, apart from the rows of the
    // excluded group.
    const auto &chemicalCreate = *_game.chemicalCreate();
    std::vector<std::shared_ptr<Item>> breakable;
    for (const auto &item : inventoryOf(character)) {
        if (item->plotFlag()) {
            continue;
        }
        const int row = chemicalCreate.indexByCellValue("label", item->tag());
        if (row == -1 ||
            chemicalCreate.getInt(row, "group") == kExcludedChemicalGroup ||
            item->cost(_game.baseItemCostMultiplier(item->baseItemType())) == 0) {
            continue;
        }
        breakable.push_back(item);
    }
    // In the order of their base items' store sort keys.
    std::stable_sort(breakable.begin(), breakable.end(), [](const auto &lhs, const auto &rhs) {
        return lhs->storePanelSort() < rhs->storePanelSort();
    });
    return breakable;
}

void LabStationPanel::close(bool) {
    _game.closeStation(Game::Screen::LabStation);
}

// END LabStationPanel

// Upgradable items

static constexpr int kEquippedStrRef = 32346;
static constexpr int kNoneStrRef = 363;
static constexpr int kUpgradeCountStrRef = 42026;
static constexpr int kCannotWearStrRef = 42489;
static constexpr int kUpgradeTooHighStrRef = 116713;
static constexpr int kNoCrystalDescriptionStrRef = 32172;
static constexpr float kUnavailableSlotAlpha = 0.25f;

// TSL: the item types of melee and ranged weapons.
static constexpr int kFirstMeleeItemType = 0;
static constexpr int kLastMeleeItemType = 2;
static constexpr int kFirstRangedItemType = 3;
static constexpr int kLastRangedItemType = 5;

// KotOR: the kind of item each upgrade.2da UpgradeType fits.
static constexpr std::array<UpgradableType, 10> kK1UpgradeKinds {
    UpgradableType::Lightsaber,
    UpgradableType::Melee, UpgradableType::Melee, UpgradableType::Melee,
    UpgradableType::Ranged, UpgradableType::Ranged, UpgradableType::Ranged, UpgradableType::Ranged,
    UpgradableType::Armor, UpgradableType::Armor};

// KotOR names the upgrades an item takes in its properties' upgrade rows, the
// last of them read deciding; TSL upgrades an item with an upgrade level by its
// item type.
static std::optional<UpgradableType> upgradableTypeOf(const Item &item, const Game &game, const TwoDA &upgrades) {
    if (game.isTSL()) {
        const int itemType = item.itemType();
        if (item.upgradeLevel() == 0) {
            return std::nullopt;
        }
        if (itemType >= kFirstMeleeItemType && itemType <= kLastMeleeItemType) {
            return UpgradableType::Melee;
        }
        if (itemType >= kFirstRangedItemType && itemType <= kLastRangedItemType) {
            return UpgradableType::Ranged;
        }
        if (itemType >= Item::kDoubleLightsaberItemType && itemType <= Item::kLightsaberItemType) {
            return UpgradableType::Lightsaber;
        }
        if (itemType >= Item::kArmorClass4ItemType && itemType <= Item::kLastBodyItemType) {
            return UpgradableType::Armor;
        }
        return std::nullopt;
    }
    std::optional<UpgradableType> type;
    // The use properties are read before the others, each in their order.
    for (const bool use : {true, false}) {
        for (const auto &property : item.properties()) {
            if (Item::isUseProperty(property.propertyName) != use || property.upgradeType == Item::PropertyEntry::kNoUpgrade) {
                continue;
            }
            // A blank cell, or a row past the table, reads as 0. A value that
            // names no kind leaves the kind as the earlier properties made it.
            const int value = upgrades.getInt(property.upgradeType, "UpgradeType", 0);
            if (value >= 0 && value < static_cast<int>(kK1UpgradeKinds.size())) {
                type = kK1UpgradeKinds[value];
            }
        }
    }
    return type;
}

// The count on a workbench's item and upgrade rows, whether or not the row
// is chosen or can be chosen.
static glm::vec3 upgradeEntryCountColor(const Game &game) {
    return game.isTSL() ? kTSLColorUpgradeText : kK1ColorBlue;
}

// A list row for an upgradable item; one worn is marked so.
static ListBox::Item upgradableEntry(Game &game, ServicesView &services, const UpgradableItem &entry, int index,
                                     std::shared_ptr<graphics::Texture> frame) {
    auto row = listEntry(game, *entry.item.resolve(), index, std::move(frame));
    row.iconTextColor = upgradeEntryCountColor(game);
    if (entry.slot) {
        row.text += " (" + services.resource.strings.getText(kEquippedStrRef) + ")";
    }
    return row;
}

// END Upgradable items

// UpgradeItemsPanel

UpgradeItemsPanel::UpgradeItemsPanel(Game &game, ServicesView &services) :
    GameGUI(game, services) {
    _resRef = guiResRef("upgradeitems");
}

void UpgradeItemsPanel::onGUILoaded() {
    _controls.BTN_BACK = findControl<Button>("BTN_BACK");
    _controls.BTN_UPGRADEITEM = findControl<Button>("BTN_UPGRADEITEM");
    _controls.LB_DESCRIPTION = findControl<ListBox>("LB_DESCRIPTION");
    _controls.LB_ITEMS = findControl<ListBox>("LB_ITEMS");
    _controls.LB_ITEMS->setSelectionMode(ListBox::SelectionMode::OnHoverKept);
    // KotOR's entries are framed blue with a yellow hilight, their fills
    // taking the same colours.
    auto &protoItem = _controls.LB_ITEMS->protoItem();
    protoItem.setBorderColor(kK1ColorBlue);
    protoItem.setHilightColor(kK1ColorYellow);
    _controls.LB_ITEMS->setOnItemClick([this](const std::string &tag) {
        pick(std::stoi(tag));
    });
    _controls.BTN_UPGRADEITEM->setOnClick([this]() {
        // Nothing is chosen while the list is empty.
        const int row = _controls.LB_ITEMS->selectedItemIndex();
        if (row >= 0) {
            pick(row);
        }
    });
    _controls.BTN_BACK->setOnClick([this]() {
        _onBack();
    });
}

void UpgradeItemsPanel::open(std::vector<UpgradableItem> items,
                             const std::shared_ptr<Item> &selected,
                             std::function<void(const UpgradableItem &)> onPick,
                             std::function<void()> onBack) {
    _items = std::move(items);
    _onPick = std::move(onPick);
    _onBack = std::move(onBack);
    _controls.LB_ITEMS->clearItems();
    int selectedRow = 0;
    for (size_t i = 0; i < _items.size(); ++i) {
        const int row = static_cast<int>(i);
        const auto item = _items[i].item.resolve();
        if (item == selected) {
            selectedRow = row;
        }
        _controls.LB_ITEMS->addItem(upgradableEntry(_game, _services, _items[i], row, itemFrameTexture(item->stackSize())));
    }
    _controls.LB_DESCRIPTION->clearItems();
    _describedRow.reset();
    if (!_items.empty()) {
        _controls.LB_ITEMS->setSelectedItemIndex(selectedRow);
        describe(selectedRow);
    }
}

void UpgradeItemsPanel::clear() {
    _items.clear();
    _controls.LB_ITEMS->clearItems();
    _controls.LB_DESCRIPTION->clearItems();
    _describedRow.reset();
}

bool UpgradeItemsPanel::handle(const input::Event &event) {
    if (event.type == input::EventType::KeyDown &&
        !event.key.repeat &&
        event.key.code == input::KeyCode::Escape) {
        _services.audio.mixer.play(_services.game.guiSounds.getOnClick(), AudioType::Sound);
        _onBack();
        return true;
    }
    // The list holds the keyboard; its entry clicks as it is chosen.
    if (event.type == input::EventType::KeyDown &&
        handleListKey(_services, event.key, *_controls.LB_ITEMS, [this](int row) {
            _services.audio.mixer.play(_services.game.guiSounds.getOnClick(), AudioType::Sound);
            pick(row);
        })) {
        return true;
    }
    return GameGUI::handle(event);
}

void UpgradeItemsPanel::update(float dt) {
    GameGUI::update(dt);
    const int row = _controls.LB_ITEMS->selectedItemIndex();
    if (row >= 0 && _describedRow != row) {
        describe(row);
    }
}

void UpgradeItemsPanel::describe(int row) {
    _describedRow = row;
    _controls.LB_DESCRIPTION->clearItems();
    _controls.LB_DESCRIPTION->addTextLinesAsItems(describedItem(_game, _services, *_items[row].item.resolve()));
}

void UpgradeItemsPanel::pick(int row) {
    _onPick(_items[row]);
}

// END UpgradeItemsPanel

// ItemUpgradePanel

// A lightsaber's colour crystal slot sits between its two crystal slots,
// which take the same upgrades.
static constexpr int kLightsaberFirstCrystalSlot = 0;
static constexpr int kLightsaberColourSlot = 1;
static constexpr int kLightsaberSecondCrystalSlot = 2;

// The 3D view: the light model whose hooks hold the camera and the item, the
// room it stands in, its view angle and clip planes, and the turn of an item
// standing on no hook.
static constexpr char kUpgradeLightModel[] = "upgitem_light";
static constexpr char kGUIRoomModel[] = "gui3d_room";
static constexpr float kItemViewAngle = 22.726f;
static constexpr float kItemViewClipPlaneNear = 0.1f;
static constexpr float kItemViewClipPlaneFar = 10000.0f;
static constexpr float kItemSpinDegreesPerSecond = -70.0f;
// Two base items, the stun baton and the blaster carbine, have hooks of their
// own beside their item type's.
static constexpr int kMeleeItemType = 0;
static constexpr int kStunBatonBaseItem = 1;
static constexpr int kRangedItemType = 4;
static constexpr int kBlasterCarbineBaseItem = 20;
// TSL's armour item types from this one up to the last body item type share
// the hooks of the first armour item type.
static constexpr int kTSLFirstSharedArmourItemType = 36;
static constexpr char kTSLSharedArmourHooks[] = "31";
// Armour is shown in an armour look of this appearance.2da row.
static constexpr int kArmourAppearanceRow = 137;
// KotOR's armour looks run from a to j, TSL's from a to n.
static constexpr int kK1ArmourLookCount = 10;
static constexpr int kTSLArmourLookCount = 14;

// The look armour is shown in, by its base item's body variation: a variation
// past the looks shows the last look.
static int armourLook(const Item &item, bool tsl) {
    const int lookCount = tsl ? kTSLArmourLookCount : kK1ArmourLookCount;
    return std::min(item.baseBodyVariation(), lookCount - 1);
}

// The hooks of the light model are named for the item's type: the camera
// hook and the rotate hook end in this.
static std::string hookSuffix(const Item &item, bool tsl) {
    const int itemType = item.itemType();
    if (tsl && itemType >= kTSLFirstSharedArmourItemType && itemType <= Item::kLastBodyItemType) {
        return kTSLSharedArmourHooks;
    }
    const bool ownHooks = (itemType == kMeleeItemType && item.baseItemType() == kStunBatonBaseItem) ||
                          (itemType == kRangedItemType && item.baseItemType() == kBlasterCarbineBaseItem);
    return std::to_string(itemType) + (ownHooks ? "a" : "");
}

// A 3D view is not built without the light model, whose hooks hold the camera
// and the item.
static std::shared_ptr<graphics::Model> requiredModel(IModels &models, const std::string &resRef) {
    auto model = models.get(resRef);
    if (!model) {
        throw ResourceNotFoundException("Model not found: " + resRef);
    }
    return model;
}

ItemUpgradePanel::ItemUpgradePanel(Game &game, ServicesView &services) :
    GameGUI(game, services) {
    _resRef = guiResRef("upgrade");
}

// The slots of each kind of item in each game, in UpgradableType order: the
// upgrade type each takes, the icon it shows empty, its name and its button.
const std::vector<ItemUpgradePanel::SlotInfo> &ItemUpgradePanel::slotInfos() const {
    static const std::array<std::vector<SlotInfo>, 4> kK1Slots {
        std::vector<SlotInfo> {{0, "i_powerc", 36977, "BTN_UPGRADE31"}, {std::nullopt, "i_colorc", 36978, "BTN_UPGRADE32"}, {0, "i_powerc", 36977, "BTN_UPGRADE33"}},
        std::vector<SlotInfo> {{4, "i_scope", 32487, "BTN_UPGRADE41"}, {5, "i_energy", 32488, "BTN_UPGRADE42"}, {6, "i_beam", 32489, "BTN_UPGRADE43"}, {7, "i_hair", 32490, "BTN_UPGRADE44"}},
        std::vector<SlotInfo> {{1, "i_vcell", 32493, "BTN_UPGRADE31"}, {2, "i_durasteel", 32494, "BTN_UPGRADE32"}, {3, "i_imp_eng", 32495, "BTN_UPGRADE33"}},
        std::vector<SlotInfo> {{8, "i_armorrein", 32491, "BTN_UPGRADE42"}, {9, "i_armorrein", 32492, "BTN_UPGRADE43"}}};
    static const std::array<std::vector<SlotInfo>, 4> kTSLSlots {
        std::vector<SlotInfo> {{0, "i_powerc", 48473, "BTN_UPGRADE31_LS"}, {std::nullopt, "i_powerc", 48472, "BTN_UPGRADE32_LS"}, {0, "i_powerc", 48473, "BTN_UPGRADE33_LS"}, {1, "i_lens", 48464, "BTN_UPGRADE34_LS"}, {2, "i_emitter", 48465, "BTN_UPGRADE35_LS"}, {3, "i_lsenergy", 48466, "BTN_UPGRADE36_LS"}},
        std::vector<SlotInfo> {{4, "i_scope", 48471, "BTN_UPGRADE31"}, {5, "i_energy", 48470, "BTN_UPGRADE32"}, {6, "i_imp_eng", 48469, "BTN_UPGRADE33"}},
        std::vector<SlotInfo> {{7, "i_grip", 48467, "BTN_UPGRADE31"}, {8, "i_edge", 48468, "BTN_UPGRADE32"}, {9, "i_vcell", 48466, "BTN_UPGRADE33"}},
        std::vector<SlotInfo> {{10, "i_armorrein", 48475, "BTN_UPGRADE31"}, {11, "i_armorrein", 48474, "BTN_UPGRADE32"}}};
    const auto &slots = _game.isTSL() ? kTSLSlots : kK1Slots;
    return slots[static_cast<size_t>(_type)];
}

void ItemUpgradePanel::onGUILoaded() {
    _upgradeTable = getRequiredTwoDA(_services.resource.twoDas, "upgrade");
    _crystalTable = getRequiredTwoDA(_services.resource.twoDas, "upcrystals");
    const bool tsl = _game.isTSL();
    // The armour looks the 3D view shows armour in, a to j or a to n.
    const auto appearance = getRequiredTwoDA(_services.resource.twoDas, "appearance");
    _armourLooks.clear();
    for (int look = 0; look < (tsl ? kTSLArmourLookCount : kK1ArmourLookCount); ++look) {
        const std::string letter(1, static_cast<char>('a' + look));
        _armourLooks.push_back(ArmourLook {
            boost::to_lower_copy(appearance->getString(kArmourAppearanceRow, "model" + letter)),
            boost::to_lower_copy(appearance->getString(kArmourAppearanceRow, "tex" + letter))});
    }
    _controls.BTN_ASSEMBLE = findControl<Button>("BTN_ASSEMBLE");
    _controls.BTN_BACK = findControl<Button>("BTN_BACK");
    _controls.LBL_DESCBG = findControl<Label>("LBL_DESCBG");
    _controls.LBL_DESCBG_LS = findControl<Label>("LBL_DESCBG_LS");
    _controls.LBL_PROPERTY = findControl<Label>("LBL_PROPERTY");
    _controls.LBL_SLOTNAME = findControl<Label>("LBL_SLOTNAME");
    _controls.LBL_TITLE = findControl<Label>("LBL_TITLE");
    _controls.LB_DESC = findControl<ListBox>("LB_DESC");
    _controls.LB_ITEMS = findControl<ListBox>("LB_ITEMS");
    _controls.THREE_D_MODEL = findControl<Label>("3D_MODEL");
    _controls.THREE_D_MODEL_LS = findControl<Label>("3D_MODEL_LS");
    if (tsl) {
        _controls.BTN_OK = findControl<Button>("BTN_OK");
    } else {
        _controls.LBL_LSSLOTNAME = findControl<Label>("LBL_LSSLOTNAME");
        _controls.LBL_UPGRADES = findControl<Label>("LBL_UPGRADES");
        _controls.LBL_UPGRADE_COUNT = findControl<Label>("LBL_UPGRADE_COUNT");
        _controls.LB_DESC_LS = findControl<ListBox>("LB_DESC_LS");
    }

    // The panel's frames are KotOR's blue or TSL's upgrade border, and the
    // list's rows KotOR's blue with a yellow hilight or TSL's upgrade border
    // with an upgrade text hilight, the hilight pulsing; each fill takes its
    // frame's colour.
    const glm::vec3 frameColor = tsl ? kTSLColorUpgradeBorder : kK1ColorBlue;
    const auto frame = [&frameColor](Control &control) {
        control.setBorderColor(frameColor);
    };
    frame(*_controls.LBL_SLOTNAME);
    frame(*_controls.LBL_PROPERTY);
    frame(*_controls.LB_ITEMS);
    if (!tsl) {
        frame(*_controls.LBL_LSSLOTNAME);
        frame(*_controls.LBL_UPGRADE_COUNT);
        frame(*_controls.LBL_UPGRADES);
    }
    auto &protoItem = _controls.LB_ITEMS->protoItem();
    protoItem.setBorderColor(tsl ? kTSLColorUpgradeBorder : kK1ColorBlue);
    protoItem.setHilightColor(tsl ? kTSLColorUpgradeText : kK1ColorYellow);
    protoItem.setHilightPulsing(true, false);

    const std::vector<std::string> suffixes = tsl
                                                  ? std::vector<std::string> {"31", "32", "33", "31_LS", "32_LS", "33_LS", "34_LS", "35_LS", "36_LS"}
                                                  : std::vector<std::string> {"31", "32", "33", "41", "42", "43", "44"};
    for (const auto &suffix : suffixes) {
        const auto tag = "BTN_UPGRADE" + suffix;
        auto button = findControl<Button>(tag);
        _controls.slots[tag] = {button, findControl<Label>("LBL_UPGRADE" + suffix)};
        button->setOnClick([this, tag]() {
            for (size_t slot = 0; slot < _slots.size(); ++slot) {
                if (_slots[slot].button->tag() == tag) {
                    clickSlot(static_cast<int>(slot));
                    return;
                }
            }
        });
    }
    _controls.LB_ITEMS->setSelectionMode(ListBox::SelectionMode::OnHoverKept);
    _controls.LB_ITEMS->setOnItemClick([this](const std::string &tag) {
        pick(std::stoi(tag));
    });
    if (tsl) {
        _controls.BTN_OK->setOnClick([this]() {
            const int entry = _controls.LB_ITEMS->selectedItemIndex();
            if (entry >= 0) {
                pick(entry);
            }
        });
    }
    _controls.BTN_ASSEMBLE->setOnClick([this]() {
        assemble();
    });
    _controls.BTN_BACK->setOnClick([this]() {
        if (_listSlot) {
            closeList();
        } else {
            cancel();
        }
    });
}

// The upcrystals.2da column naming each lightsaber item type's template of a
// colour; other item types have none.
static std::optional<std::string> crystalColumn(int itemType) {
    switch (itemType) {
    case Item::kDoubleLightsaberItemType:
        return "DoubleMdlVar";
    case Item::kShortLightsaberItemType:
        return "ShortMdlVar";
    case Item::kLightsaberItemType:
        return "LongMdlVar";
    default:
        return std::nullopt;
    }
}

// A row with a blank template, or a blank cell for the item type, lists no
// colour.
std::optional<int> ItemUpgradePanel::crystalRow(const Item &item) const {
    const auto column = crystalColumn(item.itemType());
    if (!column) {
        return std::nullopt;
    }
    for (int row = 0; row < _crystalTable->getRowCount(); ++row) {
        const auto colour = _crystalTable->getString(row, *column);
        if (!colour.empty() && boost::iequals(colour, item.tag()) && !_crystalTable->getString(row, "Template").empty()) {
            return row;
        }
    }
    return std::nullopt;
}

std::shared_ptr<Object> ItemUpgradePanel::partyInventory() const {
    return _game.party().sharedInventoryReceiver(_game.party().getLeader());
}

void ItemUpgradePanel::open(std::shared_ptr<Item> item,
                            UpgradableType type,
                            const std::shared_ptr<Creature> &wearer,
                            std::function<void(std::shared_ptr<Item>)> onClose) {
    _item = std::move(item);
    _type = type;
    _wearer = wearer;
    _onClose = std::move(onClose);
    _backup = _game.newItemClone(*_item);
    _taken.clear();
    _returned.clear();
    _slots.clear();
    for (const auto &[tag, controls] : _controls.slots) {
        controls.first->setVisible(false);
        controls.second->setVisible(false);
    }
    for (const auto &info : slotInfos()) {
        const auto &[button, label] = _controls.slots.at(info.button);
        Slot slot;
        slot.button = button;
        slot.label = label;
        _slots.push_back(std::move(slot));
    }
    // TSL titles the panel with the item and clears the slot texts; KotOR's
    // keep what they showed.
    if (_game.isTSL()) {
        _controls.LBL_TITLE->setTextMessage(_game.substituteLogTokens(_item->localizedName()));
        _controls.LBL_SLOTNAME->setTextMessage("");
        _controls.LBL_PROPERTY->setTextMessage("");
    }
    placeInstalledUpgrades();
    showSlots();
    refresh3D();
}

// Armour is shown in the model of its look; any other item in its own
// model, a gender-specific one's male model.
std::string ItemUpgradePanel::viewedModelName() const {
    if (_type == UpgradableType::Armor) {
        return _armourLooks[armourLook(*_item, _game.isTSL())].model;
    }
    return _item->modelResRef('m');
}

// Each 3D view shows the light model standing in the GUI room; a room that
// does not load leaves the light model alone.
void ItemUpgradePanel::build3DViews() {
    auto &models = _services.resource.models;
    const auto lightModel = requiredModel(models, kUpgradeLightModel);
    const auto roomModel = models.get(kGUIRoomModel);
    auto build = [&](ItemView &view, const gui::Label &control) {
        _services.scene.graphs.reset(view.sceneName);
        auto &graph = _services.scene.graphs.get(view.sceneName);
        auto light = graph.newModel(*lightModel, ModelUsage::GUI);
        const auto &extent = control.extent();
        SceneInitializer {graph}
            .aspect(extent.width / static_cast<float>(extent.height))
            .depth(kItemViewClipPlaneNear, kItemViewClipPlaneFar)
            .perspective(glm::radians(kItemViewAngle))
            .room(roomModel ? graph.newModel(*roomModel, ModelUsage::Room) : nullptr)
            .modelSupplier([light](ISceneGraph &) { return light; })
            .invoke();
        view.light = std::move(light);
    };
    build(_itemView, *_controls.THREE_D_MODEL);
    build(_lightsaberView, *_controls.THREE_D_MODEL_LS);
}

ItemUpgradePanel::ItemView &ItemUpgradePanel::shownView() {
    return _type == UpgradableType::Lightsaber ? _lightsaberView : _itemView;
}

void ItemUpgradePanel::removeShownItem() {
    if (!_itemModel) {
        return;
    }
    auto &view = shownView();
    view.light->detach(*_itemModel);
    _services.scene.graphs.get(view.sceneName).removeRoot(*_itemModel);
    _itemModel.reset();
    _itemTexture.reset();
}

// The item is shown in the 3D view of its kind, seen through the light
// model's camera hook for its type, or its plain camera hook when it has none
// for the type. It hangs on the light model's rotate hook for its type, which
// the light model's turn carries round; on a light model without one it
// stands in the room and turns by itself.
void ItemUpgradePanel::refresh3D() {
    const bool tsl = _game.isTSL();
    const bool lightsaber = _type == UpgradableType::Lightsaber;
    auto &shown = lightsaber ? *_controls.THREE_D_MODEL_LS : *_controls.THREE_D_MODEL;
    auto &other = lightsaber ? *_controls.THREE_D_MODEL : *_controls.THREE_D_MODEL_LS;
    other.setSceneName("");
    auto &view = shownView();
    auto &graph = _services.scene.graphs.get(view.sceneName);
    auto &light = *view.light;
    const auto suffix = hookSuffix(*_item, tsl);
    const std::string cameraHook = light.getNodeByName("camerahook" + suffix) ? "camerahook" + suffix : "camerahook";
    auto &camera = graph.camera()->get();
    light.detach(camera);
    light.attach(cameraHook, camera);
    shown.setSceneName(view.sceneName);

    // An item whose model does not load is not shown: the view holds the
    // room and the light model alone.
    removeShownItem();
    const auto itemModel = _services.resource.models.get(viewedModelName());
    if (!itemModel) {
        return;
    }
    _itemModel = graph.newModel(*itemModel, ModelUsage::GUI);
    if (_type == UpgradableType::Armor) {
        // Armour is drawn with its look's texture of the item's texture
        // variation, a variation below 1 being 1; without that texture, with
        // its look model's first; without either, with the model's own. A
        // look naming no texture keeps the model's.
        const auto &look = _armourLooks[armourLook(*_item, tsl)];
        if (!look.texture.empty()) {
            const auto variation = str(boost::format("%02d") % std::max(1, _item->textureVariation()));
            _itemTexture = _services.resource.textures.get(look.texture + variation, TextureUsage::MainTex);
            if (!_itemTexture) {
                _itemTexture = _services.resource.textures.get(look.model + "01", TextureUsage::MainTex);
            }
            if (_itemTexture) {
                _itemModel->setMainTexture(_itemTexture.get());
            }
        }
    }
    const std::string rotateHook = "rotatehook" + suffix;
    if (light.getNodeByName(rotateHook)) {
        light.attach(rotateHook, *_itemModel);
        light.playAnimation("rotate", nullptr, AnimationProperties::fromFlags(AnimationFlags::loop));
    } else {
        graph.addRoot(_itemModel);
    }
    if (_item->isPoweredItem()) {
        _itemModel->playAnimation("powered", nullptr, AnimationProperties::fromFlags(AnimationFlags::loop));
    }
    if (_type == UpgradableType::Armor) {
        _itemModel->playAnimation("neutral", nullptr, AnimationProperties::fromFlags(AnimationFlags::loop));
    }
}

// The slots show the upgrades the item holds, made afresh from their
// templates; an upgrade.2da row goes to the first free slot of its type.
void ItemUpgradePanel::placeInstalledUpgrades() {
    const bool tsl = _game.isTSL();
    const auto &infos = slotInfos();
    if (_type == UpgradableType::Lightsaber) {
        const int row = *crystalRow(*_item);
        _slots[kLightsaberColourSlot].held = _game.newItemFromBlueprint(boost::to_lower_copy(_crystalTable->getString(row, "Template")));
    }
    // TSL dims every slot first; a slot it lights for a row stays lit.
    std::vector<bool> lit(_slots.size(), false);
    if (tsl) {
        for (const auto &place : _slots) {
            place.label->setBorderAlpha(kUnavailableSlotAlpha);
        }
    }
    const auto inventory = partyInventory();
    for (int row = 0; row < _upgradeTable->getRowCount(); ++row) {
        // A row with a blank upgrade type takes the first type.
        const int upgradeType = _upgradeTable->getInt(row, "UpgradeType", 0);
        const auto tag = boost::to_lower_copy(_upgradeTable->getString(row, "Template"));
        for (int slot = 0; slot < static_cast<int>(_slots.size()); ++slot) {
            auto &place = _slots[slot];
            if (infos[slot].upgradeType != upgradeType || place.held) {
                continue;
            }
            place.tag = tag;
            bool installed = false;
            if (tsl) {
                place.row = row;
                const auto &upgradeSlots = _item->upgradeSlots();
                installed = upgradeSlots[slot] == row;
                // The second crystal slot holds a crystal the first doesn't;
                // its row and template are left as they are.
                auto &second = _slots[kLightsaberSecondCrystalSlot];
                if (!installed &&
                    _type == UpgradableType::Lightsaber &&
                    slot == kLightsaberFirstCrystalSlot &&
                    upgradeSlots[kLightsaberSecondCrystalSlot] == row &&
                    !second.held) {
                    second.held = _game.newItemFromBlueprint(tag);
                    break;
                }
            } else {
                place.row = row;
                installed = _item->isUpgradeInstalled(row);
            }
            if (installed) {
                place.held = _game.newItemFromBlueprint(tag);
            }
            if (tsl && !installed) {
                // A row not installed lights its slot once the party has an
                // upgrade for it at or below the item's upgrade level, and a
                // lightsaber's first crystal slot lights its second; a row
                // the party has nothing for dims a slot no row has lit.
                const auto available = inventory->getItemByTag(tag);
                if (!available) {
                    if (!lit[slot]) {
                        place.label->setBorderAlpha(kUnavailableSlotAlpha);
                    }
                } else if (available->upgradeLevel() <= _item->upgradeLevel()) {
                    lit[slot] = true;
                    place.label->setBorderAlpha(1.0f);
                    if (_type == UpgradableType::Lightsaber && slot == kLightsaberFirstCrystalSlot) {
                        _slots[kLightsaberSecondCrystalSlot].label->setBorderAlpha(1.0f);
                    }
                }
            } else if (!tsl && _type != UpgradableType::Lightsaber) {
                // KotOR dims a slot whose last row is neither installed nor
                // in the party's hands; a lightsaber's slots keep what they
                // showed.
                place.label->setBorderAlpha(installed || inventory->getItemByTag(tag) ? 1.0f : kUnavailableSlotAlpha);
            }
            break;
        }
    }
}

void ItemUpgradePanel::showSlots() {
    _listSlot.reset();
    const bool lightsaber = _type == UpgradableType::Lightsaber;
    for (const auto &place : _slots) {
        place.button->setVisible(true);
        place.label->setVisible(true);
    }
    _controls.LB_ITEMS->setVisible(false);
    _controls.BTN_ASSEMBLE->setVisible(true);
    _controls.LBL_DESCBG_LS->setVisible(lightsaber);
    _controls.LBL_DESCBG->setVisible(!lightsaber);
    if (_game.isTSL()) {
        _controls.BTN_OK->setVisible(false);
        _controls.LB_DESC->setVisible(true);
        _controls.LBL_SLOTNAME->setVisible(true);
        _controls.LBL_PROPERTY->setVisible(true);
    } else {
        // A KotOR lightsaber has a description and slot name of its own, and
        // no upgrade count.
        _controls.LBL_LSSLOTNAME->setVisible(lightsaber);
        _controls.LB_DESC_LS->setVisible(lightsaber);
        _controls.LB_DESC->setVisible(!lightsaber);
        _controls.LBL_SLOTNAME->setVisible(!lightsaber);
        _controls.LBL_PROPERTY->setVisible(!lightsaber);
        _controls.LBL_UPGRADES->setVisible(!lightsaber);
        _controls.LBL_UPGRADE_COUNT->setVisible(!lightsaber);
    }
    _controls.THREE_D_MODEL_LS->setVisible(lightsaber);
    _controls.THREE_D_MODEL->setVisible(!lightsaber);
    refreshSlots();
}

ListBox &ItemUpgradePanel::itemDescription() const {
    return _type == UpgradableType::Lightsaber && !_game.isTSL() ? *_controls.LB_DESC_LS : *_controls.LB_DESC;
}

void ItemUpgradePanel::refreshSlots() {
    const bool tsl = _game.isTSL();
    const auto &infos = slotInfos();
    for (size_t slot = 0; slot < _slots.size(); ++slot) {
        const auto &place = _slots[slot];
        auto icon = place.held ? place.held->icon() : nullptr;
        if (icon) {
            place.label->setBorderFill(icon);
        } else {
            place.label->setBorderFill(infos[slot].icon);
        }
        // TSL lights every slot holding an upgrade.
        if (tsl && place.held) {
            place.label->setBorderAlpha(1.0f);
        }
    }
    auto &description = itemDescription();
    description.clearItems();
    description.addTextLinesAsItems(describedItem(_game, _services, *_item));
}

void ItemUpgradePanel::onSelectionChanged(const std::string &control, bool selected) {
    GameGUI::onSelectionChanged(control, selected);
    if (!selected || _listSlot) {
        return;
    }
    for (size_t slot = 0; slot < _slots.size(); ++slot) {
        if (_slots[slot].button->tag() == control) {
            describeSlot(static_cast<int>(slot));
            return;
        }
    }
}

void ItemUpgradePanel::describeSlot(int slot) {
    const auto &place = _slots[slot];
    const auto &info = slotInfos()[slot];
    auto &strings = _services.resource.strings;
    // A held upgrade names the slot: TSL resolves the name's tokens, KotOR
    // shows it as it stands.
    if (_type == UpgradableType::Lightsaber && !_game.isTSL()) {
        _controls.LBL_LSSLOTNAME->setTextMessage(place.held ? place.held->localizedName() : strings.getText(info.nameStrRef));
        return;
    }
    if (_game.isTSL()) {
        _controls.LBL_SLOTNAME->setTextMessage(place.held ? _game.substituteLogTokens(place.held->localizedName()) : strings.getText(info.nameStrRef));
        if (place.held) {
            _controls.LBL_PROPERTY->setTextMessage(buildItemPropertyLines(*place.held, _game, _services, [](const Item::PropertyEntry &property) {
                return !Item::isUseProperty(property.propertyName);
            }));
        } else {
            // An empty weapon or armour slot names the item's properties keyed
            // to its row; an empty lightsaber slot names none.
            _controls.LBL_PROPERTY->setTextMessage(_type == UpgradableType::Lightsaber ? std::string() : buildKeyedPropertyText(*_item, *place.row, _game, _services));
        }
        return;
    }
    // KotOR names the slot, counts the party's upgrades for it and lists the
    // item's properties of its row, installed or not.
    const auto available = partyInventory()->getItemByTag(place.tag);
    _controls.LBL_SLOTNAME->setTextMessage(strings.getText(info.nameStrRef));
    _controls.LBL_UPGRADE_COUNT->setTextMessage(std::to_string(available ? available->stackSize() : 0));
    _controls.LBL_UPGRADES->setTextMessage(strings.getText(kUpgradeCountStrRef));
    _controls.LBL_PROPERTY->setTextMessage(buildKeyedPropertyText(*_item, *place.row, _game, _services));
}

void ItemUpgradePanel::clickSlot(int slot) {
    if (_game.isTSL() || _type == UpgradableType::Lightsaber) {
        showList(slot);
        return;
    }
    auto &place = _slots[slot];
    if (place.held) {
        _item->setUpgradeInstalled(*place.row, false);
        giveBack(place.held);
        place.held.reset();
        refreshSlots();
        describeSlot(slot);
        return;
    }
    // A slot the party has no upgrade for does nothing.
    const auto upgrade = partyInventory()->getItemByTag(place.tag);
    if (!upgrade) {
        return;
    }
    const int row = *place.row;
    _item->setUpgradeInstalled(row, true);
    // An item its wearer could no longer wear is asked about first.
    if (!_wearer.empty() && evaluateEquipmentAdmission(*_wearer.resolve(), *_item) != EquipmentCandidateReason::None) {
        _game.showConfirmPopup(
            _services.resource.strings.getText(kCannotWearStrRef),
            [this, slot, upgrade]() {
                install(slot, upgrade);
            },
            [this, row]() {
                _item->setUpgradeInstalled(row, false);
            });
        return;
    }
    install(slot, upgrade);
}

void ItemUpgradePanel::install(int slot, const std::shared_ptr<Item> &upgrade) {
    _slots[slot].held = take(upgrade);
    refreshSlots();
    describeSlot(slot);
}

// Leaving a list shows the slots again and describes the slot it was for.
void ItemUpgradePanel::closeList() {
    const int slot = *_listSlot;
    showSlots();
    describeSlot(slot);
}

void ItemUpgradePanel::showList(int slot) {
    _listSlot = slot;
    _candidates.clear();
    const bool tsl = _game.isTSL();
    const auto &place = _slots[slot];
    const auto &upgradeType = slotInfos()[slot].upgradeType;
    const auto inventory = partyInventory();
    if (!upgradeType) {
        // The colour crystal can be changed, never taken out.
        _candidates.push_back(Candidate {place.held, std::nullopt, true, false});
        for (int row = 0; row < _crystalTable->getRowCount(); ++row) {
            const auto tag = boost::to_lower_copy(_crystalTable->getString(row, "Template"));
            if (tag == place.held->tag()) {
                continue;
            }
            if (const auto crystal = inventory->getItemByTag(tag)) {
                _candidates.push_back(Candidate {crystal, row, false, false});
            }
        }
    } else {
        _candidates.push_back(Candidate {});
        if (place.held) {
            _candidates.push_back(Candidate {place.held, place.row, true, false});
        }
        // A lightsaber's two crystal slots never hold the same crystal.
        std::optional<int> pairedSlot;
        if (_type == UpgradableType::Lightsaber && slot == kLightsaberFirstCrystalSlot) {
            pairedSlot = kLightsaberSecondCrystalSlot;
        } else if (_type == UpgradableType::Lightsaber && slot == kLightsaberSecondCrystalSlot) {
            pairedSlot = kLightsaberFirstCrystalSlot;
        }
        for (int row = 0; row < _upgradeTable->getRowCount(); ++row) {
            if (_upgradeTable->getInt(row, "UpgradeType", 0) != upgradeType ||
                (place.held && place.row == row) ||
                (pairedSlot && _slots[*pairedSlot].held && _slots[*pairedSlot].row == row)) {
                continue;
            }
            const auto upgrade = inventory->getItemByTag(boost::to_lower_copy(_upgradeTable->getString(row, "Template")));
            if (!upgrade) {
                continue;
            }
            const bool disabled = tsl && _type != UpgradableType::Lightsaber && upgrade->upgradeLevel() > _item->upgradeLevel();
            _candidates.push_back(Candidate {upgrade, row, false, disabled});
        }
    }
    _controls.LB_ITEMS->clearItems();
    for (size_t i = 0; i < _candidates.size(); ++i) {
        const auto &candidate = _candidates[i];
        ListBox::Item entry;
        if (candidate.item) {
            entry = listEntry(_game, *candidate.item, static_cast<int>(i), itemFrameTexture(candidate.item->stackSize()));
            entry.iconTextColor = upgradeEntryCountColor(_game);
        } else {
            entry.tag = std::to_string(i);
            entry.text = _services.resource.strings.getText(kNoneStrRef);
        }
        // A greyed entry lights in the highlight colour; its count keeps its
        // colour.
        if (candidate.disabled) {
            entry.textColor = kTSLColorDisabledText;
            entry.borderColor = kTSLColorDisabledText;
            entry.hilightColor = kTSLColorNewHighlight;
        }
        _controls.LB_ITEMS->addItem(std::move(entry));
    }
    for (const auto &other : _slots) {
        other.button->setVisible(false);
        other.label->setVisible(false);
    }
    _controls.BTN_ASSEMBLE->setVisible(false);
    _controls.LB_ITEMS->setVisible(true);
    // The slot texts leave with the slots.
    _controls.LBL_SLOTNAME->setVisible(false);
    _controls.LBL_PROPERTY->setVisible(false);
    if (tsl) {
        _controls.BTN_OK->setVisible(true);
    } else {
        _controls.LBL_LSSLOTNAME->setVisible(false);
        _controls.LBL_UPGRADES->setVisible(false);
        _controls.LBL_UPGRADE_COUNT->setVisible(false);
    }
    _controls.THREE_D_MODEL->setVisible(false);
    _controls.THREE_D_MODEL_LS->setVisible(false);
    _controls.LB_ITEMS->setSelectedItemIndex(0);
    _describedCandidate.reset();
}

// TSL installs an upgrade as the first upgrade.2da row naming its template,
// whatever that row's upgrade type.
int ItemUpgradePanel::firstRowWithTemplateOf(int row) const {
    const auto tag = _upgradeTable->getString(row, "Template");
    int first = 0;
    while (!boost::iequals(_upgradeTable->getString(first, "Template"), tag)) {
        ++first;
    }
    return first;
}

void ItemUpgradePanel::update(float dt) {
    GameGUI::update(dt);
    // A shown item standing on no hook of the light model turns by itself.
    if (_itemModel && !_itemModel->parent()) {
        _itemModel->setLocalTransform(glm::rotate(glm::mat4(1.0f), glm::radians(kItemSpinDegreesPerSecond * dt), glm::vec3(0.0f, 0.0f, 1.0f)) * _itemModel->localTransform());
    }
    if (!_listSlot) {
        return;
    }
    const int entry = _controls.LB_ITEMS->selectedItemIndex();
    if (entry < 0 || _describedCandidate == entry) {
        return;
    }
    _describedCandidate = entry;
    auto &description = itemDescription();
    description.clearItems();
    // The entry that empties the slot describes nothing.
    const auto &candidate = _candidates[entry];
    if (!candidate.item) {
        return;
    }
    if (_game.isTSL() || _type != UpgradableType::Lightsaber) {
        description.addTextLinesAsItems(describedItem(_game, _services, *candidate.item));
        return;
    }
    // KotOR describes a crystal by its identified description, a stock one
    // when it has none, straight after the lightsaber's properties keyed to
    // it for a power slot.
    std::string text = candidate.item->descIdentified();
    if (text.empty()) {
        text = _services.resource.strings.getText(kNoCrystalDescriptionStrRef);
    }
    if (slotInfos()[*_listSlot].upgradeType) {
        text = buildKeyedPropertyText(*_item, *candidate.row, _game, _services) + text;
    }
    description.addTextLinesAsItems(text);
}

void ItemUpgradePanel::pick(int entry) {
    const auto candidate = _candidates[entry];
    const int slot = *_listSlot;
    auto &place = _slots[slot];
    if (candidate.disabled) {
        _game.showMessagePopup(_services.resource.strings.getText(kUpgradeTooHighStrRef), nullptr, {}, kCraftingPopupColors);
        return;
    }
    if (candidate.current) {
        closeList();
        return;
    }
    if (!slotInfos()[slot].upgradeType) {
        swapLightsaber(*candidate.row);
        giveBack(place.held);
        place.held = take(candidate.item);
        closeList();
        refresh3D();
        return;
    }
    const bool tsl = _game.isTSL();
    if (place.held) {
        // The upgrade is counted before it rejoins a stack.
        if (tsl) {
            _item->removeUpgrade(*place.held, slot);
            _item->setUpgradeSlot(slot, std::nullopt);
        } else {
            _item->setUpgradeInstalled(*place.row, false);
        }
        giveBack(place.held);
        place.held.reset();
    }
    if (candidate.item) {
        place.held = take(candidate.item);
        if (tsl) {
            // The slot answers to the entry's row; the item records the first
            // row with its template.
            place.row = candidate.row;
            _item->setUpgradeSlot(slot, firstRowWithTemplateOf(*candidate.row));
            _item->addUpgrade(*place.held, slot);
        } else {
            place.row = candidate.row;
            _item->setUpgradeInstalled(*candidate.row, true);
        }
    }
    closeList();
}

// The colour is the lightsaber itself: another template of the chosen colour
// takes its place, keeping its upgrades.
void ItemUpgradePanel::swapLightsaber(int crystalRow) {
    const auto column = *crystalColumn(_item->itemType());
    auto lightsaber = _game.newItemFromBlueprint(boost::to_lower_copy(_crystalTable->getString(crystalRow, column)));
    lightsaber->setIdentified(true);
    lightsaber->setUpgrades(_item->upgrades());
    lightsaber->setStolen(_item->stolen());
    if (_game.isTSL()) {
        for (int slot = 0; slot < static_cast<int>(_slots.size()); ++slot) {
            if (slot == kLightsaberColourSlot) {
                continue;
            }
            if (_slots[slot].held) {
                lightsaber->addUpgrade(*_slots[slot].held, slot);
            }
            lightsaber->setUpgradeSlot(slot, _item->upgradeSlots()[slot]);
        }
    }
    destroyItem();
    _item = lightsaber;
}

// An item upgraded where it lies leaves its holder as it ceases to exist.
void ItemUpgradePanel::destroyItem() {
    if (const auto holder = _game.getObjectById(_item->owner())) {
        holder->removeItemStack(_item);
    }
    _game.destroyRuntimeObjectGraph(_item);
}

// An upgrade taken and given back this opening is neither; one given back
// that the item held when opened is a stack to take it from again on Back.
std::shared_ptr<Item> ItemUpgradePanel::take(const std::shared_ptr<Item> &stack) {
    auto returned = std::find(_returned.begin(), _returned.end(), stack);
    auto taken = takeEquipmentCandidate(_game, *partyInventory(), stack);
    if (returned != _returned.end()) {
        _returned.erase(returned);
    } else {
        _taken.push_back(taken);
    }
    return taken;
}

void ItemUpgradePanel::giveBack(const std::shared_ptr<Item> &upgrade) {
    auto taken = std::find(_taken.begin(), _taken.end(), upgrade);
    const bool takenNow = taken != _taken.end();
    if (takenNow) {
        _taken.erase(taken);
    }
    auto stack = partyInventory()->addItem(upgrade);
    if (!takenNow) {
        _returned.push_back(std::move(stack));
    }
}

void ItemUpgradePanel::onClick(const std::string &control) {
    if (control == "BTN_ASSEMBLE") {
        _services.audio.mixer.play(_services.game.guiSounds.getUpgrade(), AudioType::Sound);
        return;
    }
    GameGUI::onClick(control);
}

bool ItemUpgradePanel::handle(const input::Event &event) {
    if (event.type == input::EventType::KeyDown &&
        !event.key.repeat &&
        event.key.code == input::KeyCode::Escape) {
        _services.audio.mixer.play(_services.game.guiSounds.getOnClick(), AudioType::Sound);
        if (_listSlot) {
            closeList();
        } else {
            cancel();
        }
        return true;
    }
    return GameGUI::handle(event);
}

void ItemUpgradePanel::assemble() {
    _game.destroyRuntimeObjectGraph(_backup);
    _backup.reset();
    finish();
}

// Back restores the item as it was opened and every upgrade moved.
void ItemUpgradePanel::cancel() {
    destroyItem();
    _item = std::move(_backup);
    const auto inventory = partyInventory();
    for (const auto &upgrade : _taken) {
        for (auto &place : _slots) {
            if (place.held == upgrade) {
                place.held.reset();
            }
        }
        inventory->addItem(upgrade);
    }
    _taken.clear();
    for (const auto &stack : _returned) {
        bool last = false;
        if (inventory->removeItem(stack, last) && last) {
            _game.destroyRuntimeObjectGraph(stack);
        }
    }
    _returned.clear();
    finish();
}

// The upgrades left in the slots are consumed, or were only shown.
void ItemUpgradePanel::finish() {
    for (auto &place : _slots) {
        if (place.held) {
            _game.destroyRuntimeObjectGraph(place.held);
        }
    }
    _slots.clear();
    _taken.clear();
    _returned.clear();
    _candidates.clear();
    removeShownItem();
    _controls.THREE_D_MODEL->setSceneName("");
    _controls.THREE_D_MODEL_LS->setSceneName("");
    auto item = std::move(_item);
    _onClose(std::move(item));
}

// END ItemUpgradePanel

// UpgradeSelectionGUI

UpgradeSelectionGUI::UpgradeSelectionGUI(Game &game, ServicesView &services) :
    GameGUI(game, services),
    _items(std::make_unique<UpgradeItemsPanel>(game, services)),
    _upgrade(std::make_unique<ItemUpgradePanel>(game, services)) {
    _resRef = guiResRef("upgradesel");
    // TSL's workbench also creates items. KotOR lists the items of a kind on
    // a panel of their own, which TSL only draws, empty, under the upgrade
    // panel.
    if (game.isTSL()) {
        _creation = std::make_unique<ItemCreationPanel>(game, services);
    }
}

void UpgradeSelectionGUI::init() {
    GameGUI::init();
    _upgrade->init();
    _items->init();
    if (_game.isTSL()) {
        _creation->init();
    }
}

void UpgradeSelectionGUI::onGUILoaded() {
    const bool tsl = _game.isTSL();
    _controls.BTN_BACK = findControl<Button>("BTN_BACK");
    _controls.BTN_UPGRADEITEMS = findControl<Button>("BTN_UPGRADEITEMS");
    _controls.BTN_BACK->setOnClick([this]() {
        close();
    });
    if (tsl) {
        _controls.BTN_CREATEITEMS = findControl<Button>("BTN_CREATEITEMS");
        _controls.LB_DESCRIPTION = findControl<ListBox>("LB_DESCRIPTION");
        _controls.LB_UPGRADELIST = findControl<ListBox>("LB_UPGRADELIST");
        _controls.BTN_CREATEITEMS->setOnClick([this]() {
            openCreation();
        });
        _filterButtons = {
            {UpgradeFilter::All, findControl<Button>("BTN_ALL")},
            {UpgradeFilter::Lightsaber, findControl<Button>("BTN_LIGHTSABER")},
            {UpgradeFilter::Ranged, findControl<Button>("BTN_RANGED")},
            {UpgradeFilter::Melee, findControl<Button>("BTN_MELEE")},
            {UpgradeFilter::Armor, findControl<Button>("BTN_ARMOR")}};
        for (const auto &[filter, button] : _filterButtons) {
            button->setOnClick([this, filter = filter]() {
                _filter = filter;
                populate();
            });
        }
        auto &list = *_controls.LB_UPGRADELIST;
        list.setSelectionMode(ListBox::SelectionMode::OnHoverKept);
        // The rows keep the layout's text colour.
        auto &protoItem = list.protoItem();
        protoItem.setBorderColor(kTSLColorUpgradeBorder);
        protoItem.setHilightColor(kTSLColorUpgradeText);
        list.setOnItemClick([this](const std::string &tag) {
            beginUpgrade(_rows[std::stoi(tag)], false);
        });
        _controls.BTN_UPGRADEITEMS->setOnClick([this]() {
            // Nothing is chosen while the list is empty.
            const int row = _controls.LB_UPGRADELIST->selectedItemIndex();
            if (row >= 0) {
                beginUpgrade(_rows[row], false);
            }
        });
        return;
    }
    _kindButtons = {
        {UpgradableType::Lightsaber, findControl<Button>("BTN_LIGHTSABER"), findControl<Label>("LBL_LSABER")},
        {UpgradableType::Ranged, findControl<Button>("BTN_RANGED"), findControl<Label>("LBL_RANGED")},
        {UpgradableType::Melee, findControl<Button>("BTN_MELEE"), findControl<Label>("LBL_MELEE")},
        {UpgradableType::Armor, findControl<Button>("BTN_ARMOR"), findControl<Label>("LBL_ARMOR")}};
    for (const auto &kindButton : _kindButtons) {
        // A kind button is a radio button: pointing at it does nothing, and
        // pressing it takes up its kind before its list opens.
        kindButton.button->setSelectedOnHover(false);
        kindButton.button->setOnClick([this, kind = kindButton.kind]() {
            describeKind(kind);
            openItems(kind, nullptr);
        });
        // The kind buttons are framed blue, their hilight yellow and pulsing,
        // their fill taking the same colours.
        kindButton.button->setBorderColor(kK1ColorBlue);
        kindButton.button->setHilightColor(kK1ColorYellow);
        kindButton.button->setHilightPulsing(true, false);
    }
    // Upgrade Items takes the colours of the kind taken up, its fill
    // included, and does nothing for a kind the party has no item of.
    _upgradeItemsTextColor = _controls.BTN_UPGRADEITEMS->text().color;
    _controls.BTN_UPGRADEITEMS->setOnClick([this]() {
        if (_activeKind) {
            openItems(*_activeKind, nullptr);
        }
    });
}

void UpgradeSelectionGUI::open(UpgradeScreenRequest request) {
    _character = request.character;
    _disableUpgrade = request.disableUpgrade;
    _itemCreateTable = std::move(request.itemCreateTable);
    _openPanel = OpenPanel::None;
    _activeKind.reset();
    for (const auto &kind : _kindButtons) {
        kind.button->setSelected(false);
    }
    _filter = UpgradeFilter::All;
    _rows.clear();
    // The upgrade panel's 3D views are made with the workbench.
    _upgrade->build3DViews();
    const bool tsl = _game.isTSL();
    if (tsl) {
        _controls.BTN_CREATEITEMS->setVisible(!request.disableItemCreation);
        // The creation panel is made with the workbench: its Repair factor and
        // breakdown filter are those of this opening.
        _repairFactor = _creation->repairFactorOf(*request.character);
        _creation->resetBreakdownFilter();
        _controls.LB_UPGRADELIST->clearItems();
        _controls.LB_DESCRIPTION->clearItems();
    }
    if (_disableUpgrade) {
        openCreation();
        return;
    }
    if (!request.item) {
        if (tsl) {
            populate();
        } else {
            showKinds();
        }
        return;
    }
    // The script's item opens at once, from its possessor's equipment when
    // worn there, over an empty list of items.
    _items->clear();
    UpgradableItem entry;
    entry.item = request.item;
    if (const auto wearer = std::dynamic_pointer_cast<Creature>(_game.getObjectById(request.item->owner()))) {
        for (const auto &[slot, worn] : wearer->equipment()) {
            if (worn == request.item) {
                entry.wearer = wearer;
                entry.slot = slot;
            }
        }
    }
    // An item that can't be upgraded leaves the selection as it was made,
    // unfilled.
    if (const auto type = upgradableTypeOf(*request.item, _game, _upgrade->upgradeTable())) {
        entry.type = *type;
        beginUpgrade(entry, true);
    }
}

std::vector<UpgradableItem> UpgradeSelectionGUI::upgradableItems(std::optional<UpgradableType> kind) const {
    std::vector<UpgradableItem> items;
    auto &party = _game.party();
    auto consider = [&](const std::shared_ptr<Item> &item, const std::shared_ptr<Creature> &wearer, std::optional<int> slot) {
        const auto type = upgradableTypeOf(*item, _game, _upgrade->upgradeTable());
        if (!type || (kind && *type != *kind)) {
            return;
        }
        UpgradableItem entry;
        entry.item = item;
        entry.wearer = wearer;
        entry.slot = slot;
        entry.type = *type;
        items.push_back(std::move(entry));
    };
    // TSL leaves out what is worn in the creature slots.
    auto worn = [&](const std::shared_ptr<Creature> &creature) {
        for (const auto &[slot, item] : creature->equipment()) {
            const bool creatureSlot = slot >= InventorySlots::cWeaponL && slot <= InventorySlots::cArmour;
            if (!(_game.isTSL() && creatureSlot)) {
                consider(item, creature, slot);
            }
        }
    };
    // Whose equipment is listed: in TSL, a player-made player character's and
    // every available member's, or else the party's as it stands; in KotOR,
    // the player character's and every selectable member's. The player
    // character is the one switched to during a character switch.
    if (_game.isTSL()) {
        const auto player = party.player();
        if (player->isPlayerCreated()) {
            worn(player);
            for (int npc = 0; npc < static_cast<int>(Party::kK2NpcCount); ++npc) {
                if (const auto member = party.getAvailableMember(npc)) {
                    worn(member);
                }
            }
        } else {
            for (int i = 0; i < 3; ++i) {
                if (const auto member = party.getMember(i)) {
                    worn(member);
                }
            }
        }
    } else {
        worn(party.player());
        for (int npc = 0; npc < static_cast<int>(Party::kK1NpcCount); ++npc) {
            if (!party.isRosterSelectable({RosterKind::Npc, npc})) {
                continue;
            }
            if (const auto member = party.getAvailableMember(npc)) {
                worn(member);
            }
        }
    }
    for (const auto &item : party.sharedInventoryReceiver(party.getLeader())->items()) {
        consider(item, nullptr, std::nullopt);
    }
    return items;
}

void UpgradeSelectionGUI::showKinds() {
    _kindsHeld.clear();
    for (const auto &entry : upgradableItems(std::nullopt)) {
        _kindsHeld.insert(entry.type);
    }
    for (const auto &kind : _kindButtons) {
        const bool held = _kindsHeld.count(kind.kind) > 0;
        kind.label->setVisible(held);
        kind.button->setDisabled(!held);
    }
    // The lightsaber kind is taken up on opening, its button active even
    // when disabled.
    _controls.BTN_UPGRADEITEMS->setDisabled(false);
    describeKind(UpgradableType::Lightsaber);
}

// The kind's button becomes the active one. Upgrade Items acts on the kind
// last taken up, and shows whether the party has an item of it; it is never
// disabled.
void UpgradeSelectionGUI::describeKind(UpgradableType kind) {
    for (const auto &kindButton : _kindButtons) {
        kindButton.button->setSelected(kindButton.kind == kind);
    }
    const bool held = _kindsHeld.count(kind) > 0;
    _activeKind = held ? std::optional<UpgradableType> {kind} : std::nullopt;
    _controls.BTN_UPGRADEITEMS->setTextColor(held ? kK1ColorBlue : kK1ColorDisabledText);
    _controls.BTN_UPGRADEITEMS->setBorderColor(held ? kK1ColorEnabledIcon : kK1ColorDisabledIcon);
}

void UpgradeSelectionGUI::onSelectionChanged(const std::string &control, bool selected) {
    GameGUI::onSelectionChanged(control, selected);
    if (_game.isTSL()) {
        return;
    }
    // Leaving Upgrade Items gives it back its own text colour.
    if (!selected) {
        if (control == _controls.BTN_UPGRADEITEMS->tag()) {
            _controls.BTN_UPGRADEITEMS->setTextColor(_upgradeItemsTextColor);
        }
        return;
    }
    // The control pointed at becomes the active one in place of a kind's
    // button; the kind stays taken up.
    for (const auto &kind : _kindButtons) {
        kind.button->setSelected(false);
    }
}

void UpgradeSelectionGUI::openItems(UpgradableType kind, const std::shared_ptr<Item> &selected) {
    _openPanel = OpenPanel::Items;
    _items->open(
        upgradableItems(kind),
        selected,
        [this](const UpgradableItem &entry) {
            beginUpgrade(entry, false);
        },
        [this]() {
            _openPanel = OpenPanel::None;
        });
}

std::optional<UpgradableType> UpgradeSelectionGUI::kindOf(UpgradeFilter filter) {
    switch (filter) {
    case UpgradeFilter::Lightsaber:
        return UpgradableType::Lightsaber;
    case UpgradeFilter::Ranged:
        return UpgradableType::Ranged;
    case UpgradeFilter::Melee:
        return UpgradableType::Melee;
    case UpgradeFilter::Armor:
        return UpgradableType::Armor;
    case UpgradeFilter::All:
        break;
    }
    // ALL lists every kind.
    return std::nullopt;
}

void UpgradeSelectionGUI::populate() {
    for (const auto &[filter, button] : _filterButtons) {
        button->setTextColor(filter == _filter ? kTSLColorNewHighlight : kTSLColorUpgradeText);
    }
    _rows = upgradableItems(kindOf(_filter));
    auto &list = *_controls.LB_UPGRADELIST;
    list.clearItems();
    for (size_t i = 0; i < _rows.size(); ++i) {
        const auto item = _rows[i].item.resolve();
        list.addItem(upgradableEntry(_game, _services, _rows[i], static_cast<int>(i), itemFrameTexture(item->stackSize())));
    }
    _controls.LB_DESCRIPTION->clearItems();
    const bool empty = _rows.empty();
    // The first row is chosen and described.
    list.setSelectedItemIndex(empty ? -1 : 0);
    _describedRow.reset();
    _controls.BTN_UPGRADEITEMS->setDisabled(empty);
    _controls.BTN_UPGRADEITEMS->setTextColor(empty ? kTSLColorDisabledText : kTSLColorUpgradeText);
    _controls.BTN_UPGRADEITEMS->setBorderColor(empty ? kTSLColorDisabledText : kTSLColorUpgradeBorder);
}

// The item leaves its place while it is upgraded: a worn weapon takes the
// other hand's off with it, except a KotOR script's, and a carried item leaves
// the party inventory, one split off its stack.
void UpgradeSelectionGUI::beginUpgrade(const UpgradableItem &entry, bool direct) {
    auto item = entry.item.resolve();
    _origin = {};
    _origin.direct = direct;
    _origin.type = entry.type;
    if (entry.slot) {
        const auto wearer = entry.wearer.resolve();
        const int slot = *entry.slot;
        _origin.wearer = wearer;
        _origin.slot = slot;
        const bool hand = isMainHandWeaponSlot(slot) || isOffHandWeaponSlot(slot);
        if (hand && !(direct && !_game.isTSL())) {
            const int mainSlot = isMainHandWeaponSlot(slot) ? slot : getPairedMainHandSlot(slot);
            const int offSlot = getPairedOffHandSlot(mainSlot);
            const auto mainHand = wearer->getEquippedItem(mainSlot);
            const auto offHand = wearer->getEquippedItem(offSlot);
            // A wearer holding a weapon in each hand gives up both.
            if (mainHand && offHand) {
                _origin.offHand = slot == offSlot;
                _origin.otherHand = _origin.offHand ? mainHand : offHand;
                wearer->takeEquippedItem(offHand);
                wearer->takeEquippedItem(mainHand);
            } else {
                wearer->takeEquippedItem(item);
            }
        } else {
            wearer->takeEquippedItem(item);
        }
    } else {
        // An item the party doesn't carry is upgraded where it is, or one
        // split off its stack there.
        if (auto taken = takeEquipmentCandidate(_game, *_game.party().sharedInventoryReceiver(_game.party().getLeader()), item)) {
            item = std::move(taken);
        } else if (item->stackSize() > 1) {
            item->setStackSize(item->stackSize() - 1);
            item = _game.newItemClone(*item);
            item->setStackSize(1);
        } else {
            _origin.leftInPlace = item;
        }
    }
    _openPanel = OpenPanel::Upgrade;
    _upgrade->open(item, entry.type, _origin.wearer.resolve(), [this](std::shared_ptr<Item> result) {
        finishUpgrade(std::move(result));
    });
}

void UpgradeSelectionGUI::finishUpgrade(std::shared_ptr<Item> item) {
    _openPanel = OpenPanel::None;
    const auto held = putBack(item);
    if (_origin.direct) {
        close();
    } else if (_game.isTSL()) {
        populate();
    } else {
        openItems(_origin.type, held);
    }
}

// The item goes back where it was, worn again when its wearer still can wear
// it, else into the party inventory.
std::shared_ptr<Item> UpgradeSelectionGUI::putBack(const std::shared_ptr<Item> &item) {
    auto inventory = _game.party().sharedInventoryReceiver(_game.party().getLeader());
    if (!_origin.slot) {
        // The party inventory gains the item even when it was upgraded where
        // it is, which leaves a copy of it in both places.
        if (!_origin.leftInPlace.empty() && _origin.leftInPlace.resolve() == item) {
            return inventory->addItem(_game.newItemClone(*item));
        }
        return inventory->addItem(item);
    }
    const auto wearer = _origin.wearer.resolve();
    if (_game.isTSL()) {
        wearer->setCombatState(true, CombatActivation::Indirect);
    }
    // Worn again, an item is announced to the module, and a weapon in a hand
    // readies its wearer for combat outside a conversation.
    auto putOn = [&](int slot, const std::shared_ptr<Item> &worn) -> std::shared_ptr<Item> {
        if (!wearer->equip(slot, worn)) {
            return inventory->addItem(worn);
        }
        if ((slot == InventorySlots::rightWeapon || slot == InventorySlots::leftWeapon) && !_game.isDialogUnderWay()) {
            wearer->setCombatState(true, CombatActivation::Indirect);
        }
        _game.finishEquip(*wearer, slot, worn);
        return worn;
    };
    // The upgraded item is worn again only if its wearer can wear it; the
    // other hand's weapon goes back whatever it is.
    const bool wearable = evaluateEquipmentAdmission(*wearer, *item) == EquipmentCandidateReason::None;
    if (_origin.otherHand.empty()) {
        int slot = *_origin.slot;
        // A main-hand weapon isn't worn beside a weapon left in the off hand;
        // an off-hand weapon is worn in an empty main hand.
        if (isMainHandWeaponSlot(slot) && wearer->getEquippedItem(getPairedOffHandSlot(slot))) {
            return inventory->addItem(item);
        }
        if (isOffHandWeaponSlot(slot) && !wearer->getEquippedItem(getPairedMainHandSlot(slot))) {
            slot = getPairedMainHandSlot(slot);
        }
        return wearable ? putOn(slot, item) : inventory->addItem(item);
    }
    const auto other = _origin.otherHand.resolve();
    const int mainSlot = _origin.offHand ? getPairedMainHandSlot(*_origin.slot) : *_origin.slot;
    const int offSlot = getPairedOffHandSlot(mainSlot);
    if (_origin.offHand) {
        putOn(mainSlot, other);
        return wearable ? putOn(offSlot, item) : inventory->addItem(item);
    }
    if (wearable) {
        auto held = putOn(mainSlot, item);
        putOn(offSlot, other);
        return held;
    }
    auto held = inventory->addItem(item);
    putOn(mainSlot, other);
    return held;
}

void UpgradeSelectionGUI::openCreation() {
    _openPanel = OpenPanel::Creation;
    _creation->open(_character.resolve(), _repairFactor, _itemCreateTable, _disableUpgrade, [this](bool escape) {
        onCreationClosed(escape);
    });
}

void UpgradeSelectionGUI::onCreationClosed(bool escape) {
    if (_disableUpgrade) {
        close();
        return;
    }
    if (escape) {
        _services.audio.mixer.play(_services.game.guiSounds.getOnClick(), AudioType::Sound);
    }
    _openPanel = OpenPanel::None;
    // What was made or broken down may be upgradable.
    populate();
}

bool UpgradeSelectionGUI::handle(const input::Event &event) {
    switch (_openPanel) {
    case OpenPanel::Creation:
        return _creation->handle(event);
    case OpenPanel::Items:
        return _items->handle(event);
    case OpenPanel::Upgrade:
        return _upgrade->handle(event);
    case OpenPanel::None:
        break;
    }
    if (event.type == input::EventType::KeyDown &&
        !event.key.repeat &&
        event.key.code == input::KeyCode::Escape) {
        _services.audio.mixer.play(_services.game.guiSounds.getOnClick(), AudioType::Sound);
        close();
        return true;
    }
    // TSL's list holds the keyboard; its entry clicks as it is chosen.
    if (_game.isTSL() && event.type == input::EventType::KeyDown &&
        handleListKey(_services, event.key, *_controls.LB_UPGRADELIST, [this](int row) {
            _services.audio.mixer.play(_services.game.guiSounds.getOnClick(), AudioType::Sound);
            beginUpgrade(_rows[row], false);
        })) {
        return true;
    }
    return GameGUI::handle(event);
}

void UpgradeSelectionGUI::update(float dt) {
    GameGUI::update(dt);
    switch (_openPanel) {
    case OpenPanel::Creation:
        _creation->update(dt);
        return;
    case OpenPanel::Items:
        _items->update(dt);
        return;
    case OpenPanel::Upgrade:
        _upgrade->update(dt);
        return;
    case OpenPanel::None:
        break;
    }
    if (!_game.isTSL()) {
        return;
    }
    // TSL describes the row under the pointer.
    const int row = _controls.LB_UPGRADELIST->selectedItemIndex();
    if (row >= 0 && _describedRow != row) {
        _describedRow = row;
        _controls.LB_DESCRIPTION->clearItems();
        _controls.LB_DESCRIPTION->addTextLinesAsItems(describedItem(_game, _services, *_rows[row].item.resolve()));
    }
}

void UpgradeSelectionGUI::render() {
    GameGUI::render();
    switch (_openPanel) {
    case OpenPanel::Creation:
        _creation->render();
        break;
    case OpenPanel::Items:
        _items->render();
        break;
    case OpenPanel::Upgrade:
        // The list of items stays under the upgrade panel: empty for a
        // script's item, and never filled in TSL.
        _items->render();
        _upgrade->render();
        break;
    case OpenPanel::None:
        break;
    }
}

void UpgradeSelectionGUI::close() {
    _game.closeStation(Game::Screen::UpgradeSelection);
}

// END UpgradeSelectionGUI

} // namespace game

} // namespace reone
