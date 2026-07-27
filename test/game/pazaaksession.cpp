/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <gtest/gtest.h>

#include "reone/game/pazaaksession.h"

#include <algorithm>

using namespace reone::game;
using namespace reone::game::pazaak;

namespace {

HandSelection firstFour(const SideDeck &) {
    return {0, 1, 2, 3};
}

PazaakSession makeSession(
    int maximumWager = 0,
    int credits = 0,
    PazaakSession::HandSelector playerSelector = firstFour,
    std::vector<PazaakCollectionCard> collection = PazaakSession::temporaryK1TestCollection(),
    PazaakSession::HandSelector opponentSelector = firstFour,
    PazaakSession::MainDeckFactory mainDeckFactory = []() {
        return MainDeck::standardOrdered();
    },
    PazaakSession::FirstParticipantSelector firstParticipantSelector = {}) {

    PazaakSessionParams params;
    params.maximumWager = maximumWager;
    params.availableCredits = credits;
    params.playerName = "Player";
    params.opponentName = "Opponent";
    params.collection = std::move(collection);
    params.opponentSideDeck = PazaakSession::temporaryK1OpponentSideDeck();
    return PazaakSession(
        std::move(params),
        std::move(playerSelector),
        std::move(opponentSelector),
        std::move(mainDeckFactory),
        std::move(firstParticipantSelector));
}

void chooseTen(PazaakSession &session) {
    for (size_t index = 0; index < kSideDeckSize; ++index) {
        ASSERT_TRUE(session.selectCard(index));
    }
}

MainDeck makeMainDeck(std::initializer_list<int> prefix) {
    std::vector<int> cards(MainDeck::standardOrdered().cards());
    size_t position = 0;
    for (int value : prefix) {
        auto found = std::find(cards.begin() + static_cast<ptrdiff_t>(position), cards.end(), value);
        std::iter_swap(cards.begin() + static_cast<ptrdiff_t>(position), found);
        ++position;
    }
    return MainDeck(std::move(cards));
}

} // namespace

TEST(PazaakFlowWager, CapsAtScriptMaximumAndAvailableCredits) {
    PazaakSession scriptLimited(makeSession(3, 20));
    EXPECT_EQ(PazaakFlowScreen::Wager, scriptLimited.screen());
    EXPECT_EQ(3, scriptLimited.wager());
    for (int i = 0; i < 10; ++i) {
        scriptLimited.increaseWager();
    }
    EXPECT_EQ(3, scriptLimited.wager());
    EXPECT_EQ(3, scriptLimited.wagerLimit());

    PazaakSession creditLimited(makeSession(20, 4));
    EXPECT_EQ(4, creditLimited.wager());
    for (int i = 0; i < 10; ++i) {
        creditLimited.increaseWager();
    }
    EXPECT_EQ(4, creditLimited.wager());
    EXPECT_EQ(4, creditLimited.wagerLimit());
    for (int i = 0; i < 10; ++i) {
        creditLimited.decreaseWager();
    }
    EXPECT_EQ(1, creditLimited.wager());
}

TEST(PazaakFlowWager, FiveCreditControlsClampToMinimumAndMaximum) {
    PazaakSession session(makeSession(12, 20));
    EXPECT_EQ(12, session.wager());
    session.increaseWager();
    EXPECT_EQ(12, session.wager());
    session.decreaseWager();
    EXPECT_EQ(7, session.wager());
    session.decreaseWager();
    EXPECT_EQ(2, session.wager());
    session.decreaseWager();
    EXPECT_EQ(1, session.wager());
    session.decreaseWager();
    EXPECT_EQ(1, session.wager());
    session.increaseWager();
    EXPECT_EQ(6, session.wager());
}

TEST(PazaakFlowWager, ZeroAllowedWagerBypassesWagerScreen) {
    EXPECT_EQ(PazaakFlowScreen::Setup, makeSession(0, 100).screen());
    EXPECT_EQ(PazaakFlowScreen::Setup, makeSession(100, 0).screen());
    EXPECT_EQ(PazaakFlowScreen::Setup, makeSession(-1, 100).screen());
}

