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

#include "reone/game/gui/ingame/abilities.h"

#include "reone/game/d20/class.h"
#include "reone/game/d20/feat.h"
#include "reone/game/d20/feats.h"
#include "reone/game/d20/skill.h"
#include "reone/game/d20/skills.h"
#include "reone/game/d20/spell.h"
#include "reone/game/d20/spells.h"
#include "reone/game/gui/chargen/iconselection.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/party.h"
#include "reone/gui/control/button.h"
#include "reone/gui/control/iconchain.h"
#include "reone/gui/control/imagebutton.h"
#include "reone/gui/control/label.h"
#include "reone/gui/control/listbox.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/strings.h"

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

static constexpr int kStrRefSkillRank = 1579;
static constexpr int kStrRefBonus = 32129;
static constexpr int kStrRefTotalRank = 41904;
static constexpr int kStrRefBaseCost = 37655;
static constexpr int kStrRefCostPerUse = 41905;
static constexpr int kStrRefAdjustment = 47888;
static constexpr int kStrRefDarkSide = 41890;
static constexpr int kStrRefLightSide = 41891;

// The tutorials each view asks for as it opens, in TSL.
static constexpr int kSkillsViewTutorial = 36;
static constexpr int kFeatsViewTutorial = 37;
static constexpr int kPowersViewTutorial = 38;

void AbilitiesMenu::onGUILoaded() {
    loadBackground(BackgroundType::Menu);
    bindControls();
    turnK1PartyArrowRight();

    _controls.LBL_RANKVAL->setTextMessage("");
    _controls.LBL_BONUSVAL->setTextMessage("");
    _controls.LBL_TOTALVAL->setTextMessage("");
    _controls.LBL_NAME->setTextMessage("");

    // Every row is a skill's icon row.
    auto &protoItem = static_cast<ImageButton &>(_controls.LB_ABILITY->protoItem());
    _controls.LB_DESC->setProtoMatchContent(true);
    if (_game.isTSL()) {
        fillK2SectionStrip(_controls.LBL_BAR1, _controls.LBL_BAR2);
        useK2ShellTitle(_controls.LBL_ABILITIES);
        enableK2ButtonBodyFill(_controls.BTN_EXIT);
        enableK2ButtonBodyFill(protoItem);
        protoItem.setBorderFill("uibit_fill_2wt");
        protoItem.setHilightFill("uibit_fill_2wt");
        _controls.LB_DESC_FEATS->setProtoMatchContent(true);
    }
    useK1ListEntryColors(protoItem);
    // The active row's frames pulse; TSL's icon frame is white.
    protoItem.setHilightPulsing(true, false);
    if (_game.isTSL()) {
        protoItem.setIconFrameHilight(glm::vec3 {1.0f}, true);
    } else {
        protoItem.setIconFrameHilight(std::nullopt, true);
    }
    _controls.LB_ABILITY->setOnItemClick([this](const std::string &item) {
        showSkill(static_cast<SkillType>(stoi(item)));
    });

    // The powers and feats views share one chain of icons over the list.
    IconSelectionCallbacks callbacks;
    callbacks.onItemFocus = [this](const std::string &item) {
        if (_view == View::Feats) {
            showFeat(static_cast<FeatType>(stoi(item)));
        } else if (_view == View::Powers) {
            showPower(static_cast<SpellType>(stoi(item)));
        }
    };
    auto &scrollBar = *_controls.LB_ABILITY->scrollBarOrNull();
    _listScrollBarExtent = scrollBar.extent();
    _chain = addIconSelectionChain(
        *_gui, _game, _services, "ICONCHAIN_ABILITIES", *_controls.LB_ABILITY, _hilightColor, std::move(callbacks));
    _chainScrollBarExtent = scrollBar.extent();
    _chain->setVisible(false);

    _controls.BTN_SKILLS->setOnClick([this]() {
        setView(View::Skills);
    });
    _controls.BTN_POWERS->setOnClick([this]() {
        setView(View::Powers);
    });
    _controls.BTN_FEATS->setOnClick([this]() {
        setView(View::Feats);
    });
    _controls.BTN_EXIT->setOnClick([this]() {
        _game.openInGame();
    });
    if (!_game.isTSL()) {
        // A portrait hands control to its party member.
        _controls.BTN_CHANGE1->setOnClick([this]() { changeCharacter(1); });
        _controls.BTN_CHANGE2->setOnClick([this]() { changeCharacter(2); });
    }

    loadSkills();
}

