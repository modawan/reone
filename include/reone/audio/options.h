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

struct AudioOptions {
    /** Each volume is kept from 0 to this, a volume slider's length. */
    static constexpr int kMaxVolume = 100;
    /** The least 2D3D bias. */
    static constexpr float kMinBias2D3D = 0.1f;
    /** The greatest 2D3D bias. */
    static constexpr float kMaxBias2D3D = 1.9f;

    /** No sound is played, as the configuration's Disable Sound asks and unattended headless runs need. */
    bool muted {false};
    /**
     * Whether software sound is forced, as KotOR's advanced sound panel shows
     * and keeps it; no sound reads it.
     */
    bool forceSoftware {false};
    /** The volumes of music, voices, sound effects and movies, from 0 to kMaxVolume. */
    int musicVolume {85};
    int voiceVolume {85};
    int soundVolume {85};
    int movieVolume {85};
    /**
     * How positional sounds weigh against the others: below 1 quietens
     * positional sounds by it, above 1 quietens the others by 2 less it.
     * Kept from kMinBias2D3D to kMaxBias2D3D.
     */
    float bias2D3D {1.0f};
    /**
     * The game's voice counts: half the 2D voices play streams. Kept from
     * 24 and 16 to 64.
     */
    int voices2D {24};
    int voices3D {16};
    /** Streamed positional sounds reach the room's reverb. */
    bool streamedReverb {true};
    /** Unstreamed positional sounds reach the room's reverb, where the room has one. */
    bool unstreamedReverb {true};
};

} // namespace audio

} // namespace reone