TEST(PazaakFlowSetup, RequiresExactlyTenCardsAndHonorsAvailableCopies) {
    PazaakSession session(makeSession());
    EXPECT_FALSE(session.canConfirmSetup());
    EXPECT_FALSE(session.confirmSetup());

    EXPECT_TRUE(session.selectCard(0));
    EXPECT_TRUE(session.selectCard(0));
    EXPECT_FALSE(session.selectCard(0));
    EXPECT_EQ(0, session.remainingCopies(0));

    for (size_t index = 1; session.chosenCards().size() < kSideDeckSize; ++index) {
        ASSERT_TRUE(session.selectCard(index));
    }
    EXPECT_TRUE(session.canConfirmSetup());
    EXPECT_FALSE(session.selectCard(10));
    EXPECT_EQ(kSideDeckSize, session.chosenCards().size());

    EXPECT_TRUE(session.removeChosenCard(0));
    EXPECT_FALSE(session.canConfirmSetup());
    session.clearChosenCards();
    EXPECT_TRUE(session.chosenCards().empty());
}

TEST(PazaakFlowSetup, ConfirmationUsesInjectedDeterministicFourCardSelection) {
    PazaakSession session(makeSession(
        0,
        0,
        [](const SideDeck &) -> HandSelection { return {9, 7, 5, 3}; }));
    chooseTen(session);

    ASSERT_TRUE(session.confirmSetup());
    ASSERT_NE(nullptr, session.match());
    const auto &hand = session.match()->participant(Participant::One).hand();
    ASSERT_EQ(kHandSize, hand.size());
    EXPECT_EQ(CardDefinition::fixedNegative(4), hand[0].definition);
    EXPECT_EQ(CardDefinition::fixedNegative(2), hand[1].definition);
    EXPECT_EQ(CardDefinition::fixedPositive(6), hand[2].definition);
    EXPECT_EQ(CardDefinition::fixedPositive(4), hand[3].definition);
}

TEST(PazaakFlowBoard, ProjectionHasNineAndFourSlotsAndHidesUnusedOpponentHand) {
    PazaakSession session(makeSession());
    chooseTen(session);
    ASSERT_TRUE(session.confirmSetup());

    PazaakBoardProjection projection(session.boardProjection());
    EXPECT_EQ(kBoardSize, projection.playerBoard.size());
    EXPECT_EQ(kBoardSize, projection.opponentBoard.size());
    EXPECT_EQ(kHandSize, projection.playerHand.size());
    EXPECT_EQ(kHandSize, projection.opponentHand.size());
    EXPECT_TRUE(projection.playerBoard[0].occupied);
    EXPECT_TRUE(projection.playerActive);
    for (const auto &slot : projection.opponentHand) {
        EXPECT_TRUE(slot.occupied);
        EXPECT_TRUE(slot.hidden);
        EXPECT_FALSE(slot.definition.has_value());
    }
}

TEST(PazaakFlowBoard, LegalCommandsReachRulesAndIllegalCommandsAreProjectionAtomic) {
    PazaakSession session(makeSession());
    chooseTen(session);
    ASSERT_TRUE(session.confirmSetup());
    ASSERT_TRUE(session.boardProjection().playerHand[0].playable);

    ASSERT_EQ(ActionError::None, session.playPlayerHandCard(0));
    EXPECT_EQ(2, session.match()->set().participant(Participant::One).board().size());
    EXPECT_TRUE(session.match()->participant(Participant::One).hand()[0].used);

    MatchState before(*session.match());
    PazaakBoardProjection projectionBefore(session.boardProjection());
    EXPECT_EQ(ActionError::HandCardAlreadyPlayedThisTurn, session.playPlayerHandCard(1));
    EXPECT_EQ(before, *session.match());
    EXPECT_EQ(projectionBefore, session.boardProjection());
}

