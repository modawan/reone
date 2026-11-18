/*
 * Copyright (c) 2026 The reone project contributors
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

#include "audio.h"

#include "reone/system/threadutil.h"

namespace reone {

namespace audio {

MockMp3Reader::MockMp3Reader() = default;
MockMp3Reader::~MockMp3Reader() = default;

MockMp3ReaderFactory::MockMp3ReaderFactory() = default;
MockMp3ReaderFactory::~MockMp3ReaderFactory() = default;

MockContext::MockContext() = default;
MockContext::~MockContext() = default;

bool openTestContext(Context &context) {
#ifdef _WIN32
    _putenv_s("ALSOFT_DRIVERS", "null");
#else
    setenv("ALSOFT_DRIVERS", "null", 1);
#endif
    markMainThread();
    try {
        context.init();
    } catch (const std::runtime_error &) {
        return false;
    }
    return true;
}

std::shared_ptr<AudioClip> silentClip() {
    auto clip = std::make_shared<AudioClip>();
    AudioClip::Frame frame;
    frame.format = AudioFormat::Mono8;
    frame.sampleRate = 22050;
    frame.samples.assign(64, 0);
    clip->add(std::move(frame));
    return clip;
}

MockAudioMixer::MockAudioMixer() = default;
MockAudioMixer::~MockAudioMixer() = default;

} // namespace audio

} // namespace reone
