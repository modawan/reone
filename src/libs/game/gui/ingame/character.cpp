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

#include "reone/game/gui/ingame/character.h"

#include <optional>

#include "reone/game/d20/classes.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/gui/ingame.h"
#include "reone/game/object/creature.h"
#include "reone/game/party.h"
#include "reone/game/types.h"
#include "reone/graphics/di/services.h"
#include "reone/gui/guis.h"
#include "reone/gui/sceneinitializer.h"
#include "reone/resource/di/services.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/provider/models.h"
#include "reone/resource/strings.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;
using namespace reone::scene;

namespace reone {

namespace game {

// The tutorial window (tutorial.2da row) the AI script panel asks for.
static constexpr int kScriptSelectTutorial = 10;

/** The experience table's amount for a level no experience reaches. */
static constexpr uint32_t kUnreachableExperience = 0xFFFFFFFF;

// The Force mastery lines, one per group of Jedi and Sith classes, for Pure
// Good and for Pure Evil powers.
static constexpr int kStrRefGuardianPureGood = 114161;
static constexpr int kStrRefSentinelPureGood = 114162;
static constexpr int kStrRefConsularPureGood = 114163;
static constexpr int kStrRefGuardianPureEvil = 114164;
static constexpr int kStrRefSentinelPureEvil = 114165;
static constexpr int kStrRefConsularPureEvil = 114166;

// The Force mastery line of a class: guardians, weapon masters and
// marauders share one, consulars, Jedi masters and Sith lords another, and
// sentinels, watchmen and assassins a third. Other classes have none.
static std::optional<int> forceMasteryStrRef(ClassType clazz, bool pureGood) {
    switch (clazz) {
    case ClassType::JediGuardian:
    case ClassType::JediWeaponMaster:
    case ClassType::SithMarauder:
        return pureGood ? kStrRefGuardianPureGood : kStrRefGuardianPureEvil;
    case ClassType::JediConsular:
    case ClassType::JediMaster:
    case ClassType::SithLord:
        return pureGood ? kStrRefConsularPureGood : kStrRefConsularPureEvil;
    case ClassType::JediSentinel:
    case ClassType::JediWatchman:
    case ClassType::SithAssassin:
        return pureGood ? kStrRefSentinelPureGood : kStrRefSentinelPureEvil;
    default:
        return std::nullopt;
    }
}

void CharacterMenu::onGUILoaded() {
    loadBackground(BackgroundType::Menu);
    bindControls();

    if (_controls.LBL_GOOD1)
        _lblGood.push_back(_controls.LBL_GOOD1);
    if (_controls.LBL_GOOD2)
        _lblGood.push_back(_controls.LBL_GOOD2);
    if (_controls.LBL_GOOD3)
        _lblGood.push_back(_controls.LBL_GOOD3);
    if (_controls.LBL_GOOD4)
        _lblGood.push_back(_controls.LBL_GOOD4);
    if (_controls.LBL_GOOD5)
        _lblGood.push_back(_controls.LBL_GOOD5);
    if (_controls.LBL_GOOD6)
        _lblGood.push_back(_controls.LBL_GOOD6);
    if (_controls.LBL_GOOD7)
        _lblGood.push_back(_controls.LBL_GOOD7);
    if (_controls.LBL_GOOD8)
        _lblGood.push_back(_controls.LBL_GOOD8);
    if (_controls.LBL_GOOD9)
        _lblGood.push_back(_controls.LBL_GOOD9);
    if (_controls.LBL_GOOD10)
        _lblGood.push_back(_controls.LBL_GOOD10);

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
    if (_controls.LBL_BAR6)
        _lblBar.push_back(_controls.LBL_BAR6);

    if (_game.isTSL()) {
        _controls.LBL_STATSBORDER->setTintBorderFill(true);
        _controls.LBL_XP_BACK->setTintBorderFill(true);
        useK2ShellTitle(_controls.LBL_TITLE);
        enableK2ButtonBodyFill(_controls.BTN_EXIT);
        _controls.BTN_CHANGE1 = _inGameMenu.getBtnChange2();
        _controls.BTN_CHANGE2 = _inGameMenu.getBtnChange3();
    }

    turnK1PartyArrowRight();
    _controls.BTN_AUTO->setDisabled(true);
    // The level-up buttons' texts pulse until the pointer first leaves them.
    _controls.BTN_AUTO->setTextPulsing(true);
    _controls.BTN_LEVELUP->setTextPulsing(true);
    _controls.BTN_EXIT->setOnClick([this]() {
        _game.openInGame();
    });
    _controls.BTN_LEVELUP->setOnClick([this]() {
        _game.openLevelUp();
    });

    if (!_game.isTSL()) {
        // _controls.btnCharLeft->setVisible(false);
        // _controls.btnCharRight->setVisible(false);

        for (auto &control : _lblGood) {
            control->setVisible(false);
        }

        _controls.LBL_MORE->setVisible(false);
        _controls.BTN_SCRIPTS->setOnClick([this]() {
            openScriptSelectPanel();
        });
        loadScriptSelectPanel();
    }
}

bool CharacterMenu::handle(const input::Event &event) {
    if (!_scriptSelectOpen) {
        return GameGUI::handle(event);
    }
    // The panel is modal; Escape leaves it the same way as Back.
    if (event.type == input::EventType::KeyDown && event.key.code == input::KeyCode::Escape) {
        closeScriptSelectPanel();
        return true;
    }
    _scriptSelectGUI->handle(event);
    return true;
}

void CharacterMenu::update(float dt) {
    std::shared_ptr<Creature> leader(_game.party().getLeader());
    _controls.BTN_LEVELUP->setVisible(leader->isLevelUpPending());
    _controls.BTN_AUTO->setVisible(leader->isLevelUpPending());
    GameGUI::update(dt);
    if (_scriptSelectOpen) {
        _scriptSelectGUI->update(dt);
        // A row describes itself when it gains focus.
        const int row = _scriptSelectControls.LST_AIState->selectedItemIndex();
        if (row >= 0 && row != _scriptDescribedRow) {
            showScriptDescription(row);
        }
    }
}

void CharacterMenu::render() {
    GameGUI::render();
    if (_scriptSelectOpen) {
        _scriptSelectGUI->render();
    }
}

void CharacterMenu::clearSelection() {
    if (_scriptSelectOpen) {
        closeScriptSelectPanel();
    }
    GameGUI::clearSelection();
}

void CharacterMenu::loadScriptSelectPanel() {
    _scriptSelectGUI = _services.gui.guis.get(guiResRef("scriptselect"), [this](IGUI &gui) { preload(gui); });
    if (!_scriptSelectGUI) {
        throw ResourceNotFoundException("GUI not found: " + guiResRef("scriptselect"));
    }
    auto find = [this](const std::string &tag) { return _scriptSelectGUI->findControl(tag); };
    auto &controls = _scriptSelectControls;
    controls.BTN_Accept = std::static_pointer_cast<Button>(find("BTN_Accept"));
    controls.BTN_Back = std::static_pointer_cast<Button>(find("BTN_Back"));
    controls.LBL_TITLE = std::static_pointer_cast<Label>(find("LBL_TITLE"));
    controls.LB_DESC = std::static_pointer_cast<ListBox>(find("LB_DESC"));
    controls.LST_AIState = std::static_pointer_cast<ListBox>(find("LST_AIState"));

    // One row per aiscripts row, named by its string, in table order.
    _scriptRows.clear();
    auto &list = *controls.LST_AIState;
    list.clearItems();
    const auto &rows = _game.party().aiScripts();
    for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
        ScriptRow script;
        script.style = static_cast<NPCAIStyle>(rows[row].state);
        script.nameStrRef = rows[row].nameStrRef;
        script.descriptionStrRef = rows[row].descriptionStrRef;
        ListBox::Item item;
        item.tag = std::to_string(row);
        item.text = _services.resource.strings.getText(script.nameStrRef);
        item.on = false;
        list.addItem(std::move(item));
        _scriptRows.push_back(script);
    }