TEST(PazaakFlowBoard, SignSelectionRoutesSelectableValueToRulesModel) {
    PazaakSession session(makeSession(
        0,
        0,
        [](const SideDeck &) -> HandSelection { return {9, 8, 7, 6}; }));
    chooseTen(session);
    ASSERT_TRUE(session.confirmSetup());

    // Chosen positions 9/8/7/6 are fixed-negative in the temporary setup.
    // Build a setup whose first hand position is explicitly selectable.
    PazaakSession selectable(makeSession(
        0,
        0,
        [](const SideDeck &) -> HandSelection { return {0, 1, 2, 3}; },
        {
            {CardDefinition::signSelectable(3), 10},
        }));
    for (size_t i = 0; i < kSideDeckSize; ++i) {
        ASSERT_TRUE(selectable.selectCard(0));
    }
    ASSERT_TRUE(selectable.confirmSetup());
    EXPECT_EQ(
        CardDefinition::fixedPositive(1),
        selectable.match()->participant(Participant::Two).hand()[0].definition);
    ASSERT_TRUE(selectable.selectPlayerCardSign(0, CardSign::Negative));
    ASSERT_EQ(ActionError::None, selectable.playPlayerHandCard(0));
    EXPECT_EQ(-3, selectable.match()->set().participant(Participant::One).board().back().value());
}

TEST(PazaakFlowBoard, OpponentTurnIsExplicitlyAwaitingPolicy) {
    PazaakSession session(makeSession());
    chooseTen(session);
    ASSERT_TRUE(session.confirmSetup());
    ASSERT_EQ(ActionError::None, session.endPlayerTurn());

    PazaakBoardProjection projection(session.boardProjection());
    EXPECT_EQ(PazaakBoardState::AwaitingOpponentPolicy, projection.state);
    EXPECT_TRUE(projection.opponentActive);
    EXPECT_FALSE(projection.playerActive);
    EXPECT_FALSE(projection.canEndTurn);
    EXPECT_FALSE(projection.canStand);

    ASSERT_EQ(ActionError::None, session.applyOpponentCommand(DrawCommand {Participant::Two}));
    EXPECT_EQ(1, session.match()->set().participant(Participant::Two).board().size());
    EXPECT_EQ(PazaakBoardState::AwaitingOpponentPolicy, session.boardProjection().state);
}

TEST(PazaakFlowBoard, ForfeitCompletesSemanticallyOnce) {
    PazaakSession session(makeSession());
    chooseTen(session);
    ASSERT_TRUE(session.confirmSetup());

    EXPECT_TRUE(session.requestForfeit());
    EXPECT_TRUE(session.boardProjection().forfeitRequested);
    EXPECT_TRUE(session.confirmForfeit());
    ASSERT_TRUE(session.completedResult().has_value());
    EXPECT_EQ(PazaakCompletedResult::PlayerForfeited, *session.completedResult());
    EXPECT_FALSE(session.confirmForfeit());
    EXPECT_FALSE(session.requestForfeit());
}

TEST(PazaakFlowBoard, ParticipantTwoCanStartAndEventsDrawExactlyOneCardAtATime) {
    PazaakSession session(makeSession(
        0,
        0,
        firstFour,
        PazaakSession::temporaryK1TestCollection(),
        firstFour,
        []() { return makeMainDeck({8, 9}); },
        [](size_t) { return Participant::Two; }));
    chooseTen(session);
    ASSERT_TRUE(session.confirmSetup());

    EXPECT_TRUE(session.match()->set().participant(Participant::One).board().empty());
    EXPECT_EQ(PazaakBoardState::AwaitingOpponentPolicy, session.boardProjection().state);

    MatchState before(*session.match());
    EXPECT_EQ(PazaakOpponentEvent::Draw, session.advanceOpponentEvent());
    EXPECT_EQ(1, session.match()->set().participant(Participant::Two).board().size());
    EXPECT_TRUE(session.match()->set().participant(Participant::One).board().empty());
    EXPECT_FALSE(before == *session.match());

    before = *session.match();
    EXPECT_EQ(PazaakOpponentEvent::EndTurn, session.advanceOpponentEvent());
    EXPECT_TRUE(session.match()->set().participant(Participant::One).board().empty());
    EXPECT_EQ(TurnStage::AwaitingDraw, session.match()->set().turnStage());
    EXPECT_FALSE(before == *session.match());

    before = *session.match();
    EXPECT_EQ(PazaakOpponentEvent::PlayerDraw, session.advanceOpponentEvent());
    EXPECT_EQ(1, session.match()->set().participant(Participant::One).board().size());
    EXPECT_EQ(1, session.match()->set().participant(Participant::Two).board().size());
    EXPECT_FALSE(before == *session.match());
}

