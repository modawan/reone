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

#include <gtest/gtest.h>

#include "reone/graphics/dxtutil.h"

using namespace reone;
using namespace reone::graphics;

// Arbitrary, but distinct from the white the blocks below decode to.
static constexpr uint32_t kUntouched = 0xdeadbeef;

// A 4x2 image is one block high, so the decoder has two block rows that fall
// outside the image. The buffer has room for the whole block, so a row written
// past the image lands where the test can see it.

TEST(DxtUtil, should_decompress_dxt1_without_writing_past_the_last_row) {
    // given
    // Both colours white and every index 0.
    const uint8_t block[] = {0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00};
    auto image = std::vector<uint32_t>(4 * 4, kUntouched);

    // when
    decompressDXT1(4, 2, block, image.data());

    // then
    for (size_t i = 0; i < 4 * 2; ++i) {
        EXPECT_NE(kUntouched, image[i]) << "pixel " << i;
    }
    for (size_t i = 4 * 2; i < image.size(); ++i) {
        EXPECT_EQ(kUntouched, image[i]) << "pixel " << i;
    }
}

TEST(DxtUtil, should_decompress_dxt5_without_writing_past_the_last_row) {
    // given
    // Both alphas opaque, then a DXT1 colour block as above.
    const uint8_t block[] = {
        0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00};
    auto image = std::vector<uint32_t>(4 * 4, kUntouched);

    // when
    decompressDXT5(4, 2, block, image.data());

    // then
    for (size_t i = 0; i < 4 * 2; ++i) {
        EXPECT_NE(kUntouched, image[i]) << "pixel " << i;
    }
    for (size_t i = 4 * 2; i < image.size(); ++i) {
        EXPECT_EQ(kUntouched, image[i]) << "pixel " << i;
    }
}