void AbilitiesMenu::loadSkills() {
    for (int type = static_cast<int>(SkillType::ComputerUse); type <= static_cast<int>(SkillType::TreatInjury); ++type) {
        auto skill = static_cast<SkillType>(type);
        const auto &rules = *_services.game.skills.get(skill);

        SkillInfo skillInfo;
        skillInfo.skill = skill;
        skillInfo.name = rules.name;
        skillInfo.description = rules.description;
        skillInfo.icon = rules.icon;

        _skills.insert(std::make_pair(skill, std::move(skillInfo)));
    }

    _controls.LB_ABILITY->clearItems();
    for (auto &skill : _skills) {
        ListBox::Item item;
        item.tag = std::to_string(static_cast<int>(skill.second.skill));
        item.text = skill.second.name;
        item.iconFrame = itemFrameTexture(1);
        item.iconTexture = skill.second.icon;
        _controls.LB_ABILITY->addItem(std::move(item));
    }
}

// KotOR shows the leader's portrait above the readout.
void AbilitiesMenu::refreshControls() {
    refreshPortraits();
    if (!_game.isTSL()) {
        _controls.LBL_PORTRAIT->setBorderFill(_game.party().getLeader()->portrait());
    }
    setView(_view);
}

// A character without a Jedi class has no powers view; the skills view takes
// its place. The chosen view's button is lit, and the feats view has no value
// readout.
void AbilitiesMenu::setView(View view) {
    auto leader = _game.party().getLeader();
    const bool jedi = leader->attributes().getJediLevel() > 0;
    _controls.BTN_POWERS->setVisible(jedi);
    if (view == View::Powers && !jedi) {
        view = View::Skills;
    }
    _view = view;

    const bool tsl = _game.isTSL();
    const std::pair<std::shared_ptr<Button>, View> buttons[] {
        {_controls.BTN_SKILLS, View::Skills},
        {_controls.BTN_POWERS, View::Powers},
        {_controls.BTN_FEATS, View::Feats}};
    for (const auto &[button, buttonView] : buttons) {
        if (tsl) {
            updateK2FilterButton(button, buttonView == view);
        } else {
            button->setTextColor(buttonView == view ? kK1ColorYellow : kK1ColorBlue);
        }
    }

    const bool readout = view != View::Feats;
    for (auto &label : {_controls.LBL_INFOBG, _controls.LBL_SKILLRANK, _controls.LBL_RANKVAL, _controls.LBL_BONUS,
                        _controls.LBL_BONUSVAL, _controls.LBL_TOTAL, _controls.LBL_TOTALVAL}) {
        label->setVisible(readout);
    }
    // TSL describes feats in a box of their own.
    if (tsl) {
        _controls.LB_DESC->setVisible(view != View::Feats);
        _controls.LB_DESC_FEATS->setVisible(view == View::Feats);
    }
    _controls.LB_ABILITY->setVisible(view == View::Skills);
    _controls.LB_ABILITY->scrollBarOrNull()->setExtent(view == View::Skills ? _listScrollBarExtent : _chainScrollBarExtent);
    _chain->setVisible(view != View::Skills);
    // Only the skills view's list takes the keyboard; the other views hide it.
    _gui->clearActiveControl();
    describe("", "");

    switch (view) {
    case View::Skills:
        showSkillsView();
        break;
    case View::Powers:
        showPowersView(*leader);
        break;
    case View::Feats:
        showFeatsView(*leader);
        break;
    }
    if (tsl) {
        _game.requestTutorialWindow(view == View::Skills   ? kSkillsViewTutorial
                                    : view == View::Feats  ? kFeatsViewTutorial
                                                           : kPowersViewTutorial);
    }
}

void AbilitiesMenu::showSkillsView() {
    auto &strings = _services.resource.strings;
    _controls.LBL_SKILLRANK->setTextMessage(strings.getText(kStrRefSkillRank));
    _controls.LBL_BONUS->setTextMessage(strings.getText(kStrRefBonus));
    _controls.LBL_TOTAL->setTextMessage(strings.getText(kStrRefTotalRank));
    // The list takes the keyboard afresh, which selects and shows its first
    // skill; an empty list shows none.
    _controls.LB_ABILITY->clearSelection();
    _gui->clearActiveControl();
    _gui->setActiveControl(*_controls.LB_ABILITY);
    const int row = _controls.LB_ABILITY->selectedItemIndex();
    if (row >= 0) {
        showSkill(static_cast<SkillType>(stoi(_controls.LB_ABILITY->getItemAt(row).tag)));
    }
}

