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
#include "reone/system/binarywriter.h"
#include "reone/system/stream/memoryinput.h"
#include "reone/system/stream/memoryoutput.h"
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

// Header fields as the reader takes them, padded to the 128 bytes the pixels
// start after.
static void writeTpcHeader(BinaryWriter &writer, uint32_t dataSize, uint16_t width, uint16_t height, uint8_t encoding, uint8_t numMipMaps) {
    writer.writeUint32(dataSize);
    writer.writeFloat(1.0f);
    writer.writeUint16(width);
    writer.writeUint16(height);
    writer.writeByte(encoding);
    writer.writeByte(numMipMaps);
    writer.write(128 - 14, 0);
}

static std::shared_ptr<Texture> readTpc(ByteBuffer &bytes) {
    auto tpc = MemoryInputStream(bytes);
    auto reader = TpcReader(tpc, "some_texture", TextureUsage::Default);
    reader.load();
    return reader.texture();
}

TEST(TpcReader, should_keep_the_mip_chain_the_file_ships) {
    // given
    // An uncompressed grayscale 4x4 texture with its 2x2 and 1x1 levels.
    ByteBuffer bytes;
    MemoryOutputStream stream(bytes);
    BinaryWriter writer(stream);
    writeTpcHeader(writer, 0, 4, 4, 1, 3);
    writer.write(16, 0x10);
    writer.write(4, 0x20);
    writer.write(1, 0x30);

    // when
    auto texture = readTpc(bytes);

    // then
    ASSERT_EQ(1ll, texture->layers().size());
    const auto &layer = texture->layers().front();
    EXPECT_EQ(ByteBuffer(16, 0x10), *layer.pixels);
    ASSERT_EQ(2ll, layer.mips.size());
    EXPECT_EQ(ByteBuffer(4, 0x20), *layer.mips[0]);
    EXPECT_EQ(ByteBuffer(1, 0x30), *layer.mips[1]);
}

TEST(TpcReader, should_drop_a_mip_chain_longer_than_the_file) {
    // given
    // A 4x4 DXT1 texture whose header claims a 16-byte base and three levels,
    // while the file holds three 8-byte blocks.
    ByteBuffer bytes;
    MemoryOutputStream stream(bytes);
    BinaryWriter writer(stream);
    writeTpcHeader(writer, 16, 4, 4, 2, 3);
    writer.write(24, 0x40);

    // when
    auto texture = readTpc(bytes);

    // then
    ASSERT_EQ(1ll, texture->layers().size());
    const auto &layer = texture->layers().front();
    EXPECT_EQ(16ll, layer.pixels->size());
    EXPECT_TRUE(layer.mips.empty());
}

TEST(TpcReader, should_keep_a_mip_chain_for_every_cube_face) {
    // given
    // An uncompressed grayscale 2x2 cube map: six faces, each a 2x2 base and
    // its 1x1 level, the face index in every byte.
    ByteBuffer bytes;
    MemoryOutputStream stream(bytes);
    BinaryWriter writer(stream);
    writeTpcHeader(writer, 0, 2, 12, 1, 2);
    for (int face = 0; face < 6; ++face) {
        writer.write(4, static_cast<uint8_t>(face));
        writer.write(1, static_cast<uint8_t>(face));
    }

    // when
    auto texture = readTpc(bytes);

    // then
    ASSERT_EQ(6ll, texture->layers().size());
    for (int face = 0; face < 6; ++face) {
        const auto &layer = texture->layers()[face];
        EXPECT_EQ(ByteBuffer(4, static_cast<char>(face)), *layer.pixels) << "face " << face;
        ASSERT_EQ(1ll, layer.mips.size()) << "face " << face;
        EXPECT_EQ(ByteBuffer(1, static_cast<char>(face)), *layer.mips[0]) << "face " << face;
    }
}
