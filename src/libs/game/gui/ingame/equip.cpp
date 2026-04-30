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

#include "reone/game/di/services.h"
#include "reone/game/equipmentrules.h"
#include "reone/game/game.h"
#include "reone/game/gui/ingame.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/party.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/strings.h"

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

static constexpr int kStrRefNone = 363;
static constexpr int kStrRefBlockedByTwoHandedMainHand = 42344;
static constexpr char kNoneItemTag[] = "[none]";
static constexpr char kEquippedItemTag[] = "[equipped]";
static constexpr char kEquippedItemSuffix[] = " (equipped)";

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

static std::unordered_map<Equipment::Slot, int32_t> g_slotStrRefs = {
    {Equipment::Slot::Implant, 31388},
    {Equipment::Slot::Head, 31375},
    {Equipment::Slot::Hands, 31383},
    {Equipment::Slot::ArmL, 31376},
    {Equipment::Slot::Body, 31380},
    {Equipment::Slot::ArmR, 31377},
    {Equipment::Slot::WeapL, 31378},
    {Equipment::Slot::Belt, 31382},
    {Equipment::Slot::WeapR, 31379},
    {Equipment::Slot::WeapL2, 31378},
    {Equipment::Slot::WeapR2, 31379}};

static int getInventorySlot(Equipment::Slot slot);

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
        if ((slotName.first == Slot::WeapL2 || slotName.first == Slot::WeapR2) && !_game.isTSL())
            continue;
        _lblInv[slotName.first] = findControl<Label>("LBL_INV_" + slotName.second);
        _btnInv[slotName.first] = findControl<Button>("BTN_INV_" + slotName.second);
    }

    if (_controls.BTN_CHANGE1) {
        _controls.BTN_CHANGE1->setSelectable(false);
    }
    if (_controls.BTN_CHANGE2) {
        _controls.BTN_CHANGE2->setSelectable(false);
    }
    // _controls.btnCharLeft->setVisible(false);
    // _controls.btnCharRight->setVisible(false);
    _controls.LB_DESC->setVisible(false);
    _controls.LB_DESC->setProtoMatchContent(true);
    _controls.LBL_CANTEQUIP->setVisible(false);

    configureItemsListBox();

    _controls.BTN_EQUIP->setOnClick([this]() {
        if (_selectedSlot == Slot::None) {
            _game.openInGame();
        } else {
            confirmSelectedCandidate();
        }
    });
    _controls.BTN_BACK->setOnClick([this]() {
        if (_selectedSlot == Slot::None) {
            _game.openInGame();
        } else {
            selectSlot(Slot::None);
        }
    });

    for (auto &slotButton : _btnInv) {
        auto slot = slotButton.first;
        slotButton.second->setOnClick([this, slot]() {
            std::shared_ptr<Creature> partyLeader(_game.party().getLeader());
            auto activation = evaluateEquipmentSlotActivation(*partyLeader, getInventorySlot(slot));
            if (!activation.available) {
                _controls.LBL_CANTEQUIP->setTextMessage(_services.resource.strings.getText(kStrRefBlockedByTwoHandedMainHand));
                _controls.LBL_CANTEQUIP->setVisible(true);
                return;
            }

            selectSlot(slot);
        });
        slotButton.second->setOnSelectionChanged([this, slot](bool selected) {
            if (!selected)
                return;

            activateSlot(slot);

            std::string slotDesc;

            auto maybeStrRef = g_slotStrRefs.find(slot);
            if (maybeStrRef != g_slotStrRefs.end()) {
                slotDesc = _services.resource.strings.getText(maybeStrRef->second);
            }

            _controls.LBL_SLOTNAME->setTextMessage(slotDesc);
        });
    }
}

