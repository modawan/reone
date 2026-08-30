/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <gtest/gtest.h>

#include "../fixtures/engine.h"

#include "reone/audio/clip.h"
#include "reone/game/game.h"
#include "reone/game/script/routines.h"
#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

#include <map>
#include <set>

using namespace reone;
using namespace reone::game;
using namespace reone::game::pazaak;
using namespace reone::resource;

void reone::game::TestGameModule::configurePazaak(
    Game &game,
    bool guiLoadSucceeds,
    PazaakSession::HandSelector playerSelector,
    PazaakSession::HandSelector opponentSelector,
    PazaakSession::MainDeckFactory mainDeckFactory,
    std::function<void(const std::string &, uint32_t)> continuation) {

    game._pazaakGuiLoadOverride = [guiLoadSucceeds]() { return guiLoadSucceeds; };
    game._pazaakPlayerHandSelector = std::move(playerSelector);
    game._pazaakOpponentHandSelector = std::move(opponentSelector);
    game._pazaakMainDeckFactory = std::move(mainDeckFactory);
    game._pazaakFirstParticipantSelector = [](size_t) { return Participant::One; };
    game._pazaakPaceAutomaticDraws = false;
    game._pazaakContinuationOverride = std::move(continuation);
    game._pazaakOpponentDeckOverride = PazaakSession::temporaryK1OpponentSideDeck();
    // Own two of every card type the running title stores; entries beyond the
    // title's table stay zero so a save round trip is byte-for-byte stable.
    size_t cardCount = game.isTSL() ? Party::kK2PazaakCardCount : Party::kK1PazaakCardCount;
    Party::PazaakCardCounts counts {};
    for (size_t i = 0; i < cardCount; ++i) {
        counts[i] = 2;
    }
    Party::PazaakSideDeck sideDeck;
    sideDeck.fill(-1);
    game._party.setPazaakData(std::move(counts), std::move(sideDeck), cardCount);
}

void reone::game::TestGameModule::setCurrentScreen(Game &game, int screen) {
    game.changeScreen(static_cast<Game::Screen>(screen));
}

void reone::game::TestGameModule::setConversation(Game &game, Conversation *conversation) {
    game._conversation = conversation;
}

void reone::game::TestGameModule::initConsole(Game &game) {
    game.initConsole();
}

void reone::game::TestGameModule::setActiveModule(Game &game, bool active) {
    game._module = active ? game.newModule() : nullptr;
}

void reone::game::TestGameModule::setPazaakDevelopmentSelectedObject(
    Game &game,
    std::shared_ptr<Object> object) {

    game._pazaakDevelopmentSelectedObjectOverride = std::move(object);
}

void reone::game::TestGameModule::removeObject(Game &game, uint32_t objectId) {
    game._objectById.erase(objectId);
}

void reone::game::TestGameModule::useRuntimePazaakGUIs(Game &game) {
    game._pazaakGuiLoadOverride = {};
    game._pazaakPaceAutomaticDraws = true;
}

void reone::game::TestGameModule::useAuthoredPazaakDecks(Game &game) {
    game._pazaakOpponentDeckOverride.reset();
}

void reone::game::TestGameModule::finishPazaak(
    Game &game,
    PazaakCompletedResult result) {

    game.finishPazaak(result);
}

void reone::game::TestGameModule::serializePazaakPartyTable(
    const Game &game,
    resource::Gff &gff) {

    game.serializePazaakPartyTable(gff);
}

void reone::game::TestGameModule::deserializePartyTable(
    Game &game,
    resource::Gff &gff) {

    auto state = game.parsePartyTable(gff);
    game.replacePartyTable(std::move(state));
    game.deserializePazaakPartyTable(gff);
}

namespace {

HandSelection firstFour(const SideDeck &) {
    return {0, 1, 2, 3};
}

void configure(
    Game &game,
    bool guiLoadSucceeds = true,
    std::function<void(const std::string &, uint32_t)> continuation = {}) {

    TestGameModule::configurePazaak(
        game,
        guiLoadSucceeds,
        firstFour,
        firstFour,
        []() { return MainDeck::standardOrdered(); },
        std::move(continuation));
}

void chooseTen(PazaakSession &session) {
    if (session.chosenCards().size() == kSideDeckSize) {
        return;
    }
    for (size_t index = 0; index < kSideDeckSize; ++index) {
        ASSERT_TRUE(session.selectCard(index));
    }
}

MainDeck makeMainDeck(std::initializer_list<int> prefix) {
    std::vector<int> cards(MainDeck::standardOrdered().cards());
    size_t position = 0;
    for (int value : prefix) {
        auto found = std::find(
            cards.begin() + static_cast<ptrdiff_t>(position),
            cards.end(),
            value);
        std::iter_swap(
            cards.begin() + static_cast<ptrdiff_t>(position),
            found);
        ++position;
    }
    return MainDeck(std::move(cards));
}

std::shared_ptr<TwoDA> pazaakDeckTable(
    std::initializer_list<std::array<std::string, 10>> rows) {

    TwoDA::Builder builder;
    builder.columns({
        "card0", "card1", "card2", "card3", "card4",
        "card5", "card6", "card7", "card8", "card9",
    });
    for (const auto &row : rows) {
        builder.row(std::vector<std::string>(row.begin(), row.end()));
    }
    return std::shared_ptr<TwoDA>(builder.build().release());
}

struct GameFixture {
    TestEngine &engine {testEngine()};
    bool developerWasEnabled {engine.options().game.developer};
    StubConsole console;
    Game game {GameID::KotOR, "", engine.options(), engine.services(), console};
    std::shared_ptr<Creature> opponent {game.newCreature()};

    ~GameFixture() {
        engine.options().game.developer = developerWasEnabled;
    }
};

struct LoadedPazaakGUIs {
    std::map<std::string, std::map<std::string, std::shared_ptr<gui::Control>>> controls;
    std::vector<std::shared_ptr<gui::MockGUI>> guis;
};

void installPazaakGUIResources(
    TestEngine &engine,
    LoadedPazaakGUIs &loaded,
    std::set<std::string> missingControls = {},
    bool mockMissingAudio = true,
    int flows = 1) {
    using testing::_;
    using testing::AnyNumber;
    using testing::Invoke;
    using testing::NiceMock;
    using testing::Return;

    EXPECT_CALL(engine.resourceModule().textures(), get(_, _))
        .Times(AnyNumber())
        .WillRepeatedly(Return(std::shared_ptr<graphics::Texture> {}));
    if (mockMissingAudio) {
        EXPECT_CALL(engine.resourceModule().audioClips(), get(_))
            .Times(AnyNumber())
            .WillRepeatedly(Return(std::shared_ptr<audio::AudioClip> {}));
    }

    EXPECT_CALL(engine.guiModule().guis(), get(_, _))
        .Times(3 * flows)
        .WillRepeatedly(Invoke(
            [&, missingControls](const std::string &resRef, std::function<void(gui::IGUI &)> preload)
                -> std::shared_ptr<gui::IGUI> {
                auto mock = std::make_shared<NiceMock<gui::MockGUI>>();
                gui::MockGUI *rawGUI = mock.get();
                auto &controls = loaded.controls[resRef];
                auto *controlsPtr = &controls;
                ON_CALL(*mock, findControl(_))
                    .WillByDefault(Invoke([&, rawGUI, controlsPtr, resRef, missingControls](const std::string &tag) {
                        if (missingControls.find(resRef + ":" + tag) !=
                            missingControls.end()) {
                            return std::shared_ptr<gui::Control> {};
                        }
                        auto found = controlsPtr->find(tag);
                        if (found != controlsPtr->end()) {
                            return found->second;
                        }

                        std::shared_ptr<gui::Control> control;
                        if (tag.rfind("BTN_", 0) == 0) {
                            control = std::make_shared<gui::Button>(
                                *rawGUI,
                                engine.services().scene.graphs,
                                engine.services().graphics,
                                engine.services().resource);
                        } else {
                            control = std::make_shared<gui::Label>(
                                *rawGUI,
                                engine.services().scene.graphs,
                                engine.services().graphics,
                                engine.services().resource);
                        }
                        control->setTag(tag);
                        if (tag.rfind("LBL_PLRSCORE", 0) == 0 ||
                            tag.rfind("LBL_NPCSCORE", 0) == 0) {
                            control->setBorderFill("lbl_winmark01");
                        }
                        controlsPtr->emplace(tag, control);
                        return control;
                    }));
                preload(*mock);
                loaded.guis.push_back(mock);
                return mock;
            }));
}

void click(
    LoadedPazaakGUIs &loaded,
    const std::string &resRef,
    const std::string &tag) {

    std::static_pointer_cast<gui::Button>(loaded.controls.at(resRef).at(tag))
        ->handleClick(0, 0);
}

void prepareDevelopmentCommand(
    GameFixture &fixture,
    bool developer = true,
    bool activeModule = true,
    bool inGameScreen = true) {

    configure(fixture.game);
    fixture.engine.options().game.developer = developer;
    TestGameModule::initConsole(fixture.game);
    TestGameModule::setActiveModule(fixture.game, activeModule);
    TestGameModule::setCurrentScreen(
        fixture.game,
        static_cast<int>(
            inGameScreen ? Game::Screen::InGame : Game::Screen::MainMenu));
}

} // namespace

TEST(PazaakGameLifecycle, NiklosAuthoredArgumentsCreateFlowAndOpenWager) {
    GameFixture fixture;
    configure(fixture.game);
    fixture.game.party().giveGold(25);
    Routines routines(GameID::KotOR, &fixture.game, &fixture.engine.services());
    routines.init();
    script::ExecutionContext execution;

    routines.get(364).invoke(
        {
            script::Variable::ofInt(1),
            script::Variable::ofString("K_PTAR_NIKPAZEND"),
            script::Variable::ofInt(40),
            script::Variable::ofInt(0),
            script::Variable::ofObject(fixture.opponent->id()),
        },
        execution);

    ASSERT_NE(nullptr, fixture.game.pazaakSession());
    EXPECT_EQ(Game::Screen::PazaakWager, fixture.game.currentScreen());
    EXPECT_EQ(1, fixture.game.pazaakSession()->opponentDeck());
    EXPECT_EQ("k_ptar_nikpazend", fixture.game.pazaakSession()->continuationScript());
    EXPECT_FALSE(fixture.game.pazaakSession()->tutorialRequested());
    EXPECT_EQ(25, fixture.game.pazaakSession()->wagerLimit());
    EXPECT_EQ(25, fixture.game.pazaakSession()->wager());
}

TEST(PazaakGameLifecycle, TslBuildStartsKotorTwoPazaakFromOwnedCards) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    auto opponent = game.newCreature();
    configure(game);

    EXPECT_TRUE(game.playPazaak(0, "", 0, false, opponent));
    ASSERT_NE(nullptr, game.pazaakSession());
    // This party owns every KotOR II card type, so all 23 are offered, including
    // the special cards, and KotOR II can be played end to end.
    const auto &collection = game.pazaakSession()->collection();
    EXPECT_EQ(23u, collection.size());
    bool hasSpecial = false;
    for (const auto &card : collection) {
        if (card.definition.isSpecial()) {
            hasSpecial = true;
        }
    }
    EXPECT_TRUE(hasSpecial);
    game.abortPazaak();
}

TEST(PazaakGameLifecycle, NativeKotorTwoMatchOffersOnlyTheCardsTheSaveOwns) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    auto opponent = game.newCreature();
    configure(game);
    TestGameModule::useAuthoredPazaakDecks(game);
    auto table = pazaakDeckTable({
        {"+3", "-3", "+4", "-4", "+5", "-5", "+5", "-3", "+4", "-5"},
    });
    EXPECT_CALL(engine.resourceModule().twoDas(), get("pazaakdecks"))
        .Times(testing::AnyNumber())
        .WillRepeatedly(testing::Return(table));

    // A save carrying only the authored basic collection: two each of +1..+5.
    Party::PazaakCardCounts counts {};
    for (size_t i = 0; i < 5; ++i) {
        counts[i] = 2;
    }
    Party::PazaakSideDeck saved;
    saved.fill(-1);
    game.party().setPazaakData(counts, saved, Party::kK2PazaakCardCount);

    ASSERT_TRUE(game.playPazaak(0, "", 0, false, opponent));
    const auto *session = game.pazaakSession();
    ASSERT_NE(nullptr, session);
    // Exactly the owned cards are offered: no loaded developer collection and no
    // special cards the save does not own.
    const auto &collection = session->collection();
    ASSERT_EQ(5u, collection.size());
    for (size_t i = 0; i < collection.size(); ++i) {
        EXPECT_EQ(CardDefinition::fixedPositive(static_cast<int>(i) + 1), collection[i].definition);
        EXPECT_EQ(2u, collection[i].copies);
        EXPECT_FALSE(collection[i].definition.isSpecial());
    }
    game.abortPazaak();
}

