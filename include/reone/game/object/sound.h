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

#include <optional>

#include "../object.h"
#include "reone/resource/strings.h"

namespace reone {

namespace resource {

class Gff;

}

namespace game {

class Sound : public Object {
public:
    Sound(
        uint32_t id,
        std::string sceneName,
        Game &game,
        ServicesView &services) :
        Object(
            id,
            ObjectType::Sound,
            std::move(sceneName),
            game,
            services) {
    }

    static bool classof(const Object *from) {
        return from->type() == ObjectType::Sound;
    }

    void loadFromBlueprint(const std::string &resRef);
    void deserialize(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);

    void update(float dt) override;

    bool isActive() const { return _active; }

    glm::vec3 getPosition() const;

    float maxDistance() const { return _maxDistance; }
    float minDistance() const { return _minDistance; }
    bool continuous() const { return _continuous; }
    float elevation() const { return _elevation; }
    bool looping() const { return _looping; }
    float pitchVariation() const { return _pitchVariation; }
    bool positional() const { return _positional; }
    int volume() const { return _volume; }
    float fixedVariance() const { return _fixedVariance; }

    void setActive(bool active);
    /** 0-127, clamped. A clip already playing follows it, and so do later ones. */
    void setVolume(int volume);
    void setFixedVariance(float variance) { _fixedVariance = variance; }
    void setPitchVariation(float variation) { _pitchVariation = variation; }

    /**
     * Fades the sound out over \p seconds, then stops it, as
     * SoundObjectFadeAndStop does. A sound that is not playing, or a fade of
     * no length, stops at once. Stopping or playing the sound ends the fade.
     */
    void fadeAndStop(float seconds);
    bool isFading() const { return _fade.has_value(); }
    /** How far the fade has brought the volume down: 1 before a fade, towards 0 during one. */
    float fadeScale() const;
    /** Moves a fade on by \p dt seconds, stopping the sound when it ends. */
    void advanceFade(float dt);

private:
    friend class ModuleSnapshotBuilder;
    friend class TestGameModule;
    // Serializable
    resource::LocString _locName;
    bool _active {false};
    bool _positional {false};
    bool _looping {false};
    uint8_t _volume {0};
    uint8_t _volumeVrtn {0};
    uint8_t _times {0};
    float _pitchVariation {0.0f};
    uint32_t _hours {0};
    uint32_t _generatedType {0};
    uint32_t _interval {0};
    uint32_t _intervalVrtn {0};
    float _minDistance {0.0f};
    float _maxDistance {0.0f};
    bool _continuous {false};
    uint8_t _random {0};
    float _fixedVariance {0.0f};
    bool _randomPosition {false};
    float _randomRangeX {0.0f};
    float _randomRangeY {0.0f};
    uint8_t _priorityId {0};
    float _elevation {0.0f};
    // END Serializable

    int _soundIdx {-1};
    /** The next sound is the list's first. */
    bool _restartList {true};
    /** Seconds until the next sound: what is left of the playing one, then the interval. */
    float _timeout {0.0f};

    /** The random change to the volume of the clip on show (see nextVolumeShift). */
    int _clipVolumeShift {0};
    /** The gain the clip on show was started with, to scale it from. */
    float _playedGain {0.0f};

    struct Fade {
        float duration {0.0f};
        float remaining {0.0f};
    };
    // Not saved: a sound saved part way through a fade loads at its full
    // volume and still playing. These fades last a few seconds at most.
    std::optional<Fade> _fade;

    /** Whether the part of the day, or the hour, lets the sound play. */
    bool playsAtThisTime() const;
    /** Whether the sound plays its first sound as one loop rather than its list one by one. */
    bool loops() const;
    /** The interval before the next sound, varied at random by up to the interval variation either way. */
    float nextInterval() const;
    /** A random change to the volume for the next sound, by up to the volume variation either way. */
    int nextVolumeShift() const;
    /** The pitch for the next one-shot: the fixed variance, strayed by the pitch variation. */
    float nextPitch() const;
    /** The gain for the clip on show now: its volume, with its random change, scaled by any fade. */
    float currentGain() const;
    /** Makes a playing clip follow a change of volume or the fade. */
    void applyGainToPlayingClip();
    std::vector<std::string> _sounds;

    void deserializeAll(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);
    void loadAppearance();
    void updateTransform() override;
};

} // namespace game

} // namespace reone
