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

#include "reone/audio/source.h"
#include "reone/system/logutil.h"
#include "reone/system/threadutil.h"

#include "al.h"
#include "alc.h"

// Only OpenAL Soft has the effects extension, and its library exports the EFX
// functions. FindOpenAL defines R_ENABLE_EFX when it is the OpenAL found.
#ifdef R_ENABLE_EFX
#define AL_ALEXT_PROTOTYPES
#include "efx-presets.h"
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

#ifdef R_ENABLE_EFX
// Indexed by Environment.
static const EFXEAXREVERBPROPERTIES kEnvironmentPresets[] {
    EFX_REVERB_PRESET_GENERIC,
    EFX_REVERB_PRESET_PADDEDCELL,
    EFX_REVERB_PRESET_ROOM,
    EFX_REVERB_PRESET_BATHROOM,
    EFX_REVERB_PRESET_LIVINGROOM,
    EFX_REVERB_PRESET_STONEROOM,
    EFX_REVERB_PRESET_AUDITORIUM,
    EFX_REVERB_PRESET_CONCERTHALL,
    EFX_REVERB_PRESET_CAVE,
    EFX_REVERB_PRESET_ARENA,
    EFX_REVERB_PRESET_HANGAR,
    EFX_REVERB_PRESET_CARPETEDHALLWAY,
    EFX_REVERB_PRESET_HALLWAY,
    EFX_REVERB_PRESET_STONECORRIDOR,
    EFX_REVERB_PRESET_ALLEY,
    EFX_REVERB_PRESET_FOREST,
    EFX_REVERB_PRESET_CITY,
    EFX_REVERB_PRESET_MOUNTAINS,
    EFX_REVERB_PRESET_QUARRY,
    EFX_REVERB_PRESET_PLAIN,
    EFX_REVERB_PRESET_PARKINGLOT,
    EFX_REVERB_PRESET_SEWERPIPE,
    EFX_REVERB_PRESET_UNDERWATER};
static_assert(std::size(kEnvironmentPresets) == static_cast<size_t>(Environment::Underwater) + 1);

static void loadEaxReverb(ALuint effect, const EFXEAXREVERBPROPERTIES &preset) {
    alEffectf(effect, AL_EAXREVERB_DENSITY, preset.flDensity);
    alEffectf(effect, AL_EAXREVERB_DIFFUSION, preset.flDiffusion);
    alEffectf(effect, AL_EAXREVERB_GAIN, preset.flGain);
    alEffectf(effect, AL_EAXREVERB_GAINHF, preset.flGainHF);
    alEffectf(effect, AL_EAXREVERB_GAINLF, preset.flGainLF);
    alEffectf(effect, AL_EAXREVERB_DECAY_TIME, preset.flDecayTime);
    alEffectf(effect, AL_EAXREVERB_DECAY_HFRATIO, preset.flDecayHFRatio);
    alEffectf(effect, AL_EAXREVERB_DECAY_LFRATIO, preset.flDecayLFRatio);
    alEffectf(effect, AL_EAXREVERB_REFLECTIONS_GAIN, preset.flReflectionsGain);
    alEffectf(effect, AL_EAXREVERB_REFLECTIONS_DELAY, preset.flReflectionsDelay);
    alEffectfv(effect, AL_EAXREVERB_REFLECTIONS_PAN, preset.flReflectionsPan);
    alEffectf(effect, AL_EAXREVERB_LATE_REVERB_GAIN, preset.flLateReverbGain);
    alEffectf(effect, AL_EAXREVERB_LATE_REVERB_DELAY, preset.flLateReverbDelay);
    alEffectfv(effect, AL_EAXREVERB_LATE_REVERB_PAN, preset.flLateReverbPan);
    alEffectf(effect, AL_EAXREVERB_ECHO_TIME, preset.flEchoTime);
    alEffectf(effect, AL_EAXREVERB_ECHO_DEPTH, preset.flEchoDepth);
    alEffectf(effect, AL_EAXREVERB_MODULATION_TIME, preset.flModulationTime);
    alEffectf(effect, AL_EAXREVERB_MODULATION_DEPTH, preset.flModulationDepth);
    alEffectf(effect, AL_EAXREVERB_AIR_ABSORPTION_GAINHF, preset.flAirAbsorptionGainHF);
    alEffectf(effect, AL_EAXREVERB_HFREFERENCE, preset.flHFReference);
    alEffectf(effect, AL_EAXREVERB_LFREFERENCE, preset.flLFReference);
    alEffectf(effect, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, preset.flRoomRolloffFactor);
    alEffecti(effect, AL_EAXREVERB_DECAY_HFLIMIT, preset.iDecayHFLimit);
}

