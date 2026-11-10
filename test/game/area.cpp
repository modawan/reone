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
#include "../fixtures/game.h"
#include "../fixtures/scene.h"

#include "reone/game/game.h"
#include "reone/game/object.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/room.h"
#include "reone/graphics/walkmesh.h"
#include "reone/scene/node/walkmesh.h"
#include "reone/system/exception/validation.h"

using namespace reone;
using namespace reone::game;
using namespace testing;

void reone::game::TestGameModule::setDoorClosedWalkmesh(Door &door, std::shared_ptr<scene::WalkmeshSceneNode> walkmesh) {
    door._walkmeshClosed = std::move(walkmesh);
}

namespace {

constexpr uint32_t kFloorMaterial = 0;
constexpr uint32_t kWallMaterial = 1;

// An upright wall across the line y = 0, at x, from y = -2 to 2 and from the
// ground to 3 up.
void addWall(graphics::Walkmesh &walkmesh, float x) {
    const auto first = static_cast<uint32_t>(walkmesh.vertices.size());
    walkmesh.vertices.push_back(glm::vec3(x, -2.0f, 0.0f));
    walkmesh.vertices.push_back(glm::vec3(x, 2.0f, 0.0f));
    walkmesh.vertices.push_back(glm::vec3(x, 2.0f, 3.0f));
    walkmesh.vertices.push_back(glm::vec3(x, -2.0f, 3.0f));
    walkmesh.faces.push_back({first, first + 1, first + 2});
    walkmesh.faces.push_back({first, first + 2, first + 3});
    walkmesh.normals.insert(walkmesh.normals.end(), 2, glm::vec3(-1.0f, 0.0f, 0.0f));
    walkmesh.materials.insert(walkmesh.materials.end(), 2, kWallMaterial);
}

// A room with a wall across a walk from the origin along x, and a door with
// its own wall across it, each at a distance. The walk is settled as one
// stretch over the room, at ground level.
struct WallsFixture {
    TestEngine engine;
    NiceMock<scene::MockSceneGraph> sceneGraph;
    Room room {"room", glm::vec3(0.0f), nullptr, nullptr, nullptr};
    std::shared_ptr<graphics::Walkmesh> roomWalkmesh {std::make_shared<graphics::Walkmesh>()};
    std::shared_ptr<graphics::Walkmesh> doorWalkmesh {std::make_shared<graphics::Walkmesh>()};
    std::shared_ptr<scene::WalkmeshSceneNode> roomNode;
    std::shared_ptr<scene::WalkmeshSceneNode> doorNode;
    StubConsole console;
    std::unique_ptr<Game> game;
    std::shared_ptr<Area> area;
    std::shared_ptr<Door> door;
    std::shared_ptr<Creature> walker;

