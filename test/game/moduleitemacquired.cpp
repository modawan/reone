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

#include <vector>

#include "../fixtures/engine.h"
#include "../fixtures/scene.h"

#include "reone/game/action/giveitem.h"
#include "reone/game/action/takeitem.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/party.h"
#include "reone/game/script/routines.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include "reone/script/executioncontext.h"
#include "reone/script/program.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;
using namespace testing;

namespace {

constexpr char kAcquireScript[] = "mod_acq_probe";
constexpr char kDatapadTemplate[] = "acq_datapad";
constexpr int kCreditsBaseItem = 0;
constexpr int kDatapadBaseItem = 1;

// What the module's acquire script saw each time it ran.
struct AcquireRun {
    uint32_t acquired {kObjectInvalid};
    uint32_t from {kObjectInvalid};
    // The acquired item's holder and stack size when the script ran.
    uint32_t holder {kObjectInvalid};
    int stackSize {0};
    std::string tag;
};

// Static so the stub on the shared scripts mock stays valid past its test.
std::vector<AcquireRun> &acquireRuns() {
    static std::vector<AcquireRun> runs;
    return runs;
}

Game *&probedGame() {
    static Game *game = nullptr;
    return game;
}

std::shared_ptr<TwoDA> makeBaseItemsTable() {
    TwoDA::Builder builder;
    builder.columns({"itemclass", "itemtype", "stacking"});
    builder.row({"I_Credits", "23", "9999"});
    builder.row({"I_Datapad", "", "99"});
    return std::shared_ptr<TwoDA>(builder.build());
}

std::shared_ptr<Gff> makeItemRecord(const std::string &tag, int baseItem, int stackSize) {
    return Gff::Builder()
        .field(Gff::Field::newCExoString("Tag", tag))
        .field(Gff::Field::newInt("BaseItem", baseItem))
        .field(Gff::Field::newWord("StackSize", stackSize))
        .build();
}

class AcquireHarness : boost::noncopyable {
public:
    explicit AcquireHarness(std::string acquireScript = kAcquireScript, TestEngine &engine = testEngine()) :
        engine(engine),
        game(GameID::KotOR, "", engine.options(), engine.services(), console),
        routines(GameID::KotOR, &game, &engine.services()) {

        EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
            .Times(AnyNumber())
            .WillRepeatedly(Return(makeBaseItemsTable()));
        EXPECT_CALL(engine.resourceModule().textures(), get(_, _)).Times(AnyNumber());
        EXPECT_CALL(engine.resourceModule().gffs(), get(kDatapadTemplate, ResType::Uti))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([](const std::string &, ResType) {
                return makeItemRecord("acq_datapad", kDatapadBaseItem, 1);
            }));
        game.initLocalServices();
        routines.init();
        area = game.newArea();
        TestGameModule::setActiveModuleArea(game, area);
        auto ifo = Gff::Builder()
                       .field(Gff::Field::newResRef("Mod_Entry_Area", "entryarea"))
                       .field(Gff::Field::newResRef("Mod_OnAcquirItem", acquireScript))
                       .build();
        TestGameModule::loadModuleInfo(*game.module(), "acqmodule", *ifo);

        player = game.newCreature();
        companion = game.newCreature();
        auto &party = game.party();
        party.addMember(kNpcPlayer, player);
        party.setPlayer(player);
        party.addAvailableMember(0, companion);
        party.addMember(0, companion);

