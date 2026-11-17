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

#include "reone/game/gui/chargen/abilities.h"

#include "reone/game/gui/chargen/iconselection.h"

#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"
#include "reone/gui/control/listbox.h"
#include "reone/resource/strings.h"

#include "reone/game/d20/classes.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/gui/chargen.h"

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

static constexpr int kStartingPoints = 30;
static constexpr int kMinAbilityScore = 8;
// The highest score a new character can buy. A level-up's point raises any
// score.
static constexpr int kMaxNewCharacterAbilityScore = 18;

static const std::unordered_map<std::string, Ability> g_abilityByAlias {
    {"STR", Ability::Strength},
    {"DEX", Ability::Dexterity},
    {"CON", Ability::Constitution},
    {"INT", Ability::Intelligence},
    {"WIS", Ability::Wisdom},
    {"CHA", Ability::Charisma}};

static const std::unordered_map<Ability, int> g_descStrRefByAbility {
    {Ability::Strength, 222},
    {Ability::Dexterity, 223},
    {Ability::Constitution, 224},
    {Ability::Intelligence, 226},
    {Ability::Wisdom, 225},
    {Ability::Charisma, 227}};

/** The controls the panel shows in both titles. */
static const std::unordered_set<std::string> kPanelControls {
    "BTN_ACCEPT", "BTN_BACK", "BTN_RECOMMENDED", "STR_LBL", "STR_MINUS_BTN", "STR_PLUS_BTN",
    "STR_POINTS_BTN", "DEX_LBL", "DEX_MINUS_BTN", "DEX_PLUS_BTN", "DEX_POINTS_BTN", "CON_LBL",
    "CON_MINUS_BTN", "CON_PLUS_BTN", "CON_POINTS_BTN", "INT_LBL", "INT_MINUS_BTN", "INT_PLUS_BTN",
    "INT_POINTS_BTN", "WIS_LBL", "WIS_MINUS_BTN", "WIS_PLUS_BTN", "WIS_POINTS_BTN", "CHA_LBL",
    "CHA_MINUS_BTN", "CHA_PLUS_BTN", "CHA_POINTS_BTN", "COST_LBL", "COST_POINTS_LBL", "LB_DESC",
    "MAIN_TITLE_LBL", "REMAINING_BOX_LBL", "REMAINING_SELECTIONS_LBL", "SELECTIONS_REMAINING_LBL", "SUB_TITLE_LBL"};
/** The controls the panel also shows in KotOR. */
static const std::unordered_set<std::string> kK1PanelControls {"DESC_LBL", "LBL_ABILITY_MOD", "LBL_MODIFIER"};
/** The controls the panel also shows in TSL. */
static const std::unordered_set<std::string> kTSLPanelControls {
    "LBL_BAR1", "LBL_BAR2", "LBL_BAR3", "LBL_BAR4", "LBL_BONUS_STR", "LBL_BONUS_DEX",
    "LBL_BONUS_CON", "LBL_BONUS_INT", "LBL_BONUS_WIS", "LBL_BONUS_CHA", "LBL_PORTRAIT"};

