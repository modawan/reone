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

#include "reone/game/gui/ingame/equip.h"

#include "reone/audio/mixer.h"
#include "reone/game/gui/sounds.h"
#include "reone/input/event.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/strings.h"

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

static constexpr int kStrRefNone = 363;
static constexpr int kStrRefEquipped = 32346;
// The back button's labels: Cancel over an open slot's items, Close over the overview.
static constexpr int kStrRefCancelButton = 1581;
static constexpr int kStrRefCloseButton = 1582;
// KotOR titles an open slot's item list.
static constexpr int kStrRefK1SelectTitle = 38154;
static constexpr char kNoneItemTag[] = "[none]";
static constexpr char kEquippedItemTag[] = "[equipped]";
// KotOR shows a raised damage or attack value in green, others in blue.
static constexpr glm::vec3 kK1RaisedValueColor {0.28f, 0.92f, 0.11f};
static constexpr glm::vec3 kK1ValueColor {0.0f, 0.66f, 0.98f};

static std::unordered_map<Equipment::Slot, std::string> g_slotNames = {
    {Equipment::Slot::Implant, "IMPLANT"},
    {Equipment::Slot::Head, "HEAD"},
    {Equipment::Slot::Hands, "HANDS"},
    {Equipment::Slot::ArmL, "ARM_L"},
    {Equipment::Slot::Body, "BODY"},
    {Equipment::Slot::ArmR, "ARM_R"},
    {Equipment::Slot::WeapL, "WEAP_L"},
    {Equipment::Slot::Belt, "BELT"},
    {Equipment::Slot::WeapR, "WEAP_R"},
    {Equipment::Slot::WeapL2, "WEAP_L2"},
    {Equipment::Slot::WeapR2, "WEAP_R2"}};

// A slot's name, and its name on a droid.
struct SlotNameStrRefs {
    int normal;
    int droid;
};

static const std::unordered_map<Equipment::Slot, SlotNameStrRefs> kSlotStrRefs = {
    {Equipment::Slot::Implant, {31388, 41811}},
    {Equipment::Slot::Head, {31375, 41810}},
    {Equipment::Slot::Hands, {31383, 41811}},
    {Equipment::Slot::ArmL, {31376, 41814}},
    {Equipment::Slot::Body, {31380, 41813}},
    {Equipment::Slot::ArmR, {31377, 41814}},
    {Equipment::Slot::WeapL, {31378, 31378}},
    {Equipment::Slot::Belt, {31382, 41812}},
    {Equipment::Slot::WeapR, {31379, 31379}},
    {Equipment::Slot::WeapL2, {31378, 31378}},
    {Equipment::Slot::WeapR2, {31379, 31379}}};

static int getInventorySlot(Equipment::Slot slot);

static void enableBorderFillTint(const std::shared_ptr<Control> &control) {
    if (!control) {
        return;
    }
    control->setTintBorderFill(true);
}

static void tintK2PanelFill(const std::shared_ptr<ListBox> &listBox, const glm::vec3 &baseColor) {
    if (!listBox) {
        return;
    }
    listBox->setBorderColor(baseColor);
    listBox->setTintBorderFill(true);
}

