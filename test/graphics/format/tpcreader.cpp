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

#include <gtest/gtest.h>

#include "reone/graphics/format/tpcreader.h"
#include "reone/system/stream/memoryinput.h"
#include "reone/system/stringbuilder.h"

using namespace reone;
using namespace reone::graphics;

TEST(TpcReader, should_load_tpc) {
    // given
    auto tpcBytes = StringBuilder()
                        // Header
                        .append("\x00\x00\x00\x00", 4) // data size
                        .append("\x00\x00\x00\x00", 4) // unknown
                        .append("\x01\x00", 2)         // width
                        .append("\x01\x00", 2)         // height
                        .append("\x01\x00", 2)         // encoding
                        .append("\x01\x00", 2)         // number of mip maps
                        .append('\x00', 112)           // padding
                        // Mip Map 1
                        .append("\xff", 1)
                        .string();
    auto tpc = MemoryInputStream(tpcBytes);
    auto reader = TpcReader(tpc, "some_texture", TextureUsage::Default);

    // when
    reader.load();

    // then
    auto texture = reader.texture();
    EXPECT_TRUE(static_cast<bool>(texture));
    EXPECT_EQ(std::string("some_texture"), texture->name());
    EXPECT_EQ(1, texture->width());
    EXPECT_EQ(1, texture->height());
    EXPECT_EQ(1ll, texture->layers().size());
    EXPECT_TRUE(static_cast<bool>(texture->layers()[0].pixels));
    auto pixels = reinterpret_cast<unsigned char *>(texture->layers()[0].pixels->data());
    EXPECT_EQ(255, pixels[0]);
}

namespace {

// The size of a DXT1 image of the given size together with all of its mip
// maps, down to 1x1.
size_t dxt1ChainSize(int width, int height) {
    size_t size = 0;
    while (true) {
        size += std::max(8, ((width + 3) / 4) * ((height + 3) / 4) * 8);
        if (width == 1 && height == 1) {
            break;
        }
        width = std::max(1, width / 2);
        height = std::max(1, height / 2);
    }
    return size;
}

std::string uint32Bytes(uint32_t value) {
    std::string bytes(4, '\0');
    for (int i = 0; i < 4; ++i) {
        bytes[i] = static_cast<char>((value >> (8 * i)) & 0xff);
    }
    return bytes;
}

std::string dxt1TpcHeader(uint32_t dataSize, int width, int height) {
    return StringBuilder()
        .append(uint32Bytes(dataSize))                          // data size
        .append("\x00\x00\x00\x00", 4)                          // unknown
        .append(static_cast<char>(width & 0xff))                // width
        .append(static_cast<char>((width >> 8) & 0xff))         //
        .append(static_cast<char>(height & 0xff))               // height
        .append(static_cast<char>((height >> 8) & 0xff))        //
        .append('\x02')                                         // encoding: DXT1
        .append('\x01')                                         // number of mip maps
        .append('\x00', 114)                                    // padding
        .string();
}

// The byte at offset in a frame's mip chain: distinct across frames, offsets
// and mip maps, so a frame read from the wrong place does not match.
char frameByte(int frame, size_t offset) {
    return static_cast<char>((frame * 61 + offset * 7 + offset / 251) & 0xff);
}

// A 256x256 DXT1 TPC whose frames are laid out the way the game's cycling
// TPCs are (e.g. door_field, the Endar Spire force field): four 128x128
// frames one after another, each with its own mip chain, and a data size that
// spans all of them.
std::string fourFrameTpc(const std::string &txi) {
    size_t chainSize = dxt1ChainSize(128, 128);
    std::string pixels;
    for (int frame = 0; frame < 4; ++frame) {
        for (size_t offset = 0; offset < chainSize; ++offset) {
            pixels.push_back(frameByte(frame, offset));
        }
    }
    return dxt1TpcHeader(static_cast<uint32_t>(pixels.size()), 256, 256) + pixels + txi;
}

} // namespace

TEST(TpcReader, should_load_frames_of_cycling_tpc_as_layers_of_texture_array) {
    // given
    ASSERT_EQ(static_cast<size_t>(43744), 4 * dxt1ChainSize(128, 128)); // door_field's data size
    auto tpcBytes = fourFrameTpc("blending additive\r\nproceduretype cycle\r\nnumx 2\r\nnumy 2\r\nfps 16\r\n\r\ndecal 1\r\n");
    auto tpc = MemoryInputStream(tpcBytes);
    auto reader = TpcReader(tpc, "door_field", TextureUsage::Default);

    // when
    reader.load();

    // then
    auto texture = reader.texture();
    ASSERT_TRUE(static_cast<bool>(texture));
    EXPECT_EQ(TextureType::TwoDimArray, texture->type());
    EXPECT_EQ(PixelFormat::DXT1, texture->pixelFormat());
    EXPECT_EQ(128, texture->width());
    EXPECT_EQ(128, texture->height());
    EXPECT_EQ(Texture::ProcedureType::Cycle, texture->features().procedureType);
    EXPECT_EQ(Texture::Blending::Additive, texture->features().blending);
    EXPECT_TRUE(texture->features().decal);
    ASSERT_EQ(4ll, texture->layers().size());
    size_t chainSize = dxt1ChainSize(128, 128);
    size_t frameSize = (128 / 4) * (128 / 4) * 8;
    for (int frame = 0; frame < 4; ++frame) {
        const auto &pixels = texture->layers()[frame].pixels;
        ASSERT_TRUE(static_cast<bool>(pixels)) << "frame " << frame;
        ASSERT_EQ(frameSize, pixels->size()) << "frame " << frame;
        auto expected = std::string(tpcBytes.data() + 128 + frame * chainSize, frameSize);
        EXPECT_EQ(expected, std::string(pixels->data(), pixels->size())) << "frame " << frame;
    }
}

TEST(TpcReader, should_keep_tpc_as_one_image_when_its_txi_does_not_cycle) {
    // given
    auto tpcBytes = fourFrameTpc("blending additive\r\n");
    auto tpc = MemoryInputStream(tpcBytes);
    auto reader = TpcReader(tpc, "some_texture", TextureUsage::Default);

    // when
    reader.load();

    // then
    auto texture = reader.texture();
    ASSERT_TRUE(static_cast<bool>(texture));
    EXPECT_EQ(TextureType::TwoDim, texture->type());
    EXPECT_EQ(256, texture->width());
    EXPECT_EQ(256, texture->height());
    EXPECT_EQ(1ll, texture->layers().size());
}

TEST(TpcReader, should_keep_cycling_tpc_as_one_image_when_its_data_is_not_a_run_of_frames) {
    // given: one 256x256 image with its full mip chain, as a grid of frames
    // in one image would be stored
    size_t chainSize = dxt1ChainSize(256, 256);
    auto tpcBytes = dxt1TpcHeader(static_cast<uint32_t>(chainSize), 256, 256) +
                    std::string(chainSize, '\x5a') +
                    "proceduretype cycle\r\nnumx 2\r\nnumy 2\r\nfps 16\r\n";
    auto tpc = MemoryInputStream(tpcBytes);
    auto reader = TpcReader(tpc, "some_texture", TextureUsage::Default);

    // when
    reader.load();

    // then
    auto texture = reader.texture();
    ASSERT_TRUE(static_cast<bool>(texture));
    EXPECT_EQ(TextureType::TwoDim, texture->type());
    EXPECT_EQ(256, texture->width());
    EXPECT_EQ(1ll, texture->layers().size());
    EXPECT_EQ(Texture::ProcedureType::Cycle, texture->features().procedureType);
}