// A skill's bonus is what its effects and ability modifier add to its trained
// rank.
void AbilitiesMenu::showSkill(SkillType skill) {
    auto maybeSkillInfo = _skills.find(skill);
    if (maybeSkillInfo == _skills.end()) {
        return;
    }
    auto leader = _game.party().getLeader();
    const int rank = leader->getUnopposedSkillRank(skill, true);
    const int total = leader->getUnopposedSkillRank(skill);
    const int bonus = total - rank;
    _controls.LBL_RANKVAL->setTextMessage(std::to_string(rank));
    showChange(bonus > 0 ? "+" + std::to_string(bonus) : std::to_string(bonus), bonus);
    _controls.LBL_TOTALVAL->setTextMessage(std::to_string(total));
    describe(maybeSkillInfo->second.name, maybeSkillInfo->second.description);
}

// The feats view chains the feats the character holds, each chain from its
// first feat; TSL leaves out two feats no character chooses.
void AbilitiesMenu::showFeatsView(const Creature &character) {
    const auto &attributes = character.attributes();
    std::vector<FeatDisplayEntry> entries;
    for (auto &entry : _services.game.feats.getLevelUpDisplayEntries(attributes, *attributes.classLevels().back().first)) {
        if (!attributes.hasFeat(entry.type)) {
            continue;
        }
        if (_game.isTSL() && (entry.type == FeatType::ComplexUnarmedAnims || entry.type == FeatType::MineImmunity)) {
            continue;
        }
        entries.push_back(entry);
    }
    auto positions = getFeatIconPositions(entries);

    _chain->clearItems();
    _chain->clearLinks();
    _chain->setColumnCount(kIconSelectionColumnCount);
    for (const auto &entry : entries) {
        IconChain::Item item;
        item.tag = std::to_string(static_cast<int>(entry.type));
        item.column = positions.at(entry.type).x;
        item.row = positions.at(entry.type).y;
        item.iconTexture = _services.game.feats.get(entry.type)->icon;
        item.state = IconChain::State::Owned;
        _chain->addItem(std::move(item));
    }
    // A feat leads to the next of its chain beside it.
    for (size_t i = 1; i < entries.size(); ++i) {
        const auto &source = entries[i - 1];
        const auto &target = entries[i];
        const auto &from = positions.at(source.type);
        const auto &to = positions.at(target.type);
        if (from.y == to.y && to.x == from.x + 1) {
            _chain->addLink({std::to_string(static_cast<int>(source.type)), std::to_string(static_cast<int>(target.type))});
        }
    }
    if (!entries.empty()) {
        _chain->focusItem(std::to_string(static_cast<int>(entries.front().type)));
    }
}

// The powers view chains the powers the character knows, with the Force point
// cost of the one in focus.
void AbilitiesMenu::showPowersView(const Creature &character) {
    auto &strings = _services.resource.strings;
    _controls.LBL_SKILLRANK->setTextMessage(strings.getText(kStrRefBaseCost));
    _controls.LBL_TOTAL->setTextMessage(strings.getText(kStrRefCostPerUse));
    _controls.LBL_RANKVAL->setTextMessage("");
    _controls.LBL_BONUSVAL->setTextMessage("");
    _controls.LBL_TOTALVAL->setTextMessage("");

    const auto &attributes = character.attributes();
    std::vector<SpellDisplayEntry> entries;
    for (auto &entry : _services.game.spells.getLevelUpDisplayEntries(attributes, *attributes.classLevels().back().first, {})) {
        if (entry.visible && entry.known) {
            entries.push_back(std::move(entry));
        }
    }
    auto positions = getPowerIconPositions(entries);

    _chain->clearItems();
    _chain->clearLinks();
    _chain->setColumnCount(kIconSelectionColumnCount);
    for (const auto &entry : entries) {
        IconChain::Item item;
        item.tag = std::to_string(static_cast<int>(entry.type));
        item.column = positions.at(entry.type).x;
        item.row = positions.at(entry.type).y;
        item.iconTexture = entry.icon;
        item.state = IconChain::State::Owned;
        _chain->addItem(std::move(item));
    }
    // A power leads to the one beside it that it opens.
    for (const auto &entry : entries) {
        if (!entry.displayParent || positions.count(*entry.displayParent) == 0) {
            continue;
        }
        const auto &from = positions.at(*entry.displayParent);
        const auto &to = positions.at(entry.type);
        if (from.y == to.y && to.x == from.x + 1) {
            _chain->addLink({std::to_string(static_cast<int>(*entry.displayParent)), std::to_string(static_cast<int>(entry.type))});
        }
    }
    if (!entries.empty()) {
        _chain->focusItem(std::to_string(static_cast<int>(entries.front().type)));
    }
}