TEST(PazaakGameLifecycle, NativeKotorTwoMatchUsesTheSavedSideDeckIncludingSpecials) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    auto opponent = game.newCreature();
    configure(game);

    // A save that owns one Tiebreaker (KotOR II card ID 18) and nine numbered
    // cards, with all ten already chosen as the side deck.
    Party::PazaakCardCounts counts {};
    counts[0] = counts[1] = counts[2] = counts[3] = 2;
    counts[4] = 1;
    counts[18] = 1;
    Party::PazaakSideDeck saved {0, 0, 1, 1, 2, 2, 3, 3, 4, 18};
    game.party().setPazaakData(counts, saved, Party::kK2PazaakCardCount);

    ASSERT_TRUE(game.playPazaak(0, "", 0, false, opponent));
    const auto *session = game.pazaakSession();
    ASSERT_NE(nullptr, session);
    ASSERT_EQ(6u, session->collection().size());
    EXPECT_EQ(CardDefinition::tiebreaker(), session->collection()[5].definition);
    EXPECT_EQ(18, session->collection()[5].persistentId);
    // The saved ten-card selection is restored, so the player keeps their deck.
    EXPECT_EQ(kSideDeckSize, session->chosenCards().size());
    EXPECT_EQ(5u, session->chosenCards().back());
    game.abortPazaak();
}

TEST(PazaakGameLifecycle, MissingOpponentDoesNotStartFlow) {
    GameFixture fixture;
    configure(fixture.game);

    EXPECT_FALSE(fixture.game.playPazaak(0, "", 0, false, nullptr));
    EXPECT_EQ(nullptr, fixture.game.pazaakSession());
    EXPECT_EQ(Game::Screen::None, fixture.game.currentScreen());
}

TEST(PazaakRetailData, AuthoredSelectorsProduceDistinctSemanticDecks) {
    GameFixture fixture;
    configure(fixture.game);
    TestGameModule::useAuthoredPazaakDecks(fixture.game);
    auto table = pazaakDeckTable({
        {"+1", "+2", "+3", "+4", "+5", "+6", "-1", "-2", "*3", "*4"},
        {"+3", "-3", "+4", "-4", "+5", "-5", "+5", "-3", "+4", "-5"},
    });
    EXPECT_CALL(fixture.engine.resourceModule().twoDas(), get("pazaakdecks"))
        .Times(2)
        .WillRepeatedly(testing::Return(table));

    ASSERT_TRUE(fixture.game.playPazaak(0, "", 0, false, fixture.opponent));
    EXPECT_EQ(
        CardDefinition::fixedPositive(1),
        fixture.game.pazaakSession()->opponentSideDeck()[0]);
    fixture.game.cancelPazaak();

    ASSERT_TRUE(fixture.game.playPazaak(1, "", 0, false, fixture.opponent));
    EXPECT_EQ(
        CardDefinition::fixedNegative(3),
        fixture.game.pazaakSession()->opponentSideDeck()[1]);
    EXPECT_EQ(
        CardDefinition::fixedNegative(5),
        fixture.game.pazaakSession()->opponentSideDeck()[9]);
}

TEST(PazaakRetailData, InvalidOrMalformedAuthoredSelectorFailsSafely) {
    GameFixture fixture;
    configure(fixture.game);
    TestGameModule::useAuthoredPazaakDecks(fixture.game);
    auto table = pazaakDeckTable({
        {"+1", "+2", "+3", "+4", "+5", "+6", "-1", "-2", "*3", "bogus"},
    });
    EXPECT_CALL(fixture.engine.resourceModule().twoDas(), get("pazaakdecks"))
        .Times(2)
        .WillRepeatedly(testing::Return(table));

    EXPECT_FALSE(fixture.game.playPazaak(0, "", 0, false, fixture.opponent));
    EXPECT_EQ(nullptr, fixture.game.pazaakSession());
    EXPECT_FALSE(fixture.game.playPazaak(2, "", 0, false, fixture.opponent));
    EXPECT_EQ(nullptr, fixture.game.pazaakSession());
}

TEST(PazaakRetailData, UsesOnlyOwnedQuantitiesAndPreloadsSavedTen) {
    GameFixture fixture;
    configure(fixture.game);
    Party::PazaakCardCounts counts {};
    counts[8] = 10;
    Party::PazaakSideDeck saved;
    saved.fill(8);
    fixture.game.party().setPazaakData(counts, saved);

    ASSERT_TRUE(fixture.game.playPazaak(0, "", 0, false, fixture.opponent));
    const auto *session = fixture.game.pazaakSession();
    ASSERT_NE(nullptr, session);
    ASSERT_EQ(1u, session->collection().size());
    EXPECT_EQ(10u, session->collection()[0].copies);
    EXPECT_EQ(8, session->collection()[0].persistentId);
    EXPECT_EQ(CardDefinition::fixedNegative(3), session->collection()[0].definition);
    EXPECT_EQ(10u, session->chosenCards().size());
    EXPECT_TRUE(std::all_of(
        session->chosenCards().begin(),
        session->chosenCards().end(),
        [](size_t index) { return index == 0; }));
}

TEST(PazaakScriptBoundary, OmittedOpponentUsesAuthoredScriptCaller) {
    GameFixture fixture;
    configure(fixture.game);
    Routines routines(GameID::KotOR, &fixture.game, &fixture.engine.services());
    routines.init();
    script::ExecutionContext execution;
    execution.args.emplace_back(
        script::ArgKind::Caller,
        script::Variable::ofObject(fixture.opponent->id()));

    routines.get(364).invoke(
        {
            script::Variable::ofInt(0),
            script::Variable::ofString(""),
            script::Variable::ofInt(0),
        },
        execution);

    ASSERT_NE(nullptr, fixture.game.pazaakSession());
    EXPECT_EQ(fixture.opponent->id(), fixture.game.pazaakSession()->opponentId());
}

TEST(PazaakConsoleCommand, RegistersExactNameAndRequiresDeveloperMode) {
    GameFixture fixture;
    prepareDevelopmentCommand(fixture, false);

    ASSERT_TRUE(fixture.console.hasCommand("startpazaak"));
    EXPECT_EQ(1, fixture.console.commands.count("startpazaak"));

    fixture.console.execute("startpazaak");

    EXPECT_EQ(nullptr, fixture.game.pazaakSession());
    ASSERT_FALSE(fixture.console.lines.empty());
    EXPECT_EQ(
        "pazaak: developer mode required",
        fixture.console.lines.back());
}

TEST(PazaakConsoleCommand, RequiresAnActiveInGameModule) {
    GameFixture noModule;
    prepareDevelopmentCommand(noModule, true, false, true);
    noModule.console.execute("startpazaak");
    EXPECT_EQ(nullptr, noModule.game.pazaakSession());
    ASSERT_FALSE(noModule.console.lines.empty());
    EXPECT_EQ(
        "pazaak: no active in-game module",
        noModule.console.lines.back());

    GameFixture notInGame;
    prepareDevelopmentCommand(notInGame, true, true, false);
    notInGame.console.execute("startpazaak");
    EXPECT_EQ(nullptr, notInGame.game.pazaakSession());
    ASSERT_FALSE(notInGame.console.lines.empty());
    EXPECT_EQ(
        "pazaak: no active in-game module",
        notInGame.console.lines.back());
}

TEST(PazaakConsoleCommand, StartsExactlyOnceWithoutSelectionAndPreservesGameData) {
    GameFixture fixture;
    prepareDevelopmentCommand(fixture);
    auto player = fixture.game.newCreature();
    fixture.game.party().addMember(kNpcPlayer, player);
    fixture.game.party().setPlayer(player);
    fixture.game.party().giveGold(37);

    const int creditsBefore = fixture.game.party().gold();
    const size_t inventorySizeBefore = player->items().size();
    const auto transitionBefore =
        TestGameModule::scheduledTransition(fixture.game);
    std::shared_ptr<Module> moduleBefore = fixture.game.module();

    fixture.console.execute("startpazaak");

    PazaakSession *session = fixture.game.pazaakSession();
    ASSERT_NE(nullptr, session);
    PazaakSession *firstSession = session;
    EXPECT_EQ(Game::Screen::PazaakSetup, fixture.game.currentScreen());
    EXPECT_EQ(0, session->wager());
    EXPECT_EQ(0, session->wagerLimit());
    EXPECT_EQ(0, session->opponentDeck());
    EXPECT_EQ(0u, session->opponentId());
    EXPECT_TRUE(session->continuationScript().empty());
    EXPECT_EQ(
        PazaakSession::temporaryK1TestCollection().size(),
        session->collection().size());
    EXPECT_EQ(creditsBefore, fixture.game.party().gold());
    EXPECT_EQ(inventorySizeBefore, player->items().size());
    EXPECT_EQ(transitionBefore, TestGameModule::scheduledTransition(fixture.game));
    EXPECT_EQ(moduleBefore, fixture.game.module());
    ASSERT_FALSE(fixture.console.lines.empty());
    EXPECT_EQ(
        "pazaak: development match started against Pazaak Opponent",
        fixture.console.lines.back());

    fixture.console.execute("startpazaak");
    EXPECT_EQ(firstSession, fixture.game.pazaakSession());
    EXPECT_EQ(
        "pazaak: already active",
        fixture.console.lines.back());

    chooseTen(*session);
    ASSERT_TRUE(session->confirmSetup());
    EXPECT_EQ(
        "Pazaak Opponent",
        session->boardProjection().opponentName);
}

TEST(PazaakConsoleCommand, SelectedCreatureOnlyChangesVisibleDevelopmentIdentity) {
    GameFixture fixture;
    prepareDevelopmentCommand(fixture);
    fixture.opponent->setTag("Selected Test Creature");
    TestGameModule::setPazaakDevelopmentSelectedObject(
        fixture.game,
        fixture.opponent);

    fixture.console.execute("startpazaak");

    PazaakSession *session = fixture.game.pazaakSession();
    ASSERT_NE(nullptr, session);
    EXPECT_EQ(0u, session->opponentId());
    EXPECT_EQ(0, session->opponentDeck());
    EXPECT_TRUE(session->continuationScript().empty());
    chooseTen(*session);
    ASSERT_TRUE(session->confirmSetup());
    EXPECT_EQ(
        "Selected Test Creature",
        session->boardProjection().opponentName);
    EXPECT_EQ(
        "pazaak: development match started against Selected Test Creature",
        fixture.console.lines.back());
}

TEST(PazaakConsoleCommand, ReportsDevelopmentLaunchFailure) {
    GameFixture fixture;
    prepareDevelopmentCommand(fixture);
    TestGameModule::configurePazaak(
        fixture.game,
        false,
        firstFour,
        firstFour,
        []() { return MainDeck::standardOrdered(); },
        {});

    fixture.console.execute("startpazaak");

    EXPECT_EQ(nullptr, fixture.game.pazaakSession());
    EXPECT_EQ(Game::Screen::InGame, fixture.game.currentScreen());
    ASSERT_FALSE(fixture.console.lines.empty());
    EXPECT_EQ(
        "pazaak: development launch failed",
        fixture.console.lines.back());
}

TEST(PazaakConsoleCommand, CancellationReturnsToGameplayAndCleanupIsIdempotent) {
    GameFixture fixture;
    prepareDevelopmentCommand(fixture);
    fixture.game.party().giveGold(23);

    fixture.console.execute("startpazaak");
    ASSERT_NE(nullptr, fixture.game.pazaakSession());
    fixture.game.cancelPazaak();

    EXPECT_EQ(nullptr, fixture.game.pazaakSession());
    EXPECT_EQ(Game::Screen::InGame, fixture.game.currentScreen());
    EXPECT_EQ(23, fixture.game.party().gold());
    ASSERT_FALSE(fixture.console.lines.empty());
    EXPECT_EQ(
        "pazaak: development match cancelled",
        fixture.console.lines.back());

    fixture.game.cancelPazaak();
    fixture.game.abortPazaak();
    EXPECT_EQ(nullptr, fixture.game.pazaakSession());
    EXPECT_EQ(Game::Screen::InGame, fixture.game.currentScreen());
    EXPECT_EQ(23, fixture.game.party().gold());
}

