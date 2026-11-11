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
#include <stdexcept>

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
#include "reone/resource/provider/fonts.h"
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
static constexpr int kInfiniteStrRef = 41951;
// What stockOf answers for a carried match that never runs out.
static constexpr int kInfiniteStock = -1;
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
    // The buttons and filters never take the keyboard from the lists.
    _gui->setHilightFollowsKeyboard(true);
    for (const auto &button : {_controls.BTN_Accept, _controls.BTN_Cancel, _controls.BTN_Examine}) {
        button->setNavigable(false);
    }
    for (const auto &filter : _createFilters) {
        filter->setNavigable(false);
    }
    for (const auto &[filter, button] : _breakdownFilters) {
        button->setNavigable(false);
    }
    _controls.BTN_Accept->setOnClick([this]() {
        accept();
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
            restockCreatable();
            showCreateList();
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

// Whether the key chooses: Return or keypad Enter.
static bool isChooseKey(input::KeyCode code) {
    return code == input::KeyCode::Return || code == input::KeyCode::KeypadEnter;
}

// The keys of a panel whose list holds the keyboard: Return or keypad Enter
// chooses the selected row, and Up and Down move the selection, also while
// held, sounding as it moves; Up with no row selected selects the first
// silently. Whether the key was one of them.
static bool handleListKey(ServicesView &services, const input::KeyEvent &key, ListBox &list, const std::function<void(int)> &choose) {
    if (!key.repeat && isChooseKey(key.code)) {
        const int row = list.selectedItemIndex();
        if (row >= 0) {
            choose(row);
        }
        return true;
    }
    if (key.code == input::KeyCode::Up || key.code == input::KeyCode::Down) {
        const bool down = key.code == input::KeyCode::Down;
        const bool sounds = down || list.selectedItemIndex() >= 0;
        if (list.selectAdjacentItem(down) && sounds) {
            services.game.guiSounds.play(services.game.guiSounds.getOnEnter());
        }
        return true;
    }
    return false;
}

// The direction an arrow key moves the keyboard in; none for another key.
static std::optional<Control::MoveDirection> moveDirectionOf(input::KeyCode code) {
    switch (code) {
    case input::KeyCode::Up:
        return Control::MoveDirection::Up;
    case input::KeyCode::Left:
        return Control::MoveDirection::Left;
    case input::KeyCode::Down:
        return Control::MoveDirection::Down;
    case input::KeyCode::Right:
        return Control::MoveDirection::Right;
    default:
        return std::nullopt;
    }
}

bool CraftingPanel::handle(const input::Event &event) {
    // Return or keypad Enter released on the selected row leaves Accept idle
    // on it.
    if (event.type == input::EventType::KeyUp && isChooseKey(event.key.code)) {
        _acceptRow = currentList().selectedItemIndex();
        _acceptAction = AcceptAction::None;
    }
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
    _acceptRow = row;
    _acceptAction = AcceptAction::ChooseAgain;
    if (&list == _controls.LB_SHOPITEMS.get()) {
        _services.game.guiSounds.play(_services.game.guiSounds.getOnClick());
    }
    act(row);
}

// Nothing is chosen while the list is empty.
void CraftingPanel::accept() {
    auto &list = currentList();
    const int row = list.selectedItemIndex();
    if (row < 0) {
        return;
    }
    switch (_acceptAction) {
    case AcceptAction::ChooseAgain:
        choose(list, row);
        break;
    case AcceptAction::ActSilently:
        act(row);
        break;
    case AcceptAction::None:
        break;
    }
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
    const int row = currentList().selectedItemIndex();
    // Accept acts silently on a row the selection moves to.
    if (row != _acceptRow) {
        _acceptRow = row;
        _acceptAction = AcceptAction::ActSilently;
    }
    // The selected row is described.
    if (row >= 0 && _describedRow != row) {
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
    // The view's list holds the keyboard.
    _gui->setActiveControl(currentList());

    if (create) {
        if (clearsSkillLineOnCreateView()) {
            _controls.LBL_RELATEDSKILL->setTextMessage("");
        }
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

// An item's row: TSL resolves the tokens in its name, KotOR shows the name as
// it stands.
static ListBox::Item listEntry(Game &game, const Item &item, int index, std::shared_ptr<graphics::Texture> frame) {
    ListBox::Item entry;
    entry.tag = std::to_string(index);
    entry.text = game.isTSL() ? game.substituteLogTokens(item.localizedName()) : item.localizedName();
    entry.iconTexture = item.icon();
    entry.iconFrame = std::move(frame);
    if (item.stackSize() > 1) {
        entry.iconText = std::to_string(item.stackSize());
    }
    return entry;
}

// The font of the count on an item row.
static std::shared_ptr<graphics::Font> rowCountFont(const Game &game, ServicesView &services) {
    return services.resource.fonts.get(game.guiFontName("fnt_d16x16"));
}

void CraftingPanel::restockCreatable() {
    _stock.clear();
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
                addCreatable(item, row);
            } else if (stocksMissingTemplates()) {
                item->loadBlank();
                addCreatable(item, row);
            }
        }
    }
}

void CraftingPanel::showCreateList() {
    _creatable.clear();
    _controls.LB_SHOPITEMS->clearItems();
    _controls.LB_SHOPITEMS->setIconTextFont(rowCountFont(_game, _services));
    // Each stocked item answers to the row that stocked it, which has a skill
    // and a requirement.
    const auto leader = _game.party().getLeader();
    const auto table = createTable();
    for (const auto &stocked : _stock) {
        Creatable creatable {stocked.item, stocked.row, false, stocked.madeStack};
        const auto skill = static_cast<SkillType>(table->getInt(stocked.row, "base_skill"));
        creatable.available = table->getInt(stocked.row, skillColumn()) <= leader->getUnopposedSkillRank(skill);
        const size_t i = _creatable.size();
        auto entry = listEntry(_game, *creatable.item, static_cast<int>(i), itemFrameTexture(creatable.item->stackSize()));
        // An entry the leader can make lights in the upgrade text colour and
        // one the leader can't make is greyed. The count is in the entry's
        // text colour.
        if (creatable.available) {
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

void CraftingPanel::addCreatable(const std::shared_ptr<Item> &item, int row) {
    if (item->plotFlag()) {
        return;
    }
    item->setIdentified(true);
    const int madeStack = item->stackSize();
    // The latest stocked item with the item's tag takes one more when identical.
    for (auto it = _stock.rbegin(); it != _stock.rend(); ++it) {
        auto &held = *it->item;
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
        auto &held = *it->item;
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
    _stock.push_back(Stocked {item, row, madeStack});
}

void CraftingPanel::fillBreakdownList(int keepRow) {
    _breakable.clear();
    _controls.LB_INVITEMS->clearItems();
    _controls.LB_INVITEMS->setIconTextFont(rowCountFont(_game, _services));
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
    // Showing the list moves the selection afresh, even onto the row already
    // selected.
    _acceptAction = AcceptAction::ActSilently;
    if (count == 0) {
        list.setSelectedItemIndex(-1);
        _acceptRow = -1;
        _describedRow.reset();
        clearFocus();
        return;
    }
    const int selected = std::clamp(row, 0, count - 1);
    list.setSelectedItemIndex(selected);
    _acceptRow = selected;
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
        describeCreatable(_creatable[row]);
    } else {
        describeBreakable(*_breakable[row].resolve());
    }
}

static std::string describedItem(Game &game, ServicesView &services, const Item &item) {
    return game.substituteLogTokens(buildItemDescription(item, game, services));
}

std::string CraftingPanel::itemText(const Item &item) const {
    if (resolvesDescriptionTokens()) {
        return describedItem(_game, _services, item);
    }
    return buildItemDescription(item, _game, _services);
}

void CraftingPanel::describeCreatable(const Creatable &creatable) {
    _controls.LBL_COST_VALUE->setTextMessage(std::to_string(itemValue(*creatable.item)));
    const auto table = createTable();
    const auto baseSkill = static_cast<SkillType>(table->getInt(creatable.row, "base_skill"));
    // A skill the skills table has no row for has no name.
    const auto skill = _services.game.skills.get(baseSkill);
    const auto skillName = skill ? skill->name : std::string();
    const int rank = _game.party().getLeader()->getUnopposedSkillRank(baseSkill);
    _controls.LBL_RELATEDSKILL->setTextMessage(skillName + " (" + std::to_string(rank) + ")");
    _controls.LBL_RELATEDSKILL->setTextColor(creatable.available ? kTSLColorUpgradeText : kTSLColorRed);
    // An infinite match shows the infinite string, as the breakdown view does.
    const int stock = stockOf(*creatable.item);
    _controls.LBL_STOCK_VALUE->setTextMessage(stock == kInfiniteStock
                                                  ? _services.resource.strings.getText(kInfiniteStrRef)
                                                  : std::to_string(stock));
    const int requirement = table->getInt(creatable.row, skillColumn());
    const auto description = _services.resource.strings.getText(kRequirementHeaderStrRef) + "\n" +
                             skillName + " (" + std::to_string(requirement) + ")\n" +
                             itemText(*creatable.item);
    _controls.LB_DESCRIPTION->clearItems();
    _controls.LB_DESCRIPTION->addTextLinesAsItems(description);
}

void CraftingPanel::describeBreakable(const Item &item) {
    _controls.LBL_COST_VALUE->setTextMessage(std::to_string(breakdownGain(item)));
    _controls.LBL_STOCK_VALUE->setTextMessage(item.isInfinite()
                                                  ? _services.resource.strings.getText(kInfiniteStrRef)
                                                  : std::to_string(item.stackSize()));
    _controls.LB_DESCRIPTIONINV->clearItems();
    _controls.LB_DESCRIPTIONINV->addTextLinesAsItems(itemText(item));
}

// What the party holds of an item: the first matching stack it carries, and
// one for each matching item the first three members wear. A carried match
// that never runs out is infinite whatever is worn.
int CraftingPanel::stockOf(const Item &item) const {
    auto &party = _game.party();
    int stock = 0;
    for (const auto &carried : inventoryOf(party.getLeader())) {
        if (carried->isIdenticalTo(item)) {
            if (carried->isInfinite()) {
                return kInfiniteStock;
            }
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
    const auto &creatable = _creatable[row];
    if (!creatable.available) {
        _game.showMessagePopup(strings.getText(_messages.unavailable), nullptr, {}, kCraftingPopupColors);
        return;
    }
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
    _services.game.guiSounds.play(_services.game.guiSounds.getInventoryAdd());
    setShownCount(_shownCount - itemValue(*creatable.item));
    _game.party().setCreatureCount(*character, _count, _shownCount);
    // One entry makes one item, however many identical rows the entry stands for.
    auto made = _game.newItemClone(*creatable.item);
    made->setStackSize(creatable.madeStack);
    _game.party().sharedInventoryReceiver(character)->addItem(std::move(made));
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
    _services.game.guiSounds.play(_services.game.guiSounds.getInventoryDrop());
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
    // A click selects an entry and a double click picks it, with the entry's
    // click; pointing at an entry does neither.
    _controls.LB_ITEMS->setSelectionMode(ListBox::SelectionMode::OnClick);
    // KotOR's entries are framed blue with a yellow hilight, their fills
    // taking the same colours.
    auto &protoItem = _controls.LB_ITEMS->protoItem();
    protoItem.setBorderColor(kK1ColorBlue);
    protoItem.setHilightColor(kK1ColorYellow);
    _controls.LB_ITEMS->setOnItemDoubleClick([this](const std::string &tag) {
        _services.game.guiSounds.play(_services.game.guiSounds.getOnClick());
        pick(std::stoi(tag));
    });
    // The buttons never take the keyboard from the list.
    _gui->setHilightFollowsKeyboard(true);
    _controls.BTN_BACK->setNavigable(false);
    _controls.BTN_UPGRADEITEM->setNavigable(false);
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
    _controls.LB_ITEMS->setIconTextFont(rowCountFont(_game, _services));
    addRows();
    _controls.LB_DESCRIPTION->clearItems();
    _describedRow.reset();
    // The first row is chosen, or the first row holding the item asked for.
    if (selected) {
        select(rowOf(selected));
    } else if (!_items.empty()) {
        select(0);
    }
    // The list holds the keyboard.
    _gui->setActiveControl(*_controls.LB_ITEMS);
}

bool UpgradeItemsPanel::keepAfterUpgrade(const std::shared_ptr<Item> &held) {
    auto &entry = _items[_pickedRow];
    if (entry.slot && !held->isEquipped()) {
        // An item its wearer no longer wears is named afresh where it went,
        // carried, its row redrawn in place.
        entry.item = held;
        entry.wearer.reset();
        entry.slot.reset();
        auto &list = *_controls.LB_ITEMS;
        const int offset = list.getItemOffset();
        list.clearItems();
        addRows();
        list.setItemOffset(offset);
    } else if (entry.item.resolve() != held) {
        return false;
    }
    select(rowOf(held));
    return true;
}

void UpgradeItemsPanel::addRows() {
    for (size_t i = 0; i < _items.size(); ++i) {
        const auto item = _items[i].item.resolve();
        _controls.LB_ITEMS->addItem(upgradableEntry(_game, _services, _items[i], static_cast<int>(i), itemFrameTexture(item->stackSize())));
    }
}

// An item put back after an upgrade is listed again unless it no longer
// counts as an item of its kind, as a lightsaber of another colour may not.
int UpgradeItemsPanel::rowOf(const std::shared_ptr<Item> &item) const {
    const auto entry = std::find_if(_items.begin(), _items.end(), [&item](const UpgradableItem &upgradable) {
        return upgradable.item.resolve() == item;
    });
    return entry == _items.end() ? -1 : static_cast<int>(std::distance(_items.begin(), entry));
}

void UpgradeItemsPanel::select(int row) {
    _controls.LB_ITEMS->setSelectedItemIndex(row);
    if (row < 0) {
        _describedRow.reset();
        _controls.LB_DESCRIPTION->clearItems();
        return;
    }
    describe(row);
}

// Clicking the lists is silent: a click selecting another entry sounds as
// the selection moves, and an entry clicks as it is picked.
void UpgradeItemsPanel::onClick(const std::string &control) {
    if (control == _controls.LB_ITEMS->tag() || control == _controls.LB_DESCRIPTION->tag()) {
        return;
    }
    GameGUI::onClick(control);
}

bool UpgradeItemsPanel::handle(const input::Event &event) {
    if (event.type == input::EventType::KeyDown &&
        !event.key.repeat &&
        event.key.code == input::KeyCode::Escape) {
        _services.game.guiSounds.play(_services.game.guiSounds.getOnClick());
        _onBack();
        return true;
    }
    // The list holds the keyboard; its entry clicks as it is chosen.
    if (event.type == input::EventType::KeyDown &&
        handleListKey(_services, event.key, *_controls.LB_ITEMS, [this](int row) {
            _services.game.guiSounds.play(_services.game.guiSounds.getOnClick());
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
    _pickedRow = row;
    _onPick(_items[row]);
}

// END UpgradeItemsPanel

// ItemUpgradePanel

// A lightsaber's colour crystal slot sits between its two crystal slots,
// which take the same upgrades.
static constexpr int kLightsaberFirstCrystalSlot = 0;
static constexpr int kLightsaberColourSlot = 1;
static constexpr int kLightsaberSecondCrystalSlot = 2;

// The button of a KotOR ranged weapon's first slot.
static constexpr char kK1RangedFirstSlotButton[] = "BTN_UPGRADE41";

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
// upgrade type each takes, the icon it shows empty, its name, its button and
// its record. KotOR's armour slots use the second and third records, as their
// buttons are a ranged weapon's second and third.
const std::vector<ItemUpgradePanel::SlotInfo> &ItemUpgradePanel::slotInfos() const {
    static const std::array<std::vector<SlotInfo>, 4> kK1Slots {
        std::vector<SlotInfo> {{0, "i_powerc", 36977, "BTN_UPGRADE31", 0}, {std::nullopt, "i_colorc", 36978, "BTN_UPGRADE32", 1}, {0, "i_powerc", 36977, "BTN_UPGRADE33", 2}},
        std::vector<SlotInfo> {{4, "i_scope", 32487, "BTN_UPGRADE41", 0}, {5, "i_energy", 32488, "BTN_UPGRADE42", 1}, {6, "i_beam", 32489, "BTN_UPGRADE43", 2}, {7, "i_hair", 32490, "BTN_UPGRADE44", 3}},
        std::vector<SlotInfo> {{1, "i_vcell", 32493, "BTN_UPGRADE31", 0}, {2, "i_durasteel", 32494, "BTN_UPGRADE32", 1}, {3, "i_imp_eng", 32495, "BTN_UPGRADE33", 2}},
        std::vector<SlotInfo> {{8, "i_armorrein", 32491, "BTN_UPGRADE42", 1}, {9, "i_armorrein", 32492, "BTN_UPGRADE43", 2}}};
    static const std::array<std::vector<SlotInfo>, 4> kTSLSlots {
        std::vector<SlotInfo> {{0, "i_powerc", 48473, "BTN_UPGRADE31_LS", 0}, {std::nullopt, "i_powerc", 48472, "BTN_UPGRADE32_LS", 1}, {0, "i_powerc", 48473, "BTN_UPGRADE33_LS", 2}, {1, "i_lens", 48464, "BTN_UPGRADE34_LS", 3}, {2, "i_emitter", 48465, "BTN_UPGRADE35_LS", 4}, {3, "i_lsenergy", 48466, "BTN_UPGRADE36_LS", 5}},
        std::vector<SlotInfo> {{4, "i_scope", 48471, "BTN_UPGRADE31", 0}, {5, "i_energy", 48470, "BTN_UPGRADE32", 1}, {6, "i_imp_eng", 48469, "BTN_UPGRADE33", 2}},
        std::vector<SlotInfo> {{7, "i_grip", 48467, "BTN_UPGRADE31", 0}, {8, "i_edge", 48468, "BTN_UPGRADE32", 1}, {9, "i_vcell", 48466, "BTN_UPGRADE33", 2}},
        std::vector<SlotInfo> {{10, "i_armorrein", 48475, "BTN_UPGRADE31", 0}, {11, "i_armorrein", 48474, "BTN_UPGRADE32", 1}}};
    const auto &slots = _game.isTSL() ? kTSLSlots : kK1Slots;
    return slots[static_cast<size_t>(_type)];
}

// A slot button's tag is this, then its row's digit and its place in the row
// counted from 1.
static constexpr std::string_view kSlotButtonPrefix {"BTN_UPGRADE"};

// A slot button acts for its place in its row: the slot of the item's kind
// whose record is that place's, hidden buttons included; none when the kind
// has no slot there.
std::optional<int> ItemUpgradePanel::slotOfButton(const std::string &tag) const {
    if (_controls.slots.count(tag) == 0) {
        return std::nullopt;
    }
    const int place = tag[kSlotButtonPrefix.size() + 1] - '1';
    const auto &infos = slotInfos();
    for (size_t slot = 0; slot < infos.size(); ++slot) {
        if (infos[slot].record == place) {
            return static_cast<int>(slot);
        }
    }
    return std::nullopt;
}

ItemUpgradePanel::SlotRecord &ItemUpgradePanel::slotRecord(int slot) {
    return _slotRecords[slotInfos()[slot].record];
}

// A record holding no row yet keys no properties.
std::string ItemUpgradePanel::slotPropertyText(int slot) {
    const auto &row = slotRecord(slot).row;
    return row ? buildKeyedPropertyText(*_item, *row, _game, _services) : std::string();
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
    // A slot button keeps its hilight while it holds the keyboard. The panel
    // starts with no control holding it.
    _gui->setHilightFollowsKeyboard(true);
    _gui->clearActiveControl();
    if (tsl) {
        _controls.BTN_OK = findControl<Button>("BTN_OK");
    } else {
        _controls.LBL_LSSLOTNAME = findControl<Label>("LBL_LSSLOTNAME");
        _controls.LBL_UPGRADES = findControl<Label>("LBL_UPGRADES");
        _controls.LBL_UPGRADE_COUNT = findControl<Label>("LBL_UPGRADE_COUNT");
        _controls.LB_DESC_LS = findControl<ListBox>("LB_DESC_LS");
        // KotOR writes either description in the lightsaber description's
        // row: its place, width and text.
        const auto &lightsaberRow = _controls.LB_DESC_LS->protoItem();
        auto &row = _controls.LB_DESC->protoItem();
        const auto placed = [](Control::Extent extent, const Control::Extent &place) {
            extent.left = place.left;
            extent.top = place.top;
            extent.width = place.width;
            return extent;
        };
        row.setAuthoredExtent(placed(row.authoredExtent(), lightsaberRow.authoredExtent()));
        row.setExtent(placed(row.extent(), lightsaberRow.extent()));
        row.setText(lightsaberRow.text());
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
            if (const auto slot = slotOfButton(tag)) {
                clickSlot(*slot);
            }
        });
    }
    // TSL's list selects an entry on a click and picks it on a double click;
    // KotOR's selects the entry under the pointer and picks it on a click.
    // Either way the entry clicks as it is picked.
    if (tsl) {
        _controls.LB_ITEMS->setSelectionMode(ListBox::SelectionMode::OnClick);
        _controls.LB_ITEMS->setOnItemDoubleClick([this](const std::string &tag) {
            _services.game.guiSounds.play(_services.game.guiSounds.getOnClick());
            pick(std::stoi(tag));
        });
    } else {
        _controls.LB_ITEMS->setSelectionMode(ListBox::SelectionMode::OnHoverKept);
        _controls.LB_ITEMS->setOnItemClick([this](const std::string &tag) {
            _services.game.guiSounds.play(_services.game.guiSounds.getOnClick());
            pick(std::stoi(tag));
        });
    }
    // Assemble, Back and OK never take the keyboard.
    _controls.BTN_ASSEMBLE->setNavigable(false);
    _controls.BTN_BACK->setNavigable(false);
    if (tsl) {
        _controls.BTN_OK->setNavigable(false);
        // OK passes over a greyed entry in silence: only the entry's own pick
        // says it is too high.
        _controls.BTN_OK->setOnClick([this]() {
            const int entry = _controls.LB_ITEMS->selectedItemIndex();
            if (entry >= 0 && !_candidates[entry].disabled) {
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
    linkSlotButtons();
    // TSL's opening leaves no control hilighted and none holding the keyboard;
    // it titles the panel with the item and clears the slot texts. KotOR's
    // first slot takes the keyboard from the control holding it since the
    // last opening, and the slot texts keep what they showed until that slot
    // is described, before the upgrades are placed, from what the slot
    // records still hold.
    if (_game.isTSL()) {
        _gui->clearSelection();
        _gui->clearActiveControl();
        _controls.LBL_TITLE->setTextMessage(_game.substituteLogTokens(_item->localizedName()));
        _controls.LBL_SLOTNAME->setTextMessage("");
        _controls.LBL_PROPERTY->setTextMessage("");
    } else {
        activateFirstSlot();
    }
    placeInstalledUpgrades();
    showSlots();
    refresh3D();
}

// The links between slot buttons the arrow keys follow, by the item's kind,
// over the layout's in those directions. TSL's lightsaber slots stand in two
// columns of three; its other weapons' third slot stands between the first
// two.
void ItemUpgradePanel::linkSlotButtons() {
    using Direction = Control::MoveDirection;
    const auto link = [this](const std::string &from, Direction direction, const std::string &to) {
        _controls.slots.at(from).first->setMoveTo(direction, _controls.slots.at(to).first->id());
    };
    if (_game.isTSL()) {
        if (_type == UpgradableType::Lightsaber) {
            link("BTN_UPGRADE31_LS", Direction::Right, "BTN_UPGRADE34_LS");
            link("BTN_UPGRADE34_LS", Direction::Left, "BTN_UPGRADE31_LS");
            link("BTN_UPGRADE32_LS", Direction::Right, "BTN_UPGRADE35_LS");
            link("BTN_UPGRADE35_LS", Direction::Left, "BTN_UPGRADE32_LS");
            link("BTN_UPGRADE33_LS", Direction::Right, "BTN_UPGRADE36_LS");
            link("BTN_UPGRADE36_LS", Direction::Left, "BTN_UPGRADE33_LS");
            link("BTN_UPGRADE31_LS", Direction::Down, "BTN_UPGRADE32_LS");
            link("BTN_UPGRADE32_LS", Direction::Up, "BTN_UPGRADE31_LS");
            link("BTN_UPGRADE32_LS", Direction::Down, "BTN_UPGRADE33_LS");
            link("BTN_UPGRADE33_LS", Direction::Up, "BTN_UPGRADE32_LS");
            link("BTN_UPGRADE34_LS", Direction::Down, "BTN_UPGRADE35_LS");
            link("BTN_UPGRADE35_LS", Direction::Up, "BTN_UPGRADE34_LS");
            link("BTN_UPGRADE35_LS", Direction::Down, "BTN_UPGRADE36_LS");
            link("BTN_UPGRADE36_LS", Direction::Up, "BTN_UPGRADE35_LS");
        }
        if (_type == UpgradableType::Armor) {
            link("BTN_UPGRADE31", Direction::Right, "BTN_UPGRADE32");
            link("BTN_UPGRADE32", Direction::Left, "BTN_UPGRADE31");
        } else {
            link("BTN_UPGRADE31", Direction::Right, "BTN_UPGRADE33");
            link("BTN_UPGRADE33", Direction::Left, "BTN_UPGRADE31");
            link("BTN_UPGRADE33", Direction::Right, "BTN_UPGRADE32");
            link("BTN_UPGRADE32", Direction::Left, "BTN_UPGRADE33");
        }
        return;
    }
    if (_type == UpgradableType::Ranged) {
        link("BTN_UPGRADE42", Direction::Left, "BTN_UPGRADE41");
        link("BTN_UPGRADE43", Direction::Right, "BTN_UPGRADE44");
    } else if (_type == UpgradableType::Armor) {
        link("BTN_UPGRADE42", Direction::Left, "BTN_UPGRADE43");
        link("BTN_UPGRADE43", Direction::Right, "BTN_UPGRADE42");
    }
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
    // A lightsaber upcrystals.2da doesn't list shows no colour crystal.
    if (_type == UpgradableType::Lightsaber) {
        if (const auto row = crystalRow(*_item)) {
            _slots[kLightsaberColourSlot].held = _game.newItemFromBlueprint(boost::to_lower_copy(_crystalTable->getString(*row, "Template")));
        }
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
            // The row goes in the slot's record; KotOR keeps a lightsaber
            // slot's template as it was.
            auto &record = slotRecord(slot);
            record.row = row;
            if (tsl || _type != UpgradableType::Lightsaber) {
                record.tag = tag;
            }
            bool installed = false;
            if (tsl) {
                const auto &upgradeSlots = _item->upgradeSlots();
                installed = upgradeSlots[slot] == row;
                // The second crystal slot holds a crystal the first doesn't;
                // its record is left as it is.
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

// The first slot's button takes the keyboard and is selected without a sound,
// which describes the slot; a button holding the keyboard already stays as it
// is and describes nothing. A melee weapon's first slot is activated through a
// ranged weapon's first button, which stays hidden and acts for the slot.
void ItemUpgradePanel::activateFirstSlot() {
    auto &button = _type == UpgradableType::Melee ? *_controls.slots.at(kK1RangedFirstSlotButton).first : *_slots.front().button;
    _gui->selectActiveControl(button);
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
    if (const auto slot = slotOfButton(control)) {
        describeSlot(*slot);
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
            _controls.LBL_PROPERTY->setTextMessage(_type == UpgradableType::Lightsaber ? std::string() : slotPropertyText(slot));
        }
        return;
    }
    // KotOR names the slot, counts the party's upgrades for its record and
    // lists the item's properties of the record's row, installed or not.
    const auto available = partyInventory()->getItemByTag(slotRecord(slot).tag);
    _controls.LBL_SLOTNAME->setTextMessage(strings.getText(info.nameStrRef));
    _controls.LBL_UPGRADE_COUNT->setTextMessage(std::to_string(available ? available->stackSize() : 0));
    _controls.LBL_UPGRADES->setTextMessage(strings.getText(kUpgradeCountStrRef));
    _controls.LBL_PROPERTY->setTextMessage(slotPropertyText(slot));
}

void ItemUpgradePanel::clickSlot(int slot) {
    if (_game.isTSL() || _type == UpgradableType::Lightsaber) {
        showList(slot);
        return;
    }
    auto &place = _slots[slot];
    const auto &record = slotRecord(slot);
    if (place.held) {
        _item->setUpgradeInstalled(*record.row, false);
        giveBack(place.held);
        place.held.reset();
        refreshSlots();
        describeSlot(slot);
        return;
    }
    // A slot the party has no upgrade for does nothing.
    const auto upgrade = partyInventory()->getItemByTag(record.tag);
    if (!upgrade) {
        return;
    }
    const int row = *record.row;
    _item->setUpgradeInstalled(row, true);
    // An item its wearer could no longer wear is asked about first. No takes
    // the row off the item again, leaving the slot empty and the upgrade with
    // the party.
    if (wearerRefusesItem()) {
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

bool ItemUpgradePanel::wearerRefusesItem() const {
    return !_wearer.empty() && evaluateEquipmentAdmission(*_wearer.resolve(), *_item) != EquipmentCandidateReason::None;
}

void ItemUpgradePanel::install(int slot, const std::shared_ptr<Item> &upgrade) {
    _slots[slot].held = take(upgrade);
    refreshSlots();
    describeSlot(slot);
}

// Leaving a list shows the slots again and gives the keyboard back to the slot
// it was for, selecting its button without a sound; that slot is described
// either way.
void ItemUpgradePanel::closeList() {
    const int slot = *_listSlot;
    showSlots();
    _gui->selectActiveControl(*_slots[slot].button);
    describeSlot(slot);
}

void ItemUpgradePanel::showList(int slot) {
    // With no colour crystal there is no colour to change: a new one would
    // replace the lightsaber with another template.
    if (!slotInfos()[slot].upgradeType && !_slots[slot].held) {
        return;
    }
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
        const auto &placeRow = slotRecord(slot).row;
        if (place.held) {
            _candidates.push_back(Candidate {place.held, placeRow, true, false});
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
                (place.held && placeRow == row) ||
                (pairedSlot && _slots[*pairedSlot].held && slotRecord(*pairedSlot).row == row)) {
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
    _controls.LB_ITEMS->setIconTextFont(rowCountFont(_game, _services));
    for (size_t i = 0; i < _candidates.size(); ++i) {
        const auto &candidate = _candidates[i];
        ListBox::Item entry;
        if (candidate.item) {
            entry = listEntry(_game, *candidate.item, static_cast<int>(i), itemFrameTexture(candidate.item->stackSize()));
            entry.iconTextColor = upgradeEntryCountColor(_game);
        } else {
            // The entry that empties the slot shows the empty icon on a
            // single item's plate.
            entry.tag = std::to_string(i);
            entry.text = _services.resource.strings.getText(kNoneStrRef);
            entry.iconTexture = _services.resource.textures.get(tsl ? "inone_blue" : "inone", TextureUsage::GUI);
            entry.iconFrame = itemFrameTexture(1);
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
    // The list holds the keyboard while it is open.
    _gui->setActiveControl(*_controls.LB_ITEMS);
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
    const bool tsl = _game.isTSL();
    const bool colour = !slotInfos()[slot].upgradeType;
    // The slot's current upgrade closes the list unchanged in TSL, as the
    // current colour crystal does in both games. In KotOR any other slot's
    // current upgrade is given back and taken again like any other.
    if ((tsl || colour) && candidate.current) {
        closeList();
        return;
    }
    if (colour) {
        swapLightsaber(*candidate.row);
        giveBack(place.held);
        place.held = take(candidate.item);
        closeList();
        refresh3D();
        return;
    }
    auto &record = slotRecord(slot);
    auto chosen = candidate.item;
    if (place.held) {
        // The upgrade is counted before it rejoins a stack.
        if (tsl) {
            _item->removeUpgrade(slot);
            _item->setUpgradeSlot(slot, std::nullopt);
        } else {
            _item->setUpgradeInstalled(*record.row, false);
        }
        // The slot's own crystal, given back, is gone once it joins a stack,
        // and the slot stays empty.
        const auto stack = giveBack(place.held);
        if (chosen == place.held && stack != chosen) {
            chosen.reset();
        }
        place.held.reset();
    }
    if (chosen) {
        // The slot answers to the entry's row.
        record.row = candidate.row;
        if (tsl) {
            // The item records the first row with its template and takes the
            // upgrade's properties. An item its wearer could no longer wear
            // is asked about first: Yes takes the upgrade into the slot; No
            // takes the upgrade's properties and row off the item again and
            // leaves the list open, the slot empty and the upgrade with the
            // party.
            _item->setUpgradeSlot(slot, firstRowWithTemplateOf(*candidate.row));
            _item->addUpgrade(*chosen, slot);
            if (wearerRefusesItem()) {
                _game.showConfirmPopup(
                    _services.resource.strings.getText(kCannotWearStrRef),
                    [this, slot, chosen]() {
                        _slots[slot].held = take(chosen);
                        closeList();
                    },
                    [this, slot]() {
                        _item->removeUpgrade(slot);
                        _item->setUpgradeSlot(slot, std::nullopt);
                    });
                return;
            }
            place.held = take(chosen);
        } else {
            place.held = take(chosen);
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

// One upgrade of a larger stack is split off where the stack is, which may be
// inside an item the party carries. A single upgrade leaves the party's own
// items only: one inside a carried item stays listed there while installed.
// An upgrade taken and given back this opening is neither; one given back
// that the item held when opened is a stack to take it from again on Back.
std::shared_ptr<Item> ItemUpgradePanel::take(const std::shared_ptr<Item> &stack) {
    auto returned = std::find(_returned.begin(), _returned.end(), stack);
    auto taken = stack;
    if (stack->stackSize() > 1) {
        taken = takeEquipmentCandidate(_game, *_game.getObjectById(stack->owner()), stack);
    } else {
        partyInventory()->removeItemStack(stack);
    }
    if (returned != _returned.end()) {
        _returned.erase(returned);
    } else {
        _taken.push_back(taken);
    }
    return taken;
}

std::shared_ptr<Item> ItemUpgradePanel::giveBack(const std::shared_ptr<Item> &upgrade) {
    auto taken = std::find(_taken.begin(), _taken.end(), upgrade);
    const bool takenNow = taken != _taken.end();
    if (takenNow) {
        _taken.erase(taken);
    }
    auto stack = partyInventory()->addItem(upgrade);
    if (!takenNow) {
        _returned.push_back(stack);
    }
    return stack;
}

// Clicking the lists is silent: a click selecting another entry sounds as the
// selection moves, and an entry clicks as it is picked.
void ItemUpgradePanel::onClick(const std::string &control) {
    if (control == "BTN_ASSEMBLE") {
        _services.game.guiSounds.play(_services.game.guiSounds.getUpgrade());
        return;
    }
    if (control == _controls.LB_ITEMS->tag() ||
        control == _controls.LB_DESC->tag() ||
        (_controls.LB_DESC_LS && control == _controls.LB_DESC_LS->tag())) {
        return;
    }
    GameGUI::onClick(control);
}

bool ItemUpgradePanel::handle(const input::Event &event) {
    if (event.type == input::EventType::KeyDown &&
        !event.key.repeat &&
        event.key.code == input::KeyCode::Escape) {
        _services.game.guiSounds.play(_services.game.guiSounds.getOnClick());
        if (_listSlot) {
            closeList();
        } else {
            cancel();
        }
        return true;
    }
    if (event.type == input::EventType::KeyDown && handleActiveControlKey(event.key)) {
        return true;
    }
    return GameGUI::handle(event);
}

// The open list takes Return and Up and Down, its entry clicking as it is
// picked. Any other control holding the keyboard takes Return as its click,
// and the arrows move the keyboard along the layout's links; the control
// reached sounds as it is entered and is selected, describing a slot.
bool ItemUpgradePanel::handleActiveControlKey(const input::KeyEvent &key) {
    const auto active = _gui->activeControl();
    if (!active) {
        return false;
    }
    auto &control = active->get();
    if (&control == _controls.LB_ITEMS.get()) {
        return handleListKey(_services, key, *_controls.LB_ITEMS, [this](int entry) {
            _services.game.guiSounds.play(_services.game.guiSounds.getOnClick());
            pick(entry);
        });
    }
    if (!key.repeat && isChooseKey(key.code)) {
        clickControl(control.tag());
        return true;
    }
    const auto direction = moveDirectionOf(key.code);
    if (!direction) {
        return false;
    }
    if (const auto target = _gui->moveTarget(control, *direction)) {
        _services.game.guiSounds.play(_services.game.guiSounds.getOnEnter());
        _gui->selectActiveControl(target->get());
    }
    return true;
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
    // A control holding the keyboard keeps its hilight.
    _gui->setHilightFollowsKeyboard(true);
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
        // A click selects a row and a double click upgrades its item, with
        // the row's click; pointing at a row does neither.
        list.setSelectionMode(ListBox::SelectionMode::OnClick);
        // The rows keep the layout's text colour.
        auto &protoItem = list.protoItem();
        protoItem.setBorderColor(kTSLColorUpgradeBorder);
        protoItem.setHilightColor(kTSLColorUpgradeText);
        list.setOnItemDoubleClick([this](const std::string &tag) {
            _services.game.guiSounds.play(_services.game.guiSounds.getOnClick());
            beginUpgrade(_rows[std::stoi(tag)], false);
        });
        // The list holds the keyboard; the filters and buttons never take it.
        for (const auto &[filter, button] : _filterButtons) {
            button->setNavigable(false);
        }
        _controls.BTN_BACK->setNavigable(false);
        _controls.BTN_UPGRADEITEMS->setNavigable(false);
        _controls.BTN_CREATEITEMS->setNavigable(false);
        _gui->setActiveControl(list);
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
    // The kind buttons alone take the keyboard.
    _controls.BTN_BACK->setNavigable(false);
    _controls.BTN_UPGRADEITEMS->setNavigable(false);
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
    // With creation all the workbench offers, creation opens first; a script's
    // item still opens over it.
    if (_disableUpgrade) {
        openCreation();
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
        if (kindButton.kind == kind) {
            _gui->setActiveControl(*kindButton.button);
        }
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
    if (!selected && control == _controls.BTN_UPGRADEITEMS->tag()) {
        _controls.BTN_UPGRADEITEMS->setTextColor(_upgradeItemsTextColor);
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
    list.setIconTextFont(rowCountFont(_game, _services));
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
        // split off its stack there, and goes back to what holds it.
        if (auto taken = takeEquipmentCandidate(_game, *_game.party().sharedInventoryReceiver(_game.party().getLeader()), item)) {
            item = std::move(taken);
        } else {
            if (const auto holder = _game.getObjectById(item->owner())) {
                _origin.holder = holder;
            }
            if (item->stackSize() > 1) {
                item->setStackSize(item->stackSize() - 1);
                item = _game.newItemClone(*item);
                item->setStackSize(1);
            }
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
    } else if (_items->keepAfterUpgrade(held)) {
        _openPanel = OpenPanel::Items;
    } else {
        openItems(_origin.type, held);
    }
}

// The item goes back where it was, worn again when its wearer still can wear
// it, else into the party inventory.
std::shared_ptr<Item> UpgradeSelectionGUI::putBack(const std::shared_ptr<Item> &item) {
    auto inventory = _game.party().sharedInventoryReceiver(_game.party().getLeader());
    if (!_origin.slot) {
        // An item the party doesn't carry stays with what holds it, or goes
        // back there when it was split off or replaced; the party gains no
        // copy of it.
        if (const auto holder = _origin.holder.resolve()) {
            if (item->owner() == holder->id()) {
                return item;
            }
            return holder->addItem(item);
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
        _services.game.guiSounds.play(_services.game.guiSounds.getOnClick());
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
        _services.game.guiSounds.play(_services.game.guiSounds.getOnClick());
        close();
        return true;
    }
    // TSL's list holds the keyboard; its entry clicks as it is chosen.
    if (_game.isTSL() && event.type == input::EventType::KeyDown &&
        handleListKey(_services, event.key, *_controls.LB_UPGRADELIST, [this](int row) {
            _services.game.guiSounds.play(_services.game.guiSounds.getOnClick());
            beginUpgrade(_rows[row], false);
        })) {
        return true;
    }
    if (!_game.isTSL() && event.type == input::EventType::KeyDown && handleKindKey(event.key)) {
        return true;
    }
    return GameGUI::handle(event);
}

// The kind button holding the keyboard takes Return as its click, which opens
// its kind's list when the party has an item of it. The arrows move the
// keyboard along the layout's links; the control reached sounds as it is
// entered, and a kind button reached takes up and describes its kind.
bool UpgradeSelectionGUI::handleKindKey(const input::KeyEvent &key) {
    const auto active = _gui->activeControl();
    if (!active) {
        return false;
    }
    auto &control = active->get();
    const auto kindButtonOf = [this](const Control &button) {
        return std::find_if(_kindButtons.begin(), _kindButtons.end(), [&button](const KindButton &kindButton) {
            return kindButton.button.get() == &button;
        });
    };
    if (!key.repeat && isChooseKey(key.code)) {
        const auto kindButton = kindButtonOf(control);
        if (kindButton == _kindButtons.end()) {
            clickControl(control.tag());
            return true;
        }
        _services.game.guiSounds.play(_services.game.guiSounds.getOnClick());
        if (_kindsHeld.count(kindButton->kind) > 0) {
            openItems(kindButton->kind, nullptr);
        }
        return true;
    }
    const auto direction = moveDirectionOf(key.code);
    if (!direction) {
        return false;
    }
    if (const auto target = _gui->moveTarget(control, *direction)) {
        _services.game.guiSounds.play(_services.game.guiSounds.getOnEnter());
        const auto kindButton = kindButtonOf(target->get());
        if (kindButton != _kindButtons.end()) {
            describeKind(kindButton->kind);
        } else {
            _gui->selectActiveControl(target->get());
        }
    }
    return true;
}

// Clicking TSL's lists is silent: a click selecting another row sounds as the
// selection moves, and a row clicks as its item is upgraded.
void UpgradeSelectionGUI::onClick(const std::string &control) {
    if (_game.isTSL() &&
        (control == _controls.LB_UPGRADELIST->tag() || control == _controls.LB_DESCRIPTION->tag())) {
        return;
    }
    GameGUI::onClick(control);
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
    // TSL describes the selected row.
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
        // script's item, and never filled in TSL. With creation all the
        // workbench offers, the creation panel stays under both.
        if (_disableUpgrade) {
            _creation->render();
        }
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
