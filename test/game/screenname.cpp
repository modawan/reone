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

#include <boost/algorithm/string.hpp>

#include "reone/game/game.h"

using namespace reone::game;

TEST(GameScreenFromName, should_name_the_screens_a_script_waits_for) {
    EXPECT_EQ(Game::screenFromName("ingame"), Game::Screen::InGame);
    EXPECT_EQ(Game::screenFromName("Conversation"), Game::Screen::Conversation);
    EXPECT_EQ(Game::screenFromName("CHARGEN"), Game::Screen::CharacterGeneration);
    EXPECT_EQ(Game::screenFromName("container"), Game::Screen::Container);
}

TEST(GameScreenFromName, should_not_name_anything_else) {
    EXPECT_FALSE(Game::screenFromName("").has_value());
    EXPECT_FALSE(Game::screenFromName("in game").has_value());
    EXPECT_FALSE(Game::screenFromName("dialog").has_value());
}

// Every name the usage message lists resolves, and they come in Screen order,
// so the list and the table cannot drift apart.
TEST(GameScreenFromName, should_resolve_every_listed_name_in_order) {
    std::vector<std::string> names;
    boost::split(names, std::string(Game::kScreenNames), boost::is_any_of(","));
    ASSERT_EQ(names.size(), static_cast<size_t>(Game::Screen::Death) + 1);
    for (size_t i = 0; i < names.size(); ++i) {
        auto name = boost::trim_copy(names[i]);
        auto screen = Game::screenFromName(name);
        ASSERT_TRUE(screen.has_value()) << name;
        EXPECT_EQ(static_cast<size_t>(*screen), i) << name;
    }
}

// A developer save is refused where F4 would not quicksave: during a
// conversation, a save would drop the conversation and the scripts left on it.
TEST(GameDeveloperSave, should_save_only_on_the_in_game_screen) {
    EXPECT_TRUE(Game::isDeveloperSaveAllowedOn(Game::Screen::InGame));
    EXPECT_FALSE(Game::isDeveloperSaveAllowedOn(Game::Screen::Conversation));
    EXPECT_FALSE(Game::isDeveloperSaveAllowedOn(Game::Screen::Container));
    EXPECT_FALSE(Game::isDeveloperSaveAllowedOn(Game::Screen::MainMenu));
}