TEST(PazaakConsoleCommand, MatchCompletionReturnsToGameplay) {
    GameFixture fixture;
    prepareDevelopmentCommand(fixture);
    TestGameModule::configurePazaak(
        fixture.game,
        true,
        firstFour,
        firstFour,
        []() { return makeMainDeck({10, 9}); },
        {});
    fixture.game.party().giveGold(19);

    fixture.console.execute("startpazaak");
    PazaakSession *session = fixture.game.pazaakSession();
    ASSERT_NE(nullptr, session);
    chooseTen(*session);
    ASSERT_TRUE(session->confirmSetup());
    fixture.game.showPazaakBoard();

    for (int commandCount = 0;
        commandCount < 40 && !session->completedResult();
         ++commandCount) {

        if (session->match()->set().result() != SetResult::InProgress) {
            ASSERT_TRUE(session->advanceResultPresentation(1.5f));
        } else if (session->match()->set().activeParticipant() == Participant::One) {
            if (session->match()->set().turnStage() == TurnStage::AwaitingDraw) {
                ASSERT_EQ(
                    PazaakOpponentEvent::PlayerDraw,
                    session->advanceOpponentEvent());
            } else {
                ASSERT_EQ(ActionError::None, session->standPlayer());
            }
        } else if (session->match()->set().turnStage() == TurnStage::AwaitingDraw) {
            ASSERT_EQ(
                ActionError::None,
                session->applyOpponentCommand(DrawCommand {Participant::Two}));
        } else {
            ASSERT_EQ(
                ActionError::None,
                session->applyOpponentCommand(StandCommand {Participant::Two}));
        }
    }

    ASSERT_TRUE(session->completedResult().has_value());
    ASSERT_TRUE(session->presentationPending());
    ASSERT_TRUE(session->advanceResultPresentation(1.5f));
    fixture.game.completePazaakIfReady();

    EXPECT_EQ(nullptr, fixture.game.pazaakSession());
    EXPECT_EQ(Game::Screen::InGame, fixture.game.currentScreen());
    EXPECT_EQ(19, fixture.game.party().gold());
    ASSERT_FALSE(fixture.console.lines.empty());
    EXPECT_EQ(
        "pazaak: development match completed - player won",
        fixture.console.lines.back());
}

TEST(PazaakConsoleCommand, ForfeitReturnsToGameplay) {
    GameFixture fixture;
    prepareDevelopmentCommand(fixture);
    fixture.game.party().giveGold(11);

    fixture.console.execute("startpazaak");
    PazaakSession *session = fixture.game.pazaakSession();
    ASSERT_NE(nullptr, session);
    chooseTen(*session);
    ASSERT_TRUE(session->confirmSetup());
    fixture.game.showPazaakBoard();
    ASSERT_TRUE(session->requestForfeit());
    ASSERT_TRUE(session->confirmForfeit());
    ASSERT_TRUE(session->advanceResultPresentation(1.5f));
    fixture.game.completePazaakIfReady();

    EXPECT_EQ(nullptr, fixture.game.pazaakSession());
    EXPECT_EQ(Game::Screen::InGame, fixture.game.currentScreen());
    EXPECT_EQ(11, fixture.game.party().gold());
    ASSERT_FALSE(fixture.console.lines.empty());
    EXPECT_EQ(
        "pazaak: development match completed - player forfeited",
        fixture.console.lines.back());

    fixture.game.completePazaakIfReady();
    fixture.game.abortPazaak();
    EXPECT_EQ(nullptr, fixture.game.pazaakSession());
    EXPECT_EQ(Game::Screen::InGame, fixture.game.currentScreen());
}

TEST(PazaakGameLifecycle, ZeroWagerBypassesWagerAndDialogueDoesNotReclaimScreen) {
    GameFixture fixture;
    configure(fixture.game);
    TestGameModule::setCurrentScreen(
        fixture.game,
        static_cast<int>(Game::Screen::Conversation));

    ASSERT_TRUE(fixture.game.playPazaak(0, "", 0, false, fixture.opponent));
    EXPECT_EQ(Game::Screen::PazaakSetup, fixture.game.currentScreen());
}

TEST(PazaakGameLifecycle, WagerCancellationPreservesCreditsAndLastResult) {
    GameFixture fixture;
    configure(fixture.game);
    fixture.game.party().giveGold(8);
    ASSERT_TRUE(fixture.game.playPazaak(0, "", 20, false, fixture.opponent));
    ASSERT_NE(nullptr, fixture.game.pazaakSession());
    for (int i = 0; i < 20; ++i) {
        fixture.game.pazaakSession()->increaseWager();
    }
    EXPECT_EQ(8, fixture.game.pazaakSession()->wager());

    fixture.game.cancelPazaak();
    EXPECT_EQ(8, fixture.game.party().gold());
    EXPECT_EQ(nullptr, fixture.game.pazaakSession());
    EXPECT_FALSE(fixture.game.lastPazaakResult().has_value());

    fixture.game.cancelPazaak();
    EXPECT_EQ(8, fixture.game.party().gold());
}

TEST(PazaakGameLifecycle, SelectedWagerSettlesForfeitExactlyOnce) {
    GameFixture fixture;
    configure(fixture.game);
    fixture.game.party().giveGold(10);
    ASSERT_TRUE(fixture.game.playPazaak(0, "", 10, false, fixture.opponent));
    fixture.game.pazaakSession()->decreaseWager();
    ASSERT_EQ(5, fixture.game.pazaakSession()->wager());
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmWager());
    chooseTen(*fixture.game.pazaakSession());
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmSetup());
    fixture.game.showPazaakBoard();
    ASSERT_TRUE(fixture.game.pazaakSession()->requestForfeit());
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmForfeit());
    fixture.game.update(1.5f);

    EXPECT_EQ(5, fixture.game.party().gold());
    fixture.game.completePazaakIfReady();
    EXPECT_EQ(5, fixture.game.party().gold());
}

TEST(PazaakGameLifecycle, MatchLossSubtractsWagerExactlyOnce) {
    GameFixture fixture;
    configure(fixture.game);
    fixture.game.party().giveGold(10);
    ASSERT_TRUE(fixture.game.playPazaak(0, "", 10, false, fixture.opponent));
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmWager());
    chooseTen(*fixture.game.pazaakSession());
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmSetup());
    fixture.game.showPazaakBoard();

    TestGameModule::finishPazaak(
        fixture.game,
        PazaakCompletedResult::OpponentWon);
    EXPECT_EQ(0, fixture.game.party().gold());
    TestGameModule::finishPazaak(
        fixture.game,
        PazaakCompletedResult::OpponentWon);
    EXPECT_EQ(0, fixture.game.party().gold());
}

TEST(PazaakGameLifecycle, MatchWinAddsWagerExactlyOnce) {
    GameFixture fixture;
    configure(fixture.game);
    fixture.game.party().giveGold(10);
    ASSERT_TRUE(fixture.game.playPazaak(0, "", 10, false, fixture.opponent));
    ASSERT_EQ(10, fixture.game.pazaakSession()->wager());
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmWager());
    chooseTen(*fixture.game.pazaakSession());
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmSetup());
    fixture.game.showPazaakBoard();

    TestGameModule::finishPazaak(
        fixture.game,
        PazaakCompletedResult::PlayerWon);
    EXPECT_EQ(20, fixture.game.party().gold());
    TestGameModule::finishPazaak(
        fixture.game,
        PazaakCompletedResult::PlayerWon);
    EXPECT_EQ(20, fixture.game.party().gold());
}

TEST(PazaakGameLifecycle, TechnicalAbortAfterCommitmentDoesNotSettle) {
    GameFixture fixture;
    configure(fixture.game);
    fixture.game.party().giveGold(10);
    ASSERT_TRUE(fixture.game.playPazaak(0, "", 10, false, fixture.opponent));
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmWager());
    chooseTen(*fixture.game.pazaakSession());
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmSetup());
    fixture.game.showPazaakBoard();

    fixture.game.abortPazaak();
    EXPECT_EQ(10, fixture.game.party().gold());
    EXPECT_FALSE(fixture.game.lastPazaakResult().has_value());
}

TEST(PazaakRetailData, CompletedSettlementAndChosenDeckRoundTripPartyTable) {
    GameFixture fixture;
    configure(fixture.game);
    fixture.game.party().giveGold(10);
    ASSERT_TRUE(fixture.game.playPazaak(0, "", 10, false, fixture.opponent));
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmWager());
    chooseTen(*fixture.game.pazaakSession());
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmSetup());
    fixture.game.showPazaakBoard();
    TestGameModule::finishPazaak(
        fixture.game,
        PazaakCompletedResult::PlayerWon);

    auto partyTable = Gff::Builder().build();
    TestGameModule::serializePazaakPartyTable(fixture.game, *partyTable);
    uint32_t savedGold = 0;
    ASSERT_TRUE(partyTable->readDword(savedGold, "PT_GOLD"));
    EXPECT_EQ(20u, savedGold);

    GameFixture loaded;
    TestGameModule::deserializePartyTable(loaded.game, *partyTable);
    EXPECT_TRUE(loaded.game.party().hasValidPazaakData());
    EXPECT_EQ(
        fixture.game.party().pazaakCardCounts(),
        loaded.game.party().pazaakCardCounts());
    EXPECT_EQ(
        fixture.game.party().pazaakSideDeck(),
        loaded.game.party().pazaakSideDeck());
}

TEST(PazaakGameLifecycle, ConfirmedForfeitClearsBeforeOneShotContinuation) {
    GameFixture fixture;
    int continuationCount = 0;
    std::string continuedScript;
    uint32_t continuedOpponent = 0;
    configure(
        fixture.game,
        true,
        [&](const std::string &script, uint32_t opponentId) {
            EXPECT_EQ(nullptr, fixture.game.pazaakSession());
            ++continuationCount;
            continuedScript = script;
            continuedOpponent = opponentId;
        });

    ASSERT_TRUE(fixture.game.playPazaak(
        2,
        "after_pazaak",
        0,
        false,
        fixture.opponent));
    ASSERT_NE(nullptr, fixture.game.pazaakSession());
    chooseTen(*fixture.game.pazaakSession());
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmSetup());
    fixture.game.showPazaakBoard();
    ASSERT_TRUE(fixture.game.pazaakSession()->requestForfeit());
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmForfeit());

    fixture.game.update(1.5f);
    EXPECT_EQ(nullptr, fixture.game.pazaakSession());
    ASSERT_TRUE(fixture.game.lastPazaakResult().has_value());
    EXPECT_EQ(PazaakCompletedResult::PlayerForfeited, *fixture.game.lastPazaakResult());
    EXPECT_EQ(1, continuationCount);
    EXPECT_EQ("after_pazaak", continuedScript);
    EXPECT_EQ(fixture.opponent->id(), continuedOpponent);

    fixture.game.update(1.5f);
    fixture.game.abortPazaak();
    EXPECT_EQ(1, continuationCount);
}

TEST(PazaakGameLifecycle, DeletedContinuationCallerFailsSafely) {
    GameFixture fixture;
    int continuationCount = 0;
    configure(
        fixture.game,
        true,
        [&](const std::string &, uint32_t) {
            ++continuationCount;
        });

    ASSERT_TRUE(fixture.game.playPazaak(
        0,
        "after_pazaak",
        0,
        false,
        fixture.opponent));
    chooseTen(*fixture.game.pazaakSession());
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmSetup());
    fixture.game.showPazaakBoard();
    ASSERT_TRUE(fixture.game.pazaakSession()->requestForfeit());
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmForfeit());
    TestGameModule::removeObject(fixture.game, fixture.opponent->id());

    fixture.game.update(1.5f);
    EXPECT_EQ(nullptr, fixture.game.pazaakSession());
    EXPECT_EQ(0, continuationCount);
    ASSERT_TRUE(fixture.game.lastPazaakResult().has_value());
    EXPECT_EQ(
        PazaakCompletedResult::PlayerForfeited,
        *fixture.game.lastPazaakResult());
}

TEST(PazaakGameLifecycle, ContinuationCanReenterWithoutCompletingOldFlowTwice) {
    GameFixture fixture;
    int continuationCount = 0;
    fixture.game.party().giveGold(10);
    configure(
        fixture.game,
        true,
        [&](const std::string &, uint32_t) {
            ++continuationCount;
            ASSERT_TRUE(fixture.game.playPazaak(
                0,
                "",
                0,
                false,
                fixture.opponent));
        });

    ASSERT_TRUE(fixture.game.playPazaak(
        0,
        "after_pazaak",
        5,
        false,
        fixture.opponent));
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmWager());
    chooseTen(*fixture.game.pazaakSession());
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmSetup());
    fixture.game.showPazaakBoard();
    ASSERT_TRUE(fixture.game.pazaakSession()->requestForfeit());
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmForfeit());

    fixture.game.update(1.5f);
    EXPECT_EQ(1, continuationCount);
    EXPECT_EQ(5, fixture.game.party().gold());
    ASSERT_NE(nullptr, fixture.game.pazaakSession());
    fixture.game.completePazaakIfReady();
    EXPECT_EQ(1, continuationCount);
    EXPECT_EQ(5, fixture.game.party().gold());
    fixture.game.abortPazaak();
}

