/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <gtest/gtest.h>

#include "reone/graphics/texture.h"
#include "reone/gui/control.h"
#include "reone/gui/control/imagebutton.h"

using namespace reone;
using namespace reone::graphics;
using namespace reone::gui;

namespace {

std::shared_ptr<Texture> image(int width, int height) {
    auto texture = std::make_shared<Texture>("icon", TextureType::TwoDim, Texture::Properties {});
    texture->setPixels(width, height, PixelFormat::RGBA8, Texture::Layer {std::make_shared<ByteBuffer>(4 * width * height, 0)});
    return texture;
}

TEST(GUIFonts, a_layout_font_takes_its_small_or_large_variant_and_the_console_font_neither) {
    EXPECT_EQ("dialogfont16x16a", Control::fontName("dialogfont16x16", true));
    EXPECT_EQ("dialogfont16x16b", Control::fontName("dialogfont16x16", false));
    EXPECT_EQ("fnt_console", Control::fontName("fnt_console", true));
}

TEST(GUIFonts, an_icon_not_centred_covers_its_whole_square) {
    auto icon = image(20, 10);
    const auto rect = ImageButton::iconRect(*icon, false, 1.5f, {7, 9}, 24);
    EXPECT_EQ(glm::ivec2(7, 9), rect.first);
    EXPECT_EQ(glm::ivec2(24, 24), rect.second);
}

TEST(GUIFonts, a_centred_icon_keeps_its_art_size_within_the_square) {
    // The art at 1.5 is 30 by 15: as wide as the square at most, and centred.
    auto icon = image(20, 10);
    const auto rect = ImageButton::iconRect(*icon, true, 1.5f, {7, 9}, 24);
    EXPECT_EQ(glm::ivec2(24, 15), rect.second);
    EXPECT_EQ(glm::ivec2(7, 13), rect.first);
}

} // namespace