TEST(PazaakFlowBoard, AutoStandAtTwentyQueuesEachPlacementAndTurnOnce) {
    PazaakSession session(makeSession(
        0,
        0,
        firstFour,
        PazaakSession::temporaryK1TestCollection(),
        firstFour,
        []() { return makeMainDeck({10, 1, 10}); }));
    chooseTen(session);
    ASSERT_TRUE(session.confirmSetup());

    std::vector<PazaakPresentationEvent> events =
        session.takePresentationEvents();
    ASSERT_EQ(2, events.size());
    EXPECT_EQ(PazaakPresentationEventType::TurnStarted, events[0].type);
    EXPECT_EQ(Participant::One, events[0].participant);
    EXPECT_EQ(PazaakPresentationEventType::MainDeckDrawn, events[1].type);
    EXPECT_EQ(Participant::One, events[1].participant);
    EXPECT_TRUE(session.takePresentationEvents().empty());

    ASSERT_EQ(ActionError::None, session.endPlayerTurn());
    events = session.takePresentationEvents();
    ASSERT_EQ(1, events.size());
    EXPECT_EQ(PazaakPresentationEventType::TurnStarted, events[0].type);
    EXPECT_EQ(Participant::Two, events[0].participant);

    ASSERT_EQ(PazaakOpponentEvent::Draw, session.advanceOpponentEvent());
    ASSERT_EQ(PazaakOpponentEvent::EndTurn, session.advanceOpponentEvent());
    session.takePresentationEvents();
    ASSERT_EQ(PazaakOpponentEvent::PlayerDraw, session.advanceOpponentEvent());

    EXPECT_TRUE(
        session.match()->set().participant(Participant::One).stood());
    EXPECT_EQ(20, session.boardProjection().playerTotal);
    EXPECT_FALSE(session.boardProjection().canEndTurn);
    EXPECT_FALSE(session.boardProjection().canStand);
    EXPECT_EQ(
        ActionError::ParticipantStanding,
        session.playPlayerHandCard(0));

    events = session.takePresentationEvents();
    ASSERT_EQ(2, events.size());
    EXPECT_EQ(PazaakPresentationEventType::MainDeckDrawn, events[0].type);
    EXPECT_EQ(Participant::One, events[0].participant);
    EXPECT_EQ(PazaakPresentationEventType::TurnStarted, events[1].type);
    EXPECT_EQ(Participant::Two, events[1].participant);
    EXPECT_TRUE(session.takePresentationEvents().empty());
}

TEST(PazaakFlowBoard, PlayerHandCardAtTwentyQueuesOneCommitAndAutoStands) {
    std::vector<PazaakCollectionCard> collection {
        {CardDefinition::fixedPositive(4), kSideDeckSize},
    };
    PazaakSession session(makeSession(
        0,
        0,
        firstFour,
        std::move(collection),
        firstFour,
        []() { return makeMainDeck({10, 1, 6}); }));
    for (size_t i = 0; i < kSideDeckSize; ++i) {
        ASSERT_TRUE(session.selectCard(0));
    }
    ASSERT_TRUE(session.confirmSetup());
    session.takePresentationEvents();
    ASSERT_EQ(ActionError::None, session.endPlayerTurn());
    session.takePresentationEvents();
    ASSERT_EQ(PazaakOpponentEvent::Draw, session.advanceOpponentEvent());
    ASSERT_EQ(PazaakOpponentEvent::EndTurn, session.advanceOpponentEvent());
    ASSERT_EQ(PazaakOpponentEvent::PlayerDraw, session.advanceOpponentEvent());
    session.takePresentationEvents();

    ASSERT_EQ(ActionError::None, session.playPlayerHandCard(0));
    EXPECT_EQ(20, session.boardProjection().playerTotal);
    EXPECT_TRUE(
        session.match()->set().participant(Participant::One).stood());
    std::vector<PazaakPresentationEvent> events =
        session.takePresentationEvents();
    ASSERT_EQ(2, events.size());
    EXPECT_EQ(PazaakPresentationEventType::HandCardPlayed, events[0].type);
    EXPECT_EQ(PazaakPresentationEventType::TurnStarted, events[1].type);
    EXPECT_TRUE(session.takePresentationEvents().empty());
}

