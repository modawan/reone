/*
 * Copyright (c) 2020-2026 The reone project contributors
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

#include "reone/game/gui/upgradescreens.h"

#include "reone/resource/strings.h"
#include "reone/script/types.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/itemdescription.h"
#include "reone/game/object/item.h"

using namespace reone::gui;

namespace reone {

namespace game {

/** "Color Crystal" and "Power Crystal", the lightsaber slot names. */
static constexpr int kColorCrystalStrRef = 36978;
static constexpr int kPowerCrystalStrRef = 36977;

static bool isEscape(const input::Event &event) {
    return event.type == input::EventType::KeyDown &&
           !event.key.repeat &&
           event.key.code == input::KeyCode::Escape;
}

static void setVisible(const std::shared_ptr<Control> &control, bool visible) {
    if (control) {
        control->setVisible(visible);
    }
}

static void setText(const std::shared_ptr<Control> &control, std::string text) {
    if (control) {
        control->setTextMessage(std::move(text));
    }
}

static ListBox::Item listEntry(const Item &item) {
    ListBox::Item entry;
    entry.tag = std::to_string(item.id());
    entry.text = item.localizedName();
    entry.iconTexture = item.icon();
    if (item.stackSize() > 1) {
        entry.iconText = std::to_string(item.stackSize());
    }
    return entry;
}

