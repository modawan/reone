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

#include "reone/game/gui/chargen/quick.h"

#include "reone/game/game.h"
#include "reone/game/gui/chargen.h"
#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

/** The controls the panel shows in both titles. */
static const std::unordered_set<std::string> kPanelControls {
    "BTN_BACK", "BTN_CANCEL", "BTN_STEPNAME1", "BTN_STEPNAME2", "BTN_STEPNAME3", "LBL_1",
    "LBL_2", "LBL_3", "LBL_NUM1", "LBL_NUM2", "LBL_NUM3"};
/** The controls the panel also shows in KotOR. */
static const std::unordered_set<std::string> kK1PanelControls {"LBL_DECORATION"};
/** No further controls, for a title that names none of its own. */
static const std::unordered_set<std::string> kNoTitleControls;

void QuickCharacterGeneration::onGUILoaded() {
    bindControls();
    showOnlyNamedControls(kPanelControls, _game.isTSL() ? kNoTitleControls : kK1PanelControls);

    doSetStep(0);

    _controls.BTN_CANCEL->setOnClick([this]() {
        setStep(0);
        _charGen.resetCreationFeats();
        _charGen.openQuickOrCustom();
    });
    _controls.BTN_BACK->setOnClick([this]() {
        if (_step == 0) {
            _charGen.resetCreationFeats();
            _charGen.openQuickOrCustom();
        } else {
            setStep(_step - 1);
        }
    });
    _controls.BTN_STEPNAME1->setOnClick([this]() {
        _charGen.openPortraitSelection();
    });
    _controls.BTN_STEPNAME2->setOnClick([this]() {
        _charGen.openNameEntry();
    });
    _controls.BTN_STEPNAME3->setOnClick([this]() {
        _charGen.finish();
    });
}

void QuickCharacterGeneration::setStep(int step) {
    if (_step != step) {
        doSetStep(step);
    }
}

void QuickCharacterGeneration::doSetStep(int step) {
    _step = step;

    _controls.LBL_1->setDisabled(_step != 0);
    _controls.LBL_2->setDisabled(_step != 1);
    _controls.LBL_3->setDisabled(_step != 2);
    _controls.BTN_STEPNAME1->setDisabled(_step != 0);
    _controls.BTN_STEPNAME2->setDisabled(_step != 1);
    _controls.BTN_STEPNAME3->setDisabled(_step != 2);

    _controls.LBL_1->setSelected(_step == 0);
    _controls.LBL_2->setSelected(_step == 1);
    _controls.LBL_3->setSelected(_step == 2);
    _controls.LBL_NUM1->setSelected(_step == 0);
    _controls.LBL_NUM2->setSelected(_step == 1);
    _controls.LBL_NUM3->setSelected(_step == 2);
    _controls.BTN_STEPNAME1->setSelected(_step == 0);
    _controls.BTN_STEPNAME2->setSelected(_step == 1);
    _controls.BTN_STEPNAME3->setSelected(_step == 2);

    // The current step's number and name pulse.
    _controls.LBL_NUM1->setTextPulsing(_step == 0);
    _controls.LBL_NUM2->setTextPulsing(_step == 1);
    _controls.LBL_NUM3->setTextPulsing(_step == 2);
    _controls.BTN_STEPNAME1->setTextPulsing(_step == 0);
    _controls.BTN_STEPNAME2->setTextPulsing(_step == 1);
    _controls.BTN_STEPNAME3->setTextPulsing(_step == 2);
}

void QuickCharacterGeneration::goToNextStep() {
    if (_step < 3) {
        doSetStep(_step + 1);
    }
}

} // namespace game

} // namespace reone
