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

#include "../fixtures/engine.h"
#include "reone/game/action/usetalentonobject.h"
#include "reone/game/game.h"
#include "reone/resource/types.h"

using namespace reone;
using namespace reone::game;
using namespace testing;

TEST(Action, use_talent_dispatch_to_use_feat) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    auto player = game.newCreature();
    auto target = game.newCreature();
    auto talent = game.newTalent(TalentType::Feat, static_cast<int>(FeatType::PowerAttack));
    auto action = game.newAction<UseTalentOnObjectAction>(std::move(talent), target);
    auto subAction = action->subAction();
    ASSERT_TRUE(subAction);

    EXPECT_FALSE(action->isCompleted());
    EXPECT_FALSE(subAction->isCompleted());

    // Cycle through combat states
    for (int i = 0; i < 10; ++i) {
        action->execute(action, *target, 1.0f);
        game.combat().update(2.0f);
    }

    EXPECT_TRUE(action->isCompleted());
    EXPECT_TRUE(subAction->isCompleted());
}

TEST(Action, use_talent_dispatch_to_cast_spell) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    auto spell = std::make_shared<Spell>();
    spell->type = SpellType::LightSaberThrow;
    spell->castTime = 1.0f;
    spell->conjTime = 1.0f;

    EXPECT_CALL(engine.gameModule().spells(), get(SpellType::LightSaberThrow))
        .Times(AnyNumber())
        .WillRepeatedly(Return(spell));

    auto player = game.newCreature();
    auto target = game.newCreature();
    auto talent = game.newTalent(TalentType::Spell, static_cast<int>(SpellType::LightSaberThrow));
    auto action = game.newAction<UseTalentOnObjectAction>(std::move(talent), target);
    auto subAction = action->subAction();
    ASSERT_TRUE(subAction);

    EXPECT_FALSE(action->isCompleted());
    EXPECT_FALSE(subAction->isCompleted());

    // Cycle through combat states
    for (int i = 0; i < 10; ++i) {
        action->execute(action, *target, 1.0f);
        game.combat().update(2.0f);
    }

    EXPECT_TRUE(action->isCompleted());
    EXPECT_TRUE(subAction->isCompleted());
}
