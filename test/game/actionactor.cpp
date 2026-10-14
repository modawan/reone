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

#include "reone/game/action/movetopoint.h"
#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/placeable.h"
#include "reone/game/party.h"
#include "reone/game/script/routines.h"
#include "reone/script/executioncontext.h"

#include "../fixtures/engine.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

// Action routine numbers, as the shipped scripts encode them.
constexpr int kActionMoveToLocation = 21;
constexpr int kActionMoveToObject = 22;
constexpr int kActionForceFollowObject = 167;
constexpr int kActionFollowLeader = 730;

// Far enough that an actor still has ground to cover, so an honored action
// stays in progress rather than completing on arrival.
constexpr float kFarAway = 50.0f;

struct ActionActorFixture {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game {GameID::TSL, "", engine.options(), engine.services(), console};
    Routines routines {GameID::TSL, &game, &engine.services()};
    std::shared_ptr<Area> area;

    ActionActorFixture() {
        routines.init();
        // Actors act in the module's area.
        auto sceneGraph = std::make_shared<NiceMock<scene::MockSceneGraph>>();
        ON_CALL(engine.sceneModule().graphs(), get(_))
            .WillByDefault([sceneGraph](const std::string &) -> scene::ISceneGraph & {
                return *sceneGraph;
            });
        area = game.newArea();
        TestGameModule::setActiveModuleArea(game, area);
    }

    // Queue an action the way a script does: through the routine, with the
    // caller that the engine would have supplied. A conversation owned by a
    // placeable runs its action scripts with that placeable as the caller.
    void queueAsCaller(const std::shared_ptr<Object> &caller,
                       int routine,
                       std::vector<script::Variable> args = {}) {
        script::ExecutionContext ctx;
        ctx.args.emplace_back(script::ArgKind::Caller,
                              script::Variable::ofObject(caller->id()));
        routines.get(routine).invoke(args, ctx);
    }

    std::shared_ptr<Creature> makeStuckCreature() {
        auto creature = game.newCreature();
        creature->setMovementRestricted(true);
        return creature;
    }

    std::shared_ptr<Object> makeDistantTarget() {
        auto target = game.newPlaceable();
        target->setPosition(glm::vec3(kFarAway, 0.0f, 0.0f));
        return target;
    }

    std::shared_ptr<Location> farLocation() {
        return std::make_shared<Location>(glm::vec3(kFarAway, 0.0f, 0.0f), 0.0f);
    }

    // A script moves a creature only to an object standing in an area.
    void placeInArea(const std::shared_ptr<Object> &object) {
        area->add(object);
    }

    // Give the object one update, which is what executes the head of its queue.
    void tick(const std::shared_ptr<Object> &object, float dt = 1.0f) {
        object->update(dt);
    }
};

// A placeable that owns a conversation is a perfectly ordinary action caller;
// it just is not something that can walk.
std::shared_ptr<Placeable> makeConversationOwner(Game &game) {
    // Intact: a placeable at 0 hit points is destroyed and runs no actions.
    auto owner = game.newPlaceable();
    owner->setMaxHitPoints(1);
    owner->setCurrentHitPoints(1);
    return owner;
}

} // namespace

// -- Proven shipped shapes ---------------------------------------------------

// A. The K2 a_kumus_free_2 / a_revan_act shape: a placeable-owned conversation
// runs an action script that calls ActionMoveToObject with the placeable as the
// caller. Only a creature moves to an object, so nothing is queued and nothing
// blocks whatever the placeable is asked to do next.
TEST(ActionActor, move_to_object_queued_on_a_placeable_is_dropped) {
    ActionActorFixture fixture;
    auto owner = makeConversationOwner(fixture.game);
    auto target = fixture.makeDistantTarget();

    fixture.queueAsCaller(owner, kActionMoveToObject,
                          {script::Variable::ofObject(target->id()),
                           script::Variable::ofInt(1),
                           script::Variable::ofFloat(1.0f)});
    EXPECT_TRUE(owner->actions().empty());

    fixture.tick(owner);
    EXPECT_TRUE(owner->actions().empty());
}