void CharGenAbilities::onGUILoaded() {
    bindControls();
    showOnlyNamedControls(kPanelControls, _game.isTSL() ? kTSLPanelControls : kK1PanelControls);

    styleChargenTitles(_game, *_controls.SELECTIONS_REMAINING_LBL, *_controls.LB_DESC);
    _controls.LB_DESC->setProtoMatchContent(true);

    std::vector<Label *> labels {
        _controls.STR_LBL.get(),
        _controls.DEX_LBL.get(),
        _controls.CON_LBL.get(),
        _controls.INT_LBL.get(),
        _controls.WIS_LBL.get(),
        _controls.CHA_LBL.get()};
    for (auto &label : labels) {
        label->setSelectable(true);
        label->setHilightColor(_baseColor);
    }

    _controls.STR_POINTS_BTN->setDisabled(true);
    _controls.DEX_POINTS_BTN->setDisabled(true);
    _controls.CON_POINTS_BTN->setDisabled(true);
    _controls.INT_POINTS_BTN->setDisabled(true);
    _controls.WIS_POINTS_BTN->setDisabled(true);
    _controls.CHA_POINTS_BTN->setDisabled(true);

    _controls.BTN_ACCEPT->setOnClick([this]() {
        accept();
    });
    _controls.BTN_BACK->setOnClick([this]() {
        _charGen.openSteps();
    });
    _controls.BTN_RECOMMENDED->setOnClick([this]() {
        recommend();
    });

    _controls.STR_LBL->setOnSelectionChanged([this](bool selected) {
        onAbilityLabelSelectionChanged(Ability::Strength, *_controls.STR_LBL, *_controls.STR_POINTS_BTN, selected);
    });
    _controls.DEX_LBL->setOnSelectionChanged([this](bool selected) {
        onAbilityLabelSelectionChanged(Ability::Dexterity, *_controls.DEX_LBL, *_controls.DEX_POINTS_BTN, selected);
    });
    _controls.CON_LBL->setOnSelectionChanged([this](bool selected) {
        onAbilityLabelSelectionChanged(Ability::Constitution, *_controls.CON_LBL, *_controls.CON_POINTS_BTN, selected);
    });
    _controls.INT_LBL->setOnSelectionChanged([this](bool selected) {
        onAbilityLabelSelectionChanged(Ability::Intelligence, *_controls.INT_LBL, *_controls.INT_POINTS_BTN, selected);
    });
    _controls.WIS_LBL->setOnSelectionChanged([this](bool selected) {
        onAbilityLabelSelectionChanged(Ability::Wisdom, *_controls.WIS_LBL, *_controls.WIS_POINTS_BTN, selected);
    });
    _controls.CHA_LBL->setOnSelectionChanged([this](bool selected) {
        onAbilityLabelSelectionChanged(Ability::Charisma, *_controls.CHA_LBL, *_controls.CHA_POINTS_BTN, selected);
    });

    _controls.STR_MINUS_BTN->setOnClick([this]() {
        onMinusButtonClick(Ability::Strength);
    });
    _controls.DEX_MINUS_BTN->setOnClick([this]() {
        onMinusButtonClick(Ability::Dexterity);
    });
    _controls.CON_MINUS_BTN->setOnClick([this]() {
        onMinusButtonClick(Ability::Constitution);
    });
    _controls.INT_MINUS_BTN->setOnClick([this]() {
        onMinusButtonClick(Ability::Intelligence);
    });
    _controls.WIS_MINUS_BTN->setOnClick([this]() {
        onMinusButtonClick(Ability::Wisdom);
    });
    _controls.CHA_MINUS_BTN->setOnClick([this]() {
        onMinusButtonClick(Ability::Charisma);
    });

    _controls.STR_PLUS_BTN->setOnClick([this]() {
        onPlusButtonClick(Ability::Strength);
    });
    _controls.DEX_PLUS_BTN->setOnClick([this]() {
        onPlusButtonClick(Ability::Dexterity);
    });
    _controls.CON_PLUS_BTN->setOnClick([this]() {
        onPlusButtonClick(Ability::Constitution);
    });
    _controls.INT_PLUS_BTN->setOnClick([this]() {
        onPlusButtonClick(Ability::Intelligence);
    });
    _controls.WIS_PLUS_BTN->setOnClick([this]() {
        onPlusButtonClick(Ability::Wisdom);
    });
    _controls.CHA_PLUS_BTN->setOnClick([this]() {
        onPlusButtonClick(Ability::Charisma);
    });
}

void CharGenAbilities::reset(bool newGame) {
    _newGame = newGame;
    _attributes = _charGen.character().attributes;
    _points = newGame ? kStartingPoints : 1;

    if (newGame) {
        _attributes.setAbilityScore(Ability::Strength, kMinAbilityScore);
        _attributes.setAbilityScore(Ability::Dexterity, kMinAbilityScore);
        _attributes.setAbilityScore(Ability::Constitution, kMinAbilityScore);
        _attributes.setAbilityScore(Ability::Intelligence, kMinAbilityScore);
        _attributes.setAbilityScore(Ability::Wisdom, kMinAbilityScore);
        _attributes.setAbilityScore(Ability::Charisma, kMinAbilityScore);
    }
    _startAttributes = _attributes;
    _startPoints = _points;

    refreshControls();
}

void CharGenAbilities::recommend() {
    ClassType classType = _charGen.character().attributes.getEffectiveClass();
    std::shared_ptr<CreatureClass> clazz(_services.game.classes.get(classType));
    if (_newGame) {
        _attributes = clazz->defaultAttributes();
        _points = 0;
    } else {
        // A level-up's point goes to the class's primary ability, from the
        // scores the screen opened with, at the plus button's cost.
        _attributes = _startAttributes;
        _points = _startPoints;
        if (const auto primary = clazz->primaryAbility()) {
            while (_points >= getPointCost(*primary)) {
                _points -= getPointCost(*primary);
                _attributes.setAbilityScore(*primary, _attributes.getAbilityScore(*primary) + 1);
            }
        }
    }
    refreshControls();
}