void Equipment::onGUILoaded() {
    loadBackground(BackgroundType::Menu);
    bindControls();

    if (_controls.LBL_BAR1)
        _lblBar.push_back(_controls.LBL_BAR1);
    if (_controls.LBL_BAR2)
        _lblBar.push_back(_controls.LBL_BAR2);
    if (_controls.LBL_BAR3)
        _lblBar.push_back(_controls.LBL_BAR3);
    if (_controls.LBL_BAR4)
        _lblBar.push_back(_controls.LBL_BAR4);
    if (_controls.LBL_BAR5)
        _lblBar.push_back(_controls.LBL_BAR5);

    for (auto &slotName : g_slotNames) {
        if ((slotName.first == Slot::WeapL2 || slotName.first == Slot::WeapR2) && !isTSL())
            continue;
        _lblInv[slotName.first] = findControl<Label>("LBL_INV_" + slotName.second);
        if (_lblInv[slotName.first]) {
            _lblInv[slotName.first]->setSharpenBorderFillAlpha(true);
        }
        _btnInv[slotName.first] = findControl<Button>("BTN_INV_" + slotName.second);
    }

    turnK1PartyArrowRight();
    // The portraits beside the shown character give control to those members.
    if (_controls.BTN_CHANGE1) {
        _controls.BTN_CHANGE1->setOnClick([this]() { changeCharacter(1); });
    }
    if (_controls.BTN_CHANGE2) {
        _controls.BTN_CHANGE2->setOnClick([this]() { changeCharacter(2); });
    }
    if (isTSL()) {
        useK2ShellTitle(_controls.LBL_TITLE);
        fillK2SectionStrip(_controls.LBL_BAR1, _controls.LBL_BAR2);
        for (auto &button : {_controls.BTN_BACK, _controls.BTN_EQUIP, _controls.BTN_SWAPWEAPONS}) {
            enableK2ButtonBodyFill(button);
        }
    }
    if (_controls.BTN_NEXTNPC)
        _controls.BTN_NEXTNPC->setOnClick([this]() {
            if (_backing) {
                _backing->nextCharacter();
            }
            update();
        });
    if (_controls.BTN_PREVNPC)
        _controls.BTN_PREVNPC->setOnClick([this]() {
            if (_backing) {
                _backing->previousCharacter();
            }
            update();
        });
    _controls.LB_DESC->setVisible(false);
    _controls.LB_DESC->setProtoMatchContent(true);
    _controls.LBL_CANTEQUIP->setVisible(false);
    tintK2LoadoutOverlay();
    updateK2LoadoutOverlayVisibility(true);

    configureItemsListBox();

    // TSL asks for the shown character's weapon sets to be swapped; the slots
    // show the swap once it is made.
    if (isTSL() && _controls.BTN_SWAPWEAPONS)
        _controls.BTN_SWAPWEAPONS->setOnClick([this]() {
            if (!_backing) {
                return;
            }
            _backing->switchWeapons();
            update();
        });
    // The equip button is shown only over an open slot's items.
    _controls.BTN_EQUIP->setOnClick([this]() {
        confirmSelectedCandidate();
    });
    _controls.BTN_BACK->setOnClick([this]() {
        if (_selectedSlot == Slot::None) {
            if (_onExit) _onExit();
        } else {
            selectSlot(Slot::None);
        }
    });

    for (auto &slotButton : _btnInv) {
        auto slot = slotButton.first;
        slotButton.second->setOnClick([this, slot]() {
            _view = _backing ? _backing->readEquipment(getInventorySlot(slot)) : EquipmentView {};
            // A slot that will not open says why in a message box.
            if (!_view.slotAvailable) {
                if (_view.slotRefusalStrRef != 0 && _onMessage) {
                    _onMessage(_strings.getText(_view.slotRefusalStrRef));
                }
                return;
            }

            selectSlot(slot);
            if (_onSlotOpened) {
                _onSlotOpened();
            }
        });
        slotButton.second->setOnSelectionChanged([this, slot](bool selected) {
            if (!selected)
                return;

            activateSlot(slot);

            const auto &strRefs = kSlotStrRefs.at(slot);
            _controls.LBL_SLOTNAME->setTextMessage(interfaceText(_view.droid ? strRefs.droid : strRefs.normal));
        });
    }
}