    WallsFixture(float roomWallAt, float doorWallAt) {
        engine.init();
        ON_CALL(engine.sceneModule().graphs(), get(_))
            .WillByDefault(ReturnRef(sceneGraph));
        auto &surfaces = dynamic_cast<MockSurfaces &>(engine.services().game.surfaces);
        ON_CALL(surfaces, getWalkableSurfaces()).WillByDefault(Return(std::set<uint32_t> {kFloorMaterial}));
        ON_CALL(surfaces, getWalkcheckSurfaces()).WillByDefault(Return(std::set<uint32_t> {kFloorMaterial, kWallMaterial}));

        addWall(*roomWalkmesh, roomWallAt);
        addWall(*doorWalkmesh, doorWallAt);
        roomNode = std::make_shared<scene::WalkmeshSceneNode>(
            roomWalkmesh, sceneGraph, engine.services().graphics, engine.services().audio, engine.services().resource);
        roomNode->setUser(room);
        doorNode = std::make_shared<scene::WalkmeshSceneNode>(
            doorWalkmesh, sceneGraph, engine.services().graphics, engine.services().audio, engine.services().resource);
        ON_CALL(sceneGraph, walkRooms(_, _, _, _, _, _))
            .WillByDefault(Invoke([this](const glm::vec3 &, const glm::vec3 &, std::optional<int>, bool,
                                         std::vector<scene::RoomStretch> &stretches, scene::Collision &) {
                stretches.push_back(scene::RoomStretch {roomNode, 0.0f, graphics::Walkmesh::HeightBand {0.0f, 0.0f}});
                return scene::RoomWalkEnd::Dest;
            }));

        game = std::make_unique<Game>(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
        area = game->newArea();
        door = game->newDoor();
        doorNode->setUser(*door);
        TestGameModule::setDoorClosedWalkmesh(*door, doorNode);
        area->add(door);
        walker = game->newCreature();
    }
};

TEST(Area, get_object_by_tag_should_keep_insertion_order) {
    TestEngine engine;
    engine.init();
    // Dying reads the effects death keeps and the body's destroy delay.
    ON_CALL(engine.resourceModule().twoDas(), get("removefxondeath"))
        .WillByDefault(Return(std::shared_ptr<resource::TwoDA>(resource::TwoDA::Builder()
                                                                   .columns({"label", "effecttype"})
                                                                   .row({"EFFECT_DISGUISE", "62"})
                                                                   .row({"EFFECT_BEAM", "21"})
                                                                   .build())));
    ON_CALL(engine.resourceModule().twoDas(), get("appearance"))
        .WillByDefault(Return(std::shared_ptr<resource::TwoDA>(resource::TwoDA::Builder()
                                                                   .columns({"destroyobjectdelay"})
                                                                   .row({""})
                                                                   .build())));
    engine.initCombatTables();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    NiceMock<scene::MockSceneGraph> sceneGraph;
    ON_CALL(engine.sceneModule().graphs(), get(_))
        .WillByDefault(ReturnRef(sceneGraph));

    std::string tag = "foo";

    auto area = game.newArea();
    // The bodies' destruction is queued on the active module.
    TestGameModule::setActiveModuleArea(game, area);

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
    EXPECT_EQ(dead1, area->getObjectByTag(tag, 1));
    EXPECT_EQ(alive2, area->getObjectByTag(tag, 2));
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
    EXPECT_CALL(sceneGraph, findRoomWalkmesh(_))
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

// A door standing in front of a room's wall stops the walk at the door, and
// the door is reported.
TEST(Area, a_door_before_a_room_wall_stops_a_walk_first) {
    WallsFixture fixture(/*roomWallAt=*/4.0f, /*doorWallAt=*/2.0f);

    const auto test = fixture.area->testDirectLine(*fixture.walker, glm::vec3(0.0f), glm::vec3(6.0f, 0.0f, 0.0f));

    EXPECT_EQ(Area::DirectLine::Blocked, test.result);
    EXPECT_LT(test.stopPoint.x, 2.0f - fixture.walker->personalSpace() + 0.01f);
    EXPECT_GT(test.stopPoint.x, 1.0f);
    ASSERT_TRUE(test.door.has_value());
    EXPECT_EQ(fixture.door.get(), &test.door->get());
}

// A room's wall in front of a door stops the walk at the wall, and no door is
// reported.
TEST(Area, a_room_wall_before_a_door_stops_a_walk_first) {
    WallsFixture fixture(/*roomWallAt=*/2.0f, /*doorWallAt=*/4.0f);

    const auto test = fixture.area->testDirectLine(*fixture.walker, glm::vec3(0.0f), glm::vec3(6.0f, 0.0f, 0.0f));

    EXPECT_EQ(Area::DirectLine::Blocked, test.result);
    EXPECT_LT(test.stopPoint.x, 2.0f - fixture.walker->personalSpace() + 0.01f);
    EXPECT_GT(test.stopPoint.x, 1.0f);
    EXPECT_FALSE(test.door.has_value());
}
