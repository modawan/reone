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

#include "reone/game/d20/skill.h"
#include "reone/game/d20/skills.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/party.h"
#include "reone/gui/control/button.h"
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

void AbilitiesMenu::onGUILoaded() {
    loadBackground(BackgroundType::Menu);
    bindControls();
    tintK2InGameFooter();
    tintK2InGameHeader();
    turnK1PartyArrowRight();

    _controls.BTN_SKILLS->setDisabled(true);
    _controls.BTN_POWERS->setDisabled(true);
    _controls.BTN_FEATS->setDisabled(true);

    _controls.LBL_SKILLRANK->setTextMessage(_services.resource.strings.getText(kStrRefSkillRank));
    _controls.LBL_BONUS->setTextMessage(_services.resource.strings.getText(kStrRefBonus));
    _controls.LBL_TOTAL->setTextMessage(_services.resource.strings.getText(kStrRefTotalRank));
    _controls.LBL_RANKVAL->setTextMessage("");
    _controls.LBL_BONUSVAL->setTextMessage("");
    _controls.LBL_TOTALVAL->setTextMessage("");
    _controls.LBL_NAME->setTextMessage("");

    // Every row is a skill's icon row.
    auto &protoItem = static_cast<ImageButton &>(_controls.LB_ABILITY->protoItem());
    _controls.LB_DESC->setProtoMatchContent(true);
    if (_game.isTSL()) {
        fillK2SectionStrip(_controls.LBL_BAR1, _controls.LBL_BAR2);
        _controls.BTN_SKILLS->setDisabled(false);
        updateK2FilterButton(_controls.BTN_SKILLS, true);
        updateK2FilterButton(_controls.BTN_POWERS, false);
        updateK2FilterButton(_controls.BTN_FEATS, false);
        _controls.LB_ABILITY->setTintBorderFill(true);
        _controls.LB_DESC->setTintBorderFill(true);
        _controls.LB_DESC_FEATS->setTintBorderFill(true);
        _controls.LBL_INFOBG->setTintBorderFill(true);
        _controls.LBL_NAME->setTintBorderFill(true);
        useK2ShellTitle(_controls.LBL_ABILITIES);
        enableK2ButtonBodyFill(_controls.BTN_EXIT);
        enableK2ButtonBodyFill(protoItem);
        protoItem.setBorderFill("uibit_fill_2wt");
        protoItem.setHilightFill("uibit_fill_2wt");
        protoItem.setTintBorderFill(true);
    }
    // The active row's frames pulse; TSL's icon frame is white.
    protoItem.setHilightPulsing(true, false);
    if (_game.isTSL()) {
        protoItem.setIconFrameHilight(glm::vec3 {1.0f}, true);
    } else {
        protoItem.setIconFrameHilight(std::nullopt, true);
    }
    _controls.LB_ABILITY->setOnItemClick([this](const std::string &item) {
        auto skill = static_cast<SkillType>(stoi(item));
        auto maybeSkillInfo = _skills.find(skill);
        if (maybeSkillInfo == _skills.end())
            return;

        std::shared_ptr<Creature> partyLeader(_game.party().getLeader());

        _controls.LBL_RANKVAL->setTextMessage(std::to_string(partyLeader->attributes().getSkillRank(skill)));
        _controls.LBL_BONUSVAL->setTextMessage("0");
        _controls.LBL_TOTALVAL->setTextMessage(std::to_string(partyLeader->attributes().getSkillRank(skill)));
        _controls.LBL_NAME->setTextMessage(maybeSkillInfo->second.name);

        _controls.LB_DESC->clearItems();
        _controls.LB_DESC->addTextLinesAsItems(maybeSkillInfo->second.description);
    });
    _controls.BTN_EXIT->setOnClick([this]() {
        _game.openInGame();
    });

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

void AbilitiesMenu::refreshControls() {
    refreshPortraits();
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
