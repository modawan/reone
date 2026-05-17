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

#include "reone/game/gui/chargen/feats.h"

#include "reone/game/d20/classes.h"
#include "reone/game/d20/feats.h"
#include "reone/game/game.h"
#include "reone/game/gui/chargen.h"
#include "reone/gui/control/button.h"
#include "reone/gui/control/iconchain.h"
#include "reone/gui/control/listbox.h"
#include "reone/resource/provider/textures.h"

using namespace reone::audio;

using namespace reone::gui;
using namespace reone::graphics;
using namespace reone::resource;

namespace reone {

namespace game {

static constexpr int kFeatIconCellSize = 40;
static constexpr int kFeatIconColumnCount = 3;
static constexpr int kFeatIconSize = 32;
static constexpr int kK1VisibleFeatRows = 5;
static constexpr int kTSLVisibleFeatRows = 7;
static constexpr char kK1FeatCellFill[] = "lbl_indent";
static constexpr char kK1FeatCellBorderCorner[] = "border2d";
static constexpr char kK1FeatCellBorderEdge[] = "border1d";
static constexpr int kK1FeatCellBorderDimension = 8;
static constexpr glm::vec3 kK1FeatBorderGreen {0.278431f, 0.921569f, 0.105882f};
static constexpr glm::vec3 kK1FeatBorderGreenDim = 0.7f * kK1FeatBorderGreen;
static constexpr glm::vec3 kK1FeatBorderGold {0.980392f, 1.0f, 0.0f};
static constexpr glm::vec3 kK1FeatBorderRed {0.698039f, 0.0f, 0.0f};
static constexpr glm::vec3 kTSLLockedFeatBorderColor {0.698039f, 0.0f, 0.0f};
static constexpr glm::vec3 kTSLSelectableFeatBorderColor {0.05098f, 0.34902f, 0.270588f};
static constexpr glm::vec3 kTSLOwnedFeatBorderColor {0.101961f, 0.698039f, 0.54902f};
static constexpr glm::vec3 kTSLSelectedFeatBorderColor {1.0f};

void CharGenFeats::onGUILoaded() {
    bindControls();
    _defaultFeatNameText = _controls.LBL_NAME->text().text;

    auto iconChainControl = std::shared_ptr<Control>(_gui->newControl(ControlType::IconChain, "ICONCHAIN_FEATS"));
    _controls.ICONCHAIN_FEATS = std::static_pointer_cast<IconChain>(iconChainControl);
    _controls.ICONCHAIN_FEATS->setExtent(_controls.LB_FEATS->extent());
    _controls.ICONCHAIN_FEATS->setPadding(_controls.LB_FEATS->padding());
    _controls.ICONCHAIN_FEATS->setBorder(_controls.LB_FEATS->border());
    if (!_game.isTSL()) {
        _controls.ICONCHAIN_FEATS->setHilight(_controls.LB_FEATS->border());
        _controls.ICONCHAIN_FEATS->setHilightColor(_hilightColor);
    }
    _controls.ICONCHAIN_FEATS->setTintBorderFill(_game.isTSL());
    _controls.ICONCHAIN_FEATS->setCellSize(kFeatIconCellSize);
    auto &featListExtent = _controls.LB_FEATS->extent();
    auto &featProtoExtent = _controls.LB_FEATS->protoItem().extent();
    int featContentInsetY = featProtoExtent.top - featListExtent.top;
    int featOriginY = featContentInsetY;
    int featRowStep = featProtoExtent.height;
    if (!_game.isTSL()) {
        int featContentHeight = featListExtent.height - 2 * featContentInsetY;
        int remainingHeight = featContentHeight - kK1VisibleFeatRows * kFeatIconCellSize;
        int featGapCount = kK1VisibleFeatRows + 1;
        int featRowGap = (remainingHeight + featGapCount / 2) / featGapCount;
        featOriginY += featRowGap;
        featRowStep = kFeatIconCellSize + featRowGap;
    } else {
        int featFillInsetY = _controls.LB_FEATS->border().dimension;
        int featFillHeight = featListExtent.height - 2 * featFillInsetY;
        int featRowRange = featFillHeight - kFeatIconCellSize;
        int featGapCount = kTSLVisibleFeatRows - 1;
        std::vector<int> featRowOffsets;
        featRowOffsets.reserve(kTSLVisibleFeatRows);
        for (int row = 0; row < kTSLVisibleFeatRows; ++row) {
            featRowOffsets.push_back(
                featFillInsetY + (row * featRowRange + featGapCount / 2) / featGapCount);
        }
        _controls.ICONCHAIN_FEATS->setRowOffsets(std::move(featRowOffsets));
    }
    _controls.ICONCHAIN_FEATS->setCellOrigin(
        featProtoExtent.left - featListExtent.left,
        featOriginY);
    _controls.ICONCHAIN_FEATS->setCellStep(
        (featProtoExtent.width - kFeatIconCellSize) / (kFeatIconColumnCount - 1),
        featRowStep);
    IconChain::CellStyle cellStyle;
    if (!_game.isTSL()) {
        cellStyle.backgroundTexture = _services.resource.textures.get(
            kK1FeatCellFill,
            TextureUsage::GUI);
        cellStyle.itemBorder = std::make_shared<Control::Border>();
        cellStyle.itemBorder->corner = _services.resource.textures.get(
            kK1FeatCellBorderCorner,
            TextureUsage::GUI);
        cellStyle.itemBorder->edge = _services.resource.textures.get(
            kK1FeatCellBorderEdge,
            TextureUsage::GUI);
        cellStyle.itemBorder->dimension = kK1FeatCellBorderDimension;
        cellStyle.borderColors = std::make_shared<IconChain::CellStyle::BorderColors>();
        cellStyle.borderColors->owned = kK1FeatBorderGreenDim;
        cellStyle.borderColors->selected = kK1FeatBorderGreen;
        cellStyle.focusedBorderColors = std::make_shared<IconChain::CellStyle::FocusedBorderColors>();
        cellStyle.focusedBorderColors->locked = kK1FeatBorderRed;
        cellStyle.focusedBorderColors->selectable = kK1FeatBorderGold;
        cellStyle.focusedBorderColors->owned = kK1FeatBorderGreen;
        cellStyle.focusedBorderColors->selected = kK1FeatBorderGreen;
        cellStyle.onlyDrawItemBorderWhenBright = true;
    } else {
        cellStyle.borderColors = std::make_shared<IconChain::CellStyle::BorderColors>();
        cellStyle.borderColors->locked = kTSLLockedFeatBorderColor;
        cellStyle.borderColors->selectable = kTSLSelectableFeatBorderColor;
        cellStyle.borderColors->owned = kTSLOwnedFeatBorderColor;
        cellStyle.borderColors->selected = kTSLSelectedFeatBorderColor;
        cellStyle.focusedBorderColors = std::make_shared<IconChain::CellStyle::FocusedBorderColors>();
        cellStyle.focusedBorderColors->locked = kTSLLockedFeatBorderColor;
        cellStyle.focusedBorderColors->selectable = kTSLSelectableFeatBorderColor;
        cellStyle.focusedBorderColors->owned = kTSLOwnedFeatBorderColor;
        cellStyle.focusedBorderColors->selected = kTSLSelectedFeatBorderColor;
    }
    cellStyle.iconSize = kFeatIconSize;
    cellStyle.dimLockedBackground = !_game.isTSL();
    cellStyle.drawItemBorderFill = !_game.isTSL();
    _controls.ICONCHAIN_FEATS->setCellStyle(std::move(cellStyle));
    _controls.ICONCHAIN_FEATS->setVisible(false);
    _controls.ICONCHAIN_FEATS->setOnItemFocus([this](const std::string &item) {
        onFeatFocused(item);
    });
    _controls.ICONCHAIN_FEATS->setOnItemFocusCleared([this]() {
        resetFocusedFeatName();
    });
    _controls.ICONCHAIN_FEATS->setOnItemDoubleClick([this](const std::string &item) {
        onFeatActivated(item);
    });
    _gui->addControlToBack(_controls.ICONCHAIN_FEATS);

    _controls.LB_DESC->setProtoMatchContent(true);
    _controls.LB_FEATS->setSelectionMode(ListBox::SelectionMode::OnClick);
    _controls.LB_FEATS->setRenderItemIconsForButtonProto(true);
    _controls.LB_FEATS->setOnItemClick([this](const std::string &item) {
        onFeatFocused(item);
    });
    _controls.LB_FEATS->setOnItemDoubleClick([this](const std::string &item) {
        onFeatActivated(item);
    });

    _controls.BTN_ACCEPT->setOnClick([this]() {
        if (_levelUp) {
            if (_selectedFeats.size() != static_cast<size_t>(_points)) {
                return;
            }
            updateCharacter();
        }
        _charGen.goToNextStep();
        _charGen.openSteps();
    });
    _controls.BTN_BACK->setOnClick([this]() {
        _selectedFeats.clear();
        if (_levelUp) {
            _charGen.openSkills();
        } else {
            _charGen.openSteps();
        }
    });
    _controls.BTN_SELECT->setOnClick([this]() {
        activateFocusedFeat();
    });
    _controls.BTN_SELECT->setDisabled(true);
    _controls.BTN_RECOMMENDED->setDisabled(true);
}

void CharGenFeats::reset(bool levelUp) {
    _levelUp = levelUp;
    _points = 0;
    _displayEntries.clear();
    _selectedFeats.clear();
    _controls.LB_DESC->clearItems();
    resetFocusedFeatName();

    if (levelUp) {
        loadLevelUpDisplayEntries();
    }

    refreshControls();
    _controls.BTN_SELECT->setDisabled(true);
    _controls.BTN_RECOMMENDED->setDisabled(true);
}

void CharGenFeats::loadLevelUpDisplayEntries() {
    const CreatureAttributes &attributes = _charGen.character().attributes;
    std::shared_ptr<CreatureClass> clazz(_services.game.classes.get(attributes.getEffectiveClass()));

    _points = _services.game.feats.getLevelUpChoiceCount(attributes, *clazz);
    _displayEntries = _services.game.feats.getLevelUpDisplayEntries(attributes, *clazz);
}

void CharGenFeats::refreshControls() {
    refreshSelectionControls();
    refreshIconChain();
    refreshListBox();
}

void CharGenFeats::refreshSelectionControls() {
    _controls.STD_REMAINING_SELECTIONS_LBL->setTextMessage(std::to_string(_points - static_cast<int>(_selectedFeats.size())));
    _controls.BTN_ACCEPT->setDisabled(_levelUp && _selectedFeats.size() != static_cast<size_t>(_points));
}

void CharGenFeats::refreshIconChain() {
    _controls.ICONCHAIN_FEATS->clearItems();
    _controls.ICONCHAIN_FEATS->setVisible(_levelUp);
    if (!_levelUp) {
        return;
    }

    _controls.ICONCHAIN_FEATS->setColumnCount(kFeatIconColumnCount);

    std::map<FeatType, int> chainRowCounts;
    std::vector<FeatType> chainRoots;
    for (auto &entry : _displayEntries) {
        FeatType chainRoot = entry.chainRoot != FeatType::Invalid ? entry.chainRoot : entry.type;
        int wrappedRowCount = entry.visualIndex / kFeatIconColumnCount + 1;

        auto maybeChainRows = chainRowCounts.insert({chainRoot, wrappedRowCount});
        if (maybeChainRows.second) {
            chainRoots.push_back(chainRoot);
        } else {
            maybeChainRows.first->second = std::max(maybeChainRows.first->second, wrappedRowCount);
        }
    }

    std::map<FeatType, int> chainRowBases;
    int nextRow = 0;
    for (auto chainRoot : chainRoots) {
        chainRowBases.insert({chainRoot, nextRow});
        nextRow += chainRowCounts.at(chainRoot);
    }

    for (auto &entry : _displayEntries) {
        std::shared_ptr<Feat> feat(_services.game.feats.get(entry.type));
        if (!feat) {
            continue;
        }

        FeatType chainRoot = entry.chainRoot != FeatType::Invalid ? entry.chainRoot : entry.type;

        IconChain::Item item;
        item.tag = std::to_string(static_cast<int>(entry.type));
        item.row = chainRowBases.at(chainRoot) + entry.visualIndex / kFeatIconColumnCount;
        item.column = entry.visualIndex % kFeatIconColumnCount;
        item.iconTexture = feat->icon;
        item.selected = _selectedFeats.count(entry.type) > 0;
        switch (entry.availability) {
        case FeatAvailability::Owned:
            item.state = IconChain::State::Owned;
            break;
        case FeatAvailability::Selectable:
            item.state = IconChain::State::Selectable;
            break;
        case FeatAvailability::LockedMinLevel:
        case FeatAvailability::LockedMissingPrerequisite:
            item.state = IconChain::State::Locked;
            break;
        }
        _controls.ICONCHAIN_FEATS->addItem(std::move(item));
    }
}

void CharGenFeats::refreshIconChainSelection() {
    if (!_levelUp) {
        return;
    }

    for (auto &entry : _displayEntries) {
        _controls.ICONCHAIN_FEATS->setItemSelected(
            std::to_string(static_cast<int>(entry.type)),
            _selectedFeats.count(entry.type) > 0);
    }
}

void CharGenFeats::refreshListBox() {
    _controls.LB_FEATS->setVisible(!_levelUp);
    _controls.LB_FEATS->clearItems();
    for (auto &entry : _displayEntries) {
        std::shared_ptr<Feat> feat(_services.game.feats.get(entry.type));
        if (!feat) {
            continue;
        }

        std::string availabilityText;
        switch (entry.availability) {
        case FeatAvailability::Owned:
            availabilityText = "[OWNED] ";
            break;
        case FeatAvailability::Selectable:
            availabilityText = "[CAN PICK] ";
            break;
        case FeatAvailability::LockedMinLevel:
            availabilityText = "[LOCKED: level] ";
            break;
        case FeatAvailability::LockedMissingPrerequisite:
            availabilityText = "[LOCKED: prereq] ";
            break;
        }

        std::string tierIndent;
        if (entry.tier > 1) {
            tierIndent = std::string(static_cast<size_t>(entry.tier - 1) * 2, ' ');
        }

        ListBox::Item item;
        item.tag = std::to_string(static_cast<int>(entry.type));
        item.text = tierIndent + (_selectedFeats.count(entry.type) > 0 ? "* " : "") + availabilityText + feat->name;
        item.iconTexture = feat->icon;
        _controls.LB_FEATS->addItem(std::move(item));
    }
}

void CharGenFeats::updateCharacter() {
    Character character(_charGen.character());
    for (auto feat : _selectedFeats) {
        character.attributes.addFeat(feat);
    }
    _charGen.setCharacter(std::move(character));
}

void CharGenFeats::toggleSelectedFeat(FeatType feat) {
    auto maybeSelectedFeat = _selectedFeats.find(feat);
    if (maybeSelectedFeat != _selectedFeats.end()) {
        _selectedFeats.erase(maybeSelectedFeat);
    } else if (_points == 1) {
        _selectedFeats.clear();
        _selectedFeats.insert(feat);
    } else if (_selectedFeats.size() < static_cast<size_t>(_points)) {
        _selectedFeats.insert(feat);
    }
    refreshSelectionControls();
    refreshIconChainSelection();
}

void CharGenFeats::showFeatDescription(FeatType feat) {
    std::shared_ptr<Feat> featInfo(_services.game.feats.get(feat));
    if (!featInfo) {
        return;
    }

    _controls.LBL_NAME->setTextMessage(featInfo->name);
    _controls.LB_DESC->clearItems();
    _controls.LB_DESC->addTextLinesAsItems(featInfo->description);
}

void CharGenFeats::resetFocusedFeatName() {
    _controls.LBL_NAME->setTextMessage(_defaultFeatNameText);
}

void CharGenFeats::onFeatFocused(const std::string &feat) {
    showFeatDescription(static_cast<FeatType>(std::stoi(feat)));
    _controls.BTN_SELECT->setDisabled(!_levelUp);
}

void CharGenFeats::onFeatActivated(const std::string &feat) {
    auto featType = static_cast<FeatType>(std::stoi(feat));
    showFeatDescription(featType);

    auto maybeDisplayEntry = std::find_if(
        _displayEntries.begin(), _displayEntries.end(),
        [&featType](auto &entry) { return entry.type == featType; });
    if (_levelUp &&
        maybeDisplayEntry != _displayEntries.end() &&
        maybeDisplayEntry->availability == FeatAvailability::Selectable) {
        toggleSelectedFeat(featType);
    }
}

void CharGenFeats::activateFocusedFeat() {
    const IconChain::Item *item = _controls.ICONCHAIN_FEATS->focusedItem();
    if (!item) {
        return;
    }
    onFeatActivated(item->tag);
}

} // namespace game

} // namespace reone