TEST(PazaakGameLifecycle, UpdatePacesOneOpponentEventPerInterval) {
    GameFixture fixture;
    configure(fixture.game);
    ASSERT_TRUE(fixture.game.playPazaak(0, "", 0, false, fixture.opponent));
    chooseTen(*fixture.game.pazaakSession());
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmSetup());
    fixture.game.showPazaakBoard();
    ASSERT_EQ(ActionError::None, fixture.game.pazaakSession()->endPlayerTurn());

    fixture.game.update(0.44f);
    EXPECT_TRUE(
        fixture.game.pazaakSession()
            ->match()
            ->set()
            .participant(Participant::Two)
            .board()
            .empty());

    fixture.game.update(0.02f);
    EXPECT_EQ(
        1,
        fixture.game.pazaakSession()
            ->match()
            ->set()
            .participant(Participant::Two)
            .board()
            .size());
    EXPECT_EQ(TurnStage::AwaitingAction, fixture.game.pazaakSession()->match()->set().turnStage());

    fixture.game.update(0.44f);
    EXPECT_EQ(TurnStage::AwaitingAction, fixture.game.pazaakSession()->match()->set().turnStage());
    fixture.game.update(0.02f);
    EXPECT_EQ(Participant::One, fixture.game.pazaakSession()->match()->set().activeParticipant());
    EXPECT_EQ(TurnStage::AwaitingDraw, fixture.game.pazaakSession()->match()->set().turnStage());
}

TEST(PazaakGUIResources, RuntimeResourcesBindControlsAndDriveSetupToBoard) {
    GameFixture fixture;
    configure(fixture.game);
    TestGameModule::useRuntimePazaakGUIs(fixture.game);
    LoadedPazaakGUIs loaded;
    installPazaakGUIResources(fixture.engine, loaded);

    ASSERT_TRUE(fixture.game.playPazaak(0, "", 0, false, fixture.opponent));
    ASSERT_EQ(3, loaded.controls.size());
    EXPECT_TRUE(loaded.controls.count("pazaakwager"));
    EXPECT_TRUE(loaded.controls.count("pazaaksetup"));
    EXPECT_TRUE(loaded.controls.count("pazaakgame"));
    EXPECT_EQ(Game::Screen::PazaakSetup, fixture.game.currentScreen());
    EXPECT_TRUE(
        loaded.controls.at("pazaaksetup").at("BTN_ATEXT")->isDisabled());
    EXPECT_EQ(
        "Choose Sidedeck",
        loaded.controls.at("pazaaksetup").at("LBL_TITLE")->text().text);
    EXPECT_EQ(
        "Available cards",
        loaded.controls.at("pazaaksetup").at("LBL_LTEXT")->text().text);
    EXPECT_EQ(
        "Chosen cards",
        loaded.controls.at("pazaaksetup").at("LBL_RTEXT")->text().text);
    EXPECT_EQ(
        "Play",
        loaded.controls.at("pazaaksetup").at("BTN_ATEXT")->text().text);
    EXPECT_EQ(
        "Add card",
        loaded.controls.at("pazaaksetup").at("BTN_YTEXT")->text().text);
    EXPECT_EQ(
        "lbl_cardmpos",
        loaded.controls.at("pazaaksetup").at("BTN_AVAIL00")->borderFillResRef());
    EXPECT_EQ(
        "lbl_cardmneg",
        loaded.controls.at("pazaaksetup").at("BTN_AVAIL10")->borderFillResRef());
    EXPECT_EQ(
        "lbl_cardrarem",
        loaded.controls.at("pazaaksetup").at("BTN_AVAIL20")->borderFillResRef());
    // The plus-minus sign is a single code point: the engine draws one glyph per
    // byte, so a multi-byte sequence would render a stray leading character.
    EXPECT_EQ(
        "\xB1" "1",
        loaded.controls.at("pazaaksetup").at("LBL_AVAIL20")->text().text);

    click(loaded, "pazaaksetup", "BTN_AVAIL00");
    ASSERT_EQ(1, fixture.game.pazaakSession()->chosenCards().size());
    click(loaded, "pazaaksetup", "BTN_YTEXT");
    ASSERT_EQ(2, fixture.game.pazaakSession()->chosenCards().size());
    click(loaded, "pazaaksetup", "BTN_CHOSEN1");
    ASSERT_EQ(1, fixture.game.pazaakSession()->chosenCards().size());
    for (const std::string &tag : {
             "BTN_AVAIL10",
             "BTN_AVAIL20",
             "BTN_AVAIL01",
             "BTN_AVAIL11",
             "BTN_AVAIL21",
             "BTN_AVAIL02",
             "BTN_AVAIL12",
             "BTN_AVAIL22",
             "BTN_AVAIL03",
         }) {
        click(loaded, "pazaaksetup", tag);
    }
    ASSERT_EQ(kSideDeckSize, fixture.game.pazaakSession()->chosenCards().size());
    EXPECT_FALSE(
        loaded.controls.at("pazaaksetup").at("BTN_ATEXT")->isDisabled());
    click(loaded, "pazaaksetup", "BTN_ATEXT");

    ASSERT_EQ(Game::Screen::PazaakBoard, fixture.game.currentScreen());
    ASSERT_NE(nullptr, fixture.game.pazaakSession()->match());
    fixture.game.update(0.46f);
    // The mandatory main-deck draw renders as the gold/olive card, distinct from
    // a blue positive side card (lbl_cardmpos), and shows only its numeric value.
    EXPECT_EQ(
        "lbl_cardstand",
        loaded.controls.at("pazaakgame").at("BTN_PLR0")->borderFillResRef());
    EXPECT_EQ(
        "1",
        loaded.controls.at("pazaakgame").at("LBL_PLR0")->text().text);
    EXPECT_EQ(
        "",
        loaded.controls.at("pazaakgame").at("BTN_PLR1")->borderFillResRef());
    EXPECT_EQ(
        "lbl_cardback",
        loaded.controls.at("pazaakgame").at("BTN_NPCSIDE0")->borderFillResRef());
    EXPECT_EQ(
        "",
        loaded.controls.at("pazaakgame").at("LBL_NPCSIDE0")->text().text);
    EXPECT_EQ(
        "lbl_cardmpos",
        loaded.controls.at("pazaakgame").at("BTN_PLRSIDE0")->borderFillResRef());
    EXPECT_EQ(
        "lbl_cardmneg",
        loaded.controls.at("pazaakgame").at("BTN_PLRSIDE1")->borderFillResRef());
    EXPECT_EQ(
        "lbl_cardrarem",
        loaded.controls.at("pazaakgame").at("BTN_PLRSIDE2")->borderFillResRef());
    EXPECT_EQ(
        "+1",
        loaded.controls.at("pazaakgame").at("LBL_PLRSIDE0")->text().text);
    EXPECT_EQ(
        "-1",
        loaded.controls.at("pazaakgame").at("LBL_PLRSIDE1")->text().text);
    EXPECT_EQ(
        "+1",
        loaded.controls.at("pazaakgame").at("LBL_PLRSIDE2")->text().text);
    EXPECT_TRUE(
        loaded.controls.at("pazaakgame").at("BTN_PLRSIDE0")->isHilightOverBorder());
    EXPECT_EQ(
        "lbl_cardhilite",
        loaded.controls.at("pazaakgame").at("BTN_PLRSIDE0")->hilightFillResRef());
    EXPECT_FALSE(
        loaded.controls.at("pazaakgame").at("BTN_FLIP0")->isVisible());
    EXPECT_TRUE(
        loaded.controls.at("pazaakgame").at("BTN_FLIP2")->isVisible());
    EXPECT_EQ(
        "pazflip",
        loaded.controls.at("pazaakgame").at("BTN_FLIP2")->borderFillResRef());
    EXPECT_EQ(
        "lbl_pazaakturn",
        loaded.controls.at("pazaakgame").at("LBL_PLRTURN")->borderFillResRef());
    EXPECT_EQ(
        "End Turn",
        loaded.controls.at("pazaakgame").at("BTN_XTEXT")->text().text);
    EXPECT_EQ(
        "Stand",
        loaded.controls.at("pazaakgame").at("BTN_YTEXT")->text().text);
    EXPECT_FALSE(
        loaded.controls.at("pazaakgame").at("BTN_XTEXT")->isDisabled());
    click(loaded, "pazaakgame", "BTN_FLIP2");
    EXPECT_EQ(
        "pazflip2",
        loaded.controls.at("pazaakgame").at("BTN_FLIP2")->borderFillResRef());
    EXPECT_EQ(
        "-1",
        loaded.controls.at("pazaakgame").at("LBL_PLRSIDE2")->text().text);

    click(loaded, "pazaakgame", "BTN_PLRSIDE0");
    EXPECT_EQ(
        "",
        loaded.controls.at("pazaakgame").at("BTN_PLRSIDE0")->borderFillResRef());
    EXPECT_FALSE(
        loaded.controls.at("pazaakgame").at("LBL_PLRSIDE0")->isVisible());
    EXPECT_EQ(
        "lbl_cardmpos",
        loaded.controls.at("pazaakgame").at("BTN_PLR1")->borderFillResRef());
    EXPECT_EQ(
        "+1",
        loaded.controls.at("pazaakgame").at("LBL_PLR1")->text().text);
    click(loaded, "pazaakgame", "BTN_XTEXT");
    EXPECT_EQ(
        PazaakBoardState::AwaitingOpponentPolicy,
        fixture.game.pazaakSession()->boardProjection().state);
    EXPECT_TRUE(
        loaded.controls.at("pazaakgame").at("BTN_PLRSIDE0")->isDisabled());

    fixture.game.abortPazaak();
    EXPECT_EQ(nullptr, fixture.game.pazaakSession());
    EXPECT_TRUE(testing::Mock::VerifyAndClearExpectations(
        &fixture.engine.guiModule().guis()));
}

TEST(PazaakGUIResources, StandingScoreAndResolvedSetUseAuthoredAutomaticPresentation) {
    GameFixture fixture;
    TestGameModule::configurePazaak(
        fixture.game,
        true,
        firstFour,
        firstFour,
        []() { return makeMainDeck({10, 9}); },
        {});
    TestGameModule::useRuntimePazaakGUIs(fixture.game);
    LoadedPazaakGUIs loaded;
    installPazaakGUIResources(fixture.engine, loaded);

    ASSERT_TRUE(fixture.game.playPazaak(
        0,
        "",
        0,
        false,
        fixture.opponent));
    chooseTen(*fixture.game.pazaakSession());
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmSetup());
    fixture.game.showPazaakBoard();
    fixture.game.update(0.46f);

    click(loaded, "pazaakgame", "BTN_YTEXT");
    EXPECT_TRUE(
        fixture.game.pazaakSession()
            ->match()
            ->set()
            .participant(Participant::One)
            .stood());
    // Standing does not overlay a gold hilight: the main-deck card keeps its gold
    // provenance face (lbl_cardstand) and side cards keep their own colour, so no
    // played card changes appearance just because its owner has stood.
    EXPECT_FALSE(
        loaded.controls.at("pazaakgame").at("BTN_PLR0")->isSelected());
    EXPECT_EQ(
        "lbl_cardstand",
        loaded.controls.at("pazaakgame").at("BTN_PLR0")->borderFillResRef());
    EXPECT_EQ(
        "lbl_pazaakturn",
        loaded.controls.at("pazaakgame").at("LBL_NPCTURN")->borderFillResRef());

    ASSERT_EQ(
        ActionError::None,
        fixture.game.pazaakSession()->applyOpponentCommand(
            DrawCommand {Participant::Two}));
    ASSERT_EQ(
        ActionError::None,
        fixture.game.pazaakSession()->applyOpponentCommand(
            StandCommand {Participant::Two}));
    fixture.game.showPazaakBoard();

    ASSERT_EQ(
        PazaakBoardState::SetComplete,
        fixture.game.pazaakSession()->boardProjection().state);
    EXPECT_EQ(
        "lbl_winmark02",
        loaded.controls.at("pazaakgame").at("LBL_PLRSCORE0")->borderFillResRef());
    for (const std::string &tag : {
             "LBL_PLRSCORE1",
             "LBL_PLRSCORE2",
             "LBL_NPCSCORE0",
             "LBL_NPCSCORE1",
             "LBL_NPCSCORE2",
         }) {
        EXPECT_EQ(
            "lbl_winmark01",
            loaded.controls.at("pazaakgame").at(tag)->borderFillResRef());
        EXPECT_TRUE(loaded.controls.at("pazaakgame").at(tag)->isVisible());
    }
    EXPECT_EQ(
        "End Turn",
        loaded.controls.at("pazaakgame").at("BTN_XTEXT")->text().text);
    EXPECT_TRUE(
        loaded.controls.at("pazaakgame").at("BTN_XTEXT")->isDisabled());

    fixture.game.update(1.49f);
    EXPECT_EQ(
        PazaakBoardState::SetComplete,
        fixture.game.pazaakSession()->boardProjection().state);
    fixture.game.update(0.01f);
    EXPECT_EQ(
        PazaakBoardState::PlayerTurn,
        fixture.game.pazaakSession()->boardProjection().state);
    EXPECT_EQ(
        0,
        fixture.game.pazaakSession()
            ->match()
            ->set()
            .participant(Participant::One)
            .board()
            .size());
    EXPECT_EQ(
        TurnStage::AwaitingDraw,
        fixture.game.pazaakSession()->match()->set().turnStage());
    fixture.game.update(0.46f);
    EXPECT_EQ(
        1,
        fixture.game.pazaakSession()
            ->match()
            ->set()
            .participant(Participant::One)
            .board()
            .size());
    EXPECT_EQ(
        "End Turn",
        loaded.controls.at("pazaakgame").at("BTN_XTEXT")->text().text);

    fixture.game.abortPazaak();
    EXPECT_TRUE(testing::Mock::VerifyAndClearExpectations(
        &fixture.engine.guiModule().guis()));
}

