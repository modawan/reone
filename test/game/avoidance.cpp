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

#include <algorithm>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"
#include "../fixtures/scene.h"

#include "reone/game/action/movetopoint.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/party.h"
#include "reone/game/pathfinder.h"
#include "reone/game/room.h"
#include "reone/graphics/walkmesh.h"
#include "reone/resource/2da.h"
#include "reone/scene/node/walkmesh.h"

using namespace reone;
using namespace reone::game;
using namespace testing;

namespace {

constexpr float kTick = 1.0f / 30.0f;
constexpr float kHalfLength = 20.0f;
constexpr float kHalfWidth = 2.0f;

bool overCorridor(const glm::vec3 &position) {
    return glm::abs(position.x) <= kHalfLength && glm::abs(position.y) <= kHalfWidth;
}

// A straight corridor, x in [-20, 20] and y in [-2, 2], as one room of the
// pathfinder's walkmesh.
void loadCorridor(Pathfinder &pf) {
    graphics::Walkmesh walkmesh;
    walkmesh.vertices = {
        glm::vec3(-kHalfLength, -kHalfWidth, 0.0f),
        glm::vec3(kHalfLength, -kHalfWidth, 0.0f),
        glm::vec3(kHalfLength, kHalfWidth, 0.0f),
        glm::vec3(-kHalfLength, kHalfWidth, 0.0f)};
    walkmesh.faces = {{0, 1, 2}, {0, 2, 3}};
    walkmesh.materials = {0, 0};
    std::set<uint32_t> walkable {0};
    uniwalkLoadRoom(pf.uni, walkmesh, walkable);
    uniwalkFinalize(pf.uni);
    pf.paths.resize(64);
}

std::shared_ptr<resource::TwoDA> creatureSpeeds() {
    resource::TwoDA::Builder builder;
    builder.columns({"label", "2daname", "walkrate", "runrate"});
    builder.row({"PC_Movement", "PLAYER", "1.75", "5.40"});
    return std::shared_ptr<resource::TwoDA>(builder.build());
}

// The player character in the corridor. The scene's room walkmesh covers the
// corridor only, so a creature cannot step off it.
struct CorridorFixture {
    TestEngine engine;
    NiceMock<scene::MockSceneGraph> sceneGraph;
    Room room {"corridor", glm::vec3(0.0f), nullptr, nullptr};
    std::shared_ptr<graphics::Walkmesh> roomWalkmesh {std::make_shared<graphics::Walkmesh>()};
    std::shared_ptr<scene::WalkmeshSceneNode> roomNode;
    StubConsole console;
    std::unique_ptr<Game> game;
    std::shared_ptr<Area> area;
    std::shared_ptr<Creature> pc;