void CharGenAbilities::accept() {
    if (_points > 0)
        return;
    _charGen.acceptAbilities();
    updateCharacter();
    _charGen.goToNextStep();
    _charGen.openSteps();
}

void CharGenAbilities::refreshControls() {
    _controls.REMAINING_SELECTIONS_LBL->setTextMessage(std::to_string(_points));
    _controls.COST_POINTS_LBL->setTextMessage("");
    if (!_game.isTSL()) {
        _controls.LBL_ABILITY_MOD->setTextMessage("");
    }

    _controls.STR_POINTS_BTN->setTextMessage(std::to_string(_attributes.strength()));
    _controls.DEX_POINTS_BTN->setTextMessage(std::to_string(_attributes.dexterity()));
    _controls.CON_POINTS_BTN->setTextMessage(std::to_string(_attributes.constitution()));
    _controls.INT_POINTS_BTN->setTextMessage(std::to_string(_attributes.intelligence()));
    _controls.WIS_POINTS_BTN->setTextMessage(std::to_string(_attributes.wisdom()));
    _controls.CHA_POINTS_BTN->setTextMessage(std::to_string(_attributes.charisma()));

    // Minus takes back only what this screen placed: a new character's
    // scores start at the minimum, a level-up's at the character's own, so a
    // level-up cannot trade one score for another.
    _controls.STR_MINUS_BTN->setVisible(_attributes.strength() > _startAttributes.strength());
    _controls.DEX_MINUS_BTN->setVisible(_attributes.dexterity() > _startAttributes.dexterity());
    _controls.CON_MINUS_BTN->setVisible(_attributes.constitution() > _startAttributes.constitution());
    _controls.INT_MINUS_BTN->setVisible(_attributes.intelligence() > _startAttributes.intelligence());
    _controls.WIS_MINUS_BTN->setVisible(_attributes.wisdom() > _startAttributes.wisdom());
    _controls.CHA_MINUS_BTN->setVisible(_attributes.charisma() > _startAttributes.charisma());

    // Plus raises a score the points can pay for; only a new character's
    // scores stop at a highest one.
    const auto canRaise = [this](Ability ability) {
        return _points >= getPointCost(ability) &&
               (!_newGame || _attributes.getAbilityScore(ability) < kMaxNewCharacterAbilityScore);
    };
    _controls.STR_PLUS_BTN->setVisible(canRaise(Ability::Strength));
    _controls.DEX_PLUS_BTN->setVisible(canRaise(Ability::Dexterity));
    _controls.CON_PLUS_BTN->setVisible(canRaise(Ability::Constitution));
    _controls.INT_PLUS_BTN->setVisible(canRaise(Ability::Intelligence));
    _controls.WIS_PLUS_BTN->setVisible(canRaise(Ability::Wisdom));
    _controls.CHA_PLUS_BTN->setVisible(canRaise(Ability::Charisma));
}

int CharGenAbilities::getPointCost(Ability ability) const {
    // A new character buys scores by the ability's modifier; a level-up's
    // point raises any score by one.
    return _newGame ? _attributes.getAbilityPointCost(ability) : 1;
}

static Ability getAbilityByAlias(const std::string &alias) {
    return g_abilityByAlias.find(alias)->second;
}

void CharGenAbilities::updateCharacter() {
    Character character(_charGen.character());
    for (auto &abilityScore : _attributes.abilityScores()) {
        character.attributes.setAbilityScore(abilityScore.first, abilityScore.second);
    }
    _charGen.setCharacter(std::move(character));
}

void CharGenAbilities::onAbilityLabelSelectionChanged(Ability ability, Label &label, Button &points, bool selected) {
    // The hovered row's name and points pulse.
    label.setTextPulsing(selected);
    points.setTextPulsing(selected);
    if (!selected)
        return;

    auto maybeDescription = g_descStrRefByAbility.find(ability);
    if (maybeDescription == g_descStrRefByAbility.end())
        return;

    std::string description(_services.resource.strings.getText(maybeDescription->second));
    _controls.LB_DESC->clearItems();
    _controls.LB_DESC->addTextLinesAsItems(description);
}

void CharGenAbilities::onMinusButtonClick(Ability ability) {
    _attributes.setAbilityScore(ability, _attributes.getAbilityScore(ability) - 1);
    _points += getPointCost(ability);
    refreshControls();
}

void CharGenAbilities::onPlusButtonClick(Ability ability) {
    _points -= getPointCost(ability);
    _attributes.setAbilityScore(ability, _attributes.getAbilityScore(ability) + 1);
    refreshControls();
}

} // namespace game

} // namespace reone