TEST(PazaakGUIResources, AllThreeAuthoredPipsTrackBothParticipantsFromZeroToThree) {
    auto verifyPips = [](const LoadedPazaakGUIs &loaded, bool player, int wins) {
        const std::string prefix = player ? "LBL_PLRSCORE" : "LBL_NPCSCORE";
        for (int i = 0; i < 3; ++i) {
            EXPECT_EQ(
                i < wins ? "lbl_winmark02" : "lbl_winmark01",
                loaded.controls.at("pazaakgame")
                    .at(prefix + std::to_string(i))
                    ->borderFillResRef());
            EXPECT_TRUE(
                loaded.controls.at("pazaakgame")
                    .at(prefix + std::to_string(i))
                    ->isVisible());
        }
    };

    {
        GameFixture fixture;
        configure(
            fixture.game,
            true,
            {});
        TestGameModule::configurePazaak(
            fixture.game,
            true,
            firstFour,
            firstFour,
            []() { return makeMainDeck({10, 9}); },
            {});
        TestGameModule::useRuntimePazaakGUIs(fixture.game);
        LoadedPazaakGUIs loaded;
        installPazaakGUIResources(fixture.engine, loaded);
        ASSERT_TRUE(fixture.game.playPazaak(0, "", 0, false, fixture.opponent));
        chooseTen(*fixture.game.pazaakSession());
        ASSERT_TRUE(fixture.game.pazaakSession()->confirmSetup());
        fixture.game.showPazaakBoard();
        verifyPips(loaded, true, 0);
        verifyPips(loaded, false, 0);

        for (int wins = 1; wins <= 3; ++wins) {
            ASSERT_EQ(
                PazaakOpponentEvent::PlayerDraw,
                fixture.game.pazaakSession()->advanceOpponentEvent());
            ASSERT_EQ(ActionError::None, fixture.game.pazaakSession()->standPlayer());
            ASSERT_EQ(
                ActionError::None,
                fixture.game.pazaakSession()->applyOpponentCommand(
                    DrawCommand {Participant::Two}));
            ASSERT_EQ(
                ActionError::None,
                fixture.game.pazaakSession()->applyOpponentCommand(
                    StandCommand {Participant::Two}));
            fixture.game.showPazaakBoard();
            verifyPips(loaded, true, wins);
            verifyPips(loaded, false, 0);
            if (wins < 3) {
                ASSERT_TRUE(
                    fixture.game.pazaakSession()->advanceResultPresentation(1.5f));
                fixture.game.showPazaakBoard();
            }
        }
    }

    {
        GameFixture fixture;
        TestGameModule::configurePazaak(
            fixture.game,
            true,
            firstFour,
            firstFour,
            []() { return makeMainDeck({9, 10}); },
            {});
        TestGameModule::useRuntimePazaakGUIs(fixture.game);
        LoadedPazaakGUIs loaded;
        installPazaakGUIResources(fixture.engine, loaded);
        ASSERT_TRUE(fixture.game.playPazaak(0, "", 0, false, fixture.opponent));
        chooseTen(*fixture.game.pazaakSession());
        ASSERT_TRUE(fixture.game.pazaakSession()->confirmSetup());
        fixture.game.showPazaakBoard();
        verifyPips(loaded, true, 0);
        verifyPips(loaded, false, 0);

        for (int wins = 1; wins <= 3; ++wins) {
            ASSERT_EQ(
                PazaakOpponentEvent::PlayerDraw,
                fixture.game.pazaakSession()->advanceOpponentEvent());
            ASSERT_EQ(ActionError::None, fixture.game.pazaakSession()->standPlayer());
            ASSERT_EQ(
                ActionError::None,
                fixture.game.pazaakSession()->applyOpponentCommand(
                    DrawCommand {Participant::Two}));
            ASSERT_EQ(
                ActionError::None,
                fixture.game.pazaakSession()->applyOpponentCommand(
                    StandCommand {Participant::Two}));
            fixture.game.showPazaakBoard();
            verifyPips(loaded, true, 0);
            verifyPips(loaded, false, wins);
            if (wins < 3) {
                ASSERT_TRUE(
                    fixture.game.pazaakSession()->advanceResultPresentation(1.5f));
                fixture.game.showPazaakBoard();
            }
        }
    }
}

TEST(PazaakGUIResources, BoardCardFacesFollowProvenanceGoldMainRedMinusBluePositive) {
    GameFixture fixture;
    // Surface a negative side card in the player's first hand slot so a red
    // minus card (lbl_cardmneg) reaches the board next to the gold main-deck
    // draw (lbl_cardstand) and a blue positive side card (lbl_cardmpos). The
    // temporary side deck orders cards +1..+6 then -1..-4, so slot 6 is -1.
    auto minusFirst = [](const SideDeck &) { return HandSelection {6, 0, 1, 2}; };
    TestGameModule::configurePazaak(
        fixture.game,
        true,
        minusFirst,
        firstFour,
        []() { return makeMainDeck({3, 3}); },
        {});
    TestGameModule::useRuntimePazaakGUIs(fixture.game);
    LoadedPazaakGUIs loaded;
    installPazaakGUIResources(fixture.engine, loaded);

    ASSERT_TRUE(fixture.game.playPazaak(0, "", 0, false, fixture.opponent));
    chooseTen(*fixture.game.pazaakSession());
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmSetup());
    fixture.game.showPazaakBoard();
    fixture.game.update(0.46f);

    // Mandatory main-deck card renders gold and shows only its numeric value.
    EXPECT_EQ(
        "lbl_cardstand",
        loaded.controls.at("pazaakgame").at("BTN_PLR0")->borderFillResRef());
    EXPECT_EQ(
        "3",
        loaded.controls.at("pazaakgame").at("LBL_PLR0")->text().text);
    // Hand: slot 0 is a red -1 side card, slot 1 a blue +1 side card.
    EXPECT_EQ(
        "lbl_cardmneg",
        loaded.controls.at("pazaakgame").at("BTN_PLRSIDE0")->borderFillResRef());
    EXPECT_EQ(
        "-1",
        loaded.controls.at("pazaakgame").at("LBL_PLRSIDE0")->text().text);
    EXPECT_EQ(
        "lbl_cardmpos",
        loaded.controls.at("pazaakgame").at("BTN_PLRSIDE1")->borderFillResRef());

    // Playing the red side card keeps its red provenance on the board, and the
    // gold main-deck card is unchanged by the play.
    click(loaded, "pazaakgame", "BTN_PLRSIDE0");
    EXPECT_EQ(
        "lbl_cardmneg",
        loaded.controls.at("pazaakgame").at("BTN_PLR1")->borderFillResRef());
    EXPECT_EQ(
        "-1",
        loaded.controls.at("pazaakgame").at("LBL_PLR1")->text().text);
    EXPECT_EQ(
        "lbl_cardstand",
        loaded.controls.at("pazaakgame").at("BTN_PLR0")->borderFillResRef());

    fixture.game.abortPazaak();
    EXPECT_TRUE(testing::Mock::VerifyAndClearExpectations(
        &fixture.engine.guiModule().guis()));
}

TEST(PazaakGUIResources, ScoreMarkersResetToGreyOnAConsecutiveMatch) {
    GameFixture fixture;
    TestGameModule::configurePazaak(
        fixture.game,
        true,
        firstFour,
        firstFour,
        []() { return makeMainDeck({10, 9}); },
        {});
    TestGameModule::useRuntimePazaakGUIs(fixture.game);
    LoadedPazaakGUIs loaded;
    installPazaakGUIResources(fixture.engine, loaded, {}, true, 2);

    // First match: the player wins the opening set, lighting a gold pip.
    ASSERT_TRUE(fixture.game.playPazaak(0, "", 0, false, fixture.opponent));
    chooseTen(*fixture.game.pazaakSession());
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmSetup());
    fixture.game.showPazaakBoard();
    ASSERT_EQ(
        PazaakOpponentEvent::PlayerDraw,
        fixture.game.pazaakSession()->advanceOpponentEvent());
    ASSERT_EQ(ActionError::None, fixture.game.pazaakSession()->standPlayer());
    ASSERT_EQ(
        ActionError::None,
        fixture.game.pazaakSession()->applyOpponentCommand(
            DrawCommand {Participant::Two}));
    ASSERT_EQ(
        ActionError::None,
        fixture.game.pazaakSession()->applyOpponentCommand(
            StandCommand {Participant::Two}));
    fixture.game.showPazaakBoard();
    EXPECT_EQ(
        "lbl_winmark02",
        loaded.controls.at("pazaakgame").at("LBL_PLRSCORE0")->borderFillResRef());
    fixture.game.abortPazaak();

    // Consecutive match: the reused marker controls must all read grey again,
    // proving no win state is inherited from the previous match.
    ASSERT_TRUE(fixture.game.playPazaak(0, "", 0, false, fixture.opponent));
    chooseTen(*fixture.game.pazaakSession());
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmSetup());
    fixture.game.showPazaakBoard();
    for (const std::string &tag : {
             "LBL_PLRSCORE0",
             "LBL_PLRSCORE1",
             "LBL_PLRSCORE2",
             "LBL_NPCSCORE0",
             "LBL_NPCSCORE1",
             "LBL_NPCSCORE2",
         }) {
        EXPECT_EQ(
            "lbl_winmark01",
            loaded.controls.at("pazaakgame").at(tag)->borderFillResRef());
        EXPECT_TRUE(loaded.controls.at("pazaakgame").at(tag)->isVisible());
    }
    fixture.game.abortPazaak();
    EXPECT_TRUE(testing::Mock::VerifyAndClearExpectations(
        &fixture.engine.guiModule().guis()));
}

