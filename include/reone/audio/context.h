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

class IContext {
public:
    virtual ~IContext() = default;

    /** Where every positional sound is heard from. */
    virtual glm::vec3 listenerPosition() const = 0;

    virtual void setListenerPosition(glm::vec3 position) = 0;
    /** Turns the listener to face \p forward with \p up above it, both unit vectors. */
    virtual void setListenerOrientation(glm::vec3 forward, glm::vec3 up) = 0;
    /** Scales the gain of every sound. */
    virtual void setListenerGain(float gain) = 0;
};

/** The fullest reverb the device renders, which decides how much of an environment is heard. */
enum class ReverbTier {
    /** No effects extension or no auxiliary send: every sound is dry. */
    None,
    /** EFX's standard reverb, which carries EAX 2's properties. */
    Reverb,
    /** EFX's EAX reverb, which carries EAX 3's properties. */
    EaxReverb
};

class Context : public IContext, boost::noncopyable {
public:
    ~Context() { deinit(); }

    /**
     * Opens the default device and makes its context current, then sets up
     * the reverb slot at the fullest tier the device takes.
     *
     * Throws std::runtime_error when the device or context cannot be opened.
     * A device without effects is not an error: its tier is ReverbTier::None.
     */
    void init();
    void deinit();

    glm::vec3 listenerPosition() const override { return _listenerPosition; }

    void setListenerPosition(glm::vec3 position) override;
    void setListenerOrientation(glm::vec3 forward, glm::vec3 up) override;
    void setListenerGain(float gain) override;

    ReverbTier reverbTier() const { return _reverbTier; }

private:
    /**
     * OpenAL's device and context. They are held opaque so that this header
     * needs no OpenAL, whose implementations name the two types differently.
     */
    void *_device {nullptr};
    void *_context {nullptr};

    ReverbTier _reverbTier {ReverbTier::None};
    /** The reverb effect and the slot that renders it; 0 at ReverbTier::None. */
    uint32_t _reverbEffect {0};
    uint32_t _reverbSlot {0};

    glm::vec3 _listenerPosition {0.0f};
    glm::vec3 _listenerForward {0.0f};
    glm::vec3 _listenerUp {0.0f};

    void initReverb();
    void deinitReverb();
};

} // namespace audio

} // namespace reone
