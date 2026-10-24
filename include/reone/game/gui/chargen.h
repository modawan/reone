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

#pragma once

#include <array>

#include "reone/gui/control/label.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/model.h"

#include "../character.h"
#include "../gui.h"

#include "chargen/abilities.h"
#include "chargen/classselect.h"
#include "chargen/custom.h"
#include "chargen/feats.h"
#include "chargen/levelup.h"
#include "chargen/nameentry.h"
#include "chargen/portraitselect.h"
#include "chargen/powers.h"
#include "chargen/quick.h"
#include "chargen/quickorcustom.h"
#include "chargen/skills.h"

namespace reone {

namespace gui {

class Label;

}

namespace game {

enum class CharGenScreen {
    ClassSelection,
    QuickOrCustom,
    Quick,
    PortraitSelection,
    Name,
    Custom,
    Abilities,
    Skills,
    Feats,
    Powers,
    LevelUp
};

class CharacterGeneration : public GameGUI {
public:
    enum class Type {
        Quick,
        Custom,
        LevelUp
    };

    CharacterGeneration(Game &game, ServicesView &services);

    bool handle(const input::Event &event) override;
    void update(float dt) override;
    void render() override;

    /**
     * Starts creating a new character: the panel returns to its creation
     * state, with no character yet, and opens the class selection with new
     * characters to choose from.
     */
    void startCreation();
    void startQuick();
    void startCustom();
    void startLevelUp();

    void finish();
    void cancel();

    void openClassSelection();
    void openNameEntry();
    void openAbilities();
    void openSkills();
    CharGenSkills &skills() { return *_charGenSkills; }
    CharGenFeats &feats() { return *_charGenFeats; }
    CharGenPowers &powers() { return *_charGenPowers; }

    /**
     * Opens the feats screen for the choice of the level's normal feats, or,
     * on a level-up that gains no normal feats, of its bonus feats.
     */
    void openFeats();
    /** Opens the feats screen for the choice of the level's bonus feats. */
    void openBonusFeats();
    void openPowers();
    void openPortraitSelection();
    void openQuick();
    void openQuickOrCustom();
    void openCustom();
    void openSteps();
    void openLevelUp();

    void goToNextStep();

    /**
     * Returns the new character's feats to those its class grants at its
     * first level, as abandoning its quick or custom creation does.
     */
    void resetCreationFeats();

    /**
     * Records that the abilities screen was accepted: from then on, a
     * level-up's attributes panel counts the character's chosen scores rather
     * than its effective ones.
     */
    void acceptAbilities();

    const Character &character() const { return _character; }

    /** The level-up character's attributes with its coming level. */
    CreatureAttributes levelledAttributes() const;

    void setCharacter(Character character);
    void setCharacterName(std::string name);

    const std::string &musicResRef() const {
        return _musicResRef;
    }

private:
    struct Controls {
        std::shared_ptr<gui::Label> CHA_AB_LBL;
        std::shared_ptr<gui::Label> CHA_LBL;
        std::shared_ptr<gui::Label> CON_AB_LBL;
        std::shared_ptr<gui::Label> CON_LBL;
        std::shared_ptr<gui::Label> DEF_ARROW_LBL;
        std::shared_ptr<gui::Label> DEX_AB_LBL;
        std::shared_ptr<gui::Label> DEX_LBL;
        std::shared_ptr<gui::Label> FORT_ARROW_LBL;
        std::shared_ptr<gui::Label> INT_AB_LBL;
        std::shared_ptr<gui::Label> INT_LBL;
        std::shared_ptr<gui::Label> LBL_BAR1;
        std::shared_ptr<gui::Label> LBL_BEVEL_L;
        std::shared_ptr<gui::Label> LBL_BEVEL_M;
        std::shared_ptr<gui::Label> LBL_BEVEL_R;
        std::shared_ptr<gui::Label> LBL_CLASS;
        std::shared_ptr<gui::Label> LBL_DEF;
        std::shared_ptr<gui::Label> LBL_DEF_NAME;
        std::shared_ptr<gui::Label> LBL_FORTITUDE;
        std::shared_ptr<gui::Label> LBL_LEVEL;
        std::shared_ptr<gui::Label> LBL_LEVEL_VAL;
        std::shared_ptr<gui::Label> LBL_NAME;
        std::shared_ptr<gui::Label> LBL_PORTBORDER;
        std::shared_ptr<gui::Label> LBL_REFLEX;
        std::shared_ptr<gui::Label> LBL_STATSBACK;
        std::shared_ptr<gui::Label> LBL_STATSBORDER;
        std::shared_ptr<gui::Label> LBL_VIT;
        std::shared_ptr<gui::Label> LBL_VIT_NAME;
        std::shared_ptr<gui::Label> LBL_WILL;
        std::shared_ptr<gui::Label> MAIN_TITLE_LBL;
        std::shared_ptr<gui::Label> MODEL_LBL;
        std::shared_ptr<gui::Label> NEW_DEF_LBL;
        std::shared_ptr<gui::Label> NEW_FORT_LBL;
        std::shared_ptr<gui::Label> NEW_LBL;
        std::shared_ptr<gui::Label> NEW_REFL_LBL;
        std::shared_ptr<gui::Label> NEW_VIT_LBL;
        std::shared_ptr<gui::Label> NEW_WILL_LBL;
        std::shared_ptr<gui::Label> OLD_DEF_LBL;
        std::shared_ptr<gui::Label> OLD_FORT_LBL;
        std::shared_ptr<gui::Label> OLD_LBL;
        std::shared_ptr<gui::Label> OLD_REFL_LBL;
        std::shared_ptr<gui::Label> OLD_VIT_LBL;
        std::shared_ptr<gui::Label> OLD_WILL_LBL;
        std::shared_ptr<gui::Label> PORTRAIT_LBL;
        std::shared_ptr<gui::Label> REFL_ARROW_LBL;
        std::shared_ptr<gui::Label> STR_AB_LBL;
        std::shared_ptr<gui::Label> STR_LBL;
        std::shared_ptr<gui::Label> VIT_ARROW_LBL;
        std::shared_ptr<gui::Label> WILL_ARROW_LBL;
        std::shared_ptr<gui::Label> WIS_AB_LBL;
        std::shared_ptr<gui::Label> WIS_LBL;
    };

