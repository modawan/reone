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

namespace reone {

namespace resource {

class AudioClips;
class TwoDA;
class TwoDAs;

} // namespace resource

namespace audio {

class AudioClip;
class AudioSource;
class IAudioMixer;

} // namespace audio

namespace game {

/** A sound of the interface: a row of guisounds.2da. */
struct GUISound {
    int row {0};
};

class IGUISounds {
public:
    virtual ~IGUISounds() = default;

    /** The sound of a click. */
    virtual GUISound getOnClick() const = 0;
    /** The sound of entering a control, or a list's row. */
    virtual GUISound getOnEnter() const = 0;
    /** The sound of an accepted action-menu selection. */
    virtual GUISound getActionAccepted() const = 0;
    /** The sound of a refused action-menu selection. */
    virtual GUISound getActionUnavailable() const = 0;
    /** The sound of an action menu scrolling to another entry. */
    virtual GUISound getActionMenuScroll() const = 0;
    /** The sound of a level-up becoming available. */
    virtual GUISound getOnLevelUpNotify() const = 0;
    /** The notice of a new or advanced quest on the status summary. */
    virtual GUISound getQuestNewNotify() const = 0;
    /** The notice of a completed quest on the status summary. */
    virtual GUISound getQuestCompleteNotify() const = 0;
    /** The click of a toggle button. */
    virtual GUISound getCheckboxCheck() const = 0;
    /** An item made and added to the inventory. */
    virtual GUISound getInventoryAdd() const = 0;
    /** The sound of an item chosen for an equipment slot. */
    virtual GUISound getInventorySelect() const = 0;
    /** An item's upgrades assembled at a workbench. */
    virtual GUISound getUpgrade() const = 0;
    /** The sound of an item cleared from an equipment slot. */
    virtual GUISound getInventoryDrop() const = 0;
    /** TSL: the sting of a light (true) or dark side shift on the status summary. */
    virtual GUISound getAlignmentShift(bool light) const = 0;
    /** The sounds of a full-screen panel shown and taken away. */
    virtual GUISound getPanelAdded() const = 0;
    virtual GUISound getPanelRemoved() const = 0;

    /**
     * Plays one of these sounds, as playInterfaceSound does. Each row plays
     * from a source of its own: playing it again while it plays starts it
     * over.
     *
     * @return the playing sound, or none when the row has no sound to play
     */
    virtual std::shared_ptr<audio::AudioSource> play(GUISound sound) = 0;
};

/**
 * The interface sounds, one for each row of guisounds.2da, played by row.
 */
class GUISounds : public IGUISounds, boost::noncopyable {
public:
    GUISounds(resource::AudioClips &audioClips, resource::TwoDAs &twoDas, audio::IAudioMixer &mixer) :
        _audioClips(audioClips),
        _twoDas(twoDas),
        _mixer(mixer) {
    }

    ~GUISounds() { deinit(); }

    void init();
    void deinit();

    GUISound getOnClick() const override;
    GUISound getOnEnter() const override;
    GUISound getActionAccepted() const override;
    GUISound getActionUnavailable() const override;
    GUISound getActionMenuScroll() const override;
    GUISound getOnLevelUpNotify() const override;
    GUISound getQuestNewNotify() const override;
    GUISound getQuestCompleteNotify() const override;
    GUISound getCheckboxCheck() const override;
    GUISound getInventoryAdd() const override;
    GUISound getInventorySelect() const override;
    GUISound getUpgrade() const override;
    GUISound getInventoryDrop() const override;
    GUISound getAlignmentShift(bool light) const override;
    GUISound getPanelAdded() const override;
    GUISound getPanelRemoved() const override;

    std::shared_ptr<audio::AudioSource> play(GUISound sound) override;

private:
    resource::AudioClips &_audioClips;
    resource::TwoDAs &_twoDas;
    audio::IAudioMixer &_mixer;

    /** By guisounds.2da row, the row's sound; none for a row without one. */
    std::vector<std::shared_ptr<audio::AudioClip>> _sounds;
    /** By guisounds.2da row, the source the row's sound last played from. */
    std::unordered_map<int, std::shared_ptr<audio::AudioSource>> _sources;
};

/**
 * Plays a sound of the interface's own. It is in the GUI's priority group,
 * which the menus and the player's pause keep playing.
 *
 * @return the playing sound, or none when there is no clip to play
 */
std::shared_ptr<audio::AudioSource> playInterfaceSound(audio::IAudioMixer &mixer, std::shared_ptr<audio::AudioClip> clip);

} // namespace game

} // namespace reone
