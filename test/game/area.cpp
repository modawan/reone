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

#include "reone/game/game.h"
#include "reone/game/object.h"
#include "reone/game/object/area.h"
#include "reone/system/exception/validation.h"

using namespace reone;
using namespace reone::game;
using namespace testing;

namespace {

TEST(Area, get_object_by_tag_should_partition_by_is_dead) {
    TestEngine &engine = testEngine();
    engine.init();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    NiceMock<scene::MockSceneGraph> sceneGraph;
    ON_CALL(engine.sceneModule().graphs(), get(_))
        .WillByDefault(ReturnRef(sceneGraph));

    std::string tag = "foo";

    auto area = game.newArea();

    auto alive0 = game.newCreature();
    alive0->setTag(tag);

    auto dead1 = game.newCreature();
    dead1->setTag(tag);

    auto alive2 = game.newCreature();
    alive2->setTag(tag);

    auto dead3 = game.newCreature();
    dead3->setTag(tag);

    area->add(alive0);
    area->add(dead1);
    area->add(alive2);
    area->add(dead3);

    dead1->damage(1, nullptr);
    dead3->damage(1, nullptr);

    EXPECT_EQ(alive0, area->getObjectByTag(tag, 0));
    EXPECT_EQ(alive2, area->getObjectByTag(tag, 1));
    EXPECT_EQ(dead1, area->getObjectByTag(tag, 2));
    EXPECT_EQ(dead3, area->getObjectByTag(tag, 3));
}

TEST(Area, failed_room_attachment_rolls_back_area_ownership) {
    TestEngine &engine = testEngine();
    engine.init();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    NiceMock<scene::MockSceneGraph> sceneGraph;
    ON_CALL(engine.sceneModule().graphs(), get(_))
        .WillByDefault(ReturnRef(sceneGraph));
    EXPECT_CALL(sceneGraph, testElevation(_, _))
        .WillOnce(Throw(ValidationException("injected room attachment failure")));

    auto area = game.newArea();
    auto item = game.newItem();
    item->setTag("attachment_failure");
    const size_t registrySize = engine.gameModule().objectRegistrySize(game);

    EXPECT_THROW(area->add(item), ValidationException);

    EXPECT_TRUE(item->isRuntimeLive());
    EXPECT_EQ(item, game.getObjectById(item->id()));
    EXPECT_EQ(registrySize, engine.gameModule().objectRegistrySize(game));
    EXPECT_TRUE(area->getObjectsByType(ObjectType::Item).empty());
    EXPECT_FALSE(area->getObjectByTag("attachment_failure", 0));
    EXPECT_EQ(nullptr, item->room());
}

} // namespace
