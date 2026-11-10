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

#include "reone/audio/context.h"

#include "reone/system/logutil.h"
#include "reone/system/threadutil.h"

#include "al.h"
#include "alc.h"

// Only OpenAL Soft has the effects extension, and its library exports the EFX
// functions. FindOpenAL defines R_ENABLE_EFX when it is the OpenAL found.
#ifdef R_ENABLE_EFX
#define AL_ALEXT_PROTOTYPES
#include "efx.h"
#endif

namespace reone {

namespace audio {

static_assert(std::is_same_v<ALuint, uint32_t>, "the context hands its uint32_t names to OpenAL as ALuint");

void Context::init() {
    checkMainThread();
    _device = alcOpenDevice(nullptr);
    if (!_device) {
        throw std::runtime_error("alcOpenDevice failed");
    }
    _context = alcCreateContext(static_cast<ALCdevice *>(_device), nullptr);
    if (!_context) {
        throw std::runtime_error("alcCreateContext failed");
    }
    alcMakeContextCurrent(static_cast<ALCcontext *>(_context));
    initReverb();
}

static const char *describeReverbTier(ReverbTier tier) {
    switch (tier) {
    case ReverbTier::None:
        return "none";
    case ReverbTier::Reverb:
        return "standard reverb";
    case ReverbTier::EaxReverb:
        return "EAX reverb";
    }
    return "none";
}

void Context::initReverb() {
#ifdef R_ENABLE_EFX
    ALCint sends = 0;
    if (alcIsExtensionPresent(static_cast<ALCdevice *>(_device), ALC_EXT_EFX_NAME) == ALC_TRUE) {
        alcGetIntegerv(static_cast<ALCdevice *>(_device), ALC_MAX_AUXILIARY_SENDS, 1, &sends);
    }
    if (sends > 0) {
        alGenEffects(1, &_reverbEffect);
        // An effect type the device does not render is refused with an
        // error, so the fullest is tried first.
        alGetError();
        alEffecti(_reverbEffect, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB);
        if (alGetError() == AL_NO_ERROR) {
            _reverbTier = ReverbTier::EaxReverb;
        } else {
            alEffecti(_reverbEffect, AL_EFFECT_TYPE, AL_EFFECT_REVERB);
            if (alGetError() == AL_NO_ERROR) {
                _reverbTier = ReverbTier::Reverb;
            }
        }
        // A device can be out of slots, and then there is no reverb either.
        if (_reverbTier != ReverbTier::None) {
            alGenAuxiliaryEffectSlots(1, &_reverbSlot);
            if (alGetError() != AL_NO_ERROR) {
                _reverbSlot = 0;
                _reverbTier = ReverbTier::None;
            }
        }
        if (_reverbTier == ReverbTier::None) {
            alDeleteEffects(1, &_reverbEffect);
            _reverbEffect = 0;
        } else {
            // The slot takes the effect's name as an ALint, the API's signature.
            alAuxiliaryEffectSloti(_reverbSlot, AL_EFFECTSLOT_EFFECT, static_cast<ALint>(_reverbEffect));
        }
    }
#endif
    info(std::string("Audio reverb: ") + describeReverbTier(_reverbTier));
}

void Context::deinitReverb() {
#ifdef R_ENABLE_EFX
    if (_reverbSlot) {
        alDeleteAuxiliaryEffectSlots(1, &_reverbSlot);
        _reverbSlot = 0;
    }
    if (_reverbEffect) {
        alDeleteEffects(1, &_reverbEffect);
        _reverbEffect = 0;
    }
#endif
    _reverbTier = ReverbTier::None;
}

void Context::deinit() {
    checkMainThread();
    if (_context) {
        deinitReverb();
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(static_cast<ALCcontext *>(_context));
        _context = nullptr;
    }
    if (_device) {
        alcCloseDevice(static_cast<ALCdevice *>(_device));
        _device = nullptr;
    }
}

void Context::setListenerPosition(glm::vec3 position) {
    if (_listenerPosition == position) {
        return;
    }
    alListener3f(AL_POSITION, position.x, position.y, position.z);
    _listenerPosition = std::move(position);
}

void Context::setListenerOrientation(glm::vec3 forward, glm::vec3 up) {
    if (_listenerForward == forward && _listenerUp == up) {
        return;
    }
    const float orientation[] {forward.x, forward.y, forward.z, up.x, up.y, up.z};
    alListenerfv(AL_ORIENTATION, orientation);
    _listenerForward = forward;
    _listenerUp = up;
}

void Context::setListenerGain(float gain) {
    alListenerf(AL_GAIN, gain);
}

} // namespace audio

} // namespace reone
