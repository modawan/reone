/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <gtest/gtest.h>

#include "reone/graphics/format/tgareader.h"
#include "reone/graphics/format/tgawriter.h"
#include "reone/graphics/texture.h"
#include "reone/system/exception/validation.h"
#include "reone/system/stream/memoryinput.h"

using namespace reone;
using namespace reone::graphics;

namespace {

uint8_t byteAt(const ByteBuffer &bytes, size_t index) {
    return static_cast<uint8_t>(bytes.at(index));
}

std::shared_ptr<Texture> readTga(ByteBuffer bytes) {
    MemoryInputStream input(bytes);
    TgaReader reader(input, "roundtrip", TextureUsage::Default);
    reader.load();
    return reader.texture();
}

} // namespace

TEST(TgaWriter, structurally_encodes_rgb_pixels_and_round_trips) {
    ByteBuffer rgb {
        static_cast<char>(0x11), static_cast<char>(0x22), static_cast<char>(0x33),
        static_cast<char>(0x44), static_cast<char>(0x55), static_cast<char>(0x66)};
    TgaWriter writer(2, 1, PixelFormat::RGB8, rgb);

    auto encoded = writer.toBytes();

    ASSERT_EQ(24u, encoded.size());
    EXPECT_EQ(2u, byteAt(encoded, 2));
    EXPECT_EQ(2u, byteAt(encoded, 12));
    EXPECT_EQ(0u, byteAt(encoded, 13));
    EXPECT_EQ(1u, byteAt(encoded, 14));
    EXPECT_EQ(0u, byteAt(encoded, 15));
    EXPECT_EQ(24u, byteAt(encoded, 16));
    EXPECT_EQ(0u, byteAt(encoded, 17));
    EXPECT_EQ(0x33u, byteAt(encoded, 18));
    EXPECT_EQ(0x22u, byteAt(encoded, 19));
    EXPECT_EQ(0x11u, byteAt(encoded, 20));
    EXPECT_EQ(0x66u, byteAt(encoded, 21));
    EXPECT_EQ(0x55u, byteAt(encoded, 22));
    EXPECT_EQ(0x44u, byteAt(encoded, 23));

    auto texture = readTga(encoded);
    ASSERT_TRUE(texture);
    EXPECT_EQ(2, texture->width());
    EXPECT_EQ(1, texture->height());
    EXPECT_EQ(PixelFormat::BGR8, texture->pixelFormat());
    EXPECT_EQ(ByteBuffer(encoded.begin() + 18, encoded.end()), *texture->layers()[0].pixels);
}

TEST(TgaWriter, structurally_encodes_rgba_pixels_with_explicit_top_left_origin) {
    ByteBuffer rgba {
        static_cast<char>(0x10), static_cast<char>(0x20),
        static_cast<char>(0x30), static_cast<char>(0x40)};
    TgaWriter writer(1, 1, PixelFormat::RGBA8, rgba, TgaOrigin::TopLeft);

    auto encoded = writer.toBytes();

    ASSERT_EQ(22u, encoded.size());
    EXPECT_EQ(32u, byteAt(encoded, 16));
    EXPECT_EQ(0x28u, byteAt(encoded, 17));
    EXPECT_EQ(0x30u, byteAt(encoded, 18));
    EXPECT_EQ(0x20u, byteAt(encoded, 19));
    EXPECT_EQ(0x10u, byteAt(encoded, 20));
    EXPECT_EQ(0x40u, byteAt(encoded, 21));

    auto texture = readTga(encoded);
    ASSERT_TRUE(texture);
    EXPECT_EQ(PixelFormat::BGRA8, texture->pixelFormat());
    EXPECT_EQ(ByteBuffer(encoded.begin() + 18, encoded.end()), *texture->layers()[0].pixels);
}

TEST(TgaWriter, output_is_deterministic_and_rle_round_trips) {
    ByteBuffer rgb {
        1, 2, 3,
        1, 2, 3,
        4, 5, 6};
    TgaWriter writer(3, 1, PixelFormat::RGB8, rgb);

    auto first = writer.toBytes(true);
    auto second = writer.toBytes(true);

    EXPECT_EQ(first, second);
    EXPECT_EQ(10u, byteAt(first, 2));
    auto texture = readTga(first);
    ASSERT_TRUE(texture);
    EXPECT_EQ((ByteBuffer {3, 2, 1, 3, 2, 1, 6, 5, 4}), *texture->layers()[0].pixels);
}

TEST(TgaWriter, encodes_save_screenshot_dimensions_without_a_fixture) {
    ByteBuffer pixels(256u * 256u * 3u, static_cast<char>(0x7f));
    auto encoded = TgaWriter(256, 256, PixelFormat::RGB8, std::move(pixels)).toBytes();

    EXPECT_EQ(18u + 256u * 256u * 3u, encoded.size());
    EXPECT_EQ(0u, byteAt(encoded, 12));
    EXPECT_EQ(1u, byteAt(encoded, 13));
    EXPECT_EQ(0u, byteAt(encoded, 14));
    EXPECT_EQ(1u, byteAt(encoded, 15));
    EXPECT_EQ(24u, byteAt(encoded, 16));
}

TEST(TgaWriter, rejects_invalid_dimensions_formats_and_payload_lengths) {
    EXPECT_THROW(TgaWriter(0, 1, PixelFormat::RGB8, {}).toBytes(), ValidationException);
    EXPECT_THROW(TgaWriter(1, 0, PixelFormat::RGB8, {}).toBytes(), ValidationException);
    EXPECT_THROW(TgaWriter(65536, 1, PixelFormat::RGB8, {}).toBytes(), ValidationException);
    EXPECT_THROW(TgaWriter(1, 65536, PixelFormat::RGBA8, {}).toBytes(), ValidationException);
    EXPECT_THROW(TgaWriter(2, 2, PixelFormat::RGB8, ByteBuffer(11)).toBytes(), ValidationException);
    EXPECT_THROW(TgaWriter(1, 1, PixelFormat::R8, ByteBuffer(1)).toBytes(), ValidationException);

    // The largest representable dimensions fail on the supplied length before
    // any multi-gigabyte allocation is attempted.
    EXPECT_THROW(TgaWriter(65535, 65535, PixelFormat::RGBA8, {}).toBytes(), ValidationException);
}