// B. The K1 k_pman_sur13 / k_psta_malakfght shape: a door- or placeable-owned
// conversation queues ActionFollowLeader. The party leader is present
// throughout, so this exercises the actor-type guard and not the separate
// missing-leader guard.
TEST(ActionActor, follow_leader_queued_on_a_placeable_is_dropped) {
    ActionActorFixture fixture;
    auto leader = fixture.game.newCreature();
    leader->setPosition(glm::vec3(kFarAway, 0.0f, 0.0f));
    fixture.game.party().addMember(kNpcPlayer, leader);
    ASSERT_EQ(leader, fixture.game.party().getLeader());

    auto owner = makeConversationOwner(fixture.game);
    fixture.queueAsCaller(owner, kActionFollowLeader);
    // A placeable caller queues nothing.
    EXPECT_TRUE(owner->actions().empty());

    // The leader was there the whole time: this is the actor guard, not #297's.
    EXPECT_TRUE(fixture.game.party().getLeader());
}

// -- The same condition on the remaining actions -----------------------------

TEST(ActionActor, move_to_location_queued_on_a_placeable_is_dropped) {
    ActionActorFixture fixture;
    auto owner = makeConversationOwner(fixture.game);

    fixture.queueAsCaller(owner, kActionMoveToLocation,
                          {script::Variable::ofLocation(fixture.farLocation()),
                           script::Variable::ofInt(1)});
    ASSERT_EQ(1u, owner->actions().size());

    fixture.tick(owner);

    EXPECT_TRUE(owner->actions().front()->isCompleted());
}

TEST(ActionActor, follow_queued_on_a_placeable_is_dropped) {
    ActionActorFixture fixture;
    auto owner = makeConversationOwner(fixture.game);
    auto target = fixture.makeDistantTarget();

    fixture.queueAsCaller(owner, kActionForceFollowObject,
                          {script::Variable::ofObject(target->id()),
                           script::Variable::ofFloat(1.0f)});
    ASSERT_EQ(1u, owner->actions().size());

    fixture.tick(owner);

    EXPECT_TRUE(owner->actions().front()->isCompleted());
}

// MoveToPoint has no routine of its own, so the action is queued directly. The
// action is a real one and the queueing is ordinary; only the entry point
// differs from the cases above.
TEST(ActionActor, move_to_point_queued_on_a_placeable_is_dropped) {
    ActionActorFixture fixture;
    auto owner = makeConversationOwner(fixture.game);

    owner->addAction(fixture.game.newAction<MoveToPointAction>(
        glm::vec3(kFarAway, 0.0f, 0.0f)));
    ASSERT_EQ(1u, owner->actions().size());

    fixture.tick(owner);

    EXPECT_TRUE(owner->actions().front()->isCompleted());
}

// -- Creature actors keep their behaviour ------------------------------------

// The guard must reject nothing it should not. A creature with ground still to
// cover keeps the action in progress rather than having it dropped, which is
// what separates "left alone" from "silently completed".
TEST(ActionActor, a_creature_actor_keeps_moving_to_an_object) {
    ActionActorFixture fixture;
    auto walker = fixture.makeStuckCreature();
    auto target = fixture.makeDistantTarget();
    fixture.placeInArea(target);

    fixture.queueAsCaller(walker, kActionMoveToObject,
                          {script::Variable::ofObject(target->id()),
                           script::Variable::ofInt(1),
                           script::Variable::ofFloat(1.0f)});
    fixture.tick(walker);

    EXPECT_FALSE(walker->actions().front()->isCompleted());
}

// The complement: a creature already within range finishes the move, so normal
// completion still happens.
TEST(ActionActor, a_creature_actor_completes_a_move_it_has_arrived_at) {
    ActionActorFixture fixture;
    auto walker = fixture.game.newCreature();
    auto target = fixture.game.newPlaceable();
    // Use range is measured only between objects in the same area.
    fixture.placeInArea(walker);
    fixture.placeInArea(target);
    ASSERT_EQ(walker->position(), target->position());

    fixture.queueAsCaller(walker, kActionMoveToObject,
                          {script::Variable::ofObject(target->id()),
                           script::Variable::ofInt(1),
                           script::Variable::ofFloat(1.0f)});
    fixture.tick(walker);

    EXPECT_TRUE(walker->actions().front()->isCompleted());
}

