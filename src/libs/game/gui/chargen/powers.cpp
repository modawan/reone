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

#include "reone/game/gui/chargen/powers.h"

#include "reone/game/gui/chargen/iconselection.h"

#include "reone/game/d20/classes.h"
#include "reone/game/game.h"
#include "reone/game/gui/chargen.h"
#include "reone/gui/control/button.h"
#include "reone/gui/control/iconchain.h"
#include "reone/gui/control/listbox.h"
#include "reone/resource/provider/textures.h"

#include <algorithm>
#include <cmath>

using namespace reone::gui;
using namespace reone::graphics;
using namespace reone::resource;

namespace reone {

namespace game {

static IconChain::State getPowerIconState(SpellAvailability availability) {
    switch (availability) {
    case SpellAvailability::Known:
        return IconChain::State::Owned;
    case SpellAvailability::Chosen:
    case SpellAvailability::Selectable:
        return IconChain::State::Selectable;
    case SpellAvailability::Hidden:
    case SpellAvailability::LockedClassLevel:
    case SpellAvailability::LockedMissingPrerequisite:
        return IconChain::State::Locked;
    }
    return IconChain::State::Locked;
}

static std::map<SpellType, glm::ivec2> getPowerIconPositions(
    const std::vector<SpellDisplayEntry> &entries) {

    std::map<SpellType, int> nextIndexByRoot;
    std::vector<SpellType> chainRoots;
    std::map<SpellType, glm::ivec2> result;
    for (const auto &entry : entries) {
        if (!entry.visible) {
            continue;
        }

        SpellType root = entry.chainRoot != SpellType::All ? entry.chainRoot : entry.type;
        auto maybeIndex = nextIndexByRoot.insert({root, 0});
        if (maybeIndex.second) {
            chainRoots.push_back(root);
        }
        int index = maybeIndex.first->second++;
        result.emplace(entry.type, glm::ivec2(index % kIconSelectionColumnCount, index / kIconSelectionColumnCount));
    }

    std::map<SpellType, int> rowBases;
    int nextRow = 0;
    for (auto root : chainRoots) {
        int count = nextIndexByRoot.at(root);
        rowBases.emplace(root, nextRow);
        nextRow += (count + kIconSelectionColumnCount - 1) / kIconSelectionColumnCount;
    }

    for (const auto &entry : entries) {
        if (!entry.visible) {
            continue;
        }
        SpellType root = entry.chainRoot != SpellType::All ? entry.chainRoot : entry.type;
        result.at(entry.type).y += rowBases.at(root);
    }
    return result;
}

void CharGenPowers::onGUILoaded() {
    bindControls();
    _defaultPowerNameText = _controls.LBL_POWER->text().text;

    styleChargenTitles(_game, *_controls.SELECTIONS_REMAINING_LBL, *_controls.LB_DESC);

    IconSelectionCallbacks callbacks;
    callbacks.onItemFocus = [this](const std::string &item) {
        onPowerFocused(item);
    };
    callbacks.onItemFocusCleared = [this]() {
        resetFocusedPowerName();
    };
    callbacks.onItemDoubleClick = [this](const std::string &item) {
        onPowerActivated(item);
    };
    _controls.ICONCHAIN_POWERS = addIconSelectionChain(
        *_gui,
        _game,
        _services,
        "ICONCHAIN_POWERS",
        *_controls.LB_POWERS,
        _hilightColor,
        std::move(callbacks));

    _controls.LB_DESC->setProtoMatchContent(true);
    _controls.LB_POWERS->setVisible(false);
    _controls.SELECT_BTN->setOnClick([this]() {
        activateFocusedPower();
    });
    _controls.SELECT_BTN->setVisible(true);
    _controls.RECOMMENDED_BTN->setDisabled(true);
    _controls.RECOMMENDED_BTN->setVisible(false);
    _controls.ACCEPT_BTN->setOnClick([this]() {
        if (_selectedPowers.size() != static_cast<size_t>(_powerGain)) {
            return;
        }
        updateCharacter();
        _charGen.goToNextStep();
        _charGen.openSteps();
    });
    _controls.BACK_BTN->setOnClick([this]() {
        _charGen.openSteps();
    });
}

void CharGenPowers::reset() {
    _powerGain = 0;
    _displayEntries.clear();
    _selectedPowers.clear();
    _controls.LB_DESC->clearItems();
    _controls.SELECT_BTN->setDisabled(true);
    resetFocusedPowerName();
    loadDisplayEntries();
    refreshControls();
}

void CharGenPowers::loadDisplayEntries() {
    const CreatureAttributes &attributes = _charGen.character().attributes;
    std::shared_ptr<CreatureClass> clazz(_services.game.classes.get(attributes.getEffectiveClass()));
    if (!clazz) {
        _powerGain = 0;
        return;
    }

    int targetClassLevel = attributes.getClassLevel(clazz->type()) + 1;
    _powerGain = clazz->getPowerGain(targetClassLevel);
    _displayEntries = _services.game.spells.getLevelUpDisplayEntries(attributes, *clazz, _selectedPowers);
}

void CharGenPowers::refreshControls() {
    refreshSelectionControls();
    refreshIconChain();
}

void CharGenPowers::refreshSelectionControls() {
    int remaining = _powerGain - static_cast<int>(_selectedPowers.size());
    _controls.REMAINING_SELECTIONS_LBL->setTextMessage(std::to_string(remaining));
    _controls.ACCEPT_BTN->setDisabled(remaining != 0);
}

void CharGenPowers::refreshIconChain() {
    _controls.ICONCHAIN_POWERS->clearItems();
    _controls.ICONCHAIN_POWERS->setColumnCount(kIconSelectionColumnCount);

    auto positions = getPowerIconPositions(_displayEntries);
    std::string firstVisible;
    for (const auto &entry : _displayEntries) {
        if (!entry.visible) {
            continue;
        }

        IconChain::Item item;
        item.tag = std::to_string(static_cast<int>(entry.type));
        item.column = positions.at(entry.type).x;
        item.row = positions.at(entry.type).y;
        item.iconTexture = entry.icon;
        item.selected = entry.chosen;
        item.state = getPowerIconState(entry.availability);
        if (firstVisible.empty()) {
            firstVisible = item.tag;
        }
        _controls.ICONCHAIN_POWERS->addItem(std::move(item));
    }

    refreshIconChainLinks(positions);
    _controls.ICONCHAIN_POWERS->setVisible(true);
    if (!firstVisible.empty()) {
        _controls.ICONCHAIN_POWERS->focusItem(firstVisible);
    }
}

void CharGenPowers::refreshIconChainSelection() {
    for (const auto &entry : _displayEntries) {
        if (!entry.visible) {
            continue;
        }

        std::string tag = std::to_string(static_cast<int>(entry.type));
        _controls.ICONCHAIN_POWERS->setItemSelected(tag, entry.chosen);
        _controls.ICONCHAIN_POWERS->setItemState(tag, getPowerIconState(entry.availability));
    }

    refreshIconChainLinks(getPowerIconPositions(_displayEntries));
}

void CharGenPowers::refreshIconChainLinks(const std::map<SpellType, glm::ivec2> &positions) {
    _controls.ICONCHAIN_POWERS->clearLinks();
    for (const auto &entry : _displayEntries) {
        if (!entry.visible || !entry.displayParent) {
            continue;
        }

        auto sourceEntry = std::find_if(
            _displayEntries.begin(), _displayEntries.end(),
            [&entry](const SpellDisplayEntry &candidate) { return candidate.type == *entry.displayParent; });
        bool sourceEffectivelyKnown = sourceEntry != _displayEntries.end() &&
                                      (sourceEntry->availability == SpellAvailability::Known ||
                                       sourceEntry->availability == SpellAvailability::Chosen);
        bool targetReachable = entry.availability == SpellAvailability::Known ||
                               entry.availability == SpellAvailability::Chosen ||
                               entry.availability == SpellAvailability::Selectable;
        if (!sourceEffectivelyKnown || !targetReachable) {
            continue;
        }

        auto source = positions.find(*entry.displayParent);
        auto target = positions.find(entry.type);
        if (source == positions.end() ||
            target == positions.end() ||
            source->second.y != target->second.y ||
            target->second.x != source->second.x + 1) {
            continue;
        }

        IconChain::Link link;
        link.sourceTag = std::to_string(static_cast<int>(*entry.displayParent));
        link.targetTag = std::to_string(static_cast<int>(entry.type));
        _controls.ICONCHAIN_POWERS->addLink(std::move(link));
    }
}

void CharGenPowers::updateCharacter() {
    Character character(_charGen.character());
    for (auto power : _selectedPowers) {
        character.attributes.addSpell(power);
    }
    _charGen.setCharacter(std::move(character));
}

void CharGenPowers::toggleSelectedPower(SpellType power) {
    auto maybeSelectedPower = _selectedPowers.find(power);
    if (maybeSelectedPower != _selectedPowers.end()) {
        _selectedPowers.erase(maybeSelectedPower);
        removeInvalidSelectedPowers();
    } else if (_selectedPowers.size() < static_cast<size_t>(_powerGain)) {
        _selectedPowers.insert(power);
    }

    std::string focusedPower = std::to_string(static_cast<int>(power));
    loadDisplayEntries();
    refreshSelectionControls();
    refreshIconChainSelection();
    onPowerFocused(focusedPower);
}

void CharGenPowers::removeInvalidSelectedPowers() {
    const CreatureAttributes &attributes = _charGen.character().attributes;
    std::shared_ptr<CreatureClass> clazz(_services.game.classes.get(attributes.getEffectiveClass()));
    if (!clazz) {
        _selectedPowers.clear();
        return;
    }

    bool removed;
    do {
        removed = false;
        for (auto power : _selectedPowers) {
            std::set<SpellType> prerequisiteChoices(_selectedPowers);
            prerequisiteChoices.erase(power);
            if (!_services.game.spells.isLevelUpCandidate(
                    power, attributes, *clazz, prerequisiteChoices)) {
                _selectedPowers.erase(power);
                removed = true;
                break;
            }
        }
    } while (removed);
}

void CharGenPowers::showPowerDescription(SpellType type) {
    auto maybeEntry = std::find_if(
        _displayEntries.begin(), _displayEntries.end(),
        [&type](const SpellDisplayEntry &entry) { return entry.type == type; });
    if (maybeEntry == _displayEntries.end()) {
        return;
    }

    _controls.LBL_POWER->setTextMessage(maybeEntry->name);
    _controls.LB_DESC->clearItems();
    _controls.LB_DESC->addTextLinesAsItems(maybeEntry->description);
}

void CharGenPowers::resetFocusedPowerName() {
    _controls.LBL_POWER->setTextMessage(_defaultPowerNameText);
}

void CharGenPowers::onPowerFocused(const std::string &power) {
    auto powerType = static_cast<SpellType>(std::stoi(power));
    showPowerDescription(powerType);

    auto maybeDisplayEntry = std::find_if(
        _displayEntries.begin(), _displayEntries.end(),
        [&powerType](const SpellDisplayEntry &entry) { return entry.type == powerType; });
    bool canActivate = maybeDisplayEntry != _displayEntries.end() &&
                       (maybeDisplayEntry->chosen ||
                        (maybeDisplayEntry->selectable &&
                         _selectedPowers.size() < static_cast<size_t>(_powerGain)));
    _controls.SELECT_BTN->setDisabled(!canActivate);
}

void CharGenPowers::onPowerActivated(const std::string &power) {
    auto powerType = static_cast<SpellType>(std::stoi(power));
    showPowerDescription(powerType);

    auto maybeDisplayEntry = std::find_if(
        _displayEntries.begin(), _displayEntries.end(),
        [&powerType](const SpellDisplayEntry &entry) { return entry.type == powerType; });
    if (maybeDisplayEntry == _displayEntries.end()) {
        return;
    }
    if (maybeDisplayEntry->chosen ||
        (maybeDisplayEntry->selectable &&
         _selectedPowers.size() < static_cast<size_t>(_powerGain))) {
        toggleSelectedPower(powerType);
    }
}

void CharGenPowers::selectFirstEntryForCapture() {
    for (const auto &entry : _displayEntries) {
        if (entry.visible && entry.selectable && !entry.chosen) {
            onPowerActivated(std::to_string(static_cast<int>(entry.type)));
            return;
        }
    }
}

void CharGenPowers::activateFocusedPower() {
    const IconChain::Item *item = _controls.ICONCHAIN_POWERS->focusedItem();
    if (!item) {
        return;
    }
    onPowerActivated(item->tag);
}

} // namespace game

} // namespace reone