    CorridorFixture() {
        engine.init();
        ON_CALL(engine.sceneModule().graphs(), get(_))
            .WillByDefault(ReturnRef(sceneGraph));
        roomNode = std::make_shared<scene::WalkmeshSceneNode>(
            roomWalkmesh, sceneGraph, engine.services().graphics, engine.services().audio, engine.services().resource);
        roomNode->setUser(room);
        ON_CALL(sceneGraph, findRoomWalkmesh(_))
            .WillByDefault(Invoke([this](const glm::vec3 &position) -> std::shared_ptr<scene::WalkmeshSceneNode> {
                return overCorridor(position) ? roomNode : nullptr;
            }));
        ON_CALL(sceneGraph, findRoomFace(_, _, _))
            .WillByDefault(Return(std::optional<scene::RoomFace>(scene::RoomFace {0, 0.0f, glm::vec3(0.0f, 0.0f, 1.0f)})));
        ON_CALL(dynamic_cast<MockSurfaces &>(engine.services().game.surfaces), isWalkable(_)).WillByDefault(Return(true));
        ON_CALL(engine.resourceModule().twoDas(), get("creaturespeed"))
            .WillByDefault(Return(creatureSpeeds()));
        game = std::make_unique<Game>(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
        // The script runner: a walker that finds no way round a creature
        // runs its path-failure script.
        game->initLocalServices();
        area = game->newArea();
        TestGameModule::setActiveModuleArea(*game, area);
        loadCorridor(area->pathfinder());
        pc = spawn(glm::vec3(0.0f));
        game->party().addMember(kNpcPlayer, pc);
        game->party().setPlayer(pc);
    }

    std::shared_ptr<Creature> spawn(const glm::vec3 &position) {
        auto creature = game->newCreature();
        creature->setMovementRate(0);
        // A party member at 0 vitality is down and cannot move.
        creature->setPC(false);
        creature->setMaxHitPoints(10);
        creature->setCurrentHitPoints(10);
        creature->setPosition(position);
        area->add(creature);
        return creature;
    }

    std::shared_ptr<Creature> addCompanion(const glm::vec3 &position) {
        auto companion = spawn(position);
        game->party().addAvailableMember(0, companion);
        EXPECT_TRUE(game->party().addMember(0, companion));
        return companion;
    }
};

} // namespace

// The leader walks up a corridor and bumps a companion standing near its edge.
// The way past the companion's far side leaves the corridor: its corners have
// no pathfinder ground under them, and walked, it would leave the leader
// against the edge, where no path can be planned from again. That way counts
// as blocked, so the leader goes round the other side.
TEST(CreatureAvoidance, the_way_round_a_companion_keeps_to_the_walkmesh) {
    CorridorFixture fixture;
    fixture.pc->setPosition(glm::vec3(-10.0f, 0.3f, 0.0f));
    const float bearing = glm::radians(57.0f);
    fixture.addCompanion(glm::vec3(-10.0f + std::cos(bearing), 0.3f + std::sin(bearing), 0.0f));
    auto action = fixture.game->newAction<MoveToPointAction>(glm::vec3(10.0f, 0.3f, 0.0f));
    fixture.pc->addAction(action);

    for (int tick = 0; tick < 30 * 30 && !action->isCompleted(); ++tick) {
        fixture.pc->update(kTick);
    }

    EXPECT_TRUE(action->isCompleted());
    EXPECT_GT(fixture.pc->position().x, 8.5f) << "walked past the companion";
    EXPECT_LE(glm::abs(fixture.pc->position().y), kHalfWidth);
}

// The leader sets out up the corridor with a wall across it at its back and
// bumps a companion standing just ahead. The leader stands clear of the wall,
// but the corner of the companion's hexagon nearest the leader lies behind it,
// nearer the wall than the leader's personal space, and both ways round start
// there. Their first legs are tested as the leader's steps will be, by the
// thin line, so the wall does not block both ways round.
TEST(CreatureAvoidance, a_leader_with_a_wall_at_its_back_goes_round_a_companion) {
    constexpr uint32_t kFloorMaterial = 0;
    constexpr uint32_t kWallMaterial = 1;
    constexpr float kWallX = -10.7f;
    CorridorFixture fixture;
    auto &surfaces = dynamic_cast<MockSurfaces &>(fixture.engine.services().game.surfaces);
    ON_CALL(surfaces, getWalkableSurfaces()).WillByDefault(Return(std::set<uint32_t> {kFloorMaterial}));
    ON_CALL(surfaces, getWalkcheckSurfaces()).WillByDefault(Return(std::set<uint32_t> {kFloorMaterial, kWallMaterial}));
    // An upright wall across the corridor, from the ground to 3 up.
    auto &walkmesh = *fixture.roomWalkmesh;
    walkmesh.vertices = {
        glm::vec3(kWallX, -kHalfWidth, 0.0f),
        glm::vec3(kWallX, kHalfWidth, 0.0f),
        glm::vec3(kWallX, kHalfWidth, 3.0f),
        glm::vec3(kWallX, -kHalfWidth, 3.0f)};
    walkmesh.faces = {{0, 1, 2}, {0, 2, 3}};
    walkmesh.normals = {glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f)};
    walkmesh.materials = {kWallMaterial, kWallMaterial};
    // A walk is settled as one stretch over the room, at ground level.
    ON_CALL(fixture.sceneGraph, walkRooms(_, _, _, _, _, _))
        .WillByDefault(Invoke([&fixture](const glm::vec3 &, const glm::vec3 &, std::optional<int>, bool,
                                         std::vector<scene::RoomStretch> &stretches, scene::Collision &) {
            stretches.push_back(scene::RoomStretch {fixture.roomNode, 0.0f, graphics::Walkmesh::HeightBand {0.0f, 0.0f}});
            return scene::RoomWalkEnd::Dest;
        }));
    fixture.pc->setPosition(glm::vec3(-10.0f, 0.0f, 0.0f));
    ASSERT_GT(fixture.pc->position().x - kWallX, fixture.pc->personalSpace()) << "the leader stands clear of the wall";
    fixture.addCompanion(glm::vec3(-8.75f, 0.0f, 0.0f));
    auto action = fixture.game->newAction<MoveToPointAction>(glm::vec3(10.0f, 0.0f, 0.0f));
    fixture.pc->addAction(action);

    for (int tick = 0; tick < 30 * 30 && !action->isCompleted(); ++tick) {
        fixture.pc->update(kTick);
    }