TEST(ActionActor, a_creature_actor_keeps_moving_to_a_location) {
    ActionActorFixture fixture;
    auto walker = fixture.makeStuckCreature();

    fixture.queueAsCaller(walker, kActionMoveToLocation,
                          {script::Variable::ofLocation(fixture.farLocation()),
                           script::Variable::ofInt(1)});
    fixture.tick(walker);

    EXPECT_FALSE(walker->actions().front()->isCompleted());
}

TEST(ActionActor, a_creature_actor_keeps_following_an_object) {
    ActionActorFixture fixture;
    auto walker = fixture.makeStuckCreature();
    auto target = fixture.makeDistantTarget();

    fixture.queueAsCaller(walker, kActionForceFollowObject,
                          {script::Variable::ofObject(target->id()),
                           script::Variable::ofFloat(1.0f)});
    fixture.tick(walker);

    EXPECT_FALSE(walker->actions().front()->isCompleted());
}

TEST(ActionActor, a_creature_actor_keeps_moving_to_a_point) {
    ActionActorFixture fixture;
    auto walker = fixture.makeStuckCreature();

    walker->addAction(fixture.game.newAction<MoveToPointAction>(
        glm::vec3(kFarAway, 0.0f, 0.0f)));
    fixture.tick(walker);

    EXPECT_FALSE(walker->actions().front()->isCompleted());
}

// FollowLeader with a creature actor and a leader present stays in progress,
// keeping this distinct from both the actor guard and the missing-leader guard.
TEST(ActionActor, a_creature_actor_keeps_following_the_leader) {
    ActionActorFixture fixture;
    auto leader = fixture.game.newCreature();
    leader->setPosition(glm::vec3(kFarAway, 0.0f, 0.0f));
    fixture.game.party().addMember(kNpcPlayer, leader);

    auto walker = fixture.makeStuckCreature();
    // Only a party member follows the leader, and one at 0 hit points runs
    // no actions.
    walker->setMaxHitPoints(1);
    walker->setCurrentHitPoints(1);
    // A standing party member regenerates, with its Constitution modifier
    // and healing skill, and searches for mines.
    ON_CALL(fixture.engine.resourceModule().twoDas(), get("regeneration"))
        .WillByDefault(Return(std::shared_ptr<TwoDA>(TwoDA::Builder().build())));
    ON_CALL(fixture.engine.resourceModule().twoDas(), get("skills"))
        .WillByDefault(Return(std::shared_ptr<TwoDA>(TwoDA::Builder().build())));
    // A follower standing still glances at the leader within its head arc.
    ON_CALL(fixture.engine.resourceModule().twoDas(), get("appearance"))
        .WillByDefault(Return(std::shared_ptr<TwoDA>(TwoDA::Builder().columns({"head_arc_h"}).row({"40"}).build())));
    ON_CALL(fixture.engine.resourceModule().twoDas(), get("racialtypes"))
        .WillByDefault(Return(std::shared_ptr<resource::TwoDA>(resource::TwoDA::Builder()
                                                                   .columns({"stradjust", "dexadjust", "conadjust", "intadjust", "wisadjust", "chaadjust"})
                                                                   .row({"0", "0", "0", "0", "0", "0"})
                                                                   .build())));
    ASSERT_TRUE(fixture.game.party().addAvailableMember(0, walker));
    ASSERT_TRUE(fixture.game.party().addMember(0, walker));
    fixture.queueAsCaller(walker, kActionFollowLeader);
    ASSERT_EQ(1u, walker->actions().size());
    fixture.tick(walker);

    EXPECT_FALSE(walker->actions().front()->isCompleted());
}