TEST(PazaakFlowBoard, SetCompletionAutoAdvancesAfterReadableIntervalAndPreservesUsedHand) {
    PazaakSession session(makeSession(
        0,
        0,
        firstFour,
        PazaakSession::temporaryK1TestCollection(),
        firstFour,
        []() { return makeMainDeck({10, 9}); },
        [](size_t) { return Participant::One; }));
    chooseTen(session);
    ASSERT_TRUE(session.confirmSetup());
    ASSERT_EQ(ActionError::None, session.playPlayerHandCard(0));
    ASSERT_EQ(ActionError::None, session.standPlayer());
    ASSERT_EQ(ActionError::None, session.applyOpponentCommand(DrawCommand {Participant::Two}));
    ASSERT_EQ(ActionError::None, session.applyOpponentCommand(StandCommand {Participant::Two}));

    ASSERT_EQ(SetResult::ParticipantOneWon, session.match()->set().result());
    EXPECT_EQ(PazaakBoardState::SetComplete, session.boardProjection().state);
    EXPECT_EQ(2, session.match()->set().participant(Participant::One).board().size());
    EXPECT_TRUE(session.match()->participant(Participant::One).hand()[0].used);

    MatchState completed(*session.match());
    EXPECT_FALSE(session.advanceResultPresentation(1.0f));
    EXPECT_EQ(completed, *session.match());
    EXPECT_TRUE(session.advanceResultPresentation(0.5f));
    EXPECT_EQ(SetResult::InProgress, session.match()->set().result());
    EXPECT_EQ(1, session.match()->set().participant(Participant::One).board().size());
    EXPECT_EQ(10, session.match()->set().participant(Participant::One).total());
    EXPECT_TRUE(session.match()->participant(Participant::One).hand()[0].used);
    EXPECT_EQ(1, session.match()->participant(Participant::One).setWins());

    MatchState before(*session.match());
    EXPECT_FALSE(session.advanceResultPresentation(10.0f));
    EXPECT_EQ(before, *session.match());
}

TEST(PazaakFlowBoard, TiedSetAutoTransitionsWithoutPipOrInventedCue) {
    PazaakSession session(makeSession(
        0,
        0,
        firstFour,
        PazaakSession::temporaryK1TestCollection(),
        firstFour,
        []() { return makeMainDeck({10, 10}); }));
    chooseTen(session);
    ASSERT_TRUE(session.confirmSetup());
    session.takePresentationEvents();
    ASSERT_EQ(ActionError::None, session.standPlayer());
    session.takePresentationEvents();
    ASSERT_EQ(PazaakOpponentEvent::Draw, session.advanceOpponentEvent());
    ASSERT_EQ(
        ActionError::None,
        session.applyOpponentCommand(StandCommand {Participant::Two}));

    ASSERT_EQ(SetResult::Tie, session.match()->set().result());
    EXPECT_EQ(0, session.boardProjection().playerSetWins);
    EXPECT_EQ(0, session.boardProjection().opponentSetWins);
    ASSERT_TRUE(session.presentationPending());
    std::vector<PazaakPresentationEvent> events =
        session.takePresentationEvents();
    ASSERT_EQ(2, events.size());
    EXPECT_EQ(PazaakPresentationEventType::MainDeckDrawn, events[0].type);
    EXPECT_EQ(PazaakPresentationEventType::SetTied, events[1].type);

    EXPECT_FALSE(session.advanceResultPresentation(1.49f));
    EXPECT_TRUE(session.advanceResultPresentation(0.01f));
    EXPECT_EQ(SetResult::InProgress, session.match()->set().result());
    EXPECT_EQ(0, session.boardProjection().playerSetWins);
    EXPECT_EQ(0, session.boardProjection().opponentSetWins);
}