    EXPECT_TRUE(action->isCompleted());
    EXPECT_GT(fixture.pc->position().x, 8.5f) << "walked past the companion";
    EXPECT_LE(glm::abs(fixture.pc->position().y), kHalfWidth);
}

// On a floor that is not at height zero, the way in from inside the hexagon is
// still the corner nearest the walker. The corners stand on the ground and the
// walker's point is taken flat (zero height), so a distance in three
// dimensions picked the lowest corner, which can lie across the creature's
// flank: the leg to it ran into the creature again and the walk ended.
// Measured on a sloping floor at about height 29.
TEST(CreatureAvoidance, the_way_in_from_inside_the_hexagon_is_the_nearest_corner_seen_from_above) {
    const glm::vec3 companion(219.476f, 140.429f, 29.04f);
    const glm::vec3 stepDir(-0.93f, 0.37f, 0.0f);
    // Lower to the east and the north.
    const auto ground = [](const glm::vec2 &p) { return 29.0f - 0.2f * (p.x - 219.476f) - 0.15f * (p.y - 140.429f); };
    const AvoidanceHex hex = computeAvoidanceHex(companion, stepDir, 0.955f, ground);
    const glm::vec3 leader(220.049f, 139.861f, 0.0f);
    int nearest = 0;
    for (int i = 1; i < 6; ++i) {
        if (glm::distance2(glm::vec2(hex.v[i]), glm::vec2(leader)) < glm::distance2(glm::vec2(hex.v[nearest]), glm::vec2(leader))) {
            nearest = i;
        }
    }
    EXPECT_EQ(nearest, closestAvoidanceHexCorner(hex, leader));
}

// The leader walks up the corridor beside a wall along it, nearer the wall
// than its personal space, as a path may run and a walker's steps follow, and
// bumps a companion standing on its path. The way between the companion and
// the wall is blocked. The way round the open side rejoins the path at a
// corner as near the wall as the path, so its last leg, swept with the
// leader's personal space, meets the wall too. With neither way clear by the
// swept test, both are tested as the leader's steps will be, by the thin line,
// and the leader goes round the open side.
TEST(CreatureAvoidance, a_leader_walking_close_by_a_wall_goes_round_a_companion_on_the_open_side) {
    constexpr uint32_t kFloorMaterial = 0;
    constexpr uint32_t kWallMaterial = 1;
    constexpr float kWallY = 1.0f;
    constexpr float kPathY = 0.8f;
    CorridorFixture fixture;
    auto &surfaces = dynamic_cast<MockSurfaces &>(fixture.engine.services().game.surfaces);
    ON_CALL(surfaces, getWalkableSurfaces()).WillByDefault(Return(std::set<uint32_t> {kFloorMaterial}));
    ON_CALL(surfaces, getWalkcheckSurfaces()).WillByDefault(Return(std::set<uint32_t> {kFloorMaterial, kWallMaterial}));
    // An upright wall along the corridor, from the ground to 3 up.
    auto &walkmesh = *fixture.roomWalkmesh;
    walkmesh.vertices = {
        glm::vec3(-kHalfLength, kWallY, 0.0f),
        glm::vec3(kHalfLength, kWallY, 0.0f),
        glm::vec3(kHalfLength, kWallY, 3.0f),
        glm::vec3(-kHalfLength, kWallY, 3.0f)};
    walkmesh.faces = {{0, 1, 2}, {0, 2, 3}};
    walkmesh.normals = {glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)};
    walkmesh.materials = {kWallMaterial, kWallMaterial};
    ON_CALL(fixture.sceneGraph, walkRooms(_, _, _, _, _, _))
        .WillByDefault(Invoke([&fixture](const glm::vec3 &, const glm::vec3 &, std::optional<int>, bool,
                                         std::vector<scene::RoomStretch> &stretches, scene::Collision &) {
            stretches.push_back(scene::RoomStretch {fixture.roomNode, 0.0f, graphics::Walkmesh::HeightBand {0.0f, 0.0f}});
            return scene::RoomWalkEnd::Dest;
        }));
    fixture.pc->setPosition(glm::vec3(-10.0f, kPathY, 0.0f));
    ASSERT_LT(kWallY - kPathY, fixture.pc->personalSpace()) << "the leader walks within its personal space of the wall";
    fixture.addCompanion(glm::vec3(-8.75f, kPathY, 0.0f));
    auto action = fixture.game->newAction<MoveToPointAction>(glm::vec3(10.0f, kPathY, 0.0f));
    fixture.pc->addAction(action);

    for (int tick = 0; tick < 30 * 30 && !action->isCompleted(); ++tick) {
        fixture.pc->update(kTick);
    }

    EXPECT_TRUE(action->isCompleted());
    EXPECT_GT(fixture.pc->position().x, 8.5f) << "walked past the companion";
    EXPECT_LT(fixture.pc->position().y, kWallY);
}