TEST(PazaakGUIResources, NoSetOrMatchResultTextIsShownWhileMarkersStillUpdate) {
    GameFixture fixture;
    TestGameModule::configurePazaak(
        fixture.game,
        true,
        firstFour,
        firstFour,
        []() { return makeMainDeck({10, 9}); },
        {});
    TestGameModule::useRuntimePazaakGUIs(fixture.game);
    LoadedPazaakGUIs loaded;
    installPazaakGUIResources(fixture.engine, loaded);
    ASSERT_TRUE(fixture.game.playPazaak(0, "", 0, false, fixture.opponent));
    chooseTen(*fixture.game.pazaakSession());
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmSetup());
    fixture.game.showPazaakBoard();

    auto noResultText = [&]() {
        EXPECT_EQ(
            "",
            loaded.controls.at("pazaakgame").at("LBL_PLRTURN")->text().text);
        EXPECT_EQ(
            "",
            loaded.controls.at("pazaakgame").at("LBL_NPCTURN")->text().text);
    };

    for (int wins = 1; wins <= 3; ++wins) {
        ASSERT_EQ(
            PazaakOpponentEvent::PlayerDraw,
            fixture.game.pazaakSession()->advanceOpponentEvent());
        ASSERT_EQ(ActionError::None, fixture.game.pazaakSession()->standPlayer());
        ASSERT_EQ(
            ActionError::None,
            fixture.game.pazaakSession()->applyOpponentCommand(
                DrawCommand {Participant::Two}));
        ASSERT_EQ(
            ActionError::None,
            fixture.game.pazaakSession()->applyOpponentCommand(
                StandCommand {Participant::Two}));
        fixture.game.showPazaakBoard();
        // The set (or, on the third win, the match) is resolved: the marker
        // lights, but no textual "you win the set/match" message is shown.
        noResultText();
        EXPECT_EQ(
            "lbl_winmark02",
            loaded.controls.at("pazaakgame")
                .at("LBL_PLRSCORE" + std::to_string(wins - 1))
                ->borderFillResRef());
        if (wins < 3) {
            ASSERT_TRUE(
                fixture.game.pazaakSession()->advanceResultPresentation(1.5f));
            fixture.game.showPazaakBoard();
            noResultText();
        }
    }
    EXPECT_EQ(
        PazaakBoardState::MatchComplete,
        fixture.game.pazaakSession()->boardProjection().state);
    noResultText();
    fixture.game.abortPazaak();
    EXPECT_TRUE(testing::Mock::VerifyAndClearExpectations(
        &fixture.engine.guiModule().guis()));
}

TEST(PazaakGUIResources, NoResultTextOnTiedSetOrForfeitedMatch) {
    // Tied set: both draw 10 and stand.
    {
        GameFixture fixture;
        TestGameModule::configurePazaak(
            fixture.game,
            true,
            firstFour,
            firstFour,
            []() { return makeMainDeck({10, 10}); },
            {});
        TestGameModule::useRuntimePazaakGUIs(fixture.game);
        LoadedPazaakGUIs loaded;
        installPazaakGUIResources(fixture.engine, loaded);
        ASSERT_TRUE(fixture.game.playPazaak(0, "", 0, false, fixture.opponent));
        chooseTen(*fixture.game.pazaakSession());
        ASSERT_TRUE(fixture.game.pazaakSession()->confirmSetup());
        fixture.game.showPazaakBoard();
        ASSERT_EQ(
            PazaakOpponentEvent::PlayerDraw,
            fixture.game.pazaakSession()->advanceOpponentEvent());
        ASSERT_EQ(ActionError::None, fixture.game.pazaakSession()->standPlayer());
        ASSERT_EQ(
            ActionError::None,
            fixture.game.pazaakSession()->applyOpponentCommand(
                DrawCommand {Participant::Two}));
        ASSERT_EQ(
            ActionError::None,
            fixture.game.pazaakSession()->applyOpponentCommand(
                StandCommand {Participant::Two}));
        fixture.game.showPazaakBoard();
        EXPECT_EQ(SetResult::Tie, fixture.game.pazaakSession()->boardProjection().setResult);
        EXPECT_EQ(
            "",
            loaded.controls.at("pazaakgame").at("LBL_PLRTURN")->text().text);
        EXPECT_EQ(
            "",
            loaded.controls.at("pazaakgame").at("LBL_NPCTURN")->text().text);
        fixture.game.abortPazaak();
    }
    // Forfeited match: no textual result either.
    {
        GameFixture fixture;
        configure(fixture.game);
        TestGameModule::useRuntimePazaakGUIs(fixture.game);
        LoadedPazaakGUIs loaded;
        installPazaakGUIResources(fixture.engine, loaded);
        ASSERT_TRUE(fixture.game.playPazaak(0, "", 0, false, fixture.opponent));
        chooseTen(*fixture.game.pazaakSession());
        ASSERT_TRUE(fixture.game.pazaakSession()->confirmSetup());
        fixture.game.showPazaakBoard();
        fixture.game.update(0.46f);
        ASSERT_TRUE(fixture.game.pazaakSession()->requestForfeit());
        ASSERT_TRUE(fixture.game.pazaakSession()->confirmForfeit());
        fixture.game.showPazaakBoard();
        EXPECT_EQ(
            "",
            loaded.controls.at("pazaakgame").at("LBL_PLRTURN")->text().text);
        EXPECT_EQ(
            "",
            loaded.controls.at("pazaakgame").at("LBL_NPCTURN")->text().text);
        fixture.game.abortPazaak();
    }
}

TEST(PazaakGUIResources, VerifiedAudioCuesFireOncePerModelEventAndNotOnRefresh) {
    using testing::_;
    using testing::Return;

    GameFixture fixture;
    configure(fixture.game);
    TestGameModule::useRuntimePazaakGUIs(fixture.game);
    LoadedPazaakGUIs loaded;

    auto clip = std::make_shared<audio::AudioClip>();
    EXPECT_CALL(fixture.engine.resourceModule().audioClips(), get("mgs_startturn"))
        .Times(2)
        .WillRepeatedly(Return(clip));
    EXPECT_CALL(fixture.engine.resourceModule().audioClips(), get("mgs_drawmain"))
        .Times(2)
        .WillRepeatedly(Return(clip));
    EXPECT_CALL(fixture.engine.resourceModule().audioClips(), get("mgs_playside"))
        .Times(1)
        .WillRepeatedly(Return(clip));
    EXPECT_CALL(fixture.engine.audioModule().mixer(), play(_, _, _, _, _))
        .Times(5)
        .WillRepeatedly(Return(std::shared_ptr<audio::AudioSource> {}));
    installPazaakGUIResources(fixture.engine, loaded, {}, false);

    ASSERT_TRUE(fixture.game.playPazaak(
        0,
        "",
        0,
        false,
        fixture.opponent));
    PazaakSession *session = fixture.game.pazaakSession();
    ASSERT_NE(nullptr, session);
    ASSERT_TRUE(session->selectCard(12));
    for (size_t index = 0; index < 9; ++index) {
        ASSERT_TRUE(session->selectCard(index));
    }
    ASSERT_TRUE(session->confirmSetup());
    fixture.game.showPazaakBoard();

    // Refreshing the same projection consumes no new model event.
    fixture.game.showPazaakBoard();
    fixture.game.update(0.46f);
    click(loaded, "pazaakgame", "BTN_FLIP0");
    fixture.game.showPazaakBoard();
    click(loaded, "pazaakgame", "BTN_PLRSIDE0");
    click(loaded, "pazaakgame", "BTN_XTEXT");
    ASSERT_EQ(PazaakOpponentEvent::Draw, session->advanceOpponentEvent());
    fixture.game.showPazaakBoard();
    fixture.game.showPazaakBoard();

    fixture.game.abortPazaak();
    EXPECT_TRUE(testing::Mock::VerifyAndClearExpectations(
        &fixture.engine.resourceModule().audioClips()));
    EXPECT_TRUE(testing::Mock::VerifyAndClearExpectations(
        &fixture.engine.audioModule().mixer()));
    EXPECT_TRUE(testing::Mock::VerifyAndClearExpectations(
        &fixture.engine.guiModule().guis()));
}

TEST(PazaakGUIResources, VerifiedAudioManifestMapsEveryEventAndLeavesTieSilent) {
    EXPECT_STREQ(
        "mgs_startturn",
        pazaakAudioResRef(PazaakPresentationEventType::TurnStarted));
    EXPECT_STREQ(
        "mgs_drawmain",
        pazaakAudioResRef(PazaakPresentationEventType::MainDeckDrawn));
    EXPECT_STREQ(
        "mgs_playside",
        pazaakAudioResRef(PazaakPresentationEventType::HandCardPlayed));
    EXPECT_STREQ(
        "mgs_winset",
        pazaakAudioResRef(PazaakPresentationEventType::PlayerSetWon));
    EXPECT_STREQ(
        "mgs_loseset",
        pazaakAudioResRef(PazaakPresentationEventType::PlayerSetLost));
    EXPECT_STREQ(
        "mgs_winmatch",
        pazaakAudioResRef(PazaakPresentationEventType::PlayerMatchWon));
    EXPECT_STREQ(
        "mgs_losematch",
        pazaakAudioResRef(PazaakPresentationEventType::PlayerMatchLost));
    EXPECT_EQ(
        nullptr,
        pazaakAudioResRef(PazaakPresentationEventType::SetTied));
}

TEST(PazaakGUIResources, MissingAuthoredControlFailsFlowSafely) {
    using testing::_;
    using testing::NiceMock;
    using testing::Return;

    GameFixture fixture;
    configure(fixture.game);
    TestGameModule::useRuntimePazaakGUIs(fixture.game);
    auto malformed = std::make_shared<NiceMock<gui::MockGUI>>();
    ON_CALL(*malformed, findControl(_))
        .WillByDefault(Return(std::shared_ptr<gui::Control> {}));
    EXPECT_CALL(fixture.engine.guiModule().guis(), get(_, _))
        .Times(3)
        .WillRepeatedly(Return(malformed));

    EXPECT_FALSE(fixture.game.playPazaak(0, "", 0, false, fixture.opponent));
    EXPECT_EQ(nullptr, fixture.game.pazaakSession());
    EXPECT_EQ(Game::Screen::None, fixture.game.currentScreen());
    EXPECT_FALSE(fixture.game.lastPazaakResult().has_value());
    EXPECT_TRUE(testing::Mock::VerifyAndClearExpectations(
        &fixture.engine.guiModule().guis()));
}

TEST(PazaakGUIResources, MissingKotorOneOptionalControlsUseDevelopmentFallbacks) {
    GameFixture fixture;
    configure(fixture.game);
    TestGameModule::useRuntimePazaakGUIs(fixture.game);
    LoadedPazaakGUIs loaded;
    installPazaakGUIResources(
        fixture.engine,
        loaded,
        {
            "pazaaksetup:BTN_CLEARCARDS",
            "pazaaksetup:LBL_HELP",
            "pazaaksetup:BTN_AVAIL30",
            "pazaaksetup:LBL_AVAIL30",
            "pazaaksetup:LBL_AVAILNUM30",
            "pazaakgame:BTN_FORFEITGAME",
        });

    ASSERT_TRUE(fixture.game.playPazaak(
        0,
        "",
        0,
        false,
        fixture.opponent));
    chooseTen(*fixture.game.pazaakSession());
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmSetup());
    fixture.game.showPazaakBoard();
    fixture.game.update(0.46f);

    ASSERT_EQ(Game::Screen::PazaakBoard, fixture.game.currentScreen());
    ASSERT_TRUE(
        loaded.controls.at("pazaakgame").count("BTN_FLIP0"));
    click(loaded, "pazaakgame", "BTN_FLIP0");
    input::Event escape = input::Event::newKeyDown(
        input::KeyEvent(true, input::KeyCode::Escape, 0, false));
    EXPECT_TRUE(fixture.game.handle(escape));
    ASSERT_TRUE(
        fixture.game.pazaakSession()->boardProjection().forfeitRequested);
    EXPECT_TRUE(fixture.game.handle(escape));
    fixture.game.update(1.5f);

    EXPECT_EQ(nullptr, fixture.game.pazaakSession());
    EXPECT_TRUE(testing::Mock::VerifyAndClearExpectations(
        &fixture.engine.guiModule().guis()));
}

TEST(PazaakGameLifecycle, ResetAndTechnicalAbortDoNotInventResult) {
    GameFixture fixture;
    testing::NiceMock<scene::MockSceneGraph> sceneGraph;
    ON_CALL(fixture.engine.sceneModule().graphs(), get(testing::_))
        .WillByDefault(testing::ReturnRef(sceneGraph));
    configure(fixture.game);
    ASSERT_TRUE(fixture.game.playPazaak(0, "", 0, false, fixture.opponent));
    fixture.game.abortPazaak();
    EXPECT_EQ(nullptr, fixture.game.pazaakSession());
    EXPECT_FALSE(fixture.game.lastPazaakResult().has_value());

    ASSERT_TRUE(fixture.game.playPazaak(0, "", 0, false, fixture.opponent));
    fixture.game.resetGame();
    EXPECT_EQ(nullptr, fixture.game.pazaakSession());
    EXPECT_FALSE(fixture.game.lastPazaakResult().has_value());
}