    // A click takes that row's style at once; Accept takes the focused row's.
    list.setOnItemClick([this](const std::string &tag) {
        selectScript(std::stoi(tag));
    });
    controls.BTN_Accept->setOnClick([this]() {
        selectScript(_scriptSelectControls.LST_AIState->selectedItemIndex());
    });
    controls.BTN_Back->setOnClick([this]() {
        closeScriptSelectPanel();
    });
}

// The panel opens for the leader with its current style marked and focused,
// and asks for its tutorial window.
void CharacterMenu::openScriptSelectPanel() {
    auto leader = _game.party().getLeader();
    _scriptSubject = leader->id();
    auto &list = *_scriptSelectControls.LST_AIState;
    list.clearSelection();
    for (size_t row = 0; row < _scriptRows.size(); ++row) {
        const bool current = _scriptRows[row].style == leader->aiStyle();
        list.setItemOn(static_cast<int>(row), current);
        if (current) {
            list.setSelectedItemIndex(static_cast<int>(row));
        }
    }
    _scriptDescribedRow = -1;
    _scriptSelectOpen = true;
    _game.requestTutorialWindow(kScriptSelectTutorial);
}

void CharacterMenu::closeScriptSelectPanel() {
    _scriptSelectGUI->clearSelection();
    _scriptSelectOpen = false;
}

// The chosen row's style becomes the creature's own AI style, and the panel
// closes.
void CharacterMenu::selectScript(int row) {
    auto subject = _game.getObjectById<Creature>(_scriptSubject);
    if (subject && row >= 0 && row < static_cast<int>(_scriptRows.size())) {
        subject->setAIStyle(_scriptRows[static_cast<size_t>(row)].style);
    }
    closeScriptSelectPanel();
}

void CharacterMenu::showScriptDescription(int row) {
    _scriptDescribedRow = row;
    _scriptSelectControls.LB_DESC->clearItems();
    _scriptSelectControls.LB_DESC->addTextLinesAsItems(
        _services.resource.strings.getText(_scriptRows[static_cast<size_t>(row)].descriptionStrRef));
}

static std::string toStringOrEmptyIfZero(int value) {
    return value != 0 ? std::to_string(value) : "";
}

static std::string describeAbilityModifier(int value) {
    return value > 0 ? "+" + std::to_string(value) : std::to_string(value);
}

void CharacterMenu::refreshControls() {
    std::shared_ptr<Creature> partyLeader(_game.party().getLeader());
    CreatureAttributes &attributes = partyLeader->attributes();

    if (!_game.isTSL()) {
        _controls.LBL_CLASS1->setTextMessage(describeClass(attributes.getClassByPosition(1)));
        _controls.LBL_CLASS2->setTextMessage(describeClass(attributes.getClassByPosition(2)));
        _controls.LBL_LEVEL1->setTextMessage(toStringOrEmptyIfZero(attributes.getLevelByPosition(1)));
        _controls.LBL_LEVEL2->setTextMessage(toStringOrEmptyIfZero(attributes.getLevelByPosition(2)));
    }

    _controls.LBL_VITALITY_STAT->setTextMessage(str(boost::format("%d/%d") % partyLeader->currentHitPoints() % partyLeader->maxHitPoints()));
    _controls.LBL_DEFENSE_STAT->setTextMessage(std::to_string(partyLeader->getDefense()));
    _controls.LBL_FORCE_STAT->setTextMessage("");

    _controls.LBL_STR->setTextMessage(std::to_string(partyLeader->getEffectiveAbilityScore(Ability::Strength)));
    _controls.LBL_STR_MOD->setTextMessage(describeAbilityModifier(partyLeader->getEffectiveAbilityModifier(Ability::Strength)));
    _controls.LBL_DEX->setTextMessage(std::to_string(partyLeader->getEffectiveAbilityScore(Ability::Dexterity)));
    _controls.LBL_DEX_MOD->setTextMessage(describeAbilityModifier(partyLeader->getEffectiveAbilityModifier(Ability::Dexterity)));
    _controls.LBL_CON->setTextMessage(std::to_string(partyLeader->getEffectiveAbilityScore(Ability::Constitution)));
    _controls.LBL_CON_MOD->setTextMessage(describeAbilityModifier(partyLeader->getEffectiveAbilityModifier(Ability::Constitution)));
    _controls.LBL_INT->setTextMessage(std::to_string(partyLeader->getEffectiveAbilityScore(Ability::Intelligence)));
    _controls.LBL_INT_MOD->setTextMessage(describeAbilityModifier(partyLeader->getEffectiveAbilityModifier(Ability::Intelligence)));
    _controls.LBL_WIS->setTextMessage(std::to_string(partyLeader->getEffectiveAbilityScore(Ability::Wisdom)));
    _controls.LBL_WIS_MOD->setTextMessage(describeAbilityModifier(partyLeader->getEffectiveAbilityModifier(Ability::Wisdom)));
    _controls.LBL_CHA->setTextMessage(std::to_string(partyLeader->getEffectiveAbilityScore(Ability::Charisma)));
    _controls.LBL_CHA_MOD->setTextMessage(describeAbilityModifier(partyLeader->getEffectiveAbilityModifier(Ability::Charisma)));

    SavingThrows savingThrows(attributes.getAggregateSavingThrows());
    _controls.LBL_FORTITUDE_STAT->setTextMessage(std::to_string(savingThrows.fortitude));
    _controls.LBL_REFLEX_STAT->setTextMessage(std::to_string(savingThrows.reflex));
    _controls.LBL_WILL_STAT->setTextMessage(std::to_string(savingThrows.will));

    _controls.LBL_EXPERIENCE_STAT->setTextMessage(std::to_string(partyLeader->xp()));
    // The experience table's amount at the leader's level prints signed; it
    // is hidden when the table holds the unreachable amount there.
    const auto needed = partyLeader->getNeededXP();
    if (needed) {
        _controls.LBL_NEEDED_XP->setTextMessage(std::to_string(static_cast<int>(*needed)));
    }
    const bool neededShown = needed && *needed != kUnreachableExperience;
    _controls.LBL_NEEDED_XP->setVisible(neededShown);
    _controls.LBL_NEXT_LEVEL->setVisible(neededShown);

    if (_game.isTSL()) {
        refreshForceMastery(*partyLeader);
    }
    refreshPortraits();
    refresh3D();
}

// The Force mastery line describes the leader's Pure Good or Pure Evil powers
// for its last class.
void CharacterMenu::refreshForceMastery(const Creature &leader) {
    const auto &classLevels = leader.attributes().classLevels();
    const bool pureGood = leader.hasEffect(EffectType::PureGoodPowers);
    const bool pureEvil = !pureGood && leader.hasEffect(EffectType::PureEvilPowers);
    std::optional<int> strRef;
    if ((pureGood || pureEvil) && !classLevels.empty()) {
        strRef = forceMasteryStrRef(classLevels.back().first->type(), pureGood);
    }
    _controls.LBL_FORCEMASTERY->setVisible(strRef.has_value());
    if (strRef) {
        _controls.LBL_FORCEMASTERY->setTextMessage(_services.resource.strings.getText(*strRef));
    }
}

std::string CharacterMenu::describeClass(ClassType clazz) const {
    if (clazz == ClassType::Invalid)
        return "";

    return _services.game.classes.get(clazz)->name();
}

void CharacterMenu::refreshPortraits() {
    if (_game.isTSL())
        return;

    Party &party = _game.party();
    std::shared_ptr<Creature> partyMember1(party.getMember(1));
    std::shared_ptr<Creature> partyMember2(party.getMember(2));

    _controls.BTN_CHANGE1->setBorderFill(partyMember1 ? partyMember1->portrait() : nullptr);
    _controls.BTN_CHANGE1->setHilightFill(partyMember1 ? partyMember1->portrait() : nullptr);

    _controls.BTN_CHANGE2->setBorderFill(partyMember2 ? partyMember2->portrait() : nullptr);
    _controls.BTN_CHANGE2->setHilightFill(partyMember2 ? partyMember2->portrait() : nullptr);
}

void CharacterMenu::refresh3D() {
    auto &sceneGraph = _services.scene.graphs.get(kSceneCharacter);
    float aspect = _controls.LBL_3DCHAR->extent().width / static_cast<float>(_controls.LBL_3DCHAR->extent().height);

    SceneInitializer(sceneGraph)
        .aspect(aspect)
        .depth(kDefaultClipPlaneNear, 10.0f)
        .modelSupplier(bind(&CharacterMenu::getSceneModel, this, std::placeholders::_1))
        .modelOffset(glm::vec2(0.0f, 1.7f))
        .cameraFromModelNode("camerahook")
        .invoke();

    _controls.LBL_3DCHAR->setSceneName(kSceneCharacter);
}

std::shared_ptr<ModelSceneNode> CharacterMenu::getSceneModel(ISceneGraph &sceneGraph) const {
    auto partyLeader = _game.party().getLeader();

    std::shared_ptr<Creature> character =
        _game.newPresentationCreature(sceneGraph.name());
    character->setFacing(-glm::half_pi<float>());
    character->setAppearance(partyLeader->appearance());

    for (auto &item : partyLeader->equipment()) {
        switch (item.first) {
        case InventorySlots::body: {
            auto previewItem = _game.newPresentationItem();
            previewItem->clone(*item.second);
            character->equip(item.first, previewItem);
            break;
        }
        default:
            break;
        }
    }

    character->loadAppearance();
    character->updateModelAnimation();

    auto sceneModel = sceneGraph.newModel(*_services.resource.models.get("charmain_light"), ModelUsage::GUI);
    sceneModel->attach("charmain_light", *character->sceneNode());

    return sceneModel;
}

} // namespace game

} // namespace reone
