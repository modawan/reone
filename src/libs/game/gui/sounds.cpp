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

#include "reone/game/gui/sounds.h"

#include "reone/resource/format/2dareader.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/audioclips.h"

using namespace reone::audio;
using namespace reone::resource;

namespace reone {

namespace game {

/** The guisounds.2da rows of the error, checkbox, action-menu use and action-menu scroll sounds. */
static constexpr int kErrorSoundRow = 2;
static constexpr int kCheckboxCheckSoundRow = 3;
static constexpr int kActionMenuUseSoundRow = 6;
static constexpr int kActionMenuScrollSoundRow = 7;

void GUISounds::init() {
    std::shared_ptr<TwoDA> sounds(_twoDas.get("guisounds"));
    if (!sounds) {
        return;
    }
    loadSound(*sounds, "Clicked_Default", _onClick);
    loadSound(*sounds, "Entered_Default", _onEnter);
    loadSound(*sounds, "Level_Up_Notify", _onLevelUpNotify);
    // An item chosen for, or cleared from, an equipment slot.
    loadSound(*sounds, "Inventory_Select", _inventorySelect);
    loadSound(*sounds, "Inventory_Drop", _inventoryDrop);
    // TSL only: the light and dark side stings.
    loadSound(*sounds, "DarkSide", _darkSide);
    loadSound(*sounds, "LightSide", _lightSide);
    // A refused and an accepted action-menu selection, an action menu's
    // scroll, and a toggle button's click.
    _actionUnavailable = _audioClips.get(sounds->getString(kErrorSoundRow, "soundresref"));
    _actionAccepted = _audioClips.get(sounds->getString(kActionMenuUseSoundRow, "soundresref"));
    _actionMenuScroll = _audioClips.get(sounds->getString(kActionMenuScrollSoundRow, "soundresref"));
    _checkboxCheck = _audioClips.get(sounds->getString(kCheckboxCheckSoundRow, "soundresref"));
}

void GUISounds::loadSound(const TwoDA &twoDa, const std::string &label, std::shared_ptr<AudioClip> &sound) {
    int row = twoDa.indexByCellValue("label", label);
    if (row != -1) {
        sound = _audioClips.get(twoDa.getString(row, "soundresref"));
    }
}

void GUISounds::deinit() {
    _onClick.reset();
    _onEnter.reset();
    _onLevelUpNotify.reset();
    _actionAccepted.reset();
    _actionUnavailable.reset();
    _actionMenuScroll.reset();
    _checkboxCheck.reset();
    _inventorySelect.reset();
    _inventoryDrop.reset();
    _darkSide.reset();
    _lightSide.reset();
}

} // namespace game

} // namespace reone