static std::optional<int> indexOfTag(const std::string &tag) {
    try {
        return std::stoi(tag);
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

// Selection

UpgradeSelectionGUI::UpgradeSelectionGUI(Game &game, ServicesView &services) :
    GameGUI(game, services),
    _bench(game, services) {
    _resRef = "upgradesel";
}

void UpgradeSelectionGUI::onGUILoaded() {
    _controls.BTN_ARMOR = findControl<Button>("BTN_ARMOR");
    _controls.BTN_BACK = findControl<Button>("BTN_BACK");
    _controls.BTN_LIGHTSABER = findControl<Button>("BTN_LIGHTSABER");
    _controls.BTN_MELEE = findControl<Button>("BTN_MELEE");
    _controls.BTN_RANGED = findControl<Button>("BTN_RANGED");
    _controls.BTN_UPGRADEITEMS = findControl<Button>("BTN_UPGRADEITEMS");

    auto bind = [this](const std::shared_ptr<Button> &button, std::optional<UpgradeCategory> category) {
        if (button) {
            button->setOnClick([this, category]() { _game.openUpgradeItems(category); });
        }
    };
    bind(_controls.BTN_RANGED, UpgradeCategory::Ranged);
    bind(_controls.BTN_MELEE, UpgradeCategory::Melee);
    bind(_controls.BTN_LIGHTSABER, UpgradeCategory::Lightsaber);
    bind(_controls.BTN_ARMOR, UpgradeCategory::Armor);
    bind(_controls.BTN_UPGRADEITEMS, std::nullopt);
    if (_controls.BTN_BACK) {
        _controls.BTN_BACK->setOnClick([this]() { _game.openInGame(); });
    }
}

// Chosen: a kind of item the party has none of to upgrade cannot be picked.
void UpgradeSelectionGUI::prepare() {
    auto enable = [this](const std::shared_ptr<Button> &button, UpgradeCategory category) {
        if (button) {
            button->setDisabled(_bench.upgradeableItems(category).empty());
        }
    };
    enable(_controls.BTN_RANGED, UpgradeCategory::Ranged);
    enable(_controls.BTN_MELEE, UpgradeCategory::Melee);
    enable(_controls.BTN_LIGHTSABER, UpgradeCategory::Lightsaber);
    enable(_controls.BTN_ARMOR, UpgradeCategory::Armor);
}

bool UpgradeSelectionGUI::handle(const input::Event &event) {
    if (isEscape(event)) {
        _game.openInGame();
        return true;
    }
    return GameGUI::handle(event);
}

// Items

UpgradeItemsGUI::UpgradeItemsGUI(Game &game, ServicesView &services) :
    GameGUI(game, services),
    _bench(game, services) {
    _resRef = "upgradeitems";
}

void UpgradeItemsGUI::onGUILoaded() {
    _controls.BTN_BACK = findControl<Button>("BTN_BACK");
    _controls.BTN_UPGRADEITEM = findControl<Button>("BTN_UPGRADEITEM");
    _controls.LB_DESCRIPTION = findControl<ListBox>("LB_DESCRIPTION");
    _controls.LB_ITEMS = findControl<ListBox>("LB_ITEMS");

    if (_controls.BTN_BACK) {
        _controls.BTN_BACK->setOnClick([this]() { _game.openUpgradeSelection(); });
    }
    if (_controls.BTN_UPGRADEITEM) {
        _controls.BTN_UPGRADEITEM->setOnClick([this]() { upgradeSelected(); });
    }
    if (_controls.LB_ITEMS) {
        _controls.LB_ITEMS->setSelectionMode(ListBox::SelectionMode::OnClick);
        _controls.LB_ITEMS->setOnItemClick([this](const std::string &tag) {
            if (auto index = indexOfTag(tag)) {
                select(*index);
            }
        });
        _controls.LB_ITEMS->setOnItemDoubleClick([this](const std::string &tag) {
            if (auto index = indexOfTag(tag)) {
                select(*index);
                upgradeSelected();
            }
        });
    }
}

void UpgradeItemsGUI::prepare(std::optional<UpgradeCategory> category) {
    _category = category;
    _listed.clear();
    auto items = category ? _bench.upgradeableItems(*category) : _bench.carriedUpgrades();
    if (_controls.LB_ITEMS) {
        _controls.LB_ITEMS->clearItems();
    }
    for (size_t i = 0; i < items.size(); ++i) {
        _listed.emplace_back(items[i]);
        if (_controls.LB_ITEMS) {
            auto entry = listEntry(*items[i]);
            entry.tag = std::to_string(i);
            _controls.LB_ITEMS->addItem(std::move(entry));
        }
    }
    // Chosen: the carried upgrade items are only listed; nothing is upgraded from them.
    setVisible(_controls.BTN_UPGRADEITEM, category.has_value());
    select(_listed.empty() ? -1 : 0);
}

void UpgradeItemsGUI::select(int index) {
    _selected = index >= 0 && index < static_cast<int>(_listed.size()) ? index : -1;
    if (_controls.LB_ITEMS && _selected >= 0) {
        _controls.LB_ITEMS->setSelectedItemIndex(_selected);
    }
    auto item = _selected >= 0 ? _listed[_selected].resolve() : nullptr;
    if (_controls.BTN_UPGRADEITEM) {
        _controls.BTN_UPGRADEITEM->setDisabled(!item);
    }
    if (_controls.LB_DESCRIPTION) {
        _controls.LB_DESCRIPTION->clearItems();
        if (item) {
            _controls.LB_DESCRIPTION->addTextLinesAsItems(buildItemDescription(*item, _game, _services));
        }
    }
}

void UpgradeItemsGUI::upgradeSelected() {
    if (!_category || _selected < 0) {
        return;
    }
    if (auto item = _listed[_selected].resolve()) {
        _game.openItemUpgrade(item, true);
    }
}

bool UpgradeItemsGUI::handle(const input::Event &event) {
    if (isEscape(event)) {
        _game.openUpgradeSelection();
        return true;
    }
    return GameGUI::handle(event);
}

// Upgrade

UpgradeGUI::UpgradeGUI(Game &game, ServicesView &services) :
    GameGUI(game, services),
    _bench(game, services) {
    _resRef = "upgrade";
}

void UpgradeGUI::onGUILoaded() {
    _controls.THREE_D_MODEL = findControl<Label>("3D_MODEL");
    _controls.THREE_D_MODEL_LS = findControl<Label>("3D_MODEL_LS");
    _controls.BTN_ASSEMBLE = findControl<Button>("BTN_ASSEMBLE");
    _controls.BTN_BACK = findControl<Button>("BTN_BACK");
    _controls.LBL_DESCBG = findControl<Label>("LBL_DESCBG");
    _controls.LBL_DESCBG_LS = findControl<Label>("LBL_DESCBG_LS");
    _controls.LBL_LSSLOTNAME = findControl<Label>("LBL_LSSLOTNAME");
    _controls.LBL_PROPERTY = findControl<Label>("LBL_PROPERTY");
    _controls.LBL_SLOTNAME = findControl<Label>("LBL_SLOTNAME");
    _controls.LBL_UPGRADES = findControl<Label>("LBL_UPGRADES");
    _controls.LBL_UPGRADE_COUNT = findControl<Label>("LBL_UPGRADE_COUNT");
    _controls.LB_DESC = findControl<ListBox>("LB_DESC");
    _controls.LB_DESC_LS = findControl<ListBox>("LB_DESC_LS");
    _controls.LB_ITEMS = findControl<ListBox>("LB_ITEMS");

    auto bindRow = [this](std::vector<SlotControls> &row, int first, int count) {
        for (int i = 0; i < count; ++i) {
            auto suffix = std::to_string(first + i);
            SlotControls slot;
            slot.label = findControl<Label>("LBL_UPGRADE" + suffix);
            slot.button = findControl<Button>("BTN_UPGRADE" + suffix);
            if (slot.label && slot.label->hasBorder()) {
                slot.emptyFill = slot.label->border().fill;
            }
            if (slot.button) {
                const int index = i;
                slot.button->setOnClick([this, index]() { openSlot(index); });
            }
            row.push_back(std::move(slot));
        }
    };
    bindRow(_threeRow, 31, 3);
    bindRow(_fourRow, 41, 4);

    // The 3D previews are not drawn; their labels author no text of their own.
    setText(_controls.THREE_D_MODEL, "");
    setText(_controls.THREE_D_MODEL_LS, "");

    if (_controls.BTN_ASSEMBLE) {
        _controls.BTN_ASSEMBLE->setOnClick([this]() {
            if (_picking >= 0) {
                fitChoice(_selectedChoice);
            } else {
                leave();
            }
        });
    }
    if (_controls.BTN_BACK) {
        _controls.BTN_BACK->setOnClick([this]() { back(); });
    }
    if (_controls.LB_ITEMS) {
        _controls.LB_ITEMS->setSelectionMode(ListBox::SelectionMode::OnClick);
        _controls.LB_ITEMS->setOnItemClick([this](const std::string &tag) {
            if (auto index = indexOfTag(tag)) {
                selectChoice(*index);
            }
        });
        _controls.LB_ITEMS->setOnItemDoubleClick([this](const std::string &tag) {
            if (auto index = indexOfTag(tag)) {
                fitChoice(*index);
            }
        });
    }
}

void UpgradeGUI::prepare(const std::shared_ptr<Item> &item, bool backToList) {
    _item = item;
    _backToList = backToList;
    if (item) {
        _listCategory = _bench.categoryOf(*item);
    }
    _slots = item ? _bench.slotsOf(*item) : std::vector<UpgradeSlot> {};
    _picking = -1;
    _choices.clear();
    _selectedChoice = -1;
    showSlots();
}

bool UpgradeGUI::isLightsaberLayout() const {
    auto item = _item.resolve();
    return item && item->isLightsaber();
}

// The four-slot row is for items with more slots than the three-slot row holds.
std::vector<UpgradeGUI::SlotControls> &UpgradeGUI::slotRow() {
    return !isLightsaberLayout() && _slots.size() > _threeRow.size() ? _fourRow : _threeRow;
}

// KotOR's upgrade.gui authors LB_DESC without a row prototype, so it can show
// no text; the lightsaber description list, over the same column, stands in.
bool UpgradeGUI::usesLightsaberDescription() const {
    return isLightsaberLayout() || !_controls.LB_DESC || !_controls.LB_DESC->protoItemOrNull();
}

std::shared_ptr<ListBox> UpgradeGUI::descriptionList() const {
    return usesLightsaberDescription() ? _controls.LB_DESC_LS : _controls.LB_DESC;
}

std::shared_ptr<Label> UpgradeGUI::slotNameLabel() const {
    return isLightsaberLayout() ? _controls.LBL_LSSLOTNAME : _controls.LBL_SLOTNAME;
}

const UpgradeGUI::Preview &UpgradeGUI::preview(const std::string &resRef) {
    auto found = _previews.find(resRef);
    if (found != _previews.end()) {
        return found->second;
    }
    // A template outside the world, read only for how it looks.
    Item scratch(script::kObjectInvalid, _game, _services);
    scratch.loadFromBlueprint(resRef);
    Preview preview;
    preview.name = scratch.localizedName();
    preview.description = buildItemDescription(scratch, _game, _services);
    preview.icon = scratch.icon();
    return _previews.emplace(resRef, std::move(preview)).first->second;
}

// Chosen: an upgrade slot is named after the upgrade item of the first row of
// its kind, which upgradetypes.2da gives no name of its own.
std::string UpgradeGUI::slotName(int slot) {
    if (slot < 0 || slot >= static_cast<int>(_slots.size())) {
        return "";
    }
    const auto &upgradeSlot = _slots[slot];
    if (upgradeSlot.type == UpgradeSlot::Type::ColorCrystal) {
        return _services.resource.strings.getText(kColorCrystalStrRef);
    }
    if (upgradeSlot.slotKind == kLightsaberUpgradeKind) {
        return _services.resource.strings.getText(kPowerCrystalStrRef);
    }
    for (const auto &row : _bench.table().rows()) {
        if (row.slotKind == upgradeSlot.slotKind) {
            return preview(row.templateResRef).name;
        }
    }
    return "";
}

std::string UpgradeGUI::installedTemplate(int slot) {
    auto item = _item.resolve();
    if (!item || slot < 0 || slot >= static_cast<int>(_slots.size())) {
        return "";
    }
    if (_slots[slot].type == UpgradeSlot::Type::ColorCrystal) {
        return colorCrystalForBladeVariation(item->modelVariation()).value_or("");
    }
    int row = _bench.installedRowsOf(*item)[slot];
    const auto *entry = row >= 0 ? _bench.table().findByRow(row) : nullptr;
    return entry ? entry->templateResRef : "";
}

void UpgradeGUI::showSlots() {
    const bool lightsaber = isLightsaberLayout();
    const bool picking = _picking >= 0;
    auto item = _item.resolve();

    const bool longDescription = usesLightsaberDescription();
    setVisible(_controls.THREE_D_MODEL, !longDescription);
    setVisible(_controls.LBL_DESCBG, !longDescription);
    setVisible(_controls.LB_DESC, !longDescription);
    setVisible(_controls.LBL_DESCBG_LS, longDescription);
    setVisible(_controls.LB_DESC_LS, longDescription);
    // The upgrade list takes the left half of the screen while it is shown.
    setVisible(_controls.LB_ITEMS, picking);
    setVisible(_controls.THREE_D_MODEL_LS, lightsaber && !picking);
    setVisible(_controls.LBL_LSSLOTNAME, lightsaber && !picking);
    setVisible(_controls.LBL_SLOTNAME, !lightsaber && !picking);
    setVisible(_controls.LBL_UPGRADES, !lightsaber && !picking);
    setVisible(_controls.LBL_UPGRADE_COUNT, !lightsaber && !picking);
    setVisible(_controls.LBL_PROPERTY, !lightsaber && !picking);

    auto &row = slotRow();
    for (auto *other : {&_threeRow, &_fourRow}) {
        for (auto &slot : *other) {
            setVisible(slot.label, false);
            setVisible(slot.button, false);
        }
    }
    int installed = 0;
    std::string installedNames;
    for (size_t i = 0; i < row.size(); ++i) {
        // Chosen: a row shows as many slots as it has places; the rest are not shown.
        const bool shown = !picking && i < _slots.size();
        setVisible(row[i].label, shown);
        setVisible(row[i].button, shown);
        if (!shown || !row[i].label) {
            continue;
        }
        auto resRef = installedTemplate(static_cast<int>(i));
        if (resRef.empty()) {
            row[i].label->setBorderFill(row[i].emptyFill);
            continue;
        }
        const auto &look = preview(resRef);
        row[i].label->setBorderFill(look.icon ? look.icon : row[i].emptyFill);
        if (_slots[i].type == UpgradeSlot::Type::Upgrade) {
            ++installed;
            installedNames += (installedNames.empty() ? "" : "\n") + look.name;
        }
    }

    if (!picking) {
        // Chosen: the slot name label names the item until a slot is pointed at.
        setText(slotNameLabel(), item ? item->localizedName() : "");
        // Chosen: the count is the upgrades fitted out of the upgrade slots.
        int upgradeSlots = static_cast<int>(std::count_if(_slots.begin(), _slots.end(), [](const auto &slot) {
            return slot.type == UpgradeSlot::Type::Upgrade;
        }));
        setText(_controls.LBL_UPGRADE_COUNT, str(boost::format("%d/%d") % installed % upgradeSlots));
        setText(_controls.LBL_PROPERTY, installedNames);
        showItemDescription();
    }
}

void UpgradeGUI::showDescription(const std::string &text) {
    auto list = descriptionList();
    if (!list) {
        return;
    }
    list->clearItems();
    list->addTextLinesAsItems(text);
}

void UpgradeGUI::showItemDescription() {
    auto item = _item.resolve();
    showDescription(item ? buildItemDescription(*item, _game, _services) : "");
}

void UpgradeGUI::onSelectionChanged(const std::string &control, bool selected) {
    GameGUI::onSelectionChanged(control, selected);
    if (_picking >= 0) {
        return;
    }
    auto &row = slotRow();
    for (size_t i = 0; i < row.size() && i < _slots.size(); ++i) {
        if (!row[i].button || row[i].button->tag() != control) {
            continue;
        }
        if (!selected) {
            showSlots();
            return;
        }
        setText(slotNameLabel(), slotName(static_cast<int>(i)));
        auto resRef = installedTemplate(static_cast<int>(i));
        if (!resRef.empty()) {
            showDescription(preview(resRef).description);
        }
        return;
    }
}

// Chosen: the list shows what the slot holds first, picking which takes it
// out, then the carried upgrades that fit; a lightsaber's crystal is never
// taken out, only replaced.
void UpgradeGUI::openSlot(int slot) {
    auto item = _item.resolve();
    if (!item || slot < 0 || slot >= static_cast<int>(_slots.size())) {
        return;
    }
    _picking = slot;
    _choices.clear();
    auto installed = installedTemplate(slot);
    if (!installed.empty() && _slots[slot].type == UpgradeSlot::Type::Upgrade) {
        _choices.push_back(Choice {installed, true});
    }
    for (const auto &carried : _bench.carriedFittingSlot(*item, slot)) {
        auto resRef = UpgradeBench::templateOf(*carried);
        bool listed = std::any_of(_choices.begin(), _choices.end(), [&resRef](const auto &choice) {
            return choice.templateResRef == resRef && !choice.installed;
        });
        if (!listed) {
            _choices.push_back(Choice {resRef, false});
        }
    }
    if (_controls.LB_ITEMS) {
        _controls.LB_ITEMS->clearItems();
        for (size_t i = 0; i < _choices.size(); ++i) {
            const auto &look = preview(_choices[i].templateResRef);
            ListBox::Item entry;
            entry.tag = std::to_string(i);
            entry.text = look.name;
            entry.iconTexture = look.icon;
            if (_choices[i].installed) {
                entry.textColor = kGUIColorHilight;
            }
            _controls.LB_ITEMS->addItem(std::move(entry));
        }
    }
    showSlots();
    setText(slotNameLabel(), slotName(slot));
    selectChoice(_choices.empty() ? -1 : 0);
}

void UpgradeGUI::selectChoice(int index) {
    _selectedChoice = index >= 0 && index < static_cast<int>(_choices.size()) ? index : -1;
    if (_selectedChoice < 0) {
        showItemDescription();
        return;
    }
    if (_controls.LB_ITEMS) {
        _controls.LB_ITEMS->setSelectedItemIndex(_selectedChoice);
    }
    showDescription(preview(_choices[_selectedChoice].templateResRef).description);
}

void UpgradeGUI::fitChoice(int index) {
    auto item = _item.resolve();
    if (!item || _picking < 0 || index < 0 || index >= static_cast<int>(_choices.size())) {
        return;
    }
    _bench.apply(*item, _picking, _choices[index].templateResRef);
    _picking = -1;
    _choices.clear();
    _selectedChoice = -1;
    showSlots();
}

void UpgradeGUI::back() {
    if (_picking >= 0) {
        _picking = -1;
        _choices.clear();
        _selectedChoice = -1;
        showSlots();
        return;
    }
    leave();
}

// Chosen: changes apply as they are made, so leaving never undoes them.
void UpgradeGUI::leave() {
    _previews.clear();
    if (_backToList) {
        _game.openUpgradeItems(_listCategory);
    } else {
        _game.openInGame();
    }
}

bool UpgradeGUI::handle(const input::Event &event) {
    if (isEscape(event)) {
        back();
        return true;
    }
    return GameGUI::handle(event);
}

} // namespace game

} // namespace reone