TEST(PazaakFlowBoard, CompletedSetRejectsCommandsWithCompleteStateEquality) {
    PazaakSession session(makeSession(
        0,
        0,
        firstFour,
        PazaakSession::temporaryK1TestCollection(),
        firstFour,
        []() { return makeMainDeck({10, 9}); }));
    chooseTen(session);
    ASSERT_TRUE(session.confirmSetup());
    ASSERT_EQ(ActionError::None, session.standPlayer());
    ASSERT_EQ(ActionError::None, session.applyOpponentCommand(DrawCommand {Participant::Two}));
    ASSERT_EQ(ActionError::None, session.applyOpponentCommand(StandCommand {Participant::Two}));
    ASSERT_NE(SetResult::InProgress, session.match()->set().result());

    MatchState before(*session.match());
    PazaakBoardProjection projectionBefore(session.boardProjection());
    EXPECT_EQ(ActionError::SetComplete, session.endPlayerTurn());
    EXPECT_EQ(ActionError::SetComplete, session.standPlayer());
    EXPECT_EQ(ActionError::SetComplete, session.playPlayerHandCard(0));
    EXPECT_FALSE(session.selectPlayerCardSign(0, CardSign::Negative));
    EXPECT_FALSE(session.requestForfeit());
    EXPECT_EQ(before, *session.match());
    EXPECT_EQ(projectionBefore, session.boardProjection());
}

TEST(PazaakFlowBoard, ForfeitMakesEveryMutationTerminalAndStatePreserving) {
    PazaakSession session(makeSession());
    chooseTen(session);
    ASSERT_TRUE(session.confirmSetup());
    ASSERT_TRUE(session.requestForfeit());
    ASSERT_TRUE(session.confirmForfeit());

    MatchState before(*session.match());
    PazaakBoardProjection projectionBefore(session.boardProjection());
    auto resultBefore = session.completedResult();
    EXPECT_TRUE(session.terminal());
    EXPECT_EQ(ActionError::MatchComplete, session.endPlayerTurn());
    EXPECT_EQ(ActionError::MatchComplete, session.standPlayer());
    EXPECT_EQ(ActionError::MatchComplete, session.playPlayerHandCard(0));
    EXPECT_EQ(
        ActionError::MatchComplete,
        session.applyOpponentCommand(DrawCommand {Participant::Two}));
    EXPECT_EQ(PazaakOpponentEvent::None, session.advanceOpponentEvent());
    EXPECT_FALSE(session.selectPlayerCardSign(0, CardSign::Negative));
    EXPECT_FALSE(session.requestForfeit());
    EXPECT_FALSE(session.cancelForfeitRequest());
    EXPECT_FALSE(session.confirmForfeit());
    EXPECT_EQ(before, *session.match());
    EXPECT_EQ(projectionBefore, session.boardProjection());
    EXPECT_EQ(resultBefore, session.completedResult());
}

TEST(PazaakFlowBoard, SignSelectionRejectsInactiveAndStandingPlayerAtomically) {
    std::vector<PazaakCollectionCard> selectable {
        {CardDefinition::signSelectable(3), kSideDeckSize},
    };
    PazaakSession session(makeSession(0, 0, firstFour, std::move(selectable)));
    for (size_t i = 0; i < kSideDeckSize; ++i) {
        ASSERT_TRUE(session.selectCard(0));
    }
    ASSERT_TRUE(session.confirmSetup());
    ASSERT_TRUE(session.selectPlayerCardSign(0, CardSign::Negative));
    ASSERT_EQ(ActionError::None, session.endPlayerTurn());

    PazaakBoardProjection before(session.boardProjection());
    EXPECT_FALSE(session.selectPlayerCardSign(0, CardSign::Positive));
    EXPECT_EQ(before, session.boardProjection());

    ASSERT_EQ(ActionError::None, session.applyOpponentCommand(DrawCommand {Participant::Two}));
    ASSERT_EQ(ActionError::None, session.applyOpponentCommand(StandCommand {Participant::Two}));
    ASSERT_EQ(PazaakOpponentEvent::PlayerDraw, session.advanceOpponentEvent());
    ASSERT_EQ(ActionError::None, session.standPlayer());
    before = session.boardProjection();
    EXPECT_FALSE(session.selectPlayerCardSign(0, CardSign::Positive));
    EXPECT_EQ(before, session.boardProjection());
}

