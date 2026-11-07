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

#include <gmock/gmock.h>

#include "../fixtures/engine.h"

#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/waypoint.h"
#include "reone/game/room.h"
#include "reone/game/roomanimation.h"
#include "reone/game/script/routines.h"
#include "reone/graphics/animation.h"
#include "reone/graphics/model.h"
#include "reone/graphics/modelnode.h"
#include "reone/resource/types.h"
#include "reone/scene/node/model.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;

void reone::game::TestGameModule::addAreaRoom(Area &area, std::shared_ptr<Room> room) {
    area._roomOrder.push_back(room.get());
    area._rooms.emplace(room->name(), std::move(room));
}

namespace {

class RoutineHarness : boost::noncopyable {
public:
    RoutineHarness() :
        _game(GameID::KotOR, "", testEngine().options(), testEngine().services(), _console),
        _routines(GameID::KotOR, &_game, &testEngine().services()) {
        _routines.init();
    }

    Variable call(const std::string &name, std::vector<Variable> args, std::optional<uint32_t> caller = std::nullopt) {
        Routine &routine = _routines.get(_routines.getIndexByName(name));
        ExecutionContext execution;
        execution.routines = &_routines;
        if (caller) {
            execution.args.push_back({ArgKind::Caller, Variable::ofObject(*caller)});
        }
        return routine.invoke(args, execution);
    }

    Game &game() { return _game; }

private:
    StubConsole _console;
    Game _game;
    Routines _routines;
};

} // namespace

// 386: a map pin is a waypoint with a map note; the map shows it while enabled.
TEST(UIRoutines, set_map_pin_enabled_turns_a_waypoint_note_on_and_off) {
    RoutineHarness harness;
    auto waypoint = harness.game().newWaypoint();
    ASSERT_FALSE(waypoint->isMapNoteEnabled());

    harness.call("SetMapPinEnabled", {Variable::ofObject(waypoint->id()), Variable::ofInt(1)});
    EXPECT_TRUE(waypoint->isMapNoteEnabled());

    harness.call("SetMapPinEnabled", {Variable::ofObject(waypoint->id()), Variable::ofInt(0)});
    EXPECT_FALSE(waypoint->isMapNoteEnabled());
}

TEST(UIRoutines, set_map_pin_enabled_ignores_what_is_not_a_waypoint) {
    RoutineHarness harness;
    auto creature = harness.game().newCreature();
    EXPECT_NO_THROW(harness.call("SetMapPinEnabled", {Variable::ofObject(creature->id()), Variable::ofInt(1)}));
    EXPECT_NO_THROW(harness.call("SetMapPinEnabled", {Variable::ofObject(script::kObjectInvalid), Variable::ofInt(1)}));
}

// 46: "Play sSoundName": once, where the caller stands.
TEST(UIRoutines, play_sound_plays_the_named_sound_where_the_caller_stands) {
    RoutineHarness harness;
    auto creature = harness.game().newCreature();
    creature->setPosition(glm::vec3(3.0f, 4.0f, 5.0f));
    auto clip = std::make_shared<audio::AudioClip>();
    EXPECT_CALL(testEngine().resourceModule().audioClips(), get("cs_droidbreak")).WillOnce(::testing::Return(clip));
    EXPECT_CALL(testEngine().audioModule().mixer(),
                play(clip, audio::AudioType::Sound, 1.0f, false, std::optional<glm::vec3>(glm::vec3(3.0f, 4.0f, 5.0f))))
        .WillOnce(::testing::Return(nullptr));

    harness.call("PlaySound", {Variable::ofString("cs_droidbreak")}, creature->id());

    ::testing::Mock::VerifyAndClearExpectations(&testEngine().resourceModule().audioClips());
    ::testing::Mock::VerifyAndClearExpectations(&testEngine().audioModule().mixer());
}

TEST(UIRoutines, play_sound_with_no_such_sound_plays_nothing) {
    RoutineHarness harness;
    EXPECT_CALL(testEngine().resourceModule().audioClips(), get("missing")).WillOnce(::testing::Return(nullptr));
    EXPECT_CALL(testEngine().audioModule().mixer(), play(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_)).Times(0);

    harness.call("PlaySound", {Variable::ofString("missing")});

    ::testing::Mock::VerifyAndClearExpectations(&testEngine().resourceModule().audioClips());
    ::testing::Mock::VerifyAndClearExpectations(&testEngine().audioModule().mixer());
}

// 738: ANIMATION_ROOM_SCRIPTLOOP01..12 name the room model's scriptloop01..12.
TEST(UIRoutines, room_animation_numbers_name_the_script_loops) {
    EXPECT_EQ("scriptloop01", roomScriptLoopName(1));
    EXPECT_EQ("scriptloop09", roomScriptLoopName(9));
    EXPECT_EQ("scriptloop10", roomScriptLoopName(10));
    EXPECT_EQ("scriptloop12", roomScriptLoopName(12));
    EXPECT_FALSE(roomScriptLoopName(0).has_value());
    EXPECT_FALSE(roomScriptLoopName(13).has_value());
    EXPECT_FALSE(roomScriptLoopName(-1).has_value());
}

TEST(UIRoutines, play_room_animation_without_an_area_plays_nothing) {
    RoutineHarness harness;
    EXPECT_NO_THROW(harness.call("PlayRoomAnimation", {Variable::ofString("m12aa_01q"), Variable::ofInt(9)}));
}

TEST(UIRoutines, play_room_animation_loops_the_script_loop_on_the_named_room) {
    auto root = std::make_shared<graphics::ModelNode>(0, "m12aa_01q", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    std::vector<std::shared_ptr<graphics::Animation>> animations;
    for (const char *name : {"scriptloop01", "scriptloop09"}) {
        animations.push_back(std::make_shared<graphics::Animation>(name, 1.0f, 0.0f, "m12aa_01q", root, std::vector<graphics::Animation::Event>()));
    }
    graphics::Model model("m12aa_01q", 0, root, animations, "", 1.0f);
    model.init();
    ::testing::NiceMock<scene::MockSceneGraph> sceneGraph;
    auto node = std::make_shared<scene::ModelSceneNode>(
        model, scene::ModelUsage::Room, sceneGraph,
        testEngine().graphicsModule().services(),
        testEngine().audioModule().services(),
        testEngine().resourceModule().services());

    RoutineHarness harness;
    auto area = harness.game().newArea();
    TestGameModule::setActiveModuleArea(harness.game(), area);
    TestGameModule::addAreaRoom(*area, std::make_shared<Room>("m12aa_01q", glm::vec3(0.0f), node, nullptr, nullptr));

    harness.call("PlayRoomAnimation", {Variable::ofString("m12aa_01q"), Variable::ofInt(13)});
    harness.call("PlayRoomAnimation", {Variable::ofString("m12aa_02q"), Variable::ofInt(9)});
    EXPECT_EQ("", node->activeAnimationName());

    harness.call("PlayRoomAnimation", {Variable::ofString("M12AA_01Q"), Variable::ofInt(9)});
    EXPECT_EQ("scriptloop09", node->activeAnimationName());
    ASSERT_EQ(1u, node->animationChannelCount());
    EXPECT_TRUE(node->animationChannels().front().properties.flags & scene::AnimationFlags::loop);
}
