/*
 * Copyright (c) 2025 The reone project contributors
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

// Shouts and listen patterns: the pattern matcher, the patterns an object
// keeps, the routines, the broadcast, the conversation event and saving.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "reone/game/action.h"
#include "reone/game/event.h"
#include "reone/game/game.h"
#include "reone/game/listenpattern.h"
#include "reone/game/modulesnapshot.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/script/routines.h"
#include "reone/script/executioncontext.h"
#include "reone/script/program.h"

#include "../fixtures/engine.h"
#include "../fixtures/game.h"
#include "../fixtures/scene.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;
using namespace testing;

namespace {

constexpr int kActionSpeakString = 39;
constexpr int kSignalEvent = 131;
constexpr int kGetIsListening = 174;
constexpr int kSetListening = 175;
constexpr int kSetListenPattern = 176;
constexpr int kTestStringAgainstPattern = 177;
constexpr int kGetMatchedSubstring = 178;
constexpr int kGetMatchedSubstringsCount = 179;
constexpr int kGetListenPatternNumber = 195;
constexpr int kSpeakString = 221;
constexpr int kActionSpeakStringByStrRef = 240;
constexpr int kGetLastSpeaker = 254;
constexpr int kEventConversation = 295;
constexpr int kSetAssociateListenPatterns = 327;

constexpr int kTalk = 0;
constexpr int kWhisper = 1;
constexpr int kShout = 2;
constexpr int kSilentTalk = 3;
constexpr int kSilentShout = 4;

const std::string kFallbackScript = "k_hen_dialogue01";

bool matches(const std::string &pattern, const std::string &str) {
    auto parsed = ListenPattern::parse(pattern);
    return parsed && parsed->match(str);
}

std::vector<std::string> piecesOf(const std::string &pattern, const std::string &str) {
    std::vector<std::string> pieces;
    auto parsed = ListenPattern::parse(pattern);
    EXPECT_TRUE(parsed && parsed->match(str, pieces)) << pattern << " against " << str;
    return pieces;
}

class ShoutHarness {
public:
    explicit ShoutHarness(GameID gameId = GameID::KotOR) {
        engine.init();
        ON_CALL(engine.sceneModule().graphs(), get(_))
            .WillByDefault(ReturnRef(sceneGraph));
        EXPECT_CALL(engine.resourceModule().scripts(), get(_)).Times(AnyNumber());
        game = std::make_unique<Game>(gameId, "", engine.options(), engine.services(), console);
        game->initLocalServices();
        routines = std::make_unique<Routines>(gameId, game.get(), &engine.services());
        routines->init();
    }

    // A fresh active module and area; objects placed afterwards go into it.
    void enterArea() {
        area = game->newArea();
        TestGameModule::setActiveModuleArea(*game, area);
    }

    template <class T>
    std::shared_ptr<T> place(std::shared_ptr<T> object, const glm::vec3 &position) {
        object->setPosition(position);
        area->add(object);
        return object;
    }

    std::shared_ptr<Creature> addCreature(const glm::vec3 &position) {
        return place(game->newCreature(), position);
    }

    // A creature that listens for one pattern and hears the speaker.
    std::shared_ptr<Creature> addListener(
        const std::shared_ptr<Object> &speaker,
        const glm::vec3 &position,
        const std::string &pattern = "GEN_I_WAS_ATTACKED",
        int32_t number = 1) {

        auto listener = addCreature(position);
        listener->setListening(true);
        listener->setListenPattern(pattern, number);
        listener->setObjectHeard(speaker, true);
        return listener;
    }

    Variable call(int routine, const std::vector<Variable> &args, uint32_t callerId = kObjectInvalid) {
        ExecutionContext ctx;
        if (callerId != kObjectInvalid) {
            ctx.args.emplace_back(ArgKind::Caller, Variable::ofObject(callerId));
        }
        return routines->get(routine).invoke(args, ctx);
    }

    void speak(const Object &speaker, const std::string &message, int volume) {
        call(kSpeakString, {Variable::ofString(message), Variable::ofInt(volume)}, speaker.id());
    }

    // The conversation events waiting in the active module.
    std::vector<SavedEventRecord> conversationEvents() const {
        std::vector<SavedEventRecord> result;
        for (const auto &record : game->module()->saveEventSnapshot()) {
            if (record.eventId != static_cast<uint32_t>(SavedEventType::SignalEvent)) {
                continue;
            }
            const auto *event = std::get_if<SavedScriptEvent>(&record.payload);
            if (event && event->type == 7) {
                result.push_back(record);
            }
        }
        return result;
    }

    void deliver() {
        game->module()->dispatchDueSavedEvents();
    }

    // Counts runs of a script, which resolves to nothing.
    void countScriptRuns(const std::string &resRef) {
        _scriptRuns[resRef] = 0;
        EXPECT_CALL(engine.resourceModule().scripts(), get(resRef))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([this](const std::string &key) {
                ++_scriptRuns[key];
                return std::shared_ptr<ScriptProgram>();
            }));
    }

    int scriptRuns(const std::string &resRef) {
        return _scriptRuns[resRef];
    }

    TestEngine engine;
    NiceMock<scene::MockSceneGraph> sceneGraph;
    StubConsole console;
    std::unique_ptr<Game> game;
    std::unique_ptr<Routines> routines;
    std::shared_ptr<Area> area;

private:
    std::map<std::string, int> _scriptRuns;
};

const SavedScriptEvent &payloadOf(const SavedEventRecord &record) {
    return std::get<SavedScriptEvent>(record.payload);
}

std::shared_ptr<Gff> creatureRecord(const Gff &git, uint32_t id) {
    for (const auto &record : git.getList("Creature List")) {
        if (record->getUint("ObjectId", kSavedRuntimeInvalidObjectId) == id) {
            return record;
        }
    }
    return nullptr;
}

} // namespace

// Matcher

TEST(ListenPattern, literal_is_whole_string_and_case_insensitive) {
    EXPECT_TRUE(matches("GEN_I_WAS_ATTACKED", "GEN_I_WAS_ATTACKED"));
    EXPECT_TRUE(matches("GEN_I_WAS_ATTACKED", "gen_i_was_attacked"));
    EXPECT_FALSE(matches("GEN_I_WAS_ATTACKED", "GEN_I_WAS_ATTACKED!"));
    EXPECT_FALSE(matches("GEN_I_WAS_ATTACKED", "XGEN_I_WAS_ATTACKED"));
    EXPECT_FALSE(matches("GEN_I_WAS_ATTACKED", "GEN_I_WAS"));
}

TEST(ListenPattern, pieces_start_with_full_string_in_original_case) {
    EXPECT_EQ(
        (std::vector<std::string> {"GEN_I_WAS_ATTACKED", "GEN_I_WAS_ATTACKED"}),
        piecesOf("gen_i_was_attacked", "GEN_I_WAS_ATTACKED"));
    EXPECT_EQ((std::vector<std::string> {"GEN_42", "GEN_", "42"}), piecesOf("gen_*n", "GEN_42"));
}

TEST(ListenPattern, star_star_is_lazy_any_run_with_empty_piece) {
    EXPECT_EQ((std::vector<std::string> {"ab", "a", "", "b"}), piecesOf("a**b", "ab"));
    EXPECT_EQ((std::vector<std::string> {"aXYZb", "a", "XYZ", "b"}), piecesOf("a**b", "aXYZb"));
    EXPECT_FALSE(matches("a**b", "abc"));
}

TEST(ListenPattern, classes_take_one_or_more) {
    EXPECT_TRUE(matches("*n", "123"));
    EXPECT_FALSE(matches("*n", "12a"));
    EXPECT_FALSE(matches("*n", ""));
    EXPECT_TRUE(matches("*a", "ABC"));
    EXPECT_TRUE(matches("*w", "   "));
    EXPECT_FALSE(matches("*w", "\t"));
    EXPECT_TRUE(matches("*p", "!?."));
    // Lowercased, *A is *a: letters only.
    EXPECT_FALSE(matches("*A", "ab12"));
}

TEST(ListenPattern, alternation_and_grouping) {
    EXPECT_TRUE(matches("foo|bar", "foo"));
    EXPECT_TRUE(matches("foo|bar", "bar"));
    EXPECT_FALSE(matches("foo|bar", "foobar"));
    EXPECT_TRUE(matches("(foo|bar)baz", "foobaz"));
    EXPECT_TRUE(matches("(foo|bar)baz", "barbaz"));
    EXPECT_FALSE(matches("(foo|bar)baz", "baz"));
}

// The right side of | goes first. When it leaves a class nothing to take, the
// whole match fails without trying the left side.
TEST(ListenPattern, right_branch_first_and_class_at_end_fails_whole_match) {
    EXPECT_TRUE(matches("(12|1)*n", "12"));
    EXPECT_FALSE(matches("(1|12)*n", "12"));
}

TEST(ListenPattern, parse_errors_match_nothing) {
    ShoutHarness harness;
    for (const std::string pattern : {"(foo", "foo)", "|foo", "foo*", "*x"}) {
        SCOPED_TRACE(pattern);
        EXPECT_FALSE(ListenPattern::parse(pattern));
        EXPECT_EQ(0, harness.call(kTestStringAgainstPattern, {Variable::ofString(pattern), Variable::ofString("foo")}).intValue);
    }
}

TEST(ListenPattern, empty_pattern_matches_nothing) {
    ShoutHarness harness;
    EXPECT_FALSE(ListenPattern::parse(""));
    EXPECT_EQ(0, harness.call(kTestStringAgainstPattern, {Variable::ofString(""), Variable::ofString("")}).intValue);
}

// Patterns an object keeps

TEST(ListenPatterns, same_number_replaces_in_place) {
    ShoutHarness harness;
    auto object = harness.game->newCreature();
    object->setListenPattern("foo", 1);
    object->setListenPattern("bar", 2);
    object->setListenPattern("baz", 1);

    const auto &expressions = object->listenExpressions();
    ASSERT_EQ(2u, expressions.size());
    EXPECT_EQ(1, expressions[0].number);
    EXPECT_EQ("baz", expressions[0].pattern);
    EXPECT_EQ(2, expressions[1].number);
    EXPECT_EQ("bar", expressions[1].pattern);
    std::vector<std::string> pieces;
    EXPECT_FALSE(object->testListenExpressions("foo", pieces));
    EXPECT_EQ(std::optional<int32_t>(1), object->testListenExpressions("baz", pieces));
}

TEST(ListenPatterns, first_registered_wins) {
    ShoutHarness harness;
    std::vector<std::string> pieces;

    auto first = harness.game->newCreature();
    first->setListenPattern("foo", 1);
    first->setListenPattern("foo", 2);
    EXPECT_EQ(std::optional<int32_t>(1), first->testListenExpressions("foo", pieces));

    auto second = harness.game->newCreature();
    second->setListenPattern("**", 5);
    second->setListenPattern("foo", 6);
    EXPECT_EQ(std::optional<int32_t>(5), second->testListenExpressions("foo", pieces));
}

TEST(ListenPatterns, failed_reparse_keeps_entry_that_matches_nothing) {
    ShoutHarness harness;
    auto object = harness.game->newCreature();
    object->setListenPattern("foo", 1);
    object->setListenPattern("foo*", 1);

    ASSERT_EQ(1u, object->listenExpressions().size());
    EXPECT_EQ("foo", object->listenExpressions()[0].pattern);
    std::vector<std::string> pieces;
    EXPECT_FALSE(object->testListenExpressions("foo", pieces));
}

TEST(ListenPatterns, patterns_are_stored_lowercased) {
    ShoutHarness harness;
    auto object = harness.game->newCreature();
    object->setListenPattern("GEN_I_WAS_ATTACKED", 1);

    ASSERT_EQ(1u, object->listenExpressions().size());
    EXPECT_EQ("gen_i_was_attacked", object->listenExpressions()[0].pattern);
}

// Routines

TEST(ListenRoutines, invalid_object_is_silent_and_non_creatures_work) {
    ShoutHarness harness;
    const auto invalid = Variable::ofObject(kObjectInvalid);
    EXPECT_NO_THROW(harness.call(kSetListening, {invalid, Variable::ofInt(1)}));
    EXPECT_NO_THROW(harness.call(kSetListenPattern, {invalid, Variable::ofString("foo"), Variable::ofInt(3)}));
    EXPECT_EQ(0, harness.call(kGetIsListening, {invalid}).intValue);

    auto placeable = harness.game->newPlaceable();
    const auto target = Variable::ofObject(placeable->id());
    harness.call(kSetListening, {target, Variable::ofInt(1)});
    EXPECT_EQ(1, harness.call(kGetIsListening, {target}).intValue);
    harness.call(kSetListenPattern, {target, Variable::ofString("foo"), Variable::ofInt(3)});
    ASSERT_EQ(1u, placeable->listenExpressions().size());
    EXPECT_EQ(3, placeable->listenExpressions()[0].number);
    EXPECT_EQ("foo", placeable->listenExpressions()[0].pattern);
}

TEST(ListenRoutines, test_string_against_pattern_is_one_or_zero_and_stateless) {
    ShoutHarness harness;
    auto caller = harness.game->newCreature();
    caller->receiveConversationEvent({kObjectInvalid, 1, {"EARLIER"}});
    ASSERT_EQ(std::vector<std::string> {"EARLIER"}, caller->matchedSubstrings());

    EXPECT_EQ(1, harness.call(kTestStringAgainstPattern, {Variable::ofString("gen_*n"), Variable::ofString("GEN_7")}, caller->id()).intValue);
    EXPECT_EQ(0, harness.call(kTestStringAgainstPattern, {Variable::ofString("foo"), Variable::ofString("bar")}, caller->id()).intValue);
    EXPECT_EQ(std::vector<std::string> {"EARLIER"}, caller->matchedSubstrings());
}

TEST(ListenRoutines, set_associate_listen_patterns_is_a_no_op) {
    ShoutHarness harness;
    auto creature = harness.game->newCreature();
    EXPECT_NO_THROW(harness.call(kSetAssociateListenPatterns, {Variable::ofObject(kObjectInvalid)}));
    EXPECT_NO_THROW(harness.call(kSetAssociateListenPatterns, {Variable::ofObject(creature->id())}, creature->id()));
}

TEST(ListenRoutines, event_conversation_is_type_7_without_parameters) {
    ShoutHarness harness;
    auto result = harness.call(kEventConversation, {});
    auto event = std::static_pointer_cast<Event>(result.engineType);
    ASSERT_TRUE(event);
    EXPECT_EQ(7, event->number());
    EXPECT_TRUE(event->integers().empty());
    EXPECT_TRUE(event->strings().empty());
}

TEST(ListenRoutines, initial_state) {
    ShoutHarness harness;
    auto creature = harness.game->newCreature();
    EXPECT_FALSE(creature->isListening());
    EXPECT_EQ(0, creature->listenPatternNumber());
    EXPECT_EQ(kObjectInvalid, creature->lastSpeaker());
    EXPECT_TRUE(creature->matchedSubstrings().empty());

    EXPECT_EQ(0, harness.call(kGetListenPatternNumber, {}, creature->id()).intValue);
    EXPECT_EQ(kObjectInvalid, harness.call(kGetLastSpeaker, {}, creature->id()).objectId);
    EXPECT_EQ(0, harness.call(kGetMatchedSubstringsCount, {}, creature->id()).intValue);
    EXPECT_EQ("", harness.call(kGetMatchedSubstring, {Variable::ofInt(0)}, creature->id()).strValue);
}

// Broadcast and delivery

TEST(Shout, speak_string_queues_event_7_at_speak_time) {
    ShoutHarness harness;
    harness.enterArea();
    auto speaker = harness.addCreature({0.0f, 0.0f, 0.0f});
    auto listener = harness.addListener(speaker, {5.0f, 0.0f, 0.0f});

    harness.speak(*speaker, "GEN_I_WAS_ATTACKED", kSilentShout);

    auto events = harness.conversationEvents();
    ASSERT_EQ(1u, events.size());
    EXPECT_EQ(listener->id(), events[0].object.id);
    EXPECT_EQ(speaker->id(), events[0].caller.id);
    EXPECT_EQ((std::vector<int32_t> {2, 1}), payloadOf(events[0]).integers);
    EXPECT_EQ((std::vector<std::string> {"", "GEN_I_WAS_ATTACKED"}), payloadOf(events[0]).strings);

    // The listeners were chosen when the words were spoken.
    harness.call(kSetListening, {Variable::ofObject(listener->id()), Variable::ofInt(0)});
    EXPECT_EQ(1u, harness.conversationEvents().size());
}

TEST(Shout, delivery_sets_fields_and_runs_on_dialog) {
    ShoutHarness harness;
    harness.enterArea();
    auto speaker = harness.addCreature({0.0f, 0.0f, 0.0f});
    auto listener = harness.addListener(speaker, {5.0f, 0.0f, 0.0f});
    listener->setOnDialogue("l_dialog");
    harness.countScriptRuns("l_dialog");

    harness.speak(*speaker, "GEN_I_WAS_ATTACKED", kSilentShout);
    harness.deliver();

    EXPECT_EQ(speaker->id(), listener->lastSpeaker());
    EXPECT_EQ(1, listener->listenPatternNumber());
    EXPECT_EQ(std::vector<std::string> {"GEN_I_WAS_ATTACKED"}, listener->matchedSubstrings());
    EXPECT_EQ(1, harness.call(kGetMatchedSubstringsCount, {}, listener->id()).intValue);
    EXPECT_EQ("GEN_I_WAS_ATTACKED", harness.call(kGetMatchedSubstring, {Variable::ofInt(0)}, listener->id()).strValue);
    EXPECT_EQ("", harness.call(kGetMatchedSubstring, {Variable::ofInt(1)}, listener->id()).strValue);
    EXPECT_EQ(1, harness.call(kGetListenPatternNumber, {}, listener->id()).intValue);
    EXPECT_EQ(speaker->id(), harness.call(kGetLastSpeaker, {}, listener->id()).objectId);
    EXPECT_EQ(1, harness.scriptRuns("l_dialog"));
}

// A script run by a later event, such as a user-defined one, reads what the
// conversation event left on the object.
TEST(Shout, fields_persist_into_a_later_event) {
    ShoutHarness harness;
    harness.enterArea();
    auto speaker = harness.addCreature({0.0f, 0.0f, 0.0f});
    auto listener = harness.addListener(speaker, {5.0f, 0.0f, 0.0f});

    harness.speak(*speaker, "GEN_I_WAS_ATTACKED", kSilentShout);
    harness.deliver();

    EXPECT_EQ(1, harness.call(kGetListenPatternNumber, {}, listener->id()).intValue);
    EXPECT_EQ(speaker->id(), harness.call(kGetLastSpeaker, {}, listener->id()).objectId);
}

TEST(Shout, on_dialog_fallback) {
    ShoutHarness harness;
    harness.enterArea();
    harness.countScriptRuns(kFallbackScript);
    auto converse = [&harness](Object &target) {
        harness.game->queueScriptEvent(target, std::nullopt, Event(7));
        harness.deliver();
    };

    for (const std::string script : {"", "default"}) {
        SCOPED_TRACE(script);
        auto creature = harness.game->newCreature();
        creature->setOnDialogue(script);
        const int before = harness.scriptRuns(kFallbackScript);
        converse(*creature);
        EXPECT_EQ(before + 1, harness.scriptRuns(kFallbackScript));
        EXPECT_EQ(kFallbackScript, creature->onDialogue());
    }

    auto placeable = harness.game->newPlaceable();
    placeable->setOnDialog("");
    int before = harness.scriptRuns(kFallbackScript);
    converse(*placeable);
    EXPECT_EQ(before + 1, harness.scriptRuns(kFallbackScript));

    auto silentDoor = harness.game->newDoor();
    silentDoor->setOnDialog("");
    before = harness.scriptRuns(kFallbackScript);
    converse(*silentDoor);
    EXPECT_EQ(before, harness.scriptRuns(kFallbackScript));

    auto door = harness.game->newDoor();
    door->setOnDialog("default");
    converse(*door);
    EXPECT_EQ(before + 1, harness.scriptRuns(kFallbackScript));
}

TEST(Shout, volume_radius_and_perception) {
    struct Case {
        int volume;
        float distance;
        bool heard;
        bool seen;
        bool listening;
        bool delivered;
    };
    const std::vector<Case> cases {
        {kTalk, 30.0f, true, false, true, true},
        {kWhisper, 2.5f, true, false, true, true},
        {kWhisper, 4.0f, true, false, true, false},
        {kShout, 240.0f, true, false, true, true},
        {kShout, 260.0f, true, false, true, false},
        {kSilentTalk, 9.0f, true, false, true, true},
        {kSilentTalk, 20.0f, true, false, true, false},
        {kSilentTalk, 20.0f, true, true, true, true},
        {kSilentTalk, 36.0f, true, true, true, false},
        {kSilentShout, 900.0f, true, false, true, true},
        {kTalk, 5.0f, false, false, true, false},
        {kTalk, 5.0f, true, false, false, false},
        {7, 30.0f, true, false, true, true},
    };
    ShoutHarness harness;
    for (size_t i = 0; i < cases.size(); ++i) {
        SCOPED_TRACE(i);
        const Case &c = cases[i];
        harness.enterArea();
        auto speaker = harness.addCreature({0.0f, 0.0f, 0.0f});
        auto listener = harness.addListener(speaker, {c.distance, 0.0f, 0.0f});
        listener->setObjectHeard(speaker, c.heard);
        listener->setObjectSeen(speaker, c.seen);
        listener->setListening(c.listening);

        EXPECT_NO_THROW(harness.speak(*speaker, "GEN_I_WAS_ATTACKED", c.volume));

        EXPECT_EQ(c.delivered ? 1u : 0u, harness.conversationEvents().size());
    }
}

TEST(Shout, speaker_and_other_areas_excluded) {
    ShoutHarness harness;
    harness.enterArea();
    auto speaker = harness.addCreature({0.0f, 0.0f, 0.0f});
    speaker->setListening(true);
    speaker->setListenPattern("GEN_I_WAS_ATTACKED", 1);

    auto elsewhere = harness.game->newArea();
    auto distant = harness.game->newCreature();
    distant->setPosition({5.0f, 0.0f, 0.0f});
    elsewhere->add(distant);
    distant->setListening(true);
    distant->setListenPattern("GEN_I_WAS_ATTACKED", 1);
    distant->setObjectHeard(speaker, true);

    harness.speak(*speaker, "GEN_I_WAS_ATTACKED", kTalk);

    EXPECT_TRUE(harness.conversationEvents().empty());
}

TEST(Shout, non_creature_speaker_uses_listener_hearing_range) {
    ShoutHarness harness;
    for (const auto &[distance, delivered] : std::vector<std::pair<float, bool>> {{8.0f, true}, {12.0f, false}}) {
        SCOPED_TRACE(distance);
        harness.enterArea();
        auto speaker = harness.place(harness.game->newPlaceable(), {0.0f, 0.0f, 0.0f});
        auto listener = harness.addCreature({distance, 0.0f, 0.0f});
        listener->setListening(true);
        listener->setListenPattern("GEN_I_WAS_ATTACKED", 1);
        TestGameModule::setPerceptionRanges(*listener, 10.0f, 10.0f);

        harness.speak(*speaker, "GEN_I_WAS_ATTACKED", kTalk);

        EXPECT_EQ(delivered ? 1u : 0u, harness.conversationEvents().size());
    }
}

TEST(Shout, non_creature_listener_has_no_perception_filter) {
    ShoutHarness harness;
    harness.enterArea();
    auto speaker = harness.addCreature({0.0f, 0.0f, 0.0f});
    auto listener = harness.place(harness.game->newPlaceable(), {30.0f, 0.0f, 0.0f});
    listener->setListening(true);
    listener->setListenPattern("GEN_I_WAS_ATTACKED", 4);

    harness.speak(*speaker, "GEN_I_WAS_ATTACKED", kTalk);
    ASSERT_EQ(1u, harness.conversationEvents().size());
    harness.deliver();

    EXPECT_EQ(4, listener->listenPatternNumber());
    EXPECT_EQ(std::vector<std::string> {"GEN_I_WAS_ATTACKED"}, listener->matchedSubstrings());
    // Only a creature answers who spoke last.
    EXPECT_EQ(kObjectInvalid, harness.call(kGetLastSpeaker, {}, listener->id()).objectId);
}

TEST(Shout, one_event_per_listener_first_match) {
    ShoutHarness harness;
    harness.enterArea();
    auto speaker = harness.addCreature({0.0f, 0.0f, 0.0f});
    auto listener = harness.addListener(speaker, {5.0f, 0.0f, 0.0f}, "**", 7);
    listener->setListenPattern("GEN_X", 8);

    harness.speak(*speaker, "GEN_X", kTalk);

    auto events = harness.conversationEvents();
    ASSERT_EQ(1u, events.size());
    ASSERT_EQ(2u, payloadOf(events[0]).integers.size());
    EXPECT_EQ(7, payloadOf(events[0]).integers[1]);
}

TEST(Shout, action_speak_string_broadcasts_when_action_runs) {
    ShoutHarness harness;
    harness.enterArea();
    auto speaker = harness.addCreature({0.0f, 0.0f, 0.0f});
    auto strRefSpeaker = harness.addCreature({1.0f, 0.0f, 0.0f});
    auto listener = harness.addListener(speaker, {5.0f, 0.0f, 0.0f});
    listener->setObjectHeard(strRefSpeaker, true);

    // Words spoken by string reference reach no one.
    harness.call(kActionSpeakStringByStrRef, {Variable::ofInt(1), Variable::ofInt(kTalk)}, strRefSpeaker->id());
    auto byStrRef = strRefSpeaker->getCurrentAction();
    ASSERT_TRUE(byStrRef);
    byStrRef->execute(byStrRef, *strRefSpeaker, 0.0f);
    EXPECT_TRUE(harness.conversationEvents().empty());

    harness.call(kActionSpeakString, {Variable::ofString("GEN_I_WAS_ATTACKED"), Variable::ofInt(kTalk)}, speaker->id());
    EXPECT_TRUE(harness.conversationEvents().empty());
    auto action = speaker->getCurrentAction();
    ASSERT_TRUE(action);
    action->execute(action, *speaker, 0.0f);
    EXPECT_EQ(1u, harness.conversationEvents().size());
}

TEST(Shout, signal_event_conversation) {
    ShoutHarness harness;
    harness.enterArea();
    auto speaker = harness.addCreature({0.0f, 0.0f, 0.0f});
    auto listener = harness.addListener(speaker, {5.0f, 0.0f, 0.0f});
    listener->setOnDialogue("l_dialog");
    harness.countScriptRuns("l_dialog");
    harness.speak(*speaker, "GEN_I_WAS_ATTACKED", kTalk);
    harness.deliver();
    ASSERT_FALSE(listener->matchedSubstrings().empty());
    auto signaller = harness.addCreature({1.0f, 0.0f, 0.0f});

    auto event = harness.call(kEventConversation, {});
    harness.call(kSignalEvent, {Variable::ofObject(listener->id()), event}, signaller->id());
    harness.deliver();

    EXPECT_EQ(0, listener->listenPatternNumber());
    EXPECT_EQ(signaller->id(), listener->lastSpeaker());
    EXPECT_TRUE(listener->matchedSubstrings().empty());
    EXPECT_EQ(2, harness.scriptRuns("l_dialog"));
}

TEST(Shout, destroyed_listener_is_dropped) {
    ShoutHarness harness;
    harness.enterArea();
    auto speaker = harness.addCreature({0.0f, 0.0f, 0.0f});
    auto listener = harness.addListener(speaker, {5.0f, 0.0f, 0.0f});
    listener->setOnDialogue("l_dialog");
    harness.countScriptRuns("l_dialog");
    harness.speak(*speaker, "GEN_I_WAS_ATTACKED", kTalk);
    ASSERT_EQ(1u, harness.conversationEvents().size());

    harness.game->destroyRuntimeObjectGraph(listener);
    EXPECT_NO_THROW(harness.deliver());

    EXPECT_EQ(0, harness.scriptRuns("l_dialog"));
    EXPECT_TRUE(speaker->isRuntimeLive());
}

// Saving

TEST(ListenSave, expression_list_round_trips) {
    ShoutHarness harness(GameID::TSL);
    auto &game = *harness.game;
    auto area = game.newArea();
    auto player = game.newCreature();
    TestGameModule::configureModuleSnapshot(game, area, player, "301nar", "301nar");
    TestGameModule::addSnapshotObject(*area, player);
    auto empty = Gff::Builder().type(0xffffffff).build();
    game.captureSaveResourceShadow({SaveResourceKind::ModuleIfo, "301nar"}, *empty);
    game.captureSaveResourceShadow({SaveResourceKind::AreaAre, "301nar"}, *empty);
    game.captureSaveResourceShadow({SaveResourceKind::AreaGit, "301nar"}, *empty);

    auto listener = game.newCreature();
    listener->setPC(false);
    listener->setListening(true);
    listener->setListenPattern("GEN_A", 1);
    listener->setListenPattern("GEN_B", 15);
    listener->setListenPattern("foo", 2);
    listener->setListenPattern("foo*", 2);
    TestGameModule::addSnapshotObject(*area, listener);
    auto deaf = game.newCreature();
    deaf->setPC(false);
    TestGameModule::addSnapshotObject(*area, deaf);

    auto saved = ModuleSnapshotBuilder(game, "301nar").build();
    ASSERT_TRUE(saved) << saved.message;

    auto record = creatureRecord(*saved.snapshot->git, listener->id());
    ASSERT_TRUE(record);
    ASSERT_TRUE(record->has("ExpressionList"));
    const auto expressions = record->getList("ExpressionList");
    ASSERT_EQ(3u, expressions.size());
    const std::vector<std::pair<int32_t, std::string>> expected {{1, "gen_a"}, {15, "gen_b"}, {2, "foo"}};
    for (size_t i = 0; i < expected.size(); ++i) {
        SCOPED_TRACE(i);
        EXPECT_EQ(5u, expressions[i]->type());
        EXPECT_EQ(expected[i].first, expressions[i]->getInt("ExpressionId"));
        EXPECT_EQ(expected[i].second, expressions[i]->getString("ExpressionString"));
    }
    auto deafRecord = creatureRecord(*saved.snapshot->git, deaf->id());
    ASSERT_TRUE(deafRecord);
    EXPECT_FALSE(deafRecord->has("ExpressionList"));

    ON_CALL(harness.engine.resourceModule().twoDas(), get("appearance"))
        .WillByDefault(Return(std::shared_ptr<TwoDA>(TwoDA::Builder()
                                                         .columns({"label", "modeltype"})
                                                         .row({"listener", "S"})
                                                         .build())));
    auto restored = game.newCreature();
    restored->deserialize(*record, SerializedIdentityContext::templateResource());
    EXPECT_TRUE(restored->isListening());
    ASSERT_EQ(3u, restored->listenExpressions().size());
    for (size_t i = 0; i < expected.size(); ++i) {
        SCOPED_TRACE(i);
        EXPECT_EQ(expected[i].first, restored->listenExpressions()[i].number);
        EXPECT_EQ(expected[i].second, restored->listenExpressions()[i].pattern);
    }
    std::vector<std::string> pieces;
    EXPECT_EQ(std::optional<int32_t>(15), restored->testListenExpressions("gen_b", pieces));
}