        acquireRuns().clear();
        probedGame() = &game;
        EXPECT_CALL(engine.resourceModule().scripts(), get(kAcquireScript))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([](const std::string &) {
                Game &game = *probedGame();
                const auto &events = game.module()->itemEvents();
                AcquireRun run;
                run.acquired = events.acquired;
                run.from = events.acquiredFrom;
                if (auto item = game.getObjectById<Item>(events.acquired)) {
                    run.holder = item->owner();
                    run.stackSize = item->stackSize();
                    run.tag = item->tag();
                }
                acquireRuns().push_back(run);
                return program;
            }));
    }

    ~AcquireHarness() {
        probedGame() = nullptr;
        program.reset();
    }

    std::shared_ptr<Item> makeItem(const std::string &tag, int baseItem = kDatapadBaseItem, int stackSize = 1) {
        auto item = game.newItem();
        item->deserialize(*makeItemRecord(tag, baseItem, stackSize), SerializedIdentityContext::templateResource());
        item->setDropable(true);
        return item;
    }

    std::shared_ptr<Item> giveTo(Object &owner, const std::string &tag, int baseItem = kDatapadBaseItem, int stackSize = 1) {
        auto item = makeItem(tag, baseItem, stackSize);
        owner.addItem(item);
        return item;
    }

    std::shared_ptr<Creature> addCreature() {
        auto creature = game.newCreature();
        creature->setPosition(glm::vec3(0.0f));
        return creature;
    }

    Variable call(const std::string &name, std::vector<Variable> args) {
        Routine &routine = routines.get(routines.getIndexByName(name));
        ExecutionContext execution;
        execution.routines = &routines;
        return routine.invoke(args, execution);
    }

    uint32_t createItemOn(const Object &target, const std::string &resRef = kDatapadTemplate) {
        return call("CreateItemOnObject", {Variable::ofString(resRef), Variable::ofObject(target.id())}).objectId;
    }

    // The acquire events still waiting in the module.
    size_t pendingAcquireEvents() const {
        size_t count = 0;
        for (const auto &record : game.module()->saveEventSnapshot()) {
            const auto *event = std::get_if<SavedScriptEvent>(&record.payload);
            if (record.eventId == static_cast<uint32_t>(SavedEventType::SignalEvent) && event &&
                event->type == static_cast<uint16_t>(SavedSignalEventType::ItemAcquired)) {
                ++count;
            }
        }
        return count;
    }

    void deliver() {
        game.module()->dispatchDueSavedEvents();
    }

    TestEngine &engine;
    StubConsole console;
    Game game;
    Routines routines;
    std::shared_ptr<Area> area;
    std::shared_ptr<Creature> player;
    std::shared_ptr<Creature> companion;
    // What the acquire script resolves to; none by default, so only its runs count.
    static inline std::shared_ptr<ScriptProgram> program;
};

} // namespace

// Section 5, test 1: a script creating a plot item on the player character.
TEST(ModuleItemAcquired, creating_an_item_on_the_player_runs_the_acquire_script_once) {
    AcquireHarness harness;

    const uint32_t created = harness.createItemOn(*harness.player);
    ASSERT_NE(kObjectInvalid, created);
    EXPECT_EQ(0u, acquireRuns().size()) << "the script runs once the move is over, not inside it";
    harness.deliver();

    ASSERT_EQ(1u, acquireRuns().size());
    EXPECT_EQ(created, acquireRuns()[0].acquired);
    EXPECT_EQ(kObjectInvalid, acquireRuns()[0].from);
    EXPECT_EQ(harness.player->id(), acquireRuns()[0].holder);
    EXPECT_EQ(0u, harness.pendingAcquireEvents());
}

// Test 2: party members share one inventory, which the player character holds.
TEST(ModuleItemAcquired, creating_an_item_on_a_companion_acquires_it_into_the_shared_inventory) {
    AcquireHarness harness;

    const uint32_t created = harness.createItemOn(*harness.companion);
    harness.deliver();

    ASSERT_EQ(1u, acquireRuns().size());
    EXPECT_EQ(created, acquireRuns()[0].acquired);
    EXPECT_EQ(harness.player->id(), acquireRuns()[0].holder);
}

// Test 3: a container with one item, taken through the container screen.
TEST(ModuleItemAcquired, taking_from_a_container_acquires_the_item_from_the_container) {
    AcquireHarness harness;
    auto container = harness.game.newPlaceable();
    auto spice = harness.giveTo(*container, "ebo_spice");

    container->moveDropableItemsTo(*harness.player);
    harness.deliver();

    ASSERT_EQ(1u, acquireRuns().size());
    EXPECT_EQ(spice->id(), acquireRuns()[0].acquired);
    EXPECT_EQ(container->id(), acquireRuns()[0].from);
    EXPECT_EQ(harness.player->id(), acquireRuns()[0].holder);
}

