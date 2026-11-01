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

#include "reone/game/gui/chargen.h"

#include <algorithm>
#include <cstdint>
#include <unordered_set>

#include "reone/game/d20/classes.h"
#include "reone/game/d20/feats.h"
#include "reone/game/d20/races.h"
#include "reone/game/d20/spells.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/party.h"
#include "reone/game/portraits.h"
#include "reone/game/types.h"
#include "reone/graphics/di/services.h"
#include "reone/gui/control/label.h"
#include "reone/gui/sceneinitializer.h"
#include "reone/resource/provider/models.h"
#include "reone/resource/resources.h"
#include "reone/resource/strings.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"

using namespace reone::audio;

using namespace reone::gui;
using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;

namespace reone {

namespace game {

/** The tutorial windows that the attribute, skill and power screens bring. */
static constexpr int kAttributesTutorial = 16;
static constexpr int kSkillsTutorial = 17;
static constexpr int kPowersTutorial = 19;
/** A new character starts balanced between the light and dark sides. */
static constexpr int kNewCharacterGoodEvil = 50;
/** The TSL globals that record the player character's alignment and level. */
static constexpr char kPlayerAlignmentGlobal[] = "G_PC_Align_Val";
static constexpr char kPlayerLevelGlobal[] = "G_PC_LEVEL";
/** The title of the level-up panel. */
static constexpr int kLevelUpTitleStrRef = 1071;
/** The caption before the level a level-up rises to, on a panel without a separate level caption. */
static constexpr int kLevelValueStrRef = 74696;
/** The movement rate of the player's pace. */
static constexpr int kPlayerMovementRate = 0;

/** The controls the panel shows in both titles, at creation or on a level-up. */
static const std::unordered_set<std::string> kPanelControls {
    "MAIN_TITLE_LBL", "LBL_NAME", "LBL_CLASS", "LBL_LEVEL_VAL",
    "STR_LBL", "DEX_LBL", "CON_LBL", "INT_LBL", "WIS_LBL", "CHA_LBL",
    "STR_AB_LBL", "DEX_AB_LBL", "CON_AB_LBL", "INT_AB_LBL", "WIS_AB_LBL", "CHA_AB_LBL",
    "NEW_FORT_LBL", "NEW_REFL_LBL", "NEW_WILL_LBL",
    "LBL_BEVEL_L", "LBL_BEVEL_M", "LBL_BEVEL_R", "LBL_FORTITUDE", "LBL_REFLEX", "LBL_WILL",
    "LBL_VIT", "LBL_DEF", "MODEL_LBL", "PORTRAIT_LBL"};
/** The controls the panel also shows in KotOR: its separate level caption. */
static const std::unordered_set<std::string> kK1PanelControls {"LBL_LEVEL"};
/** The controls the panel also shows in TSL: its statistics frame and captions. */
static const std::unordered_set<std::string> kTSLPanelControls {
    "LBL_STATSBACK", "LBL_STATSBORDER", "LBL_PORTBORDER", "LBL_VIT_NAME", "LBL_DEF_NAME", "LBL_BAR1"};

static constexpr float kModelScale = 1.1f;

CharacterGeneration::CharacterGeneration(Game &game, ServicesView &services) :
    GameGUI(game, services) {
    _resRef = guiResRef("maincg");

    // KotOR's character generation plays its own menu music; TSL's plays its
    // loading screen's music instead, from the loading-screen table.
    _musicResRef = game.isTSL() ? "" : "mus_theme_rep";
}

void CharacterGeneration::onGUILoaded() {
    loadBackground(BackgroundType::Menu);
    bindControls();

    _controls.LBL_NAME->setTextMessage("");
    _creationTitle = _controls.MAIN_TITLE_LBL->text().text;

    showOnlyNamedControls(kPanelControls, _game.isTSL() ? kTSLPanelControls : kK1PanelControls);
    showLevelUpTitle(false);

    loadClassSelection();
    loadQuickOrCustom();
    loadQuick();
    loadPortraitSelection();
    loadNameEntry();
    loadCustom();
    loadAbilities();
    loadSkills();
    loadFeats();
    loadPowers();
    loadLevelUp();
}

void CharacterGeneration::loadClassSelection() {
    _classSelection = std::make_unique<ClassSelection>(*this, _game, _services);
    _classSelection->init();
}

void CharacterGeneration::loadQuickOrCustom() {
    _quickOrCustom = std::make_unique<QuickOrCustom>(*this, _game, _services);
    _quickOrCustom->init();
}

void CharacterGeneration::loadQuick() {
    _quick = std::make_unique<QuickCharacterGeneration>(*this, _game, _services);
    _quick->init();
}

void CharacterGeneration::loadCustom() {
    _custom = std::make_unique<CustomCharacterGeneration>(*this, _game, _services);
    _custom->init();
}

void CharacterGeneration::loadPortraitSelection() {
    _portraitSelection = std::make_unique<PortraitSelection>(*this, _game, _services);
    _portraitSelection->init();
}

void CharacterGeneration::loadAbilities() {
    _abilities = std::make_unique<CharGenAbilities>(*this, _game, _services);
    _abilities->init();
}

void CharacterGeneration::loadSkills() {
    _charGenSkills = std::make_unique<CharGenSkills>(*this, _game, _services);
    _charGenSkills->init();
}

void CharacterGeneration::loadFeats() {
    _charGenFeats = std::make_unique<CharGenFeats>(*this, _game, _services);
    _charGenFeats->init();
}

void CharacterGeneration::loadPowers() {
    _charGenPowers = std::make_unique<CharGenPowers>(*this, _game, _services);
    _charGenPowers->init();
}

void CharacterGeneration::loadNameEntry() {
    _nameEntry = std::make_unique<NameEntry>(*this, _game, _services);
    _nameEntry->init();
}

void CharacterGeneration::loadLevelUp() {
    _levelUp = std::make_unique<LevelUpMenu>(*this, _game, _services);
    _levelUp->init();
}

bool CharacterGeneration::handle(const input::Event &event) {
    // Escape acts as the back button of the panel in front.
    if (event.type == input::EventType::KeyDown && event.key.code == input::KeyCode::Escape && !event.key.repeat) {
        getSubGUI()->clickControl(_screen == CharGenScreen::Powers ? "BACK_BTN" : "BTN_BACK");
        return true;
    }
    if (getSubGUI()->handle(event)) {
        return true;
    }
    return _gui->handle(event);
}

GameGUI *CharacterGeneration::getSubGUI() const {
    switch (_screen) {
    case CharGenScreen::ClassSelection:
        return _classSelection.get();
    case CharGenScreen::QuickOrCustom:
        return _quickOrCustom.get();
    case CharGenScreen::Quick:
        return _quick.get();
    case CharGenScreen::Custom:
        return _custom.get();
    case CharGenScreen::PortraitSelection:
        return _portraitSelection.get();
    case CharGenScreen::Abilities:
        return _abilities.get();
    case CharGenScreen::Skills:
        return _charGenSkills.get();
    case CharGenScreen::Feats:
        return _charGenFeats.get();
    case CharGenScreen::Powers:
        return _charGenPowers.get();
    case CharGenScreen::Name:
        return _nameEntry.get();
    case CharGenScreen::LevelUp:
        return _levelUp.get();
    default:
        throw std::logic_error("Invalid screen: " + std::to_string(static_cast<int>(_screen)));
    }
}

void CharacterGeneration::update(float dt) {
    GameGUI::update(dt);
    getSubGUI()->update(dt);
}

void CharacterGeneration::render() {
    GameGUI::render();
    getSubGUI()->render();
}

void CharacterGeneration::openClassSelection() {
    _controls.MODEL_LBL->setVisible(false);
    changeScreen(CharGenScreen::ClassSelection);
}

void CharacterGeneration::changeScreen(CharGenScreen screen) {
    auto gui = getSubGUI();
    if (gui) {
        gui->clearSelection();
    }
    _screen = screen;
}

void CharacterGeneration::openQuickOrCustom() {
    setAttributesVisible(false);
    _controls.MODEL_LBL->setVisible(true);
    changeScreen(CharGenScreen::QuickOrCustom);
}

void CharacterGeneration::setAttributesVisible(bool visible) {
    std::vector<Label *> attributesLabels {
        _controls.LBL_VIT.get(),
        _controls.LBL_DEF.get(),
        _controls.STR_AB_LBL.get(),
        _controls.DEX_AB_LBL.get(),
        _controls.CON_AB_LBL.get(),
        _controls.INT_AB_LBL.get(),
        _controls.WIS_AB_LBL.get(),
        _controls.CHA_AB_LBL.get(),
        _controls.NEW_FORT_LBL.get(),
        _controls.NEW_REFL_LBL.get(),
        _controls.NEW_WILL_LBL.get()};
    for (auto &label : attributesLabels) {
        label->setVisible(visible);
    }
}

void CharacterGeneration::startCreation() {
    _type = Type::Quick;
    _character = Character();
    _controls.LBL_NAME->setTextMessage("");
    showLevelUpTitle(false);
    _classSelection->setupClassButtons();
    openClassSelection();
}

void CharacterGeneration::startCustom() {
    _type = Type::Custom;
    showLevelUpTitle(false);
    _custom->setStep(0);
    openSteps();
}

void CharacterGeneration::startQuick() {
    setAttributesVisible(true);
    _type = Type::Quick;
    showLevelUpTitle(false);
    // A quick character is given its class's feats as it is made.
    _services.game.feats.addGrantedFeats(_character.attributes, RacialType::Human, kObjectTagPlayer, _game.isTSL(), true);
    updateAttributes();
    _quick->setStep(0);
    openSteps();
}

void CharacterGeneration::startLevelUp() {
    _type = Type::LevelUp;

    std::shared_ptr<Creature> partyLeader(_game.party().getLeader());

    Character character;
    character.name = partyLeader->name();
    character.appearance = partyLeader->appearance();
    character.gender = partyLeader->gender();
    character.attributes = partyLeader->attributes();
    // The attributes panel adds the coming level to the character as it is.
    for (size_t ability = 0; ability < _levelUpBase.effectiveScores.size(); ++ability) {
        _levelUpBase.effectiveScores[ability] = partyLeader->getEffectiveAbilityScore(static_cast<Ability>(ability));
    }
    _levelUpBase.race = partyLeader->racialType();
    _levelUpBase.maxHitPoints = partyLeader->maxHitPoints();
    _levelUpBase.vitalityConstitutionModifier = partyLeader->vitalityConstitutionModifier();
    _levelUpBase.defense = partyLeader->getDefense();
    _levelUpBase.dexterityModifier = partyLeader->getEffectiveAbilityModifier(Ability::Dexterity);
    // While the character is debilitated, the armour's limit is not counted.
    _levelUpBase.reflexDexterityLimit = partyLeader->isDebilitated(true)
                                            ? 0
                                            : partyLeader->defenseDexterityModifier() - _levelUpBase.dexterityModifier;
    _levelUpBase.abilitiesAccepted = false;
    setCharacter(std::move(character));

    // The level the character is rising to, after its caption when the
    // panel has no separate level caption.
    const std::string level = std::to_string(partyLeader->attributes().getAggregateLevel() + 1);
    _controls.LBL_LEVEL_VAL->setTextMessage(_game.isTSL() ? _services.resource.strings.getText(kLevelValueStrRef) + level : level);
    showLevelUpTitle(true);

    // The coming level grants its feats and powers at once when it offers no
    // choice of them, and otherwise when their screens open.
    if (!_levelUp->hasFeatChoices()) {
        addLevelFeats();
    }
    if (!_levelUp->hasPowerChoices()) {
        addLevelPowers();
    }

    int nextLevel = partyLeader->attributes().getAggregateLevel() + 1;
    bool hasAttributes = nextLevel % 4 == 0;
    _levelUp->setStep(hasAttributes ? 0 : 1);

    openSteps();
}

void CharacterGeneration::openSteps() {
    switch (_type) {
    case Type::LevelUp:
        openLevelUp();
        break;
    case Type::Custom:
        openCustom();
        break;
    default:
        openQuick();
        break;
    }
}

void CharacterGeneration::openQuick() {
    _quick->refreshStep();
    _controls.MODEL_LBL->setVisible(true);
    changeScreen(CharGenScreen::Quick);
}

void CharacterGeneration::openCustom() {
    _custom->refreshStep();
    setAttributesVisible(_custom->step() > 1);
    _controls.MODEL_LBL->setVisible(true);
    changeScreen(CharGenScreen::Custom);
}

void CharacterGeneration::openPortraitSelection() {
    _controls.MODEL_LBL->setVisible(false);
    changeScreen(CharGenScreen::PortraitSelection);
}

void CharacterGeneration::openAbilities() {
    _abilities->reset(_type != Type::LevelUp);
    _controls.MODEL_LBL->setVisible(false);
    changeScreen(CharGenScreen::Abilities);
    _game.requestTutorialWindow(kAttributesTutorial);
}

void CharacterGeneration::openSkills() {
    // A level-up on its skills screen is at its skills step, so accepting the
    // skills enters its feats step again.
    if (_type == Type::LevelUp) {
        _levelUp->setStep(1);
    }
    _charGenSkills->reset(_type != Type::LevelUp);
    _controls.MODEL_LBL->setVisible(false);
    changeScreen(CharGenScreen::Skills);
    _game.requestTutorialWindow(kSkillsTutorial);
}

void CharacterGeneration::openFeats() {
    // A level-up that gains no normal feats chooses its bonus feats.
    bool bonus = false;
    if (_type == Type::LevelUp) {
        const auto levelled = levelledAttributes();
        const auto &[clazz, classLevel] = levelled.classLevels().back();
        bonus = clazz->getFeatGain(classLevel) == 0;
    }
    openFeatsScreen(bonus);
}

void CharacterGeneration::openBonusFeats() {
    openFeatsScreen(true);
}

void CharacterGeneration::openFeatsScreen(bool bonus) {
    std::set<FeatType> originalFeats;
    std::vector<FeatType> grantedFeats;
    if (_type == Type::LevelUp) {
        originalFeats = _character.attributes.feats();
        // The game grants none of the level's feats on a bonus-only level; reone grants them as the normal screen does.
        const auto levelled = levelledAttributes();
        const auto &[clazz, classLevel] = levelled.classLevels().back();
        if (!bonus || clazz->getFeatGain(classLevel) == 0) {
            grantedFeats = addLevelFeats();
        }
    } else if (bonus) {
        originalFeats = _character.attributes.feats();
    } else {
        // A new character's feats screen starts from the feats its class
        // grants, taking back any it was given before.
        grantedFeats = regrantCreationFeats(true);
    }
    _charGenFeats->reset(_type == Type::LevelUp, bonus, std::move(originalFeats), grantedFeats);
    if (_charGenFeats->remainingPicks() == 0) {
        _charGenFeats->accept();
        return;
    }
    _controls.MODEL_LBL->setVisible(false);
    changeScreen(CharGenScreen::Feats);
}

void CharacterGeneration::openPowers() {
    if (_type == Type::LevelUp) {
        addLevelPowers();
    }
    _charGenPowers->reset();
    _controls.MODEL_LBL->setVisible(false);
    changeScreen(CharGenScreen::Powers);
    // TSL asks only when powers are granted, which is not presented yet.
    if (!_game.isTSL()) {
        _game.requestTutorialWindow(kPowersTutorial);
    }
}

void CharacterGeneration::openNameEntry() {
    _controls.MODEL_LBL->setVisible(false);
    changeScreen(CharGenScreen::Name);
}

void CharacterGeneration::openLevelUp() {
    _levelUp->reset();
    _levelUp->refreshStep();
    _controls.MODEL_LBL->setVisible(true);
    changeScreen(CharGenScreen::LevelUp);
}

void CharacterGeneration::resetCreationFeats() {
    regrantCreationFeats(false);
}

void CharacterGeneration::resetLevelFeats() {
    // A level-up only adds feats, so those the leader does not hold are the
    // level's.
    const CreatureAttributes &held = _game.party().getLeader()->attributes();
    auto feats = _character.attributes.featOrder();
    for (auto feat : feats) {
        if (!held.hasFeat(feat)) {
            _character.attributes.removeFeat(feat);
        }
    }
}

std::vector<FeatType> CharacterGeneration::regrantCreationFeats(bool newCharacter) {
    auto held = _character.attributes.featOrder();
    for (auto feat : held) {
        _character.attributes.removeFeat(feat);
    }
    return _services.game.feats.addGrantedFeats(_character.attributes, RacialType::Human, kObjectTagPlayer, _game.isTSL(), newCharacter);
}

void CharacterGeneration::acceptAbilities() {
    _levelUpBase.abilitiesAccepted = true;
}

void CharacterGeneration::cancel() {
    if (_type == Type::LevelUp) {
        _game.openInGame();
    } else {
        _game.openMainMenu();
    }
}

void CharacterGeneration::finish() {
    if (_type == Type::LevelUp) {
        ClassType classType = _character.attributes.getEffectiveClass();
        std::shared_ptr<CreatureClass> clazz(_services.game.classes.get(classType));
        std::shared_ptr<Creature> partyLeader(_game.party().getLeader());
        partyLeader->applyLevelUp(std::move(_character.attributes), *clazz);
        // The player character's new level moves every companion with a
        // creature through its influence.
        if (partyLeader->isPlayerCreated()) {
            for (int npc = 0; npc < static_cast<int>(Party::kK2NpcCount); ++npc) {
                if (auto companion = _game.party().getAvailableMember(npc)) {
                    companion->recomputeInfluenceAlignment();
                }
            }
        }
        _game.openInGame();
    } else {
        // Character preview objects belong to the pre-playable character-
        // generation runtime. Retire them before publishing the new game.
        _game.retireRuntimeSession();

        std::vector<std::shared_ptr<Object>> noObsolete;
        std::shared_ptr<Creature> player;
        _game.replaceRuntimeObjectGraph(
            noObsolete,
            [&]() {
                player = _game.newCreature();
                player->setTag(kObjectTagPlayer);
                player->setPlayerCreated(true);
                // The entered name is the first name.
                player->setFirstAndLastName(
                    resource::LocString(-1, _character.name, _services.resource.strings), resource::LocString());
                player->setGender(_character.gender);
                player->setAppearance(_character.appearance);
                player->loadAppearance();
                // A new character moves at the player's pace.
                player->setMovementRate(kPlayerMovementRate);
                player->setFaction(Faction::Player);
                // A new character is human.
                player->setRacialType(RacialType::Human);
                player->setGoodEvil(kNewCharacterGoodEvil);
                player->attributes() = _character.attributes;
                player->initializeGeneratedVitality();

                player->setOnHeartbeat("k_hen_heartbt01");
                player->setOnSpawn("k_hen_spawn01");
                player->setOnNotice("k_hen_percept01");
                player->setOnEndRound("k_hen_combend01");
                player->setOnSpellAt("k_def_spellat01");
                player->setOnAttacked("k_hen_attacked01");
                player->setOnDamaged("k_def_damage01");
                player->setOnBlocked("k_def_blocked01");
                player->setOnDialogue("k_hen_dialogue01");
                player->setOnUserDefined("k_def_userdef01");
            },
            []() noexcept {});

        Party &party = _game.party();
        party.reset();
        party.initializeNewGameState();
        party.addMember(kNpcPlayer, player);
        party.setPlayer(player);
        // The canonical PC has to exist before any script hands control to a
        // stand-in, or there is nothing to hand control back to.
        party.setActualPlayer(player);
        // TSL starts the player character's recorded alignment balanced, and
        // records the new character's level before the first module loads, so
        // creatures balanced there freeze it rather than following it later.
        if (_game.isTSL()) {
            _game.setGlobalNumber(kPlayerAlignmentGlobal, kNewCharacterGoodEvil);
            _game.setGlobalNumber(kPlayerLevelGlobal, player->attributes().getAggregateLevel());
        }

        // Starting the game first fades the menu music out.
        _game.fadeOutMenuMusic();

        std::string moduleName(!_game.isTSL() ? "end_m01aa" : "001ebo");
        // The new game's loading screen plays the module's music; TSL first
        // stops character generation's.
        if (_game.isTSL()) {
            _game.stopLoadMusic();
        }
        _game.setLoadMusicForModule(moduleName);
        _game.loadModule(moduleName);
    }
}

void CharacterGeneration::setCharacter(Character character) {
    bool appearanceChanged = character.appearance != _character.appearance;

    _character = std::move(character);
    _controls.LBL_NAME->setTextMessage(_character.name);

    if (appearanceChanged) {
        reloadCharacterModel();
        _portraitSelection->updatePortraits();
        _nameEntry->loadRandomName();
    }

    updateAttributes();
}

void CharacterGeneration::setCharacterName(std::string name) {
    _character.name = std::move(name);
    _controls.LBL_NAME->setTextMessage(_character.name);
}

void CharacterGeneration::reloadCharacterModel() {
    auto &sceneGraph = _services.scene.graphs.get(kSceneCharGen);
    const Control::Extent &extent = _controls.MODEL_LBL->extent();
    float aspect = extent.width / static_cast<float>(extent.height);

    SceneInitializer(sceneGraph)
        .aspect(aspect)
        .depth(kDefaultClipPlaneNear, 10.0f)
        .modelSupplier(bind(&CharacterGeneration::getCharacterModel, this, std::placeholders::_1))
        .modelScale(kModelScale)
        .cameraFromModelNode("camerahook")
        .invoke();

    _controls.MODEL_LBL->setSceneName(kSceneCharGen);
    _controls.PORTRAIT_LBL->setBorderFill(_services.game.portraits.getTextureByAppearance(_character.appearance));
}

std::shared_ptr<ModelSceneNode> CharacterGeneration::getCharacterModel(ISceneGraph &sceneGraph) {
    std::shared_ptr<Creature> creature =
        _game.newPresentationCreature(sceneGraph.name());
    creature->setFacing(-glm::half_pi<float>());
    creature->setAppearance(_character.appearance);
    creature->equip("g_a_clothes01");
    creature->loadAppearance();
    creature->updateModelAnimation();

    auto model = sceneGraph.newModel(*_services.resource.models.get("cgbody_light"), ModelUsage::GUI);
    model->attach("cgbody_light", *creature->sceneNode());

    return model;
}

void CharacterGeneration::updateAttributes() {
    std::shared_ptr<CreatureClass> clazz(_services.game.classes.get(_character.attributes.getEffectiveClass()));
    _controls.LBL_CLASS->setTextMessage(clazz->name());

    const CreatureAttributes &attributes = _character.attributes;
    const int hitDie = attributes.classLevels().back().first->hitdie();
    const int constitution = panelAbilityModifier(Ability::Constitution);
    const int dexterity = panelAbilityModifier(Ability::Dexterity);
    int vitality = 0;
    int defense = 0;
    SavingThrows throws;
    int reflexDexterityLimit = 0;
    if (_type == Type::LevelUp) {
        // A level-up shows the character with its coming level: its maximum
        // vitality with the level's gain, which also follows the change to
        // the Constitution modifier over the earlier levels and is at least
        // 1; its defense with the change to its Dexterity modifier; and the
        // saving throws of its classes at their new levels.
        const int level = attributes.getAggregateLevel();
        // The gain wraps as a signed byte.
        const int gain = static_cast<int8_t>(hitDie + constitution + level * (constitution - _levelUpBase.vitalityConstitutionModifier));
        vitality = _levelUpBase.maxHitPoints + std::max(1, gain);
        defense = _levelUpBase.defense - _levelUpBase.dexterityModifier + dexterity;
        throws = levelledAttributes().getAggregateSavingThrows();
        reflexDexterityLimit = _levelUpBase.reflexDexterityLimit;
    } else {
        // A new character's vitality is its class's hit die and its
        // Constitution modifier.
        vitality = hitDie + constitution;
        defense = 10 + attributes.getAggregateDefenseBonus(_game.isTSL()) + dexterity;
        throws = attributes.getAggregateSavingThrows();
    }
    _controls.LBL_VIT->setTextMessage(std::to_string(vitality));
    _controls.LBL_DEF->setTextMessage(std::to_string(defense));

    _controls.STR_AB_LBL->setTextMessage(std::to_string(_character.attributes.strength()));
    _controls.DEX_AB_LBL->setTextMessage(std::to_string(_character.attributes.dexterity()));
    _controls.CON_AB_LBL->setTextMessage(std::to_string(_character.attributes.constitution()));
    _controls.INT_AB_LBL->setTextMessage(std::to_string(_character.attributes.intelligence()));
    _controls.WIS_AB_LBL->setTextMessage(std::to_string(_character.attributes.wisdom()));
    _controls.CHA_AB_LBL->setTextMessage(std::to_string(_character.attributes.charisma()));

    // Each saving throw adds its ability's modifier, and is summed in a signed
    // byte; on a level-up, the Reflex save also counts what armour takes off
    // the Dexterity modifier.
    const int fortitude = static_cast<int8_t>(throws.fortitude + constitution);
    const int reflex = static_cast<int8_t>(throws.reflex + dexterity + reflexDexterityLimit);
    const int will = static_cast<int8_t>(throws.will + panelAbilityModifier(Ability::Wisdom));
    _controls.NEW_FORT_LBL->setTextMessage(std::to_string(fortitude));
    _controls.NEW_REFL_LBL->setTextMessage(std::to_string(reflex));
    _controls.NEW_WILL_LBL->setTextMessage(std::to_string(will));
}

void CharacterGeneration::showLevelUpTitle(bool levelUp) {
    _controls.MAIN_TITLE_LBL->setTextMessage(levelUp ? _services.resource.strings.getText(kLevelUpTitleStrRef) : _creationTitle);
    _controls.LBL_LEVEL_VAL->setVisible(levelUp);
    if (!_game.isTSL()) {
        _controls.LBL_LEVEL->setVisible(levelUp);
    }
}

int CharacterGeneration::panelAbilityModifier(Ability ability) const {
    const int index = static_cast<int>(ability);
    if (_type == Type::LevelUp && !_levelUpBase.abilitiesAccepted) {
        return getAbilityModifierFromScore(_levelUpBase.effectiveScores[index]);
    }
    const RacialType race = _type == Type::LevelUp ? _levelUpBase.race : RacialType::Human;
    const int adjustment = _services.game.races.get(race).abilityAdjustments[index];
    return getAbilityModifierFromScore((_character.attributes.getAbilityScore(ability) + adjustment) & 0xff);
}

CreatureAttributes CharacterGeneration::levelledAttributes() const {
    auto levelled = _character.attributes;
    levelled.addClassLevels(levelled.classLevels().back().first, 1);
    return levelled;
}

std::vector<FeatType> CharacterGeneration::addLevelFeats() {
    auto levelled = levelledAttributes();
    auto partyLeader = _game.party().getLeader();
    auto granted = _services.game.feats.addGrantedFeats(levelled, partyLeader->racialType(), partyLeader->tag(), _game.isTSL(), false);
    for (auto feat : levelled.featOrder()) {
        _character.attributes.addFeat(feat);
    }
    return granted;
}

void CharacterGeneration::addLevelPowers() {
    auto partyLeader = _game.party().getLeader();
    auto levelled = levelledAttributes();
    _services.game.spells.addGrantedPowers(levelled, _game.isTSL());
    const ClassType newest = levelled.getEffectiveClass();
    for (auto power : levelled.spellsForClass(newest)) {
        if (partyLeader->canLearnPower(power, newest)) {
            _character.attributes.addSpell(power);
        }
    }
}

void CharacterGeneration::goToNextStep() {
    switch (_type) {
    case Type::LevelUp:
        _levelUp->goToNextStep();
        break;
    case Type::Custom:
        _custom->goToNextStep();
        break;
    default:
        _quick->goToNextStep();
        break;
    }
}

} // namespace game

} // namespace reone
