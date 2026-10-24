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

#include "reone/game/gui/chargen/iconselection.h"

#include "reone/game/d20/classes.h"
#include "reone/game/d20/feats.h"
#include "reone/game/game.h"
#include "reone/game/gui/chargen.h"
#include "reone/game/party.h"
#include "reone/gui/control/button.h"
#include "reone/gui/control/iconchain.h"
#include "reone/gui/control/listbox.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/strings.h"

#include <algorithm>
#include <cmath>

using namespace reone::audio;

using namespace reone::gui;
using namespace reone::graphics;
using namespace reone::resource;

namespace reone {

namespace game {

// The screen's titles: the main title of a level-up, and the subtitle of the
// choice of bonus feats.
static constexpr int kLevelUpTitleStrRef = 1071;
static constexpr int kBonusFeatsTitleStrRef = 1316;

// The messages for a pick: no feats left to choose, a feat the level grants,
// a feat already held, and one that cannot be chosen.
static constexpr int kNoPicksLeftStrRef = 42530;
static constexpr int kGrantedFeatStrRef = 42183;
static constexpr int kOwnedFeatStrRef = 42182;
static constexpr int kUnavailableFeatStrRef = 42184;

// The message for an accept while a feat can still be chosen.
static constexpr int kK1FeatsLeftStrRef = 48215;
static constexpr int kTSLFeatsLeftStrRef = 48730;

static IconChain::State getIconState(CharGenFeats::FeatStatus status) {
    switch (status) {
    case CharGenFeats::FeatStatus::Owned:
    case CharGenFeats::FeatStatus::Granted:
        return IconChain::State::Owned;
    case CharGenFeats::FeatStatus::Available:
    case CharGenFeats::FeatStatus::Chosen:
        return IconChain::State::Selectable;
    default:
        return IconChain::State::Locked;
    }
}

static std::map<FeatType, glm::ivec2> getFeatIconPositions(
    const std::vector<FeatDisplayEntry> &entries) {

    std::map<FeatType, int> chainRowCounts;
    std::vector<FeatType> chainRoots;
    for (auto &entry : entries) {
        FeatType chainRoot = entry.chainRoot != FeatType::Invalid ? entry.chainRoot : entry.type;
        int wrappedRowCount = entry.visualIndex / kIconSelectionColumnCount + 1;

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

    std::map<FeatType, glm::ivec2> result;
    for (auto &entry : entries) {
        FeatType chainRoot = entry.chainRoot != FeatType::Invalid ? entry.chainRoot : entry.type;
        result.insert({
            entry.type,
            {
                entry.visualIndex % kIconSelectionColumnCount,
                chainRowBases.at(chainRoot) + entry.visualIndex / kIconSelectionColumnCount
            }});
    }
    return result;
}

/** The controls the panel shows in both titles. */
static const std::unordered_set<std::string> kPanelControls {
    "BTN_ACCEPT", "BTN_BACK", "BTN_RECOMMENDED", "BTN_SELECT", "LBL_NAME", "LB_DESC",
    "LB_FEATS", "MAIN_TITLE_LBL", "REMAINING_BOX_LBL", "STD_REMAINING_SELECTIONS_LBL", "STD_SELECTIONS_REMAINING_LBL", "SUB_TITLE_LBL"};
/** The controls the panel also shows in KotOR. */
static const std::unordered_set<std::string> kK1PanelControls {"DESC_LBL"};
/** The controls the panel also shows in TSL. */
static const std::unordered_set<std::string> kTSLPanelControls {"LBL_BAR1", "LBL_BAR2"};

void CharGenFeats::onGUILoaded() {
    bindControls();
    showOnlyNamedControls(kPanelControls, _game.isTSL() ? kTSLPanelControls : kK1PanelControls);
    _defaultFeatNameText = _controls.LBL_NAME->text().text;
    _defaultMainTitleText = _controls.MAIN_TITLE_LBL->text().text;
    _defaultSubTitleText = _controls.SUB_TITLE_LBL->text().text;

    styleChargenTitles(_game, *_controls.STD_SELECTIONS_REMAINING_LBL, *_controls.LB_DESC);

    IconSelectionCallbacks callbacks;
    callbacks.onItemFocus = [this](const std::string &item) {
        onFeatFocused(item);
    };
    callbacks.onItemFocusCleared = [this]() {
        resetFocusedFeatName();
    };
    callbacks.onItemDoubleClick = [this](const std::string &item) {
        onFeatActivated(item);
    };
    _controls.ICONCHAIN_FEATS = addIconSelectionChain(
        *_gui,
        _game,
        _services,
        "ICONCHAIN_FEATS",
        *_controls.LB_FEATS,
        _hilightColor,
        std::move(callbacks));
    _controls.ICONCHAIN_FEATS->setVisible(false);

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
        accept();
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

void CharGenFeats::reset(bool levelUp, bool bonus, std::set<FeatType> originalFeats, const std::vector<FeatType> &grantedFeats) {
    _levelUp = levelUp;
    _bonus = bonus;
    _originalFeats = std::move(originalFeats);
    // TSL does not count Complex Unarmed Anims and Mine Immunity among the
    // feats held before the screen.
    if (_game.isTSL()) {
        _originalFeats.erase(FeatType::ComplexUnarmedAnims);
        _originalFeats.erase(FeatType::MineImmunity);
    }
    _grantedFeats.clear();
    for (auto feat : grantedFeats) {
        if (_originalFeats.count(feat) == 0) {
            _grantedFeats.insert(feat);
        }
    }
    // A level-up character's effects give it feats; a new character has none.
    _effectFeats = levelUp ? _game.party().getLeader()->effectFeats() : std::set<FeatType>();
    _displayEntries.clear();
    _selectedFeats.clear();
    _availableFeats.clear();
    _controls.LB_DESC->clearItems();
    resetFocusedFeatName();

    auto &strings = _services.resource.strings;
    _controls.MAIN_TITLE_LBL->setTextMessage(levelUp ? strings.getText(kLevelUpTitleStrRef) : _defaultMainTitleText);
    _controls.SUB_TITLE_LBL->setTextMessage(bonus ? strings.getText(kBonusFeatsTitleStrRef) : _defaultSubTitleText);

    loadDisplayEntries();
    buildAvailableFeats();

    refreshControls();
    _controls.BTN_SELECT->setDisabled(true);
    _controls.BTN_RECOMMENDED->setDisabled(true);
}

CreatureAttributes CharGenFeats::choiceAttributes() const {
    return _levelUp ? _charGen.levelledAttributes() : _charGen.character().attributes;
}

void CharGenFeats::loadDisplayEntries() {
    CreatureAttributes attributes(choiceAttributes());
    const auto &[clazz, classLevel] = attributes.classLevels().back();

    // The level's feats: the screen offers its normal feats, or its bonus
    // feats.
    _normalFeats = clazz->getFeatGain(classLevel);
    _bonusFeats = clazz->getBonusFeatGain(classLevel);
    _points = _bonus ? _bonusFeats : _normalFeats;
    _remaining = _points;
    _displayEntries = _services.game.feats.getLevelUpDisplayEntries(attributes, *clazz);
}

void CharGenFeats::buildAvailableFeats() {
    CreatureAttributes attributes(choiceAttributes());
    const CreatureClass &clazz = *attributes.classLevels().back().first;
    // A feat can be chosen while there is a pick of its kind, counting one
    // more than the level gains.
    FeatChoice choice {
        attributes,
        clazz,
        _normalFeats > 0 ? _normalFeats + 1 : 0,
        _bonusFeats > 0 ? _bonusFeats + 1 : 0,
        _effectFeats,
        _selectedFeats,
        _levelUp ? _game.party().getLeader()->tag() : kObjectTagPlayer,
        _game.isTSL()};
    for (auto feat : _services.game.feats.getChoosableFeats(choice)) {
        _availableFeats.insert(feat);
    }
}

CharGenFeats::FeatStatus CharGenFeats::getFeatStatus(FeatType feat) const {
    if (_availableFeats.count(feat) > 0) {
        return FeatStatus::Available;
    }
    if (_selectedFeats.count(feat) > 0) {
        return FeatStatus::Chosen;
    }
    if (_originalFeats.count(feat) > 0) {
        return FeatStatus::Owned;
    }
    if (_grantedFeats.count(feat) > 0) {
        return FeatStatus::Granted;
    }
    return FeatStatus::Unavailable;
}

void CharGenFeats::pickFeat(FeatType feat) {
    switch (getFeatStatus(feat)) {
    case FeatStatus::Available:
        if (_remaining == 0) {
            showMessage(kNoPicksLeftStrRef);
            return;
        }
        addChosenFeat(feat);
        break;
    case FeatStatus::Chosen:
        removeChosenFeat(feat);
        break;
    case FeatStatus::Granted:
        showMessage(kGrantedFeatStrRef);
        return;
    case FeatStatus::Owned:
        showMessage(kOwnedFeatStrRef);
        return;
    case FeatStatus::Unavailable:
        showMessage(kUnavailableFeatStrRef);
        return;
    }
    refreshSelectionControls();
    refreshIconChainStates();
}

void CharGenFeats::addChosenFeat(FeatType feat) {
    _availableFeats.erase(feat);
    _selectedFeats.insert(feat);
    --_remaining;
    buildAvailableFeats();
}

void CharGenFeats::removeChosenFeat(FeatType feat) {
    CreatureAttributes attributes(choiceAttributes());
    _selectedFeats.erase(feat);
    ++_remaining;
    for (auto it = _selectedFeats.begin(); it != _selectedFeats.end();) {
        if (_services.game.feats.hasPrerequisites(*it, attributes, _effectFeats, _selectedFeats)) {
            ++it;
            continue;
        }
        _selectedFeats.erase(it);
        ++_remaining;
        it = _selectedFeats.begin();
    }
    _availableFeats.clear();
    buildAvailableFeats();
}

void CharGenFeats::showMessage(int strRef) {
    _game.showMessagePopup(_services.resource.strings.getText(strRef));
}

void CharGenFeats::accept() {
    if (_remaining > 0 && !_availableFeats.empty()) {
        showMessage(_game.isTSL() ? kTSLFeatsLeftStrRef : kK1FeatsLeftStrRef);
        return;
    }
    updateCharacter();
    if (!_bonus && _bonusFeats > 0) {
        _charGen.openBonusFeats();
        return;
    }
    _charGen.goToNextStep();
    _charGen.openSteps();
}

void CharGenFeats::refreshControls() {
    refreshSelectionControls();
    refreshIconChain();
    refreshListBox();
}

void CharGenFeats::refreshSelectionControls() {
    _controls.STD_REMAINING_SELECTIONS_LBL->setTextMessage(std::to_string(_remaining));
}

void CharGenFeats::refreshIconChain() {
    _controls.ICONCHAIN_FEATS->clearItems();
    _controls.ICONCHAIN_FEATS->setVisible(true);

    _controls.ICONCHAIN_FEATS->setColumnCount(kIconSelectionColumnCount);

    auto itemPositions = getFeatIconPositions(_displayEntries);

    for (auto &entry : _displayEntries) {
        std::shared_ptr<Feat> feat(_services.game.feats.get(entry.type));
        if (!feat) {
            continue;
        }

        IconChain::Item item;
        item.tag = std::to_string(static_cast<int>(entry.type));
        item.column = itemPositions.at(entry.type).x;
        item.row = itemPositions.at(entry.type).y;
        item.iconTexture = feat->icon;
        item.selected = _selectedFeats.count(entry.type) > 0;
        item.state = getIconState(getFeatStatus(entry.type));
        _controls.ICONCHAIN_FEATS->addItem(std::move(item));
    }

    refreshIconChainLinks();
}

void CharGenFeats::refreshIconChainStates() {
    for (auto &entry : _displayEntries) {
        std::string tag(std::to_string(static_cast<int>(entry.type)));
        _controls.ICONCHAIN_FEATS->setItemState(tag, getIconState(getFeatStatus(entry.type)));
        _controls.ICONCHAIN_FEATS->setItemSelected(tag, _selectedFeats.count(entry.type) > 0);
    }

    refreshIconChainLinks();
}

void CharGenFeats::refreshIconChainLinks() {
    _controls.ICONCHAIN_FEATS->clearLinks();
    if (_points == 0) {
        return;
    }

    auto isHeldOrChosen = [this](FeatType feat) {
        return _charGen.character().attributes.hasFeat(feat) || _selectedFeats.count(feat) > 0;
    };
    auto itemPositions = getFeatIconPositions(_displayEntries);

    for (size_t i = 1; i < _displayEntries.size(); ++i) {
        const FeatDisplayEntry &source = _displayEntries[i - 1];
        const FeatDisplayEntry &target = _displayEntries[i];
        FeatType sourceChainRoot = source.chainRoot != FeatType::Invalid ? source.chainRoot : source.type;
        FeatType targetChainRoot = target.chainRoot != FeatType::Invalid ? target.chainRoot : target.type;
        if (sourceChainRoot != targetChainRoot ||
            target.visualIndex != source.visualIndex + 1) {
            continue;
        }

        auto sourcePosition = itemPositions.find(source.type);
        auto targetPosition = itemPositions.find(target.type);
        if (sourcePosition == itemPositions.end() ||
            targetPosition == itemPositions.end() ||
            sourcePosition->second.y != targetPosition->second.y ||
            targetPosition->second.x != sourcePosition->second.x + 1) {
            continue;
        }

        bool sourceEffectivelyOwned = isHeldOrChosen(source.type);
        bool targetEffectivelyOwned = isHeldOrChosen(target.type);
        bool targetSelectable = _availableFeats.count(target.type) > 0;
        if (!sourceEffectivelyOwned || (!targetEffectivelyOwned && !targetSelectable)) {
            continue;
        }

        IconChain::Link link;
        link.sourceTag = std::to_string(static_cast<int>(source.type));
        link.targetTag = std::to_string(static_cast<int>(target.type));
        _controls.ICONCHAIN_FEATS->addLink(std::move(link));
    }
}

void CharGenFeats::refreshListBox() {
    _controls.LB_FEATS->setVisible(false);
    _controls.LB_FEATS->clearItems();
    for (auto &entry : _displayEntries) {
        std::shared_ptr<Feat> feat(_services.game.feats.get(entry.type));
        if (!feat) {
            continue;
        }

        std::string availabilityText;
        switch (getFeatStatus(entry.type)) {
        case FeatStatus::Owned:
        case FeatStatus::Granted:
            availabilityText = "[OWNED] ";
            break;
        case FeatStatus::Available:
        case FeatStatus::Chosen:
            availabilityText = "[CAN PICK] ";
            break;
        case FeatStatus::Unavailable:
            availabilityText = "[LOCKED] ";
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
    _controls.BTN_SELECT->setDisabled(false);
}

void CharGenFeats::onFeatActivated(const std::string &feat) {
    auto featType = static_cast<FeatType>(std::stoi(feat));
    showFeatDescription(featType);
    pickFeat(featType);
}

void CharGenFeats::selectFirstEntryForCapture() {
    for (auto &entry : _displayEntries) {
        if (getFeatStatus(entry.type) == FeatStatus::Available) {
            onFeatActivated(std::to_string(static_cast<int>(entry.type)));
            return;
        }
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