void AbilitiesMenu::showFeat(FeatType type) {
    auto feat = _services.game.feats.get(type);
    describe(feat->name, feat->description);
}

// A power's cost is its base cost adjusted by the character's alignment and,
// in TSL, its Charisma, the room's Force rating and its form. KotOR names the
// side of the Force that adjusts it.
void AbilitiesMenu::showPower(SpellType type) {
    auto spell = _services.game.spells.get(type);
    auto leader = _game.party().getLeader();
    const int base = spell->forcePointCost;
    const int cost = leader->adjustedSpellForcePointCost(*spell, true);
    const int adjustment = cost - base;

    auto &strings = _services.resource.strings;
    std::string label(strings.getText(kStrRefAdjustment));
    if (!_game.isTSL() && adjustment != 0) {
        const int band = leader->goodEvil() / 10;
        if (band >= 6) {
            label = strings.getText(kStrRefLightSide) + " " + label;
        } else if (band != 5) {
            label = strings.getText(kStrRefDarkSide) + " " + label;
        }
    }
    _controls.LBL_BONUS->setTextMessage(label);
    _controls.LBL_RANKVAL->setTextMessage(std::to_string(base));
    showChange(adjustment > 0 ? "+" + std::to_string(adjustment) : std::to_string(adjustment), -adjustment);
    _controls.LBL_TOTALVAL->setTextMessage(std::to_string(cost));
    describe(spell->name, spell->description);
}

void AbilitiesMenu::describe(const std::string &name, const std::string &description) {
    _controls.LBL_NAME->setTextMessage(name);
    auto &box = _game.isTSL() && _view == View::Feats ? *_controls.LB_DESC_FEATS : *_controls.LB_DESC;
    box.clearItems();
    if (!description.empty()) {
        box.addTextLinesAsItems(description);
    }
}

// A change for the better shows in green (orange in TSL), one for the worse
// in red, and none in the plain value colour.
void AbilitiesMenu::showChange(const std::string &text, int better) {
    const bool tsl = _game.isTSL();
    glm::vec3 color = tsl ? kTSLColorNewText : kK1ColorBlue;
    if (better > 0) {
        color = tsl ? kTSLColorBonus : kK1ColorGreen;
    } else if (better < 0) {
        color = kTSLColorRed;
    }
    _controls.LBL_BONUSVAL->setTextMessage(text);
    _controls.LBL_BONUSVAL->setTextColor(color);
}

void AbilitiesMenu::changeCharacter(int member) {
    if (_game.party().selectStandingMember(member)) {
        refreshControls();
    }
}

void AbilitiesMenu::refreshPortraits() {
    if (_game.isTSL())
        return;

    Party &party = _game.party();
    std::shared_ptr<Creature> partyLeader(party.getLeader());
    std::shared_ptr<Creature> partyMember1(party.getMember(1));
    std::shared_ptr<Creature> partyMember2(party.getMember(2));

    _controls.BTN_CHANGE1->setBorderFill(partyMember1 ? partyMember1->portrait() : nullptr);
    _controls.BTN_CHANGE1->setHilightFill(partyMember1 ? partyMember1->portrait() : nullptr);

    _controls.BTN_CHANGE2->setBorderFill(partyMember2 ? partyMember2->portrait() : nullptr);
    _controls.BTN_CHANGE2->setHilightFill(partyMember2 ? partyMember2->portrait() : nullptr);
}

} // namespace game

} // namespace reone