TEST(PazaakOpponentPolicy, RecoversAProvisionalBustWithDeterministicNegativeCard) {
    std::vector<PazaakCollectionCard> negativeCollection {
        {CardDefinition::fixedNegative(1), kSideDeckSize},
    };
    PazaakSession session(makeSession(
        0,
        0,
        firstFour,
        std::move(negativeCollection),
        [](const SideDeck &) -> HandSelection { return {6, 7, 8, 9}; },
        []() { return makeMainDeck({10, 8, 10, 10, 1, 5}); }));
    for (size_t i = 0; i < kSideDeckSize; ++i) {
        ASSERT_TRUE(session.selectCard(0));
    }
    ASSERT_TRUE(session.confirmSetup());

    ASSERT_EQ(ActionError::None, session.endPlayerTurn());
    ASSERT_EQ(PazaakOpponentEvent::Draw, session.advanceOpponentEvent());
    ASSERT_EQ(PazaakOpponentEvent::EndTurn, session.advanceOpponentEvent());
    ASSERT_EQ(PazaakOpponentEvent::PlayerDraw, session.advanceOpponentEvent());
    ASSERT_EQ(20, session.match()->set().participant(Participant::One).total());
    ASSERT_TRUE(session.match()->set().participant(Participant::One).stood());
    ASSERT_EQ(PazaakOpponentEvent::Draw, session.advanceOpponentEvent());
    ASSERT_EQ(18, session.match()->set().participant(Participant::Two).total());
    ASSERT_EQ(PazaakOpponentEvent::EndTurn, session.advanceOpponentEvent());
    ASSERT_EQ(PazaakOpponentEvent::Draw, session.advanceOpponentEvent());
    ASSERT_EQ(19, session.match()->set().participant(Participant::Two).total());
    ASSERT_EQ(PazaakOpponentEvent::EndTurn, session.advanceOpponentEvent());
    ASSERT_EQ(PazaakOpponentEvent::Draw, session.advanceOpponentEvent());
    ASSERT_EQ(24, session.match()->set().participant(Participant::Two).total());

    ASSERT_EQ(PazaakOpponentEvent::PlayHandCard, session.advanceOpponentEvent());
    EXPECT_EQ(20, session.match()->set().participant(Participant::Two).total());
    EXPECT_TRUE(session.match()->set().participant(Participant::Two).stood());
    const auto &opponentHand = session.match()->participant(Participant::Two).hand();
    EXPECT_FALSE(opponentHand[2].used);
    EXPECT_TRUE(opponentHand[3].used);
}

TEST(PazaakOpponentPolicy, ResultPresentationBlocksPolicyAndAdvancesOnlyAfterDuration) {
    PazaakSession session(makeSession(
        0,
        0,
        firstFour,
        PazaakSession::temporaryK1TestCollection(),
        firstFour,
        []() { return makeMainDeck({10, 9, 2}); }));
    chooseTen(session);
    ASSERT_TRUE(session.confirmSetup());
    ASSERT_EQ(ActionError::None, session.standPlayer());

    for (int count = 0;
         count < 10 && session.match()->set().result() == SetResult::InProgress;
         ++count) {
        ASSERT_NE(PazaakOpponentEvent::None, session.advanceOpponentEvent());
    }
    ASSERT_NE(SetResult::InProgress, session.match()->set().result());
    EXPECT_TRUE(session.presentationPending());

    MatchState before(*session.match());
    EXPECT_EQ(PazaakOpponentEvent::None, session.advanceOpponentEvent());
    EXPECT_EQ(before, *session.match());
    EXPECT_FALSE(session.advanceResultPresentation(1.49f));
    EXPECT_EQ(before, *session.match());
    EXPECT_TRUE(session.advanceResultPresentation(0.01f));
    EXPECT_EQ(SetResult::InProgress, session.match()->set().result());
    EXPECT_FALSE(session.presentationPending());
}