// Test 4: every item of a take-all is its own event, in the order they move.
TEST(ModuleItemAcquired, take_all_acquires_each_item_in_move_order) {
    AcquireHarness harness;
    auto corpse = harness.game.newPlaceable();
    auto first = harness.giveTo(*corpse, "kas24_bowcasbolt");
    auto second = harness.giveTo(*corpse, "acq_second");
    auto third = harness.giveTo(*corpse, "acq_third");

    // The take-all on closing a container moves the last item first.
    corpse->moveAllItemsTo(*harness.player);
    harness.deliver();

    ASSERT_EQ(3u, acquireRuns().size());
    EXPECT_EQ(third->id(), acquireRuns()[0].acquired);
    EXPECT_EQ(second->id(), acquireRuns()[1].acquired);
    EXPECT_EQ(first->id(), acquireRuns()[2].acquired);
    for (const auto &run : acquireRuns()) {
        EXPECT_EQ(corpse->id(), run.from);
        EXPECT_EQ(harness.player->id(), run.holder);
    }
}

// Test 5: k_ptar_acquire05 reads the merged stack size of the journal it was told of.
TEST(ModuleItemAcquired, an_item_merged_into_a_stack_names_the_surviving_stack_to_the_script) {
    AcquireHarness harness;
    auto held = harness.game.newItemFromBlueprint(kDatapadTemplate);
    harness.player->addItem(held);
    ASSERT_EQ(1, held->stackSize());

    // The script itself asks the routines, as a compiled one does.
    auto program = std::make_shared<ScriptProgram>(kAcquireScript);
    const int setLocalNumber = harness.routines.getIndexByName("SetLocalNumber");
    const auto store = [&](int local) {
        program->add(Instruction::newCONSTI(local));
        program->add(Instruction::newCONSTO(static_cast<int>(kObjectSelf)));
        program->add(Instruction::newACTION(setLocalNumber, 3));
    };
    const int acquired = harness.routines.getIndexByName("GetModuleItemAcquired");
    const int acquiredFrom = harness.routines.getIndexByName("GetModuleItemAcquiredFrom");
    program->add(Instruction::newACTION(acquired, 0));
    program->add(Instruction::newCONSTO(static_cast<int>(held->id())));
    program->add(Instruction(InstructionType::EQUALOO));
    store(40);
    program->add(Instruction::newACTION(acquired, 0));
    program->add(Instruction::newACTION(harness.routines.getIndexByName("GetItemStackSize"), 1));
    store(41);
    program->add(Instruction::newACTION(acquiredFrom, 0));
    program->add(Instruction::newCONSTO(static_cast<int>(kObjectInvalid)));
    program->add(Instruction(InstructionType::EQUALOO));
    store(42);
    program->add(Instruction(InstructionType::RETN));
    AcquireHarness::program = program;

    const uint32_t answered = harness.createItemOn(*harness.player);
    harness.deliver();

    EXPECT_EQ(held->id(), answered);
    ASSERT_EQ(1u, acquireRuns().size());
    EXPECT_EQ("acq_datapad", acquireRuns()[0].tag);
    auto &module = *harness.game.module();
    EXPECT_EQ(1, module.getLocalNumber(40)) << "the surviving stack, not the merged-away copy";
    EXPECT_EQ(2, module.getLocalNumber(41));
    EXPECT_EQ(1, module.getLocalNumber(42));
}