void Equipment::configureItemsListBox() {
    _controls.LB_ITEMS->setItemsInteractive(false);
    _controls.LB_ITEMS->setSelectionMode(ListBox::SelectionMode::OnClick);
    _controls.LB_ITEMS->setRenderItemIconsForButtonProto(true);
    // See InventoryMenu: K1's baked slot strip has a fractional period in
    // authored pixels, so the rows are repainted over it and this is a free
    // density knob. K2's equipment list uses its tighter two-pixel gap.
    _controls.LB_ITEMS->setPadding(isTSL() ? 2 : 8);
    useBakedItemSlotArt(*_controls.LB_ITEMS);
    _controls.LB_ITEMS->setOnItemClick([this](const std::string &item) {
        onItemsListBoxItemClick(item);
    });
    _controls.LB_ITEMS->setOnItemDoubleClick([this](const std::string &item) {
        confirmSelectedCandidate();
    });

    auto &protoItem = _controls.LB_ITEMS->protoItem();

    if (isTSL()) {
        enableK2ButtonBodyFill(protoItem);
        protoItem.setBorderFill("uibit_fill_2wt");
        protoItem.setHilightFill("uibit_fill_2wt");
        protoItem.setTintBorderFill(true);
        tintK2PanelFill(_controls.LB_ITEMS, _baseColor);
        tintK2PanelFill(_controls.LB_DESC, _baseColor);
    } else {
        protoItem.setBorderColor(_baseColor);
        protoItem.setHilightColor(_hilightColor);
    }
}

void Equipment::clearCandidateDescription() {
    _selectedItemIdx = -1;
    _controls.LB_DESC->clearItems();
}

void Equipment::updateCandidateDescription() {
    if (_selectedSlot == Slot::None) {
        clearCandidateDescription();
        return;
    }

    int selectedItemIdx = _controls.LB_ITEMS->selectedItemIndex();
    if (selectedItemIdx == _selectedItemIdx)
        return;

    _selectedItemIdx = selectedItemIdx;
    _controls.LB_DESC->clearItems();

    if (selectedItemIdx < 0 || selectedItemIdx >= _controls.LB_ITEMS->getItemCount())
        return;

    if (selectedItemIdx < static_cast<int>(_listedItems.size())) {
        const auto &item = _listedItems[selectedItemIdx];
        _controls.LB_DESC->addTextLinesAsItems(item.description);
        // An item the screen will not equip says why, and Equip is disabled.
        _controls.LBL_CANTEQUIP->setTextMessage(item.refusalStrRef != 0 ? interfaceText(item.refusalStrRef) : "");
        _controls.LBL_CANTEQUIP->setVisible(item.refusalStrRef != 0);
        _controls.BTN_EQUIP->setDisabled(item.refusalStrRef != 0);
    }
}

static int getInventorySlot(Equipment::Slot slot) {
    switch (slot) {
    case Equipment::Slot::Implant:
        return InventorySlots::implant;
    case Equipment::Slot::Head:
        return InventorySlots::head;
    case Equipment::Slot::Hands:
        return InventorySlots::hands;
    case Equipment::Slot::ArmL:
        return InventorySlots::leftArm;
    case Equipment::Slot::Body:
        return InventorySlots::body;
    case Equipment::Slot::ArmR:
        return InventorySlots::rightArm;
    case Equipment::Slot::WeapL:
        return InventorySlots::leftWeapon;
    case Equipment::Slot::Belt:
        return InventorySlots::belt;
    case Equipment::Slot::WeapR:
        return InventorySlots::rightWeapon;
    case Equipment::Slot::WeapL2:
        return InventorySlots::leftWeapon2;
    case Equipment::Slot::WeapR2:
        return InventorySlots::rightWeapon2;
    default:
        throw std::invalid_argument("Equipment: invalid slot: " + std::to_string(static_cast<int>(slot)));
    }
}

void Equipment::confirmSelectedCandidate() {
    int selectedItemIdx = _controls.LB_ITEMS->selectedItemIndex();
    if (selectedItemIdx < 0 || selectedItemIdx >= _controls.LB_ITEMS->getItemCount())
        return;

    confirmCandidateItem(_controls.LB_ITEMS->getItemAt(selectedItemIdx).tag);
}

