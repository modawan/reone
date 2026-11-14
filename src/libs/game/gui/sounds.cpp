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

#include "reone/audio/mixer.h"
#include "reone/audio/source.h"
#include "reone/game/types.h"
#include "reone/resource/format/2dareader.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/audioclips.h"

using namespace reone::audio;
using namespace reone::resource;

namespace reone {

namespace game {

/** The guisounds.2da rows of the sounds played by row. */
static constexpr int kClickSoundRow = 0;
static constexpr int kEnterSoundRow = 1;
static constexpr int kErrorSoundRow = 2;
static constexpr int kCheckboxCheckSoundRow = 3;
static constexpr int kPanelAddedSoundRow = 4;
static constexpr int kPanelRemovedSoundRow = 5;
static constexpr int kActionMenuUseSoundRow = 6;
static constexpr int kActionMenuScrollSoundRow = 7;
static constexpr int kInventoryAddSoundRow = 9;
static constexpr int kInventorySelectSoundRow = 10;
static constexpr int kInventoryDropSoundRow = 11;
static constexpr int kLevelUpNotifySoundRow = 12;
static constexpr int kQuestNewNotifySoundRow = 13;
static constexpr int kQuestCompleteNotifySoundRow = 14;
static constexpr int kUpgradeSoundRow = 16;
static constexpr int kDarkSideSoundRow = 17;
static constexpr int kLightSideSoundRow = 18;

void GUISounds::init() {
    std::shared_ptr<TwoDA> sounds(_twoDas.get("guisounds"));
    if (!sounds) {
        return;
    }
    for (int row = 0; row < sounds->getRowCount(); ++row) {
        _sounds.push_back(_audioClips.get(sounds->getString(row, "soundresref")));
    }
}

void GUISounds::deinit() {
    _sources.clear();
    _sounds.clear();
}

GUISound GUISounds::getOnClick() const {
    return {kClickSoundRow};
}

GUISound GUISounds::getOnEnter() const {
    return {kEnterSoundRow};
}

GUISound GUISounds::getActionAccepted() const {
    return {kActionMenuUseSoundRow};
}

GUISound GUISounds::getActionUnavailable() const {
    return {kErrorSoundRow};
}

GUISound GUISounds::getActionMenuScroll() const {
    return {kActionMenuScrollSoundRow};
}

GUISound GUISounds::getOnLevelUpNotify() const {
    return {kLevelUpNotifySoundRow};
}

GUISound GUISounds::getQuestNewNotify() const {
    return {kQuestNewNotifySoundRow};
}

GUISound GUISounds::getQuestCompleteNotify() const {
    return {kQuestCompleteNotifySoundRow};
}

GUISound GUISounds::getCheckboxCheck() const {
    return {kCheckboxCheckSoundRow};
}

GUISound GUISounds::getInventoryAdd() const {
    return {kInventoryAddSoundRow};
}

GUISound GUISounds::getInventorySelect() const {
    return {kInventorySelectSoundRow};
}

GUISound GUISounds::getUpgrade() const {
    return {kUpgradeSoundRow};
}

GUISound GUISounds::getInventoryDrop() const {
    return {kInventoryDropSoundRow};
}

GUISound GUISounds::getAlignmentShift(bool light) const {
    return {light ? kLightSideSoundRow : kDarkSideSoundRow};
}

GUISound GUISounds::getPanelAdded() const {
    return {kPanelAddedSoundRow};
}

GUISound GUISounds::getPanelRemoved() const {
    return {kPanelRemovedSoundRow};
}

std::shared_ptr<AudioSource> GUISounds::play(GUISound sound) {
    auto clip = sound.row < static_cast<int>(_sounds.size()) ? _sounds[sound.row] : nullptr;
    auto &source = _sources[sound.row];
    if (source) {
        source->stop();
    }
    source = playInterfaceSound(_mixer, clip);
    return source;
}

// The interface keeps its sounds, so a full group that interrupts stops one.
std::shared_ptr<AudioSource> playInterfaceSound(IAudioMixer &mixer, std::shared_ptr<AudioClip> clip) {
    auto source = mixer.play(std::move(clip), AudioType::Sound, kGuiSoundGroup);
    if (source) {
        source->setOwned(true);
    }
    return source;
}

} // namespace game

} // namespace reone