// Test 6: k_ptat_givegaffi hands the chief's item over from a non-party creature.
TEST(ModuleItemAcquired, an_item_given_by_a_non_party_creature_is_acquired_from_it) {
    AcquireHarness harness;
    auto chief = harness.addCreature();
    auto item = harness.giveTo(*chief, "tat20_gaffichief");

    // The give is run by a placeable so it needs no walk; the item's holder,
    // not the one acting, is who it came from.
    auto action = harness.game.newAction<GiveItemAction>(item, harness.player, GiveItemAction::kWholeStack, true);
    action->execute(action, *harness.game.newPlaceable(), 0.0f);
    ASSERT_EQ(harness.player->id(), item->owner());
    harness.deliver();

    ASSERT_EQ(1u, acquireRuns().size());
    EXPECT_EQ(item->id(), acquireRuns()[0].acquired);
    EXPECT_EQ(chief->id(), acquireRuns()[0].from);
}

// The instant GiveItem routine is the same give.
TEST(ModuleItemAcquired, an_instant_give_from_a_non_party_creature_is_acquired_from_it) {
    AcquireHarness harness;
    auto npc = harness.addCreature();
    auto item = harness.giveTo(*npc, "tat20aa_westmap");

    harness.call("GiveItem", {Variable::ofObject(item->id()), Variable::ofObject(harness.player->id())});
    harness.deliver();

    ASSERT_EQ(1u, acquireRuns().size());
    EXPECT_EQ(item->id(), acquireRuns()[0].acquired);
    EXPECT_EQ(npc->id(), acquireRuns()[0].from);
}

// Test 7.
TEST(ModuleItemAcquired, an_item_taken_from_a_non_party_creature_is_acquired_from_it) {
    AcquireHarness harness;
    auto npc = harness.addCreature();
    auto item = harness.giveTo(*npc, "acq_taken");

    auto action = harness.game.newAction<TakeItemAction>(item, npc);
    action->execute(action, *harness.player, 0.0f);
    harness.deliver();

    ASSERT_EQ(1u, acquireRuns().size());
    EXPECT_EQ(item->id(), acquireRuns()[0].acquired);
    EXPECT_EQ(npc->id(), acquireRuns()[0].from);
}

// Test 9: possession does not change inside the party. The last create is a
// control: the counter sees an acquisition.
TEST(ModuleItemAcquired, moves_within_the_party_acquire_nothing) {
    AcquireHarness harness;
    auto item = harness.giveTo(*harness.player, "acq_party");

    auto give = harness.game.newAction<GiveItemAction>(item, harness.companion, GiveItemAction::kWholeStack, false);
    give->execute(give, *harness.game.newPlaceable(), 0.0f);
    auto take = harness.game.newAction<TakeItemAction>(item, harness.player);
    take->execute(take, *harness.companion, 0.0f);
    harness.call("GiveItem", {Variable::ofObject(item->id()), Variable::ofObject(harness.companion->id())});
    harness.call("GiveItem", {Variable::ofObject(item->id()), Variable::ofObject(harness.player->id())});
    harness.deliver();
    EXPECT_EQ(0u, acquireRuns().size());

    harness.createItemOn(*harness.player);
    harness.deliver();
    EXPECT_EQ(1u, acquireRuns().size());
}

// Test 10: k_pdan_14b_itmaq marks the diary plot done without checking
// possession, so the diary sitting in its spawned container must not tell it.
TEST(ModuleItemAcquired, items_filling_a_container_or_creature_acquire_nothing) {
    AcquireHarness harness;
    auto casus = harness.game.newPlaceable();
    auto diary = harness.giveTo(*casus, "dan14_diary");
    auto npc = harness.addCreature();
    harness.giveTo(*npc, "acq_carried");
    harness.deliver();
    EXPECT_EQ(0u, acquireRuns().size());

    casus->moveDropableItemsTo(*harness.player);
    harness.deliver();
    ASSERT_EQ(1u, acquireRuns().size());
    EXPECT_EQ(diary->id(), acquireRuns()[0].acquired);
}