TEST(PazaakGameLifecycle, TechnicalAbortCoversEveryPresentationPhase) {
    GameFixture fixture;
    int continuationCount = 0;
    configure(
        fixture.game,
        true,
        [&](const std::string &, uint32_t) {
            ++continuationCount;
        });

    fixture.game.party().giveGold(5);
    ASSERT_TRUE(fixture.game.playPazaak(0, "unused", 5, false, fixture.opponent));
    fixture.game.abortPazaak();
    EXPECT_EQ(nullptr, fixture.game.pazaakSession());

    TestGameModule::configurePazaak(
        fixture.game,
        true,
        firstFour,
        firstFour,
        []() { return makeMainDeck({1, 2}); },
        [&](const std::string &, uint32_t) {
            ++continuationCount;
        });
    ASSERT_TRUE(fixture.game.playPazaak(0, "unused", 0, false, fixture.opponent));
    fixture.game.abortPazaak();
    EXPECT_EQ(nullptr, fixture.game.pazaakSession());

    ASSERT_TRUE(fixture.game.playPazaak(0, "unused", 0, false, fixture.opponent));
    chooseTen(*fixture.game.pazaakSession());
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmSetup());
    fixture.game.showPazaakBoard();
    fixture.game.abortPazaak();
    EXPECT_EQ(nullptr, fixture.game.pazaakSession());

    ASSERT_TRUE(fixture.game.playPazaak(0, "unused", 0, false, fixture.opponent));
    chooseTen(*fixture.game.pazaakSession());
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmSetup());
    fixture.game.showPazaakBoard();
    ASSERT_EQ(ActionError::None, fixture.game.pazaakSession()->endPlayerTurn());
    fixture.game.update(0.46f);
    fixture.game.abortPazaak();
    fixture.game.update(1.0f);
    EXPECT_EQ(nullptr, fixture.game.pazaakSession());

    ASSERT_TRUE(fixture.game.playPazaak(0, "unused", 0, false, fixture.opponent));
    chooseTen(*fixture.game.pazaakSession());
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmSetup());
    fixture.game.showPazaakBoard();
    ASSERT_EQ(ActionError::None, fixture.game.pazaakSession()->standPlayer());
    ASSERT_EQ(
        ActionError::None,
        fixture.game.pazaakSession()->applyOpponentCommand(
            DrawCommand {Participant::Two}));
    ASSERT_EQ(
        ActionError::None,
        fixture.game.pazaakSession()->applyOpponentCommand(
            StandCommand {Participant::Two}));
    ASSERT_TRUE(fixture.game.pazaakSession()->presentationPending());
    ASSERT_TRUE(
        fixture.game.pazaakSession()->advanceResultPresentation(1.5f));
    fixture.game.abortPazaak();
    EXPECT_EQ(nullptr, fixture.game.pazaakSession());

    ASSERT_TRUE(fixture.game.playPazaak(0, "unused", 0, false, fixture.opponent));
    chooseTen(*fixture.game.pazaakSession());
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmSetup());
    fixture.game.showPazaakBoard();
    for (int setIndex = 0; setIndex < 3; ++setIndex) {
        ASSERT_EQ(ActionError::None, fixture.game.pazaakSession()->standPlayer());
        ASSERT_EQ(
            ActionError::None,
            fixture.game.pazaakSession()->applyOpponentCommand(
                DrawCommand {Participant::Two}));
        ASSERT_EQ(
            ActionError::None,
            fixture.game.pazaakSession()->applyOpponentCommand(
                StandCommand {Participant::Two}));
        ASSERT_TRUE(
            fixture.game.pazaakSession()->advanceResultPresentation(1.5f));
    }
    ASSERT_TRUE(fixture.game.pazaakSession()->completedResult().has_value());
    fixture.game.abortPazaak();

    EXPECT_EQ(nullptr, fixture.game.pazaakSession());
    EXPECT_FALSE(fixture.game.lastPazaakResult().has_value());
    EXPECT_EQ(0, continuationCount);
    EXPECT_EQ(5, fixture.game.party().gold());
}

TEST(PazaakGameLifecycle, GuiLoadFailureRestoresSafeScreenAndClearsFlow) {
    GameFixture fixture;
    configure(fixture.game, false);

    EXPECT_FALSE(fixture.game.playPazaak(0, "", 0, false, fixture.opponent));
    EXPECT_EQ(nullptr, fixture.game.pazaakSession());
    EXPECT_EQ(Game::Screen::None, fixture.game.currentScreen());
    EXPECT_FALSE(fixture.game.lastPazaakResult().has_value());
}

TEST(PazaakGameLifecycle, LastResultChangesOnlyAtSemanticCompletion) {
    GameFixture fixture;
    configure(fixture.game);
    ASSERT_TRUE(fixture.game.playPazaak(0, "", 0, false, fixture.opponent));
    fixture.game.cancelPazaak();
    EXPECT_FALSE(fixture.game.lastPazaakResult().has_value());

    ASSERT_TRUE(fixture.game.playPazaak(0, "", 0, false, fixture.opponent));
    chooseTen(*fixture.game.pazaakSession());
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmSetup());
    fixture.game.showPazaakBoard();
    ASSERT_TRUE(fixture.game.pazaakSession()->requestForfeit());
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmForfeit());
    fixture.game.update(1.5f);
    ASSERT_TRUE(fixture.game.lastPazaakResult().has_value());

    auto completed = fixture.game.lastPazaakResult();
    fixture.game.abortPazaak();
    EXPECT_EQ(completed, fixture.game.lastPazaakResult());
}

TEST(PazaakScriptBoundary, NoCompletedMatchUsesAuthoredLossValue) {
    GameFixture fixture;
    Routines routines(GameID::KotOR, &fixture.game, &fixture.engine.services());
    routines.init();
    script::ExecutionContext execution;

    script::Variable result = routines.get(365).invoke({}, execution);
    EXPECT_EQ(script::VariableType::Int, result.type);
    EXPECT_EQ(0, result.intValue);
}

TEST(PazaakScriptBoundary, AuthoredIntegerMappingIsOneForWinZeroForLossAndForfeit) {
    GameFixture fixture;
    configure(fixture.game);
    Routines routines(GameID::KotOR, &fixture.game, &fixture.engine.services());
    routines.init();
    script::ExecutionContext execution;

    ASSERT_TRUE(fixture.game.playPazaak(0, "", 0, false, fixture.opponent));
    TestGameModule::finishPazaak(
        fixture.game,
        PazaakCompletedResult::PlayerWon);
    EXPECT_EQ(1, routines.get(365).invoke({}, execution).intValue);

    ASSERT_TRUE(fixture.game.playPazaak(0, "", 0, false, fixture.opponent));
    TestGameModule::finishPazaak(
        fixture.game,
        PazaakCompletedResult::OpponentWon);
    EXPECT_EQ(0, routines.get(365).invoke({}, execution).intValue);

    ASSERT_TRUE(fixture.game.playPazaak(0, "", 0, false, fixture.opponent));
    TestGameModule::finishPazaak(
        fixture.game,
        PazaakCompletedResult::PlayerForfeited);
    EXPECT_EQ(0, routines.get(365).invoke({}, execution).intValue);
}

// ---------------------------------------------------------------------------
// KotOR II integration.
// ---------------------------------------------------------------------------

TEST(PazaakK2Data, DeckTokensParseToSemanticSpecialCards) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    auto opponent = game.newCreature();
    configure(game);
    TestGameModule::useAuthoredPazaakDecks(game);
    auto table = pazaakDeckTable({
        {"$$", "F1", "F2", "TT", "VV", "+1", "-1", "*1", "+2", "-2"},
    });
    EXPECT_CALL(engine.resourceModule().twoDas(), get("pazaakdecks"))
        .Times(testing::AnyNumber())
        .WillRepeatedly(testing::Return(table));

    ASSERT_TRUE(game.playPazaak(0, "", 0, false, opponent));
    const auto &deck = game.pazaakSession()->opponentSideDeck();
    EXPECT_EQ(CardDefinition::doubleCard(), deck[0]);
    EXPECT_EQ(CardDefinition::flipTwoFour(), deck[1]);
    EXPECT_EQ(CardDefinition::flipThreeSix(), deck[2]);
    EXPECT_EQ(CardDefinition::tiebreaker(), deck[3]);
    EXPECT_EQ(CardDefinition::valueChange(), deck[4]);
    EXPECT_EQ(CardDefinition::fixedPositive(1), deck[5]);
    EXPECT_EQ(CardDefinition::fixedNegative(1), deck[6]);
    EXPECT_EQ(CardDefinition::signSelectable(1), deck[7]);
    game.abortPazaak();
}

TEST(PazaakK2Data, MalformedKotorTwoDeckRowFailsSafely) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    auto opponent = game.newCreature();
    configure(game);
    TestGameModule::useAuthoredPazaakDecks(game);
    auto table = pazaakDeckTable({
        {"$$", "F1", "F2", "TT", "VV", "+1", "-1", "*1", "+2", "F9"},
    });
    EXPECT_CALL(engine.resourceModule().twoDas(), get("pazaakdecks"))
        .Times(testing::AnyNumber())
        .WillRepeatedly(testing::Return(table));

    EXPECT_FALSE(game.playPazaak(0, "", 0, false, opponent));
    EXPECT_EQ(nullptr, game.pazaakSession());
}

TEST(PazaakK2GUI, WidescreenVariantUsesKotorTwoArtAndPlaysAValueChangeCard) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    auto opponent = game.newCreature();
    TestGameModule::configurePazaak(
        game, true, firstFour, firstFour,
        []() { return makeMainDeck({4}); }, {});
    TestGameModule::useRuntimePazaakGUIs(game);
    LoadedPazaakGUIs loaded;
    installPazaakGUIResources(engine, loaded);

    ASSERT_TRUE(game.playPazaak(0, "", 0, false, opponent));
    // The widescreen KotOR II _p GUI variant is loaded, not the KotOR I set.
    EXPECT_TRUE(loaded.controls.count("pazaakgame_p"));
    EXPECT_TRUE(loaded.controls.count("pazaaksetup_p"));
    EXPECT_FALSE(loaded.controls.count("pazaakgame"));

    auto &session = *game.pazaakSession();
    ASSERT_TRUE(session.selectCard(22));   // ValueChange -> hand slot 0
    ASSERT_TRUE(session.selectCard(19));   // Double -> hand slot 1
    for (size_t i : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u}) {
        ASSERT_TRUE(session.selectCard(i));
    }
    ASSERT_EQ(kSideDeckSize, session.chosenCards().size());
    ASSERT_TRUE(session.confirmSetup());
    game.showPazaakBoard();
    game.update(0.46f);

    auto &board = loaded.controls.at("pazaakgame_p");
    // The mandatory main-deck card uses the green main-deck face, a special card
    // uses the gold face, and the concealed/hover families are the KotOR II ones.
    EXPECT_EQ("pcards_generic_p", board.at("BTN_PLR0")->borderFillResRef());
    EXPECT_EQ("pcards_gold_p", board.at("BTN_PLRSIDE0")->borderFillResRef());
    EXPECT_EQ("pcards_gold_p", board.at("BTN_PLRSIDE1")->borderFillResRef());
    EXPECT_EQ("pcards_back_p", board.at("BTN_NPCSIDE0")->borderFillResRef());
    EXPECT_EQ("pcards_hilite_p", board.at("BTN_PLRSIDE0")->hilightFillResRef());
    EXPECT_EQ("pz_playerliteoff", board.at("LBL_PLRSCORE0")->borderFillResRef());
    // The value switch belongs to the Value Change card's own slot only.
    EXPECT_TRUE(board.at("BTN_CHANGE0")->isVisible());
    EXPECT_FALSE(board.at("BTN_CHANGE1")->isVisible());

    // The slot's own change control advances +1 -> +2, then play the card.
    click(loaded, "pazaakgame_p", "BTN_CHANGE0");
    EXPECT_EQ("+2", board.at("LBL_PLRSIDE0")->text().text);
    click(loaded, "pazaakgame_p", "BTN_PLRSIDE0");
    EXPECT_EQ("pcards_gold_p", board.at("BTN_PLR1")->borderFillResRef());
    EXPECT_EQ("+2", board.at("LBL_PLR1")->text().text);
    EXPECT_EQ(6, game.pazaakSession()->boardProjection().playerTotal);

    game.abortPazaak();
    EXPECT_TRUE(testing::Mock::VerifyAndClearExpectations(&engine.guiModule().guis()));
}