void Equipment::configureItemsListBox() {
    _controls.LB_ITEMS->setItemsInteractive(false);
    _controls.LB_ITEMS->setSelectionMode(ListBox::SelectionMode::OnClick);
    _controls.LB_ITEMS->setRenderItemIconsForButtonProto(true);
    _controls.LB_ITEMS->setPadding(5);
    _controls.LB_ITEMS->setOnItemClick([this](const std::string &item) {
        onItemsListBoxItemClick(item);
    });
    _controls.LB_ITEMS->setOnItemDoubleClick([this](const std::string &item) {
        confirmSelectedCandidate();
    });

    auto &protoItem = _controls.LB_ITEMS->protoItem();
    protoItem.setBorderColor(_baseColor);
    protoItem.setHilightColor(_hilightColor);
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

    const ListBox::Item &lbItem = _controls.LB_ITEMS->getItemAt(selectedItemIdx);
    if (lbItem.tag == kNoneItemTag)
        return;

    std::shared_ptr<Item> itemObj;
    if (lbItem.tag == kEquippedItemTag) {
        std::shared_ptr<Creature> partyLeader(_game.party().getLeader());
        itemObj = partyLeader->getEquippedItem(getInventorySlot(_selectedSlot));
    } else {
        std::shared_ptr<Creature> player(_game.party().player());
        for (auto &playerItem : player->items()) {
            if (playerItem->tag() == lbItem.tag) {
                itemObj = playerItem;
                break;
            }
        }
    }
    if (!itemObj)
        return;

    std::string description(itemObj->localizedName());
    if (!itemObj->descIdentified().empty()) {
        description += "\n\n";
        description += itemObj->descIdentified();
    }
    _controls.LB_DESC->addTextLinesAsItems(description);
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
    if (_selectedSlot == Slot::None)
        return;
    if (item == kEquippedItemTag) {
        selectSlot(Slot::None);
        return;
    }

    std::shared_ptr<Creature> player(_game.party().player());
    std::shared_ptr<Item> itemObj;
    if (item != kNoneItemTag) {
        for (auto &playerItem : player->items()) {
            if (playerItem->tag() == item) {
                itemObj = playerItem;
                break;
            }
        }
    }
    std::shared_ptr<Creature> partyLeader(_game.party().getLeader());
    EquipmentCandidateDecision decision(evaluateEquipmentCandidate(*partyLeader, getInventorySlot(_selectedSlot), itemObj.get()));
    if (!decision.valid)
        return;

    int slot = decision.actualSlot;
    std::shared_ptr<Item> equipped(partyLeader->getEquippedItem(slot));

    bool clearPairedOffHand = decision.action == EquipmentCandidateAction::ClearMainHandAndOffHand ||
                              decision.action == EquipmentCandidateAction::EquipAndClearOffHand;
    std::shared_ptr<Item> pairedOffHand(clearPairedOffHand ? partyLeader->getEquippedItem(decision.pairedSlot) : nullptr);

    bool clearAction = decision.action == EquipmentCandidateAction::ClearSlot ||
                       decision.action == EquipmentCandidateAction::ClearMainHandAndOffHand;
    bool equipmentChanged = equipped != itemObj || pairedOffHand;
    if (equipmentChanged) {
        if (equipped) {
            partyLeader->unequip(equipped);
            player->addItem(equipped);
        }
        if (pairedOffHand) {
            partyLeader->unequip(pairedOffHand);
            player->addItem(pairedOffHand);
        }
        if (itemObj) {
            bool last;
            if (player->removeItem(itemObj, last)) {
                if (last) {
                    if (!partyLeader->equip(slot, itemObj)) {
                        player->addItem(itemObj);
                    }
                } else {
                    std::shared_ptr<Item> clonedItem = _game.newItem();
                    clonedItem->loadFromBlueprint(itemObj->blueprintResRef());
                    if (!partyLeader->equip(slot, clonedItem)) {
                        player->addItem(itemObj);
                    }
                }
            }
        }
    }
    if (equipmentChanged || clearAction) {
        updateEquipment();
        selectSlot(Slot::None);
    }
}

void Equipment::onItemsListBoxItemClick(const std::string &item) {
    updateCandidateDescription();
}

void Equipment::update() {
    updatePortraits();
    updateEquipment();
    selectSlot(Slot::None);

    auto partyLeader(_game.party().getLeader());

    if (!_game.isTSL()) {
        std::string vitalityString(str(boost::format("%d/\n%d") % partyLeader->currentHitPoints() % partyLeader->hitPoints()));
        _controls.LBL_VITALITY->setTextMessage(vitalityString);
    }
    _controls.LBL_DEF->setTextMessage(std::to_string(partyLeader->getDefense()));
}

void Equipment::update(float dt) {
    GameGUI::update(dt);
    updateCandidateDescription();
}