void Equipment::confirmCandidateItem(const std::string &item) {
    if (_selectedSlot == Slot::None || !_backing || _awaitingRevision) return;
    if (item == kEquippedItemTag) { selectSlot(Slot::None); return; }
    auto index = _controls.LB_ITEMS->selectedItemIndex();
    if (index < 0 || index >= static_cast<int>(_listedItems.size()) || !_listedItems[index].valid) return;
    // Choosing an item or clearing the slot sounds when it is asked for,
    // whatever the outcome.
    auto clip = item == kNoneItemTag ? _presentation.sounds.getInventoryDrop() : _presentation.sounds.getInventorySelect();
    _audioSource = _presentation.mixer.play(std::move(clip), audio::AudioType::Sound);
    _awaitingRevision = _view.revision;
    _backing->equip(_view.revision, _listedItems[index].handle, getInventorySlot(_selectedSlot));
    receiveEquipmentResult();
}

void Equipment::receiveEquipmentResult() {
    if (!_backing || !_awaitingRevision) return;
    auto result = _backing->equipmentResult();
    if (!result || result->revision != *_awaitingRevision) return;
    _awaitingRevision.reset();
    updateEquipment();
    if (result->outcome != EquipmentRequestOutcome::Rejected) selectSlot(Slot::None);
    else activateSlot(_selectedSlot);
}

std::string Equipment::interfaceText(int strRef) const {
    return _backing ? _backing->interfaceText(strRef) : std::string();
}

void Equipment::setBacking(std::shared_ptr<IEquipmentMenuBacking> backing) {
    _backing = std::move(backing);
    _view = {};
    _listedItems.clear();
    _awaitingRevision.reset();
    if (_gui) update();
}

void Equipment::onItemsListBoxItemClick(const std::string &item) {
    updateCandidateDescription();
}

void Equipment::beginSession() {
    if (_backing) {
        _backing->beginEquipment();
    }
}

void Equipment::endSession() {
    if (_backing) {
        _backing->endEquipment();
    }
    _view.canBrowseCharacters = false;
}

void Equipment::update() {
    // A fresh presentation session must not react to the previous session's result.
    _awaitingRevision.reset();
    updateEquipment();
    updatePortraits();
    for (const auto &button : {_controls.BTN_NEXTNPC, _controls.BTN_PREVNPC}) {
        if (button) {
            button->setVisible(_view.canBrowseCharacters);
            button->setSelectable(_view.canBrowseCharacters);
        }
    }
    selectSlot(Slot::None);
    if (!isTSL()) _controls.LBL_VITALITY->setTextMessage(_view.subject.vitality);
    _controls.LBL_DEF->setTextMessage(_view.subject.defense);
}

bool Equipment::handle(const input::Event &event) {
    // Escape closes an open item list with a click and leaves the slot as it was.
    if (event.type == input::EventType::KeyDown && event.key.code == input::KeyCode::Escape &&
        _selectedSlot != Slot::None) {
        onClick("");
        selectSlot(Slot::None);
        return true;
    }
    // With no list open, the party key gives control to the next member standing.
    if (event.type == input::EventType::KeyDown && event.key.code == input::KeyCode::Tab &&
        _selectedSlot == Slot::None) {
        changeCharacter(-1);
        return true;
    }
    return PresentationGUI::handle(event);
}

void Equipment::changeCharacter(int member) {
    if (!_backing) {
        return;
    }
    _backing->changeCharacter(member);
    update();
}

void Equipment::update(float dt) {
    PresentationGUI::update(dt);
    // The slots show what the character has on at every frame; with an item
    // list open they are hidden.
    if (_selectedSlot == Slot::None) {
        updateEquipment();
    }
    receiveEquipmentResult();
    updateCandidateDescription();
}

void Equipment::openItems() {
    update();
    selectSlot(Slot::Body);
}