TEST(PazaakOpponentPolicy, DeterministicPolicyTerminatesACompleteMatch) {
    PazaakSession session(makeSession());
    chooseTen(session);
    ASSERT_TRUE(session.confirmSetup());

    for (int eventCount = 0;
         eventCount < 300 && !session.completedResult();
         ++eventCount) {
        const SetState &set = session.match()->set();
        if (set.result() != SetResult::InProgress) {
            ASSERT_TRUE(session.advanceResultPresentation(1.5f));
        } else if (set.activeParticipant() == Participant::One &&
                   set.turnStage() == TurnStage::AwaitingAction) {
            ASSERT_EQ(ActionError::None, session.standPlayer());
        } else {
            ASSERT_NE(PazaakOpponentEvent::None, session.advanceOpponentEvent());
        }
    }

    ASSERT_TRUE(session.completedResult().has_value());
    EXPECT_EQ(PazaakCompletedResult::OpponentWon, *session.completedResult());
    EXPECT_TRUE(session.terminal());
    EXPECT_NE(MatchResult::InProgress, session.match()->result());

    MatchState before(*session.match());
    PazaakBoardProjection projectionBefore(session.boardProjection());
    EXPECT_EQ(ActionError::MatchComplete, session.endPlayerTurn());
    EXPECT_EQ(ActionError::MatchComplete, session.standPlayer());
    EXPECT_EQ(ActionError::MatchComplete, session.playPlayerHandCard(0));
    EXPECT_EQ(PazaakOpponentEvent::None, session.advanceOpponentEvent());
    EXPECT_TRUE(session.advanceResultPresentation(1.5f));
    EXPECT_FALSE(session.advanceResultPresentation(1.5f));
    EXPECT_EQ(before, *session.match());
    EXPECT_EQ(projectionBefore, session.boardProjection());
}

TEST(PazaakFlowBoard, CompletedRulesMatchProducesSemanticPlayerWin) {
    auto setNumber = std::make_shared<int>(0);
    PazaakSessionParams params;
    params.collection = PazaakSession::temporaryK1TestCollection();
    params.opponentSideDeck = PazaakSession::temporaryK1OpponentSideDeck();
    PazaakSession session(
        std::move(params),
        firstFour,
        firstFour,
        [setNumber]() {
            bool playerStarts = ((*setNumber)++ % 2) == 0;
            return playerStarts ? makeMainDeck({10, 9}) : makeMainDeck({9, 10});
        },
        [](size_t setIndex) {
            return setIndex % 2 == 0 ? Participant::One : Participant::Two;
        });
    chooseTen(session);
    ASSERT_TRUE(session.confirmSetup());

    for (int commandCount = 0;
         commandCount < 40 && !session.completedResult();
         ++commandCount) {

        if (session.match()->set().result() != SetResult::InProgress) {
            ASSERT_TRUE(session.advanceResultPresentation(1.5f));
        } else if (session.match()->set().activeParticipant() == Participant::One) {
            if (session.match()->set().turnStage() == TurnStage::AwaitingDraw) {
                ASSERT_EQ(PazaakOpponentEvent::PlayerDraw, session.advanceOpponentEvent());
            } else {
                ASSERT_EQ(ActionError::None, session.standPlayer());
            }
        } else if (session.match()->set().turnStage() == TurnStage::AwaitingDraw) {
            ASSERT_EQ(ActionError::None, session.applyOpponentCommand(DrawCommand {Participant::Two}));
        } else {
            ASSERT_EQ(ActionError::None, session.applyOpponentCommand(StandCommand {Participant::Two}));
        }
    }

    ASSERT_TRUE(session.completedResult().has_value());
    EXPECT_EQ(PazaakCompletedResult::PlayerWon, *session.completedResult());
    EXPECT_EQ(MatchResult::ParticipantOneWon, session.match()->result());
    EXPECT_EQ(kSetWinsForMatch, session.match()->participant(Participant::One).setWins());
}
