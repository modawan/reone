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
    Room room {"corridor", glm::vec3(0.0f), nullptr, nullptr, nullptr};
    graphics::Walkmesh roomWalkmesh;
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