void Equipment::updatePortraits() {
    if (isTSL()) return;
    _controls.LBL_PORTRAIT->setBorderFill(_view.subject.portraits[0]);
    _controls.BTN_CHANGE1->setBorderFill(_view.subject.portraits[1]);
    _controls.BTN_CHANGE2->setBorderFill(_view.subject.portraits[2]);
}

void Equipment::selectSlot(Slot slot) {
    bool noneSelected = slot == Slot::None;

    for (auto &lbl : _lblInv) {
        lbl.second->setVisible(noneSelected);
    }
    for (auto &btn : _btnInv) {
        btn.second->setVisible(noneSelected);
    }

    _controls.LB_DESC->setVisible(!noneSelected);
    _controls.LBL_SLOTNAME->setVisible(noneSelected);
    // Over the overview the back button closes the screen and is its only
    // button; over a slot's items it goes back, beside the equip button.
    _controls.BTN_BACK->setTextMessage(interfaceText(noneSelected ? kStrRefCloseButton : kStrRefCancelButton));
    _controls.BTN_EQUIP->setVisible(!noneSelected);
    if (!isTSL() && !noneSelected) {
        _controls.LBL_SELECTTITLE->setTextMessage(interfaceText(kStrRefK1SelectTitle));
    }
    updateK2LoadoutOverlayVisibility(noneSelected);

    if (!isTSL()) {
        _controls.LBL_PORT_BORD->setVisible(noneSelected);
        _controls.LBL_PORTRAIT->setVisible(noneSelected);
        _controls.LBL_TXTBAR->setVisible(noneSelected);
    }
    _selectedSlot = slot;

    activateSlot(slot);
    if (noneSelected) {
        clearCandidateDescription();
    }
}

void Equipment::tintK2LoadoutOverlay() {
    if (!isTSL())
        return;

    // Preserve the muted K2 panel colours authored in equip_p.gui.
    enableBorderFillTint(_controls.LBL_BACK1);
    enableBorderFillTint(_controls.LBL_DEF_BACK);
}

void Equipment::updateK2LoadoutOverlayVisibility(bool visible) {
    if (!isTSL())
        return;

    auto setVisible = [visible](auto &control) {
        if (control) {
            control->setVisible(visible);
        }
    };

    // K2's normal loadout/stat art overlaps the candidate description panel.
    setVisible(_controls.BTN_SWAPWEAPONS);
    setVisible(_controls.LBL_ATKL);
    setVisible(_controls.LBL_ATKL2);
    setVisible(_controls.LBL_ATKR);
    setVisible(_controls.LBL_ATKR2);
    setVisible(_controls.LBL_ATTACKMOD);
    setVisible(_controls.LBL_ATTACK_INFO);
    setVisible(_controls.LBL_BACK1);
    setVisible(_controls.LBL_DAMAGE);
    setVisible(_controls.LBL_DAMTEXT);
    setVisible(_controls.LBL_DEF);
    setVisible(_controls.LBL_DEF_BACK);
    setVisible(_controls.LBL_DEF_INFO);
    setVisible(_controls.LBL_DEF_TEXT);
    setVisible(_controls.LBL_SELECTTITLE);
    setVisible(_controls.LBL_TOHIT);
    setVisible(_controls.LBL_TOHITL);
    setVisible(_controls.LBL_TOHITL2);
    setVisible(_controls.LBL_TOHITR);
    setVisible(_controls.LBL_TOHITR2);
}

void Equipment::activateSlot(Slot slot) {
    _activeSlot = slot;
    _controls.LB_ITEMS->setItemsInteractive(_selectedSlot != Slot::None);
    _controls.LBL_CANTEQUIP->setTextMessage("");
    _controls.LBL_CANTEQUIP->setVisible(false);
    _controls.BTN_EQUIP->setDisabled(false);
    clearCandidateDescription();
    updateItems();
    updateCandidateDescription();
}