// Standard reverb has no LF, echo, modulation or pan controls, and no
// reference frequencies. The rest it shares with EAX reverb.
static void loadStandardReverb(ALuint effect, const EFXEAXREVERBPROPERTIES &preset) {
    alEffectf(effect, AL_REVERB_DENSITY, preset.flDensity);
    alEffectf(effect, AL_REVERB_DIFFUSION, preset.flDiffusion);
    alEffectf(effect, AL_REVERB_GAIN, preset.flGain);
    alEffectf(effect, AL_REVERB_GAINHF, preset.flGainHF);
    alEffectf(effect, AL_REVERB_DECAY_TIME, preset.flDecayTime);
    alEffectf(effect, AL_REVERB_DECAY_HFRATIO, preset.flDecayHFRatio);
    alEffectf(effect, AL_REVERB_REFLECTIONS_GAIN, preset.flReflectionsGain);
    alEffectf(effect, AL_REVERB_REFLECTIONS_DELAY, preset.flReflectionsDelay);
    alEffectf(effect, AL_REVERB_LATE_REVERB_GAIN, preset.flLateReverbGain);
    alEffectf(effect, AL_REVERB_LATE_REVERB_DELAY, preset.flLateReverbDelay);
    alEffectf(effect, AL_REVERB_AIR_ABSORPTION_GAINHF, preset.flAirAbsorptionGainHF);
    alEffectf(effect, AL_REVERB_ROOM_ROLLOFF_FACTOR, preset.flRoomRolloffFactor);
    alEffecti(effect, AL_REVERB_DECAY_HFLIMIT, preset.iDecayHFLimit);
}
#endif

void Context::setEnvironment(Environment environment) {
#ifdef R_ENABLE_EFX
    const auto &preset = kEnvironmentPresets[static_cast<size_t>(environment)];
    switch (_reverbTier) {
    case ReverbTier::None:
        return;
    case ReverbTier::Reverb:
        loadStandardReverb(_reverbEffect, preset);
        break;
    case ReverbTier::EaxReverb:
        loadEaxReverb(_reverbEffect, preset);
        break;
    }
    // A slot renders its own copy of the effect, so changed properties reach
    // it only when the effect is loaded into it again. It takes the effect's
    // name as an ALint, the API's signature.
    alAuxiliaryEffectSloti(_reverbSlot, AL_EFFECTSLOT_EFFECT, static_cast<ALint>(_reverbEffect));
#else
    // Without the effects extension there is no environment to render.
    static_cast<void>(environment);
#endif
}

void Context::setReverbSend(AudioSource &source, float gain) {
#ifdef R_ENABLE_EFX
    const ALuint name = source.alSource();
    if (_reverbTier == ReverbTier::None || name == 0) {
        return;
    }
    if (gain <= 0.0f) {
        alSource3i(name, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL);
        return;
    }
    // A source keeps a copy of the filter it is given, so one filter serves
    // every send.
    alFilterf(_reverbSendFilter, AL_LOWPASS_GAIN, std::min(gain, 1.0f));
    alSource3i(name, AL_AUXILIARY_SEND_FILTER, static_cast<ALint>(_reverbSlot), 0, static_cast<ALint>(_reverbSendFilter));
#else
    static_cast<void>(source);
    static_cast<void>(gain);
#endif
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
            setEnvironment(Environment::Generic);
            alGenFilters(1, &_reverbSendFilter);
            alFilteri(_reverbSendFilter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
        }
    }
#endif
    info(std::string("Audio reverb: ") + describeReverbTier(_reverbTier));
}

void Context::deinitReverb() {
#ifdef R_ENABLE_EFX
    if (_reverbSendFilter) {
        alDeleteFilters(1, &_reverbSendFilter);
        _reverbSendFilter = 0;
    }
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