    /**
     * The level-up character before its coming level, as the attributes panel
     * counts it.
     */
    struct LevelUpBase {
        int maxHitPoints {0};
        int vitalityConstitutionModifier {0};
        int defense {0};
        /** The effective Dexterity modifier, which the defense changes from. */
        int dexterityModifier {0};
        /**
         * What worn body armour takes off the Dexterity modifier; the Reflex
         * save counts it. Nothing while the character is debilitated.
         */
        int reflexDexterityLimit {0};
        /** Each ability's effective score, with its effects. */
        std::array<int, 6> effectiveScores {};
        /** The character's race, whose adjustments the chosen scores take. */
        RacialType race {RacialType::Unknown};
        /** Whether the abilities screen has been accepted. */
        bool abilitiesAccepted {false};
    };

    Controls _controls;

    std::string _musicResRef;

    CharGenScreen _screen {CharGenScreen::ClassSelection};
    Type _type {Type::Quick};
    Character _character;
    LevelUpBase _levelUpBase;
    /** The panel's title as the layout gives it, which creation shows. */
    std::string _creationTitle;

    // Sub GUI

    std::unique_ptr<ClassSelection> _classSelection;
    std::unique_ptr<QuickOrCustom> _quickOrCustom;
    std::unique_ptr<QuickCharacterGeneration> _quick;
    std::unique_ptr<CustomCharacterGeneration> _custom;
    std::unique_ptr<PortraitSelection> _portraitSelection;
    std::unique_ptr<CharGenAbilities> _abilities;
    std::unique_ptr<CharGenSkills> _charGenSkills;
    std::unique_ptr<CharGenFeats> _charGenFeats;
    std::unique_ptr<CharGenPowers> _charGenPowers;
    std::unique_ptr<NameEntry> _nameEntry;
    std::unique_ptr<LevelUpMenu> _levelUp;

    // END Sub GUI

    void onGUILoaded() override;