void Equipment::updateEquipment() {
    _view = _backing ? _backing->readEquipmentOverview() : EquipmentView {};
    for (auto &[slot, label] : _lblInv) {
        auto equipped = _view.equipment.find(getInventorySlot(slot));
        label->setBorderFill(equipped == _view.equipment.end() ? getEmptySlotIcon(slot) : equipped->second);
    }
    _controls.LBL_ATKR->setTextMessage(_view.mainDamage);
    _controls.LBL_ATKL->setTextMessage(_view.offDamage);
    _controls.LBL_TOHITL->setTextMessage(_view.offAttack);
    _controls.LBL_TOHITR->setTextMessage(_view.mainAttack);
    if (isTSL()) {
        // Only TSL has a second weapon set.
        _controls.LBL_ATKR2->setTextMessage(_view.mainDamage2);
        _controls.LBL_ATKL2->setTextMessage(_view.offDamage2);
        _controls.LBL_TOHITL2->setTextMessage(_view.offAttack2);
        _controls.LBL_TOHITR2->setTextMessage(_view.mainAttack2);
    } else {
        auto color = [](bool raised) { return raised ? kK1RaisedValueColor : kK1ValueColor; };
        _controls.LBL_ATKR->setTextColor(color(_view.mainDamageRaised));
        _controls.LBL_ATKL->setTextColor(color(_view.offDamageRaised));
        _controls.LBL_TOHITL->setTextColor(color(_view.offAttackRaised));
        _controls.LBL_TOHITR->setTextColor(color(_view.mainAttackRaised));
    }
}

std::shared_ptr<Texture> Equipment::getEmptySlotIcon(Slot slot) const {
    std::string resRef;
    switch (slot) {
    case Slot::Implant:
        resRef = "iimplant";
        break;
    case Slot::Head:
        resRef = "ihead";
        break;
    case Slot::Hands:
        resRef = "ihands";
        break;
    case Slot::ArmL:
        resRef = "iforearm_l";
        break;
    case Slot::Body:
        resRef = "iarmor";
        break;
    case Slot::ArmR:
        resRef = "iforearm_r";
        break;
    case Slot::WeapL:
    case Slot::WeapL2:
        resRef = "iweap_l";
        break;
    case Slot::Belt:
        resRef = "ibelt";
        break;
    case Slot::WeapR:
    case Slot::WeapR2:
        resRef = "iweap_r";
        break;
    default:
        return nullptr;
    }

    return _presentation.textures.get(resRef, TextureUsage::GUI);
}

void Equipment::updateItems() {
    _controls.LB_ITEMS->clearItems();
    clearCandidateDescription();
    _view = _backing ? _backing->readEquipment(_activeSlot == Slot::None ? -1 : getInventorySlot(_activeSlot)) : EquipmentView {};
    _listedItems.clear();
    if (_activeSlot != Slot::None) {
        MenuItemView none;
        none.name = interfaceText(kStrRefNone);
        none.icon = _presentation.textures.get("inone", TextureUsage::GUI);
        _listedItems.push_back(std::move(none));
    }
    if (_view.slotAvailable) _listedItems.insert(_listedItems.end(), _view.items.begin(), _view.items.end());
    bool equipped = false;
    for (const auto &item : _listedItems) {
        ListBox::Item row;
        row.tag = item.handle == 0 ? kNoneItemTag : (item.equipped ? kEquippedItemTag : std::to_string(item.handle));
        row.text = item.name + (item.equipped ? " (" + interfaceText(kStrRefEquipped) + ")" : "");
        row.iconTexture = item.icon;
        row.iconFrame = itemFrameTexture(item.stackSize);
        row.invalid = !item.valid;
        if (item.stackSize > 1) row.iconText = std::to_string(item.stackSize);
        _controls.LB_ITEMS->addItem(std::move(row));
        equipped = equipped || item.equipped;
    }
    if (_selectedSlot != Slot::None && _view.slotAvailable)
        _controls.LB_ITEMS->setSelectedItemIndex(equipped ? 1 : 0);
}

} // namespace game

} // namespace reone