TEST(PazaakK2GUI, EveryCardFamilyUsesItsShippedResourceAndNoBareOrMojibakeText) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    auto opponent = game.newCreature();
    // Hand: +1 (blue), -1 (red), +/-1 (split), Flip 3&6 (gold special).
    auto hand = [](const SideDeck &) { return HandSelection {2, 3, 1, 0}; };
    TestGameModule::configurePazaak(
        game, true, hand, firstFour,
        []() { return makeMainDeck({7}); }, {});
    TestGameModule::useRuntimePazaakGUIs(game);
    LoadedPazaakGUIs loaded;
    installPazaakGUIResources(engine, loaded);

    ASSERT_TRUE(game.playPazaak(0, "", 0, false, opponent));
    auto &session = *game.pazaakSession();
    ASSERT_TRUE(session.selectCard(21));   // Flip 3&6 -> hand slot 3
    ASSERT_TRUE(session.selectCard(12));   // +/-1     -> hand slot 2
    ASSERT_TRUE(session.selectCard(0));    // +1       -> hand slot 0
    ASSERT_TRUE(session.selectCard(6));    // -1       -> hand slot 1
    for (size_t i : {1u, 2u, 3u, 4u, 5u, 7u}) {
        ASSERT_TRUE(session.selectCard(i));
    }
    ASSERT_EQ(kSideDeckSize, session.chosenCards().size());
    ASSERT_TRUE(session.confirmSetup());
    game.showPazaakBoard();
    game.update(0.46f);

    auto &board = loaded.controls.at("pazaakgame_p");
    // Fixed positive is blue, fixed negative is red, a sign-selectable card uses
    // the split family, and a special card is gold.
    EXPECT_EQ("pcards_pos_p", board.at("BTN_PLRSIDE0")->borderFillResRef());
    EXPECT_EQ("pcards_neg_p", board.at("BTN_PLRSIDE1")->borderFillResRef());
    EXPECT_EQ("pcards_dblpos_p", board.at("BTN_PLRSIDE2")->borderFillResRef());
    EXPECT_EQ("pcards_gold_p", board.at("BTN_PLRSIDE3")->borderFillResRef());
    // The mandatory draw keeps the green main-deck face regardless of its value.
    EXPECT_EQ("pcards_generic_p", board.at("BTN_PLR0")->borderFillResRef());
    EXPECT_EQ("7", board.at("LBL_PLR0")->text().text);

    // Value text is present, and no card label carries the stray high byte that a
    // multi-byte plus-minus sequence would produce, nor a raw deck token.
    for (const std::string &tag : {
             "LBL_PLRSIDE0", "LBL_PLRSIDE1", "LBL_PLRSIDE2", "LBL_PLRSIDE3",
             "LBL_PLR0", "LBL_CHANGEICON", "LBL_CHANGELEGEND",
         }) {
        const std::string &text = board.at(tag)->text().text;
        EXPECT_EQ(std::string::npos, text.find('\xC2')) << tag;
        for (const std::string &token : {"$$", "F1", "F2", "TT", "VV"}) {
            EXPECT_EQ(std::string::npos, text.find(token)) << tag;
        }
    }
    EXPECT_EQ("+1", board.at("LBL_PLRSIDE0")->text().text);
    EXPECT_EQ("-1", board.at("LBL_PLRSIDE1")->text().text);
    EXPECT_EQ("3&6", board.at("LBL_PLRSIDE3")->text().text);
    // The change icon/legend carry authored art only, never a duplicate value.
    EXPECT_EQ("", board.at("LBL_CHANGEICON")->text().text);

    // Sign switch only on the sign-selectable slot; no value switch anywhere.
    EXPECT_FALSE(board.at("BTN_FLIP0")->isVisible());
    EXPECT_FALSE(board.at("BTN_FLIP1")->isVisible());
    EXPECT_TRUE(board.at("BTN_FLIP2")->isVisible());
    EXPECT_FALSE(board.at("BTN_FLIP3")->isVisible());
    for (int i = 0; i < 4; ++i) {
        EXPECT_FALSE(board.at("BTN_CHANGE" + std::to_string(i))->isVisible());
    }
    EXPECT_FALSE(board.at("LBL_CHANGELEGEND")->isVisible());

    // Repeated refreshes must not accumulate controls onto the other slots.
    game.showPazaakBoard();
    game.showPazaakBoard();
    EXPECT_FALSE(board.at("BTN_FLIP0")->isVisible());
    EXPECT_TRUE(board.at("BTN_FLIP2")->isVisible());

    // Playing the selectable card clears its control and keeps its split family.
    click(loaded, "pazaakgame_p", "BTN_PLRSIDE2");
    EXPECT_FALSE(board.at("BTN_FLIP2")->isVisible());
    EXPECT_EQ("pcards_dblpos_p", board.at("BTN_PLR1")->borderFillResRef());
    EXPECT_EQ("+1", board.at("LBL_PLR1")->text().text);

    game.abortPazaak();
    EXPECT_TRUE(testing::Mock::VerifyAndClearExpectations(&engine.guiModule().guis()));
}

TEST(PazaakK1GUI, CardPresentationStillUsesTheKotorOneFamilies) {
    GameFixture fixture;
    TestGameModule::configurePazaak(
        fixture.game, true, firstFour, firstFour,
        []() { return makeMainDeck({3}); }, {});
    TestGameModule::useRuntimePazaakGUIs(fixture.game);
    LoadedPazaakGUIs loaded;
    installPazaakGUIResources(fixture.engine, loaded);

    ASSERT_TRUE(fixture.game.playPazaak(0, "", 0, false, fixture.opponent));
    // Side deck ordered so the opening hand is -1, +1, +/-1 and +2.
    auto &session = *fixture.game.pazaakSession();
    for (size_t index : {6u, 0u, 12u, 1u, 2u, 3u, 4u, 5u, 7u, 8u}) {
        ASSERT_TRUE(session.selectCard(index));
    }
    ASSERT_EQ(kSideDeckSize, session.chosenCards().size());
    ASSERT_TRUE(fixture.game.pazaakSession()->confirmSetup());
    fixture.game.showPazaakBoard();
    fixture.game.update(0.46f);

    auto &board = loaded.controls.at("pazaakgame");
    EXPECT_EQ("lbl_cardstand", board.at("BTN_PLR0")->borderFillResRef());
    EXPECT_EQ("lbl_cardmneg", board.at("BTN_PLRSIDE0")->borderFillResRef());
    EXPECT_EQ("lbl_cardmpos", board.at("BTN_PLRSIDE1")->borderFillResRef());
    EXPECT_EQ("lbl_cardrarem", board.at("BTN_PLRSIDE2")->borderFillResRef());
    EXPECT_EQ("lbl_cardback", board.at("BTN_NPCSIDE0")->borderFillResRef());
    EXPECT_EQ("lbl_cardhilite", board.at("BTN_PLRSIDE0")->hilightFillResRef());
    EXPECT_EQ("lbl_winmark01", board.at("LBL_PLRSCORE0")->borderFillResRef());
    EXPECT_EQ("3", board.at("LBL_PLR0")->text().text);
    fixture.game.abortPazaak();
    EXPECT_TRUE(testing::Mock::VerifyAndClearExpectations(
        &fixture.engine.guiModule().guis()));
}

TEST(PazaakK2Showcase, DeveloperLaunchUsesOrderedDeterministicShowcaseData) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    engine.options().game.developer = true;
    TestGameModule::initConsole(game);
    TestGameModule::setActiveModule(game, true);
    TestGameModule::setCurrentScreen(game, static_cast<int>(Game::Screen::InGame));
    TestGameModule::configurePazaak(
        game, true, {}, firstFour,
        []() { return makeMainDeck({5}); }, {});
    TestGameModule::useRuntimePazaakGUIs(game);
    LoadedPazaakGUIs loaded;
    installPazaakGUIResources(engine, loaded);

    console.execute("startpazaak");
    auto *session = game.pazaakSession();
    ASSERT_NE(nullptr, session);

    // The available cards are ordered +1..+6, -1..-6, +/-1..+/-6, then specials.
    const auto &collection = session->collection();
    ASSERT_EQ(23u, collection.size());
    for (int m = 1; m <= 6; ++m) {
        EXPECT_EQ(CardDefinition::fixedPositive(m), collection[m - 1].definition);
        EXPECT_EQ(CardDefinition::fixedNegative(m), collection[5 + m].definition);
        EXPECT_EQ(CardDefinition::signSelectable(m), collection[11 + m].definition);
    }
    EXPECT_EQ(CardDefinition::tiebreaker(), collection[18].definition);
    EXPECT_EQ(CardDefinition::doubleCard(), collection[19].definition);
    EXPECT_EQ(CardDefinition::flipTwoFour(), collection[20].definition);
    EXPECT_EQ(CardDefinition::flipThreeSix(), collection[21].definition);
    EXPECT_EQ(CardDefinition::valueChange(), collection[22].definition);

    // The ten-card side deck is deterministic and already selected.
    EXPECT_EQ(
        std::vector<size_t>({22, 12, 0, 19, 20, 21, 18, 6, 4, 13}),
        session->chosenCards());
    ASSERT_TRUE(session->confirmSetup());
    game.showPazaakBoard();

    // The opening hand covers a Value Change card, a sign-selectable card, a
    // fixed card and a non-switchable special.
    PazaakBoardProjection projection = session->boardProjection();
    ASSERT_TRUE(projection.playerHand[0].definition);
    EXPECT_TRUE(projection.playerHand[0].definition->isValueSelectable());
    EXPECT_EQ(CardBehavior::SignSelectable, projection.playerHand[1].definition->behavior());
    EXPECT_EQ(CardBehavior::FixedPositive, projection.playerHand[2].definition->behavior());
    EXPECT_EQ(CardBehavior::Double, projection.playerHand[3].definition->behavior());
    game.abortPazaak();
    EXPECT_TRUE(testing::Mock::VerifyAndClearExpectations(&engine.guiModule().guis()));
}

TEST(PazaakK2Showcase, AuthoredMatchesDoNotUseShowcaseData) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    auto opponent = game.newCreature();
    configure(game);

    // A native launch leaves the side-deck selection to ordinary setup.
    ASSERT_TRUE(game.playPazaak(0, "", 0, false, opponent));
    EXPECT_TRUE(game.pazaakSession()->chosenCards().empty());
    game.abortPazaak();
}

TEST(PazaakK2GUI, SetupLoadsWithoutTheOptionalAddCardButton) {
    // The shipped K2 pazaaksetup_p has no BTN_YTEXT ("Add card") that K1 has; a
    // card is added by clicking it in the available grid. The setup GUI must
    // still load, so the KotOR II development launch reaches the setup screen.
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    auto opponent = game.newCreature();
    TestGameModule::configurePazaak(
        game, true, firstFour, firstFour,
        []() { return makeMainDeck({5}); }, {});
    TestGameModule::useRuntimePazaakGUIs(game);
    LoadedPazaakGUIs loaded;
    installPazaakGUIResources(engine, loaded, {"pazaaksetup_p:BTN_YTEXT"});

    ASSERT_TRUE(game.playPazaak(0, "", 0, false, opponent));
    ASSERT_NE(nullptr, game.pazaakSession());
    EXPECT_EQ(Game::Screen::PazaakSetup, game.currentScreen());
    EXPECT_TRUE(loaded.controls.count("pazaaksetup_p"));
    game.abortPazaak();
    EXPECT_TRUE(testing::Mock::VerifyAndClearExpectations(&engine.guiModule().guis()));
}

TEST(PazaakK2Match, OpponentWithSpecialCardsCompletesAMatchWithoutDeadlock) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    auto opponent = game.newCreature();
    TestGameModule::configurePazaak(
        game, true, firstFour, firstFour,
        []() { return makeMainDeck({7, 6, 8, 5, 9, 4}); }, {});
    TestGameModule::useAuthoredPazaakDecks(game);
    auto table = pazaakDeckTable({
        {"$$", "F1", "F2", "TT", "VV", "*3", "*4", "+5", "-3", "+2"},
    });
    EXPECT_CALL(engine.resourceModule().twoDas(), get("pazaakdecks"))
        .Times(testing::AnyNumber())
        .WillRepeatedly(testing::Return(table));

    ASSERT_TRUE(game.playPazaak(0, "", 0, false, opponent));
    auto &session = *game.pazaakSession();
    chooseTen(session);
    ASSERT_TRUE(session.confirmSetup());

    // Drive the match to completion. The opponent policy uses the special cards
    // through the real rules; the guard proves it never deadlocks.
    int guard = 0;
    while (!session.terminal() && guard++ < 4000) {
        if (session.presentationPending()) {
            session.advanceResultPresentation(2.0f);
            continue;
        }
        PazaakBoardProjection proj = session.boardProjection();
        if (proj.playerActive) {
            if (proj.canStand) {
                session.standPlayer();
            } else {
                session.advanceOpponentEvent();
            }
        } else if (proj.state == PazaakBoardState::AwaitingOpponentPolicy) {
            session.advanceOpponentEvent();
        } else {
            break;
        }
    }
    EXPECT_TRUE(session.terminal());
    game.abortPazaak();
}
