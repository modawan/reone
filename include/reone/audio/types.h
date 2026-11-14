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

namespace audio {

enum class AudioFormat {
    Mono8,
    Mono16,
    Stereo8,
    Stereo16
};

enum class AudioType {
    Music,
    Voice,
    Sound,
    Movie
};

/**
 * A listener environment, in the order EAX numbers its room types. Each one
 * is rendered from OpenAL Soft's preset of the same name.
 */
enum class Environment {
    Generic,
    PaddedCell,
    Room,
    Bathroom,
    LivingRoom,
    StoneRoom,
    Auditorium,
    ConcertHall,
    Cave,
    Arena,
    Hangar,
    CarpetedHallway,
    Hallway,
    StoneCorridor,
    Alley,
    Forest,
    City,
    Mountains,
    Quarry,
    Plain,
    ParkingLot,
    SewerPipe,
    Underwater
};

/** The loudest volume a sound, or a priority group, plays at. */
constexpr int kMaxVolume = 127;

/**
 * A sound priority group's row: its sounds' priority, the volume they play at,
 * the distances its positional sounds fall off over and how far their pitch
 * strays.
 */
struct PriorityGroup {
    /** The priority of the group's sounds among those competing to play, the lowest first. */
    int priority {0};
    /** The volume, out of kMaxVolume, that scales the volume of the group's sounds. */
    int volume {kMaxVolume};
    /** Within this distance of the listener a positional sound plays at full volume. */
    float fullVolumeDistance {10.0f};
    /** Beyond this distance from the listener a positional sound is not played. */
    float cutoffDistance {20.0f};
    /**
     * How far at random a sound of the group strays in pitch: up by up to
     * this fraction of its pitch, or down by up to half of it.
     */
    float playbackVariance {0.0f};
    /** How many of the group's sounds play at once. */
    int maxPlaying {255};
    /**
     * A new sound in a full group stops the group's newest sound and plays;
     * without this a new one-shot is not played and a new loop waits for
     * room.
     */
    bool interrupt {true};
};

/** The distances a positional sound falls off over. */
struct SoundDistances {
    /** Within this distance of the listener the sound plays at full volume. */
    float fullVolumeDistance {0.0f};
    /** Beyond this distance from the listener the sound is not played. */
    float cutoffDistance {0.0f};
};

} // namespace audio

} // namespace reone
