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

#include "reone/extract/finder.h"

using namespace reone;
using namespace reone::extract;

using graphics::TextureQuality;

TEST(Finder, canonical_order_prefers_custom_and_override_locations) {
    const SearchScope expected {
        SearchLocation::CustomFolders,
        SearchLocation::Override,
        SearchLocation::Lips,
        SearchLocation::Root,
        SearchLocation::CustomModules,
        SearchLocation::Modules,
        SearchLocation::Chitin,
        SearchLocation::Executable,
    };

    EXPECT_EQ(expected, canonicalSearchOrder());
}

TEST(Finder, texture_order_selects_pack_by_quality) {
    auto high = textureSearchOrder(TextureQuality::High);
    auto medium = textureSearchOrder(TextureQuality::Medium);
    auto low = textureSearchOrder(TextureQuality::Low);

    const SearchScope expectedHigh {
        SearchLocation::CustomFolders,
        SearchLocation::Override,
        SearchLocation::CustomModules,
        SearchLocation::TexturesGui,
        SearchLocation::TexturesTpa,
        SearchLocation::Chitin,
        SearchLocation::Executable,
    };
    EXPECT_EQ(expectedHigh, high);

    EXPECT_NE(medium.end(), std::find(medium.begin(), medium.end(), SearchLocation::TexturesTpb));
    EXPECT_EQ(medium.end(), std::find(medium.begin(), medium.end(), SearchLocation::TexturesTpa));

    EXPECT_NE(low.end(), std::find(low.begin(), low.end(), SearchLocation::TexturesTpc));
    EXPECT_EQ(low.end(), std::find(low.begin(), low.end(), SearchLocation::TexturesTpa));
}

TEST(Finder, sound_order_covers_all_stream_locations) {
    const auto &order = soundSearchOrder();

    auto posMusic = std::find(order.begin(), order.end(), SearchLocation::Music);
    auto posSound = std::find(order.begin(), order.end(), SearchLocation::Sound);
    auto posVoice = std::find(order.begin(), order.end(), SearchLocation::Voice);

    ASSERT_NE(order.end(), posMusic);
    ASSERT_NE(order.end(), posSound);
    ASSERT_NE(order.end(), posVoice);
    EXPECT_LT(posMusic, posSound);
    EXPECT_LT(posSound, posVoice);
}

TEST(Finder, special_purpose_orders) {
    const SearchScope expectedTalk {SearchLocation::Root};
    EXPECT_EQ(expectedTalk, talkTableSearchOrder());

    const auto &movies = movieSearchOrder();
    EXPECT_NE(movies.end(), std::find(movies.begin(), movies.end(), SearchLocation::Movies));
}

TEST(Finder, location_names_are_distinct_for_debugging) {
    EXPECT_STREQ("Override", searchLocationName(SearchLocation::Override));
    EXPECT_STREQ("Chitin", searchLocationName(SearchLocation::Chitin));
    EXPECT_STRNE(searchLocationName(SearchLocation::TexturesTpa),
                 searchLocationName(SearchLocation::TexturesTpb));
}