    void bindControls() {
        _controls.CHA_AB_LBL = findControl<gui::Label>("CHA_AB_LBL");
        _controls.CHA_LBL = findControl<gui::Label>("CHA_LBL");
        _controls.CON_AB_LBL = findControl<gui::Label>("CON_AB_LBL");
        _controls.CON_LBL = findControl<gui::Label>("CON_LBL");
        _controls.DEF_ARROW_LBL = findControl<gui::Label>("DEF_ARROW_LBL");
        _controls.DEX_AB_LBL = findControl<gui::Label>("DEX_AB_LBL");
        _controls.DEX_LBL = findControl<gui::Label>("DEX_LBL");
        _controls.FORT_ARROW_LBL = findControl<gui::Label>("FORT_ARROW_LBL");
        _controls.INT_AB_LBL = findControl<gui::Label>("INT_AB_LBL");
        _controls.INT_LBL = findControl<gui::Label>("INT_LBL");
        _controls.LBL_BAR1 = findControl<gui::Label>("LBL_BAR1");
        _controls.LBL_BEVEL_L = findControl<gui::Label>("LBL_BEVEL_L");
        _controls.LBL_BEVEL_M = findControl<gui::Label>("LBL_BEVEL_M");
        _controls.LBL_BEVEL_R = findControl<gui::Label>("LBL_BEVEL_R");
        _controls.LBL_CLASS = findControl<gui::Label>("LBL_CLASS");
        _controls.LBL_DEF = findControl<gui::Label>("LBL_DEF");
        _controls.LBL_DEF_NAME = findControl<gui::Label>("LBL_DEF_NAME");
        _controls.LBL_FORTITUDE = findControl<gui::Label>("LBL_FORTITUDE");
        _controls.LBL_LEVEL = findControl<gui::Label>("LBL_LEVEL");
        _controls.LBL_LEVEL_VAL = findControl<gui::Label>("LBL_LEVEL_VAL");
        _controls.LBL_NAME = findControl<gui::Label>("LBL_NAME");
        _controls.LBL_PORTBORDER = findControl<gui::Label>("LBL_PORTBORDER");
        _controls.LBL_REFLEX = findControl<gui::Label>("LBL_REFLEX");
        _controls.LBL_STATSBACK = findControl<gui::Label>("LBL_STATSBACK");
        _controls.LBL_STATSBORDER = findControl<gui::Label>("LBL_STATSBORDER");
        _controls.LBL_VIT = findControl<gui::Label>("LBL_VIT");
        _controls.LBL_VIT_NAME = findControl<gui::Label>("LBL_VIT_NAME");
        _controls.LBL_WILL = findControl<gui::Label>("LBL_WILL");
        _controls.MAIN_TITLE_LBL = findControl<gui::Label>("MAIN_TITLE_LBL");
        _controls.MODEL_LBL = findControl<gui::Label>("MODEL_LBL");
        _controls.NEW_DEF_LBL = findControl<gui::Label>("NEW_DEF_LBL");
        _controls.NEW_FORT_LBL = findControl<gui::Label>("NEW_FORT_LBL");
        _controls.NEW_LBL = findControl<gui::Label>("NEW_LBL");
        _controls.NEW_REFL_LBL = findControl<gui::Label>("NEW_REFL_LBL");
        _controls.NEW_VIT_LBL = findControl<gui::Label>("NEW_VIT_LBL");
        _controls.NEW_WILL_LBL = findControl<gui::Label>("NEW_WILL_LBL");
        _controls.OLD_DEF_LBL = findControl<gui::Label>("OLD_DEF_LBL");
        _controls.OLD_FORT_LBL = findControl<gui::Label>("OLD_FORT_LBL");
        _controls.OLD_LBL = findControl<gui::Label>("OLD_LBL");
        _controls.OLD_REFL_LBL = findControl<gui::Label>("OLD_REFL_LBL");
        _controls.OLD_VIT_LBL = findControl<gui::Label>("OLD_VIT_LBL");
        _controls.OLD_WILL_LBL = findControl<gui::Label>("OLD_WILL_LBL");
        _controls.PORTRAIT_LBL = findControl<gui::Label>("PORTRAIT_LBL");
        _controls.REFL_ARROW_LBL = findControl<gui::Label>("REFL_ARROW_LBL");
        _controls.STR_AB_LBL = findControl<gui::Label>("STR_AB_LBL");
        _controls.STR_LBL = findControl<gui::Label>("STR_LBL");
        _controls.VIT_ARROW_LBL = findControl<gui::Label>("VIT_ARROW_LBL");
        _controls.WILL_ARROW_LBL = findControl<gui::Label>("WILL_ARROW_LBL");
        _controls.WIS_AB_LBL = findControl<gui::Label>("WIS_AB_LBL");
        _controls.WIS_LBL = findControl<gui::Label>("WIS_LBL");
    }

    void reloadCharacterModel();
    void updateAttributes();
    /**
     * The modifier of \p ability as the attributes panel counts it: on a
     * level-up, from the effective score until the abilities screen is
     * accepted, and otherwise from the score with its race's adjustment, held
     * in a byte. A new character is human.
     */
    int panelAbilityModifier(Ability ability) const;
    /**
     * Takes back the new character's feats and gives it those its class
     * grants at its first level; \p newCharacter adds the player-character
     * feats and War Veteran a new character starts with.
     *
     * @return the feats the grant lists as granted
     */
    std::vector<FeatType> regrantCreationFeats(bool newCharacter);
    /**
     * Shows the level-up panel's title and level labels, or the creation
     * panel's title without them.
     */
    void showLevelUpTitle(bool levelUp);
    /**
     * Gives the level-up character the feats its coming level grants.
     *
     * @return the feats the grant lists as granted
     */
    std::vector<FeatType> addLevelFeats();
    /** Gives the level-up character the powers its coming level grants. */
    void addLevelPowers();
    void changeScreen(CharGenScreen screen);
    /**
     * Opens the feats screen. The choice of normal feats first grants the
     * level's feats; with nothing to choose, the screen accepts at once.
     */
    void openFeatsScreen(bool bonus);

    GameGUI *getSubGUI() const;
    std::shared_ptr<scene::ModelSceneNode> getCharacterModel(scene::ISceneGraph &sceneGraph);

    void setAttributesVisible(bool visible);

    // Loading

    void loadClassSelection();
    void loadQuickOrCustom();
    void loadQuick();
    void loadPortraitSelection();
    void loadNameEntry();
    void loadCustom();
    void loadAbilities();
    void loadSkills();
    void loadFeats();
    void loadPowers();
    void loadLevelUp();

    // END Loading
};

} // namespace game

} // namespace reone
