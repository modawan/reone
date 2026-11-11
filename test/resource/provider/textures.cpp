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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "reone/graphics/options.h"
#include "reone/resource/provider/textures.h"

#include "../../fixtures/resource.h"

using namespace reone;
using namespace reone::graphics;
using namespace reone::resource;

using testing::_;
using testing::Return;

// Each decode of a texture that doesn't exist looks for a TXI, a TGA and a TPC.

TEST(Textures, should_decode_a_texture_once_whatever_the_case_of_its_resref) {
    // given
    auto options = GraphicsOptions();
    auto resources = MockResources();
    EXPECT_CALL(resources, find(_)).Times(3).WillRepeatedly(Return(std::nullopt));
    auto textures = resource::Textures(options, resources);

    // when
    textures.get("Sample", TextureUsage::MainTex);
    textures.get("sample", TextureUsage::MainTex);
    textures.get("SAMPLE", TextureUsage::MainTex);
}

TEST(Textures, should_decode_a_texture_again_for_another_usage) {
    // given
    auto options = GraphicsOptions();
    auto resources = MockResources();
    EXPECT_CALL(resources, find(_)).Times(6).WillRepeatedly(Return(std::nullopt));
    auto textures = resource::Textures(options, resources);

    // when
    textures.get("sample", TextureUsage::GUI);
    textures.get("sample", TextureUsage::MainTex);
    textures.get("sample", TextureUsage::GUI);
}