namespace {

int activePaths(Area &area) {
    const auto &paths = area.pathfinder().paths;
    return static_cast<int>(std::count_if(paths.begin(), paths.end(), [](const AStarPath &path) { return path.active; }));
}

// The leader sets out up the corridor towards x 10 and is a second on its way.
std::shared_ptr<MoveToPointAction> walkUpTheCorridor(CorridorFixture &fixture) {
    fixture.pc->setPosition(glm::vec3(-10.0f, 0.0f, 0.0f));
    auto walk = fixture.game->newAction<MoveToPointAction>(glm::vec3(10.0f, 0.0f, 0.0f), false, 0.0f);
    fixture.pc->addAction(walk);
    for (int tick = 0; tick < 30; ++tick) {
        fixture.pc->update(kTick);
    }
    return walk;
}

// A creature outside the party, so one that can die, a second on its way up
// the corridor beside the leader.
std::shared_ptr<Creature> walkingStranger(CorridorFixture &fixture) {
    auto walker = fixture.spawn(glm::vec3(-10.0f, 1.0f, 0.0f));
    walker->addAction(fixture.game->newAction<MoveToPointAction>(glm::vec3(10.0f, 1.0f, 0.0f), false, 0.0f));
    for (int tick = 0; tick < 30; ++tick) {
        walker->update(kTick);
    }
    return walker;
}

} // namespace

// A walk cleared on its way, as a script's ClearAllActions does, takes its
// path with it: the next walk heads for its own point. The cleared walk's path
// used to stay, and the next walk followed it to the cleared walk's point.
TEST(CreatureWalk, a_walk_after_a_cleared_one_heads_for_its_own_point) {
    CorridorFixture fixture;
    walkUpTheCorridor(fixture);
    ASSERT_GT(fixture.pc->position().x, -10.0f) << "the first walk set out";

    fixture.pc->clearAllActions(true);
    auto back = fixture.game->newAction<MoveToPointAction>(glm::vec3(-12.0f, 0.0f, 0.0f), false, 0.0f);
    fixture.pc->addAction(back);
    for (int tick = 0; tick < 30 * 30 && !back->isCompleted(); ++tick) {
        fixture.pc->update(kTick);
    }

    EXPECT_TRUE(back->isCompleted());
    EXPECT_NEAR(fixture.pc->position().x, -12.0f, 0.1f);
}

// Clearing a walk gives its path back to the area's pathfinder, whose paths
// are a fixed pool shared by every walker in the area.
TEST(CreatureWalk, a_cleared_walk_gives_its_path_back) {
    CorridorFixture fixture;
    walkUpTheCorridor(fixture);
    ASSERT_EQ(1, activePaths(*fixture.area));

    fixture.pc->clearAllActions(true);

    EXPECT_EQ(0, activePaths(*fixture.area));
}

// A walk its own action ends short of its point, as a move that a creature in
// the way ends does, gives its path back on the walker's next update.
TEST(CreatureWalk, a_walk_its_action_ends_gives_its_path_back) {
    CorridorFixture fixture;
    auto walk = walkUpTheCorridor(fixture);
    ASSERT_EQ(1, activePaths(*fixture.area));

    walk->complete();
    fixture.pc->update(kTick);

    EXPECT_EQ(0, activePaths(*fixture.area));
}

// A walker that dies on its way gives its path back, one that cannot be
// commanded included.
TEST(CreatureWalk, a_walker_that_dies_gives_its_path_back) {
    CorridorFixture fixture;
    auto walker = walkingStranger(fixture);
    ASSERT_EQ(1, activePaths(*fixture.area));

    walker->setCommandable(false);
    walker->setCurrentHitPoints(0);
    ASSERT_TRUE(walker->isDead());
    walker->update(kTick);

    EXPECT_EQ(0, activePaths(*fixture.area));
}

// A walker destroyed on its way, as a script destroys one it has walked to a
// door, gives its path back.
TEST(CreatureWalk, a_destroyed_walker_gives_its_path_back) {
    CorridorFixture fixture;
    auto walker = walkingStranger(fixture);
    ASSERT_EQ(1, activePaths(*fixture.area));

    fixture.area->destroyObject(*walker);
    fixture.area->update(kTick);

    ASSERT_FALSE(fixture.game->getObjectById(walker->id()));
    EXPECT_EQ(0, activePaths(*fixture.area));
}
