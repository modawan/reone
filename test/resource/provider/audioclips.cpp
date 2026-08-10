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

/**
 * Where a streamed asset is resolved from.
 *
 * The streaming directories are not part of the Odyssey raw lookup order, so
 * this precedence belongs to the streaming subsystem rather than to a bucket.
 */

#include <gtest/gtest.h>

#include "reone/audio/clip.h"
#include "reone/resource/format/erfwriter.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/resources.h"
#include "reone/system/stream/memoryoutput.h"
#include "reone/system/stringbuilder.h"

using namespace reone;
using namespace reone::resource;

namespace {

/// A one-sample mono WAV whose sample rate identifies the source it came from.
ByteBuffer wavBytes(uint32_t sampleRate) {
    std::string rate;
    for (int i = 0; i < 4; ++i) {
        rate.push_back(static_cast<char>((sampleRate >> (8 * i)) & 0xff));
    }
    auto data = StringBuilder()
                    .append("RIFF")
                    .append("\x00\x00\x00\x00", 4)
                    .append("WAVE")
                    .append("fmt ")
                    .append("\x10\x00\x00\x00", 4)
                    .append("\x01\x00", 2)
                    .append("\x01\x00", 2)
                    .append(rate.data(), 4)
                    .append("\x00\x00\x00\x00", 4)
                    .append("\x00\x00", 2)
                    .append("\x08\x00", 2)
                    .append("data")
                    .append("\x02\x00\x00\x00", 4)
                    .append("\xff\x7f", 2)
                    .string();
    return ByteBuffer(data.begin(), data.end());
}

ByteBuffer erfWith(const std::string &resRef, ResType type, ByteBuffer data) {
    ErfWriter writer;
    writer.add(ErfWriter::Resource {resRef, type, std::move(data)});
    ByteBuffer buffer;
    MemoryOutputStream stream(buffer);
    writer.save(ErfWriter::FileType::ERF, stream);
    return buffer;
}

constexpr uint32_t kStreamRate = 22050;
constexpr uint32_t kOrdinaryRate = 44100;

} // namespace

TEST(AudioClips, resolves_a_streamed_asset_from_the_streaming_location_first) {
    // A retail K2 installation ships hundreds of voice lines in both the
    // streaming directories and the key tables. The streamed copy is the one
    // that must play, so an ordinary source may not take its place.
    Resources ordinary;
    Resources streams;
    ordinary.addMemERF(erfWith("vo", ResType::Wav, wavBytes(kOrdinaryRate)), ContainerKind::Global);
    streams.addMemERF(erfWith("vo", ResType::Wav, wavBytes(kStreamRate)), ContainerKind::Global);

    AudioClips clips(ordinary, streams);
    auto clip = clips.get("vo");

    ASSERT_TRUE(static_cast<bool>(clip));
    ASSERT_EQ(1, clip->getFrameCount());
    EXPECT_EQ(kStreamRate, clip->getFrame(0).sampleRate);
}

TEST(AudioClips, falls_back_to_ordinary_resources_for_anything_not_streamed) {
    Resources ordinary;
    Resources streams;
    ordinary.addMemERF(erfWith("sfx", ResType::Wav, wavBytes(kOrdinaryRate)), ContainerKind::Global);

    AudioClips clips(ordinary, streams);
    auto clip = clips.get("sfx");

    ASSERT_TRUE(static_cast<bool>(clip));
    EXPECT_EQ(kOrdinaryRate, clip->getFrame(0).sampleRate);
}

TEST(AudioClips, reports_a_clip_that_neither_location_holds) {
    Resources ordinary;
    Resources streams;

    AudioClips clips(ordinary, streams);

    EXPECT_FALSE(static_cast<bool>(clips.get("missing")));
}
