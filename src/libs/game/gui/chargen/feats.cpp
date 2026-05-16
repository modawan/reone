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
#include "reone/gui/control/listbox.h"

using namespace reone::audio;

using namespace reone::gui;
using namespace reone::graphics;
using namespace reone::resource;

namespace reone {

namespace game {

void CharGenFeats::onGUILoaded() {
    bindControls();

    _controls.LB_DESC->setProtoMatchContent(true);
    _controls.LB_FEATS->setSelectionMode(ListBox::SelectionMode::OnClick);
    _controls.LB_FEATS->setRenderItemIconsForButtonProto(true);
    _controls.LB_FEATS->setOnItemClick([this](const std::string &item) {
        onFeatSelected(item);
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
    _controls.BTN_SELECT->setDisabled(true);
    _controls.BTN_RECOMMENDED->setDisabled(true);
}

void CharGenFeats::reset(bool levelUp) {
    _levelUp = levelUp;
    _points = 0;
    _displayEntries.clear();
    _selectedFeats.clear();
    _controls.LB_DESC->clearItems();

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
    _controls.STD_REMAINING_SELECTIONS_LBL->setTextMessage(std::to_string(_points - static_cast<int>(_selectedFeats.size())));
    _controls.BTN_ACCEPT->setDisabled(_levelUp && _selectedFeats.size() != static_cast<size_t>(_points));

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
    refreshControls();
}

void CharGenFeats::onFeatSelected(const std::string &feat) {
    auto featType = static_cast<FeatType>(std::stoi(feat));
    std::shared_ptr<Feat> featInfo(_services.game.feats.get(featType));
    if (!featInfo) {
        return;
    }

    _controls.LB_DESC->clearItems();
    _controls.LB_DESC->addTextLinesAsItems(featInfo->description);

    auto maybeDisplayEntry = std::find_if(
        _displayEntries.begin(), _displayEntries.end(),
        [&featType](auto &entry) { return entry.type == featType; });
    if (_levelUp &&
        maybeDisplayEntry != _displayEntries.end() &&
        maybeDisplayEntry->availability == FeatAvailability::Selectable) {
        toggleSelectedFeat(featType);
    }
}

} // namespace game

} // namespace reone