// Test 11: items entering a non-party creature or a placeable.
TEST(ModuleItemAcquired, items_given_out_of_the_party_acquire_nothing) {
    AcquireHarness harness;
    auto npc = harness.addCreature();
    auto bodyBag = harness.game.newPlaceable();
    auto item = harness.giveTo(*harness.player, "acq_given_away");

    auto give = harness.game.newAction<GiveItemAction>(item, npc, GiveItemAction::kWholeStack, false);
    give->execute(give, *harness.game.newPlaceable(), 0.0f);
    ASSERT_EQ(npc->id(), item->owner());
    ASSERT_NE(kObjectInvalid, harness.createItemOn(*bodyBag));
    ASSERT_NE(kObjectInvalid, harness.createItemOn(*npc));
    npc->moveAllItemsTo(*bodyBag);
    harness.deliver();
    EXPECT_EQ(0u, acquireRuns().size());

    harness.call("GiveItem", {Variable::ofObject(item->id()), Variable::ofObject(harness.player->id())});
    harness.deliver();
    ASSERT_EQ(1u, acquireRuns().size());
    EXPECT_EQ(bodyBag->id(), acquireRuns()[0].from);
}

// Test 12: credits leave no item for the script to be told of.
TEST(ModuleItemAcquired, credits_acquire_nothing) {
    AcquireHarness harness;
    auto container = harness.game.newPlaceable();
    harness.giveTo(*container, "acq_credits", kCreditsBaseItem, 50);
    const int before = harness.game.party().gold();

    harness.call("GiveGoldToCreature", {Variable::ofObject(harness.player->id()), Variable::ofInt(100)});
    container->moveAllItemsTo(*harness.player);
    harness.deliver();
    EXPECT_EQ(before + 150, harness.game.party().gold());
    EXPECT_EQ(0u, acquireRuns().size());

    harness.createItemOn(*harness.player);
    harness.deliver();
    EXPECT_EQ(1u, acquireRuns().size());
}

// Test 14: with no acquire script the getters still follow the event.
TEST(ModuleItemAcquired, an_empty_acquire_script_still_records_the_item) {
    // An engine of its own, so the expectation below is checked when it goes.
    TestEngine engine;
    engine.init();
    NiceMock<scene::MockSceneGraph> sceneGraph;
    ON_CALL(engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(sceneGraph));
    EXPECT_CALL(engine.resourceModule().scripts(), get("")).Times(0);
    AcquireHarness harness("", engine);

    const uint32_t created = harness.createItemOn(*harness.player);
    harness.deliver();

    EXPECT_EQ(0u, acquireRuns().size());
    EXPECT_EQ(0u, harness.pendingAcquireEvents());
    EXPECT_EQ(created, harness.call("GetModuleItemAcquired", {}).objectId);
}

// Test 15: each run sees its own item, not the last one created.
TEST(ModuleItemAcquired, two_items_in_one_frame_are_two_events_each_with_its_own_item) {
    AcquireHarness harness;

    const uint32_t first = harness.call("CreateItemOnObject", {Variable::ofString(kDatapadTemplate), Variable::ofObject(harness.player->id())}).objectId;
    auto container = harness.game.newPlaceable();
    auto second = harness.giveTo(*container, "acq_other");
    harness.call("GiveItem", {Variable::ofObject(second->id()), Variable::ofObject(harness.player->id())});
    EXPECT_EQ(2u, harness.pendingAcquireEvents());
    harness.deliver();

    ASSERT_EQ(2u, acquireRuns().size());
    EXPECT_EQ(first, acquireRuns()[0].acquired);
    EXPECT_EQ(kObjectInvalid, acquireRuns()[0].from);
    EXPECT_EQ(second->id(), acquireRuns()[1].acquired);
    EXPECT_EQ(container->id(), acquireRuns()[1].from);
}