void Equipment::updatePortraits() {
    if (_game.isTSL())
        return;

    Party &party = _game.party();
    std::shared_ptr<Creature> partyLeader(party.getLeader());
    std::shared_ptr<Creature> partyMember1(party.getMember(1));
    std::shared_ptr<Creature> partyMember2(party.getMember(2));

    _controls.LBL_PORTRAIT->setBorderFill(partyLeader->portrait());
    _controls.BTN_CHANGE1->setBorderFill(partyMember1 ? partyMember1->portrait() : nullptr);
    _controls.BTN_CHANGE2->setBorderFill(partyMember2 ? partyMember2->portrait() : nullptr);
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

    if (!_game.isTSL()) {
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

void Equipment::activateSlot(Slot slot) {
    _activeSlot = slot;
    _controls.LB_ITEMS->setItemsInteractive(_selectedSlot != Slot::None);
    _controls.LBL_CANTEQUIP->setTextMessage("");
    _controls.LBL_CANTEQUIP->setVisible(false);
    clearCandidateDescription();
    updateItems();
    updateCandidateDescription();
}

void Equipment::updateEquipment() {
    std::shared_ptr<Creature> partyLeader(_game.party().getLeader());
    auto &equipment = partyLeader->equipment();

    for (auto &lbl : _lblInv) {
        int slot = getInventorySlot(lbl.first);
        std::shared_ptr<Texture> fill;

        auto equipped = equipment.find(slot);
        if (equipped != equipment.end()) {
            fill = equipped->second->icon();
        } else {
            fill = getEmptySlotIcon(lbl.first);
        }

        lbl.second->setBorderFill(fill);
    }

    int min, max;
    partyLeader->getMainHandDamage(min, max);
    _controls.LBL_ATKR->setTextMessage(str(boost::format("%d-%d") % min % max));

    partyLeader->getOffhandDamage(min, max);
    _controls.LBL_ATKL->setTextMessage(str(boost::format("%d-%d") % min % max));

    int attackBonus = partyLeader->getAttackBonus();
    std::string attackBonusString(std::to_string(attackBonus));
    if (attackBonus > 0) {
        attackBonusString.insert(0, "+");
    }
    _controls.LBL_TOHITL->setTextMessage(attackBonusString);
    _controls.LBL_TOHITR->setTextMessage(attackBonusString);
}

std::shared_ptr<Texture> Equipment::getEmptySlotIcon(Slot slot) const {
    static std::unordered_map<Slot, std::shared_ptr<Texture>> icons;

    auto icon = icons.find(slot);
    if (icon != icons.end())
        return icon->second;

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

    std::shared_ptr<Texture> texture(_services.resource.textures.get(resRef, TextureUsage::GUI));
    auto pair = icons.insert(std::make_pair(slot, texture));

    return pair.first->second;
}

void Equipment::updateItems() {
    _controls.LB_ITEMS->clearItems();
    clearCandidateDescription();
    std::shared_ptr<Creature> partyLeader(_game.party().getLeader());
    std::shared_ptr<Item> equipped;
    int activeInventorySlot = -1;

    if (_activeSlot != Slot::None) {
        activeInventorySlot = getInventorySlot(_activeSlot);

        ListBox::Item lbItem;
        lbItem.tag = kNoneItemTag;
        lbItem.text = _services.resource.strings.getText(kStrRefNone);
        lbItem.iconTexture = _services.resource.textures.get("inone", TextureUsage::GUI);
        lbItem.iconFrame = getItemFrameTexture(1);

        _controls.LB_ITEMS->addItem(std::move(lbItem));

        auto activation = evaluateEquipmentSlotActivation(*partyLeader, activeInventorySlot);
        if (!activation.available)
            return;

        equipped = partyLeader->getEquippedItem(activeInventorySlot);
        if (equipped) {
            ListBox::Item equippedItem;
            equippedItem.tag = kEquippedItemTag;
            equippedItem.text = equipped->localizedName() + kEquippedItemSuffix;
            equippedItem.iconTexture = equipped->icon();
            equippedItem.iconFrame = getItemFrameTexture(equipped->stackSize());

            if (equipped->stackSize() > 1) {
                equippedItem.iconText = std::to_string(equipped->stackSize());
            }
            _controls.LB_ITEMS->addItem(std::move(equippedItem));
        }
    }
    std::shared_ptr<Creature> player(_game.party().player());

    for (auto &item : player->items()) {
        if (item == equipped)
            continue;

        EquipmentCandidateDecision decision;
        bool hasDecision = false;
        if (_activeSlot == Slot::None) {
            if (!item->isEquippable())
                continue;
        } else {
            decision = evaluateEquipmentCandidate(*partyLeader, activeInventorySlot, item.get());
            hasDecision = true;
            if (!decision.visible)
                continue;
        }
        ListBox::Item lbItem;
        lbItem.tag = item->tag();
        lbItem.text = item->localizedName();
        lbItem.iconTexture = item->icon();
        lbItem.iconFrame = getItemFrameTexture(item->stackSize());
        if (hasDecision) {
            lbItem.invalid = !decision.valid;
        }

        if (item->stackSize() > 1) {
            lbItem.iconText = std::to_string(item->stackSize());
        }
        _controls.LB_ITEMS->addItem(std::move(lbItem));
    }
    if (_selectedSlot != Slot::None) {
        _controls.LB_ITEMS->setSelectedItemIndex(equipped ? 1 : 0);
    }
}

std::shared_ptr<Texture> Equipment::getItemFrameTexture(int stackSize) const {
    std::string resRef;
    if (_game.isTSL()) {
        if (stackSize >= 100) {
            resRef = "uibit_eqp_itm3";
        } else if (stackSize > 1) {
            resRef = "uibit_eqp_itm2";
        } else {
            resRef = "uibit_eqp_itm1";
        }
    } else {
        if (stackSize >= 100) {
            resRef = "lbl_hex_7";
        } else if (stackSize > 1) {
            resRef = "lbl_hex_6";
        } else {
            resRef = "lbl_hex_3";
        }
    }
    return _services.resource.textures.get(resRef, TextureUsage::GUI);
}

} // namespace game

} // namespace reone
