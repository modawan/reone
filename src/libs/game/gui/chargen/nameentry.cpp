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

#include "reone/game/gui/chargen/nameentry.h"

#include <boost/algorithm/string.hpp>

#include "reone/game/game.h"
#include "reone/game/gui/chargen.h"
#include "reone/gui/control/button.h"
#include "reone/resource/provider/ltrs.h"
#include "reone/resource/strings.h"

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

static const ResRef kMaleNameLtrResRef("humanm");
static const ResRef kFemaleNameLtrResRef("humanf");
static const ResRef kLastNameLtrResRef("humanl");

/** The most characters a typed name holds. */
static constexpr size_t kMaxNameLength = 18;
static constexpr int kEmptyNameStrRef = 42360;

/** The controls the panel shows in both titles. */
static const std::unordered_set<std::string> kPanelControls {"BTN_BACK", "BTN_RANDOM", "END_BTN", "MAIN_TITLE_LBL", "NAME_BOX_EDIT", "SUB_TITLE_LBL"};
/** No further controls, for a title that names none of its own. */
static const std::unordered_set<std::string> kNoTitleControls;

void NameEntry::onGUILoaded() {
    bindControls();
    showOnlyNamedControls(kPanelControls, kNoTitleControls);
    _input.setMaxLength(kMaxNameLength);
    _input.setCodePage(codePageOfLanguage(_services.resource.strings.language()));

    _controls.BTN_RANDOM->setOnClick([this]() {
        loadRandomName();
    });
    _controls.END_BTN->setOnClick([this]() {
        confirm();
    });
    _controls.BTN_BACK->setOnClick([this]() {
        _charGen.openSteps();
    });
}

void NameEntry::reset() {
    const std::string &name = _charGen.character().name;
    setText(name);
    if (name.empty()) {
        loadRandomName();
    }
}

void NameEntry::confirm() {
    // Trailing spaces are dropped, and a name with nothing else is refused.
    std::string name(_buffer.str());
    boost::trim_right_if(name, boost::is_any_of(" "));
    if (name.empty()) {
        _game.showMessagePopup(_services.resource.strings.getText(kEmptyNameStrRef));
        return;
    }
    _charGen.setCharacterName(std::move(name));
    _charGen.goToNextStep();
    _charGen.openSteps();
}

bool NameEntry::handle(const input::Event &event) {
    if (event.type == input::EventType::KeyDown) {
        // Enter and the keypad's Enter confirm the name, as Done does.
        const auto key = event.key.code;
        if (key == input::KeyCode::Return || key == input::KeyCode::KeypadEnter) {
            confirm();
            return true;
        }
    }
    // The typed name stays in the box until Done; Back leaves the character's.
    if (_input.handle(event)) {
        _controls.NAME_BOX_EDIT->setTextMessage(std::string(_buffer.str()));
        return true;
    }
    return _gui->handle(event);
}

void NameEntry::loadRandomName() {
    auto gender = _charGen.character().gender;
    const auto &nameLtrResRef = (gender == Gender::Female) ? kFemaleNameLtrResRef : kMaleNameLtrResRef;
    auto nameLtr = _services.resource.ltrs.get(nameLtrResRef);
    auto lastNameLtr = _services.resource.ltrs.get(kLastNameLtrResRef);
    setText(nameLtr->randomName(8, {}) + " " + lastNameLtr->randomName(8, {}));
}

void NameEntry::setText(const std::string &text) {
    _input.setText(text);
    _controls.NAME_BOX_EDIT->setTextMessage(text);
}

} // namespace game

} // namespace reone