// Test 18, and an item gone by the time a script asks.
TEST(ModuleItemAcquired, the_getters_name_nothing_before_an_event_or_once_the_item_is_gone) {
    AcquireHarness harness;
    auto container = harness.game.newPlaceable();
    auto item = harness.giveTo(*container, "acq_gone");
    EXPECT_EQ(kObjectInvalid, harness.call("GetModuleItemAcquired", {}).objectId);
    EXPECT_EQ(kObjectInvalid, harness.call("GetModuleItemAcquiredFrom", {}).objectId);

    container->moveAllItemsTo(*harness.player);
    harness.deliver();
    ASSERT_EQ(item->id(), harness.call("GetModuleItemAcquired", {}).objectId);
    ASSERT_EQ(container->id(), harness.call("GetModuleItemAcquiredFrom", {}).objectId);

    bool last = false;
    harness.player->removeItem(item, last);
    harness.game.destroyRuntimeObjectGraph(item);
    harness.game.destroyRuntimeObjectGraph(container);
    EXPECT_EQ(kObjectInvalid, harness.call("GetModuleItemAcquired", {}).objectId);
    EXPECT_EQ(kObjectInvalid, harness.call("GetModuleItemAcquiredFrom", {}).objectId);
}

// The GiveItem routine hands an item for a party member to the party's
// inventory, the same as the give action does.
TEST(ModuleItemAcquired, an_instant_give_to_a_companion_lands_in_the_party_inventory) {
    AcquireHarness harness;
    auto npc = harness.addCreature();
    auto item = harness.giveTo(*npc, "acq_for_companion");

    harness.call("GiveItem", {Variable::ofObject(item->id()), Variable::ofObject(harness.companion->id())});
    harness.deliver();

    EXPECT_EQ(harness.player->id(), item->owner());
    ASSERT_EQ(1u, acquireRuns().size());
    EXPECT_EQ(item->id(), acquireRuns()[0].acquired);
    EXPECT_EQ(npc->id(), acquireRuns()[0].from);
    EXPECT_EQ(harness.player->id(), acquireRuns()[0].holder);
}

// A companion joining hands its carried items to the party; that is not an
// acquisition.
TEST(ModuleItemAcquired, a_joining_companions_items_acquire_nothing) {
    AcquireHarness harness;
    auto recruit = harness.addCreature();
    auto carried = harness.giveTo(*recruit, "acq_carried_in");

    harness.game.party().addAvailableMember(1, recruit);
    harness.game.party().addMember(1, recruit);
    harness.deliver();
    ASSERT_EQ(harness.player->id(), carried->owner());
    EXPECT_EQ(0u, acquireRuns().size());

    harness.createItemOn(*harness.player);
    harness.deliver();
    EXPECT_EQ(1u, acquireRuns().size());
}

// Section 3.4: an item inside other items came from the holder of the
// outermost one, and from nowhere when that one lies on the ground.
TEST(ModuleItemAcquired, an_item_inside_items_came_from_the_outermost_holder) {
    AcquireHarness harness;
    auto npc = harness.addCreature();
    auto bag = harness.giveTo(*npc, "acq_bag");
    auto pouch = harness.giveTo(*bag, "acq_pouch");
    auto gem = harness.giveTo(*pouch, "acq_gem");
    auto groundBag = harness.makeItem("acq_ground_bag");
    auto lying = harness.giveTo(*groundBag, "acq_lying");

    EXPECT_EQ(npc, harness.game.itemHolder(*gem));
    EXPECT_EQ(npc, harness.game.itemHolder(*bag));
    EXPECT_EQ(nullptr, harness.game.itemHolder(*lying));
    EXPECT_EQ(nullptr, harness.game.itemHolder(*groundBag));

    harness.call("GiveItem", {Variable::ofObject(gem->id()), Variable::ofObject(harness.player->id())});
    harness.call("GiveItem", {Variable::ofObject(lying->id()), Variable::ofObject(harness.player->id())});
    harness.deliver();

    ASSERT_EQ(2u, acquireRuns().size());
    EXPECT_EQ(gem->id(), acquireRuns()[0].acquired);
    EXPECT_EQ(npc->id(), acquireRuns()[0].from);
    EXPECT_EQ(lying->id(), acquireRuns()[1].acquired);
    EXPECT_EQ(kObjectInvalid, acquireRuns()[1].from);
}
