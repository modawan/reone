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

#include <gtest/gtest.h>

#include <algorithm>
#include <initializer_list>
#include <stdexcept>
#include <utility>

#include "reone/game/pazaak.h"

using namespace reone::game::pazaak;

static SideDeck makeSideDeck() {
    return {
        CardDefinition::fixedPositive(1),
        CardDefinition::fixedPositive(2),
        CardDefinition::fixedPositive(3),
        CardDefinition::fixedPositive(4),
        CardDefinition::fixedPositive(5),
        CardDefinition::fixedPositive(6),
        CardDefinition::fixedNegative(1),
        CardDefinition::fixedNegative(2),
        CardDefinition::fixedNegative(3),
        CardDefinition::fixedNegative(4),
    };
}

static SideDeck makeSelectableSideDeck() {
    return {
        CardDefinition::signSelectable(1),
        CardDefinition::signSelectable(2),
        CardDefinition::fixedPositive(2),
        CardDefinition::fixedNegative(3),
        CardDefinition::fixedPositive(3),
        CardDefinition::fixedPositive(4),
        CardDefinition::fixedPositive(5),
        CardDefinition::fixedPositive(6),
        CardDefinition::fixedNegative(1),
        CardDefinition::fixedNegative(2),
    };
}

static SideDeck makeNegativeSixSideDeck() {
    return {
        CardDefinition::fixedNegative(6),
        CardDefinition::fixedPositive(1),
        CardDefinition::fixedPositive(2),
        CardDefinition::fixedPositive(3),
        CardDefinition::fixedPositive(4),
        CardDefinition::fixedPositive(5),
        CardDefinition::fixedPositive(6),
        CardDefinition::fixedNegative(1),
        CardDefinition::fixedNegative(2),
        CardDefinition::fixedNegative(3),
    };
}

static MainDeck makeMainDeck(std::initializer_list<int> prefix) {
    std::array<int, 11> remaining {};
    remaining.fill(4);

    std::vector<int> cards;
    cards.reserve(kMainDeckSize);
    for (int value : prefix) {
        if (value < 1 || value > 10 || remaining[value] == 0) {
            throw std::invalid_argument("Invalid deterministic main-deck prefix");
        }
        cards.push_back(value);
        --remaining[value];
    }
    for (int value = 1; value <= 10; ++value) {
        for (int count = 0; count < remaining[value]; ++count) {
            cards.push_back(value);
        }
    }
    return MainDeck(std::move(cards));
}

static ParticipantMatchState makeParticipant(const SideDeck &sideDeck = makeSideDeck(),
                                             HandSelection selection = {0, 1, 6, 8}) {
    return ParticipantMatchState(sideDeck, selection);
}

static MatchState makeMatch(MainDeck mainDeck = MainDeck::standardOrdered(),
                            ParticipantMatchState one = makeParticipant(),
                            ParticipantMatchState two = makeParticipant(),
                            Participant first = Participant::One) {
    return MatchState(std::move(mainDeck), std::move(one), std::move(two), first);
}

static void expectRejectedUnchanged(MatchState &match, const Command &command, ActionError expected) {
    MatchState before(match);
    EXPECT_EQ(expected, match.validate(command));
    EXPECT_EQ(expected, match.apply(command));
    EXPECT_EQ(before, match);
}

static void finishSet(MatchState &match, int expectedOne, int expectedTwo) {
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));
    ASSERT_EQ(expectedOne, match.set().participant(Participant::One).total());
    ASSERT_EQ(ActionError::None, match.apply(StandCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::Two}));
    ASSERT_EQ(expectedTwo, match.set().participant(Participant::Two).total());
    ASSERT_EQ(ActionError::None, match.apply(StandCommand {Participant::Two}));
}

static MatchState reachParticipantOneNineCardPriority(std::initializer_list<int> deckPrefix) {
    MatchState match = makeMatch(
        makeMainDeck(deckPrefix),
        makeParticipant(makeNegativeSixSideDeck(), {0, 1, 2, 3}),
        makeParticipant());

    for (int turn = 0; turn < 8; ++turn) {
        EXPECT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));
        if (turn == 0) {
            EXPECT_EQ(ActionError::None, match.apply(PlayHandCardCommand {Participant::One, 0, std::nullopt}));
        }
        if (!match.set().participant(Participant::One).finished()) {
            EXPECT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::One}));
        }
        if (turn < 7) {
            EXPECT_EQ(ActionError::None, match.apply(DrawCommand {Participant::Two}));
            EXPECT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::Two}));
        }
    }
    return match;
}

TEST(PazaakCards, MainDeckCompositionAndDeterministicOrder) {
    MainDeck deck = MainDeck::standardOrdered();
    ASSERT_EQ(kMainDeckSize, deck.cards().size());
    for (int value = 1; value <= 10; ++value) {
        EXPECT_EQ(4, std::count(deck.cards().begin(), deck.cards().end(), value));
    }

    MatchState match = makeMatch(makeMainDeck({7, 3}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));
    EXPECT_EQ(7, match.set().participant(Participant::One).board().back().value());
    ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::Two}));
    EXPECT_EQ(3, match.set().participant(Participant::Two).board().back().value());
}

TEST(PazaakCards, ExplicitHandSelectionHasExactlyFourCards) {
    SideDeck sideDeck = makeSideDeck();
    ParticipantMatchState participant(sideDeck, {9, 0, 5, 7});

    ASSERT_EQ(kHandSize, participant.hand().size());
    EXPECT_EQ(CardDefinition::fixedNegative(4), participant.hand()[0].definition);
    EXPECT_EQ(CardDefinition::fixedPositive(1), participant.hand()[1].definition);
    EXPECT_EQ(CardDefinition::fixedPositive(6), participant.hand()[2].definition);
    EXPECT_EQ(CardDefinition::fixedNegative(2), participant.hand()[3].definition);

    EXPECT_THROW(ParticipantMatchState(sideDeck, {0, 0, 1, 2}), std::invalid_argument);
    EXPECT_THROW(ParticipantMatchState(sideDeck, {0, 1, 2, 10}), std::invalid_argument);

    SideDeck duplicateDefinitions = makeSideDeck();
    duplicateDefinitions[1] = duplicateDefinitions[0];
    ParticipantMatchState duplicateHand(duplicateDefinitions, {0, 1, 2, 3});
    EXPECT_EQ(duplicateHand.hand()[0].definition, duplicateHand.hand()[1].definition);
}

TEST(PazaakCards, RejectsInvalidMainAndKotorOneCardDefinitions) {
    std::vector<int> shortDeck(39, 1);
    EXPECT_THROW((void)MainDeck(shortDeck), std::invalid_argument);

    std::vector<int> wrongComposition(kMainDeckSize, 1);
    EXPECT_THROW((void)MainDeck(wrongComposition), std::invalid_argument);

    std::vector<int> invalidValue = MainDeck::standardOrdered().cards();
    invalidValue[0] = 0;
    EXPECT_THROW((void)MainDeck(invalidValue), std::invalid_argument);

    EXPECT_THROW(CardDefinition(CardBehavior::FixedPositive, 0), std::invalid_argument);
    EXPECT_THROW(CardDefinition(CardBehavior::FixedNegative, 7), std::invalid_argument);
    EXPECT_THROW(CardDefinition(static_cast<CardBehavior>(99), 1), std::invalid_argument);
}

TEST(PazaakTurn, MandatoryDrawAndLegalActionQuery) {
    MatchState match = makeMatch(makeMainDeck({6}));

    LegalActions beforeDraw = match.legalActions(Participant::One);
    EXPECT_TRUE(beforeDraw.canDraw);
    EXPECT_FALSE(beforeDraw.canEndTurn);
    EXPECT_FALSE(beforeDraw.canStand);
    EXPECT_TRUE(beforeDraw.playableHandCards.empty());
    expectRejectedUnchanged(match, EndTurnCommand {Participant::One}, ActionError::MustDrawFirst);
    expectRejectedUnchanged(match, StandCommand {Participant::One}, ActionError::MustDrawFirst);
    expectRejectedUnchanged(match, PlayHandCardCommand {Participant::One, 0, std::nullopt}, ActionError::MustDrawFirst);

    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));
    EXPECT_EQ(6, match.set().participant(Participant::One).total());
    EXPECT_EQ(TurnStage::AwaitingAction, match.set().turnStage());

    LegalActions afterDraw = match.legalActions(Participant::One);
    EXPECT_FALSE(afterDraw.canDraw);
    EXPECT_TRUE(afterDraw.canEndTurn);
    EXPECT_TRUE(afterDraw.canStand);
    EXPECT_EQ(std::vector<size_t>({0, 1, 2, 3}), afterDraw.playableHandCards);
}

TEST(PazaakTurn, ParticipantTwoCanStartASet) {
    MatchState match = makeMatch(makeMainDeck({7}), makeParticipant(), makeParticipant(), Participant::Two);

    EXPECT_EQ(Participant::Two, match.set().activeParticipant());
    EXPECT_EQ(ActionError::None, match.apply(DrawCommand {Participant::Two}));
    EXPECT_EQ(7, match.set().participant(Participant::Two).total());
    expectRejectedUnchanged(match, DrawCommand {Participant::One}, ActionError::NotActiveParticipant);
}

TEST(PazaakTurn, EndTurnAndStandHaveDistinctEffects) {
    MatchState endTurnMatch = makeMatch(makeMainDeck({5}));
    ASSERT_EQ(ActionError::None, endTurnMatch.apply(DrawCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, endTurnMatch.apply(EndTurnCommand {Participant::One}));
    EXPECT_FALSE(endTurnMatch.set().participant(Participant::One).stood());
    EXPECT_EQ(Participant::Two, endTurnMatch.set().activeParticipant());
    EXPECT_EQ(TurnStage::AwaitingDraw, endTurnMatch.set().turnStage());

    MatchState standMatch = makeMatch(makeMainDeck({5}));
    ASSERT_EQ(ActionError::None, standMatch.apply(DrawCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, standMatch.apply(StandCommand {Participant::One}));
    EXPECT_TRUE(standMatch.set().participant(Participant::One).stood());
    EXPECT_EQ(Participant::Two, standMatch.set().activeParticipant());
    expectRejectedUnchanged(standMatch, DrawCommand {Participant::One}, ActionError::ParticipantStanding);
    expectRejectedUnchanged(standMatch, PlayHandCardCommand {Participant::One, 0, std::nullopt}, ActionError::ParticipantStanding);
    expectRejectedUnchanged(standMatch, EndTurnCommand {Participant::One}, ActionError::ParticipantStanding);
    expectRejectedUnchanged(standMatch, StandCommand {Participant::One}, ActionError::ParticipantStanding);
}

TEST(PazaakTurn, FixedAndSelectableHandCardsResolveToImmutableBoardValues) {
    MatchState fixed = makeMatch(makeMainDeck({10}), makeParticipant(), makeParticipant());
    ASSERT_EQ(ActionError::None, fixed.apply(DrawCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, fixed.apply(PlayHandCardCommand {Participant::One, 0, std::nullopt}));
    EXPECT_EQ(1, fixed.set().participant(Participant::One).board().back().value());
    EXPECT_EQ(PlayedCardSource::Hand, fixed.set().participant(Participant::One).board().back().source());
    EXPECT_EQ(std::optional<size_t>(0), fixed.set().participant(Participant::One).board().back().handIndex());

    MatchState negative = makeMatch(makeMainDeck({10}), makeParticipant(), makeParticipant());
    ASSERT_EQ(ActionError::None, negative.apply(DrawCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, negative.apply(PlayHandCardCommand {Participant::One, 2, std::nullopt}));
    EXPECT_EQ(-1, negative.set().participant(Participant::One).board().back().value());

    ParticipantMatchState selectableParticipant(makeSelectableSideDeck(), {0, 1, 2, 3});
    MatchState selectablePositive = makeMatch(makeMainDeck({10}), selectableParticipant, makeParticipant());
    ASSERT_EQ(ActionError::None, selectablePositive.apply(DrawCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, selectablePositive.apply(PlayHandCardCommand {Participant::One, 0, CardSign::Positive}));
    EXPECT_EQ(1, selectablePositive.set().participant(Participant::One).board().back().value());

    MatchState selectableNegative = makeMatch(makeMainDeck({10}), selectableParticipant, makeParticipant());
    ASSERT_EQ(ActionError::None, selectableNegative.apply(DrawCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, selectableNegative.apply(PlayHandCardCommand {Participant::One, 1, CardSign::Negative}));
    EXPECT_EQ(-2, selectableNegative.set().participant(Participant::One).board().back().value());
}

TEST(PazaakTurn, AllowsOnlyOneHandCardPerTurnAndUsesEachOnlyOncePerMatch) {
    MatchState match = makeMatch(makeMainDeck({10, 9}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, match.apply(PlayHandCardCommand {Participant::One, 0, std::nullopt}));
    expectRejectedUnchanged(match, PlayHandCardCommand {Participant::One, 1, std::nullopt}, ActionError::HandCardAlreadyPlayedThisTurn);
    ASSERT_EQ(ActionError::None, match.apply(StandCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::Two}));
    ASSERT_EQ(ActionError::None, match.apply(StandCommand {Participant::Two}));
    ASSERT_EQ(SetResult::ParticipantOneWon, match.set().result());

    ASSERT_EQ(ActionError::None, match.startNextSet(makeMainDeck({5}), Participant::One));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));
    expectRejectedUnchanged(match, PlayHandCardCommand {Participant::One, 0, std::nullopt}, ActionError::HandCardUsed);
    EXPECT_TRUE(match.participant(Participant::One).hand()[0].used);
}

TEST(PazaakTurn, CanRecoverAfterMandatoryDrawExceedsTwenty) {
    MatchState match = makeMatch(makeMainDeck({10, 1, 10}), makeParticipant(), makeParticipant());
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, match.apply(PlayHandCardCommand {Participant::One, 1, std::nullopt}));
    ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::Two}));
    ASSERT_EQ(ActionError::None, match.apply(StandCommand {Participant::Two}));

    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));
    EXPECT_EQ(22, match.set().participant(Participant::One).total());
    EXPECT_FALSE(match.set().participant(Participant::One).busted());
    EXPECT_EQ(SetResult::InProgress, match.set().result());
    ASSERT_EQ(ActionError::None, match.apply(PlayHandCardCommand {Participant::One, 3, std::nullopt}));
    EXPECT_EQ(19, match.set().participant(Participant::One).total());
    ASSERT_EQ(ActionError::None, match.apply(StandCommand {Participant::One}));
    EXPECT_EQ(SetResult::ParticipantOneWon, match.set().result());
}

TEST(PazaakTurn, UnresolvedOverTwentyBustsWhenTurnEnds) {
    MatchState match = makeMatch(makeMainDeck({10, 1, 10}), makeParticipant(), makeParticipant());
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, match.apply(PlayHandCardCommand {Participant::One, 1, std::nullopt}));
    ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::Two}));
    ASSERT_EQ(ActionError::None, match.apply(StandCommand {Participant::Two}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));
    ASSERT_EQ(22, match.set().participant(Participant::One).total());

    ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::One}));
    EXPECT_TRUE(match.set().participant(Participant::One).busted());
    EXPECT_EQ(SetResult::ParticipantTwoWon, match.set().result());
    EXPECT_EQ(1, match.participant(Participant::Two).setWins());
}

TEST(PazaakTurn, UnresolvedOverTwentyBustsWhenStanding) {
    MatchState match = makeMatch(makeMainDeck({10, 1, 10}), makeParticipant(), makeParticipant());
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, match.apply(PlayHandCardCommand {Participant::One, 1, std::nullopt}));
    ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::Two}));
    ASSERT_EQ(ActionError::None, match.apply(StandCommand {Participant::Two}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));
    ASSERT_EQ(22, match.set().participant(Participant::One).total());

    ASSERT_EQ(ActionError::None, match.apply(StandCommand {Participant::One}));
    EXPECT_TRUE(match.set().participant(Participant::One).busted());
    EXPECT_FALSE(match.set().participant(Participant::One).hasNineCardPriority());
    EXPECT_EQ(SetResult::ParticipantTwoWon, match.set().result());
}

TEST(PazaakAutoStand, PlayerMainDeckDrawAtTwentyFinishesExactlyOnce) {
    MatchState match = makeMatch(makeMainDeck({10, 1, 10}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::Two}));
    ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::Two}));

    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));
    EXPECT_EQ(20, match.set().participant(Participant::One).total());
    EXPECT_TRUE(match.set().participant(Participant::One).stood());
    EXPECT_EQ(Participant::Two, match.set().activeParticipant());
    EXPECT_EQ(TurnStage::AwaitingDraw, match.set().turnStage());
    expectRejectedUnchanged(
        match,
        EndTurnCommand {Participant::One},
        ActionError::ParticipantStanding);
    expectRejectedUnchanged(
        match,
        StandCommand {Participant::One},
        ActionError::ParticipantStanding);
}

TEST(PazaakAutoStand, PlayerHandCardAtTwentyCommitsBeforeStanding) {
    MatchState match = makeMatch(
        makeMainDeck({10, 1, 6}),
        makeParticipant(makeSideDeck(), {3, 0, 6, 8}),
        makeParticipant());
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::Two}));
    ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::Two}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));

    ASSERT_EQ(
        ActionError::None,
        match.apply(PlayHandCardCommand {Participant::One, 0, std::nullopt}));
    const ParticipantSetState &player = match.set().participant(Participant::One);
    ASSERT_EQ(3, player.board().size());
    EXPECT_EQ(20, player.total());
    EXPECT_EQ(PlayedCardSource::Hand, player.board().back().source());
    EXPECT_TRUE(player.stood());
    EXPECT_TRUE(match.participant(Participant::One).hand()[0].used);
}

TEST(PazaakAutoStand, OpponentMainDeckDrawAtTwentyAppliesSymmetrically) {
    MatchState match = makeMatch(makeMainDeck({1, 10, 10}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, match.apply(StandCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::Two}));
    ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::Two}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::Two}));

    EXPECT_EQ(20, match.set().participant(Participant::Two).total());
    EXPECT_TRUE(match.set().participant(Participant::Two).stood());
    EXPECT_EQ(SetResult::ParticipantTwoWon, match.set().result());
}

TEST(PazaakAutoStand, OpponentHandCardAtTwentyAppliesSymmetrically) {
    MatchState match = makeMatch(
        makeMainDeck({1, 10, 6}),
        makeParticipant(),
        makeParticipant(makeSideDeck(), {3, 0, 6, 8}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, match.apply(StandCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::Two}));
    ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::Two}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::Two}));
    ASSERT_EQ(
        ActionError::None,
        match.apply(PlayHandCardCommand {Participant::Two, 0, std::nullopt}));

    const ParticipantSetState &opponent = match.set().participant(Participant::Two);
    EXPECT_EQ(20, opponent.total());
    EXPECT_TRUE(opponent.stood());
    EXPECT_TRUE(match.participant(Participant::Two).hand()[0].used);
    EXPECT_EQ(SetResult::ParticipantTwoWon, match.set().result());
}

TEST(PazaakResults, TieDoesNotIncrementWinsAndHigherTotalWins) {
    MatchState tie = makeMatch(makeMainDeck({10, 10}));
    finishSet(tie, 10, 10);
    EXPECT_EQ(SetResult::Tie, tie.set().result());
    EXPECT_EQ(0, tie.participant(Participant::One).setWins());
    EXPECT_EQ(0, tie.participant(Participant::Two).setWins());

    MatchState ordinary = makeMatch(makeMainDeck({10, 9}));
    finishSet(ordinary, 10, 9);
    EXPECT_EQ(SetResult::ParticipantOneWon, ordinary.set().result());
    EXPECT_EQ(1, ordinary.participant(Participant::One).setWins());
    EXPECT_EQ(0, ordinary.participant(Participant::Two).setWins());

    expectRejectedUnchanged(ordinary, DrawCommand {Participant::One}, ActionError::SetComplete);
    expectRejectedUnchanged(ordinary, PlayHandCardCommand {Participant::One, 0, std::nullopt}, ActionError::SetComplete);
    expectRejectedUnchanged(ordinary, EndTurnCommand {Participant::One}, ActionError::SetComplete);
    expectRejectedUnchanged(ordinary, StandCommand {Participant::One}, ActionError::SetComplete);
    EXPECT_EQ(1, ordinary.participant(Participant::One).setWins());
}

TEST(PazaakResults, FirstParticipantToThreeSetsWinsMatch) {
    MatchState match = makeMatch(makeMainDeck({10, 9}));
    for (int setNumber = 1; setNumber <= 3; ++setNumber) {
        finishSet(match, 10, 9);
        EXPECT_EQ(setNumber, match.participant(Participant::One).setWins());
        EXPECT_LE(match.participant(Participant::One).setWins(), kSetWinsForMatch);
        if (setNumber < 3) {
            EXPECT_EQ(MatchResult::InProgress, match.result());
            ASSERT_EQ(ActionError::None, match.startNextSet(makeMainDeck({10, 9}), Participant::One));
        }
    }

    EXPECT_EQ(MatchResult::ParticipantOneWon, match.result());
    expectRejectedUnchanged(match, DrawCommand {Participant::One}, ActionError::MatchComplete);
    expectRejectedUnchanged(match, PlayHandCardCommand {Participant::One, 0, std::nullopt}, ActionError::MatchComplete);
    expectRejectedUnchanged(match, EndTurnCommand {Participant::One}, ActionError::MatchComplete);
    expectRejectedUnchanged(match, StandCommand {Participant::One}, ActionError::MatchComplete);
    MatchState before(match);
    EXPECT_EQ(ActionError::MatchComplete, match.startNextSet(makeMainDeck({10, 9}), Participant::One));
    EXPECT_EQ(before, match);
}

TEST(PazaakResults, ParticipantTwoCanWinThreeSetsAndTheMatch) {
    MatchState match = makeMatch(makeMainDeck({9, 10}));
    for (int setNumber = 1; setNumber <= 3; ++setNumber) {
        finishSet(match, 9, 10);
        EXPECT_EQ(setNumber, match.participant(Participant::Two).setWins());
        EXPECT_LE(match.participant(Participant::Two).setWins(), kSetWinsForMatch);
        if (setNumber < 3) {
            EXPECT_EQ(MatchResult::InProgress, match.result());
            ASSERT_EQ(ActionError::None, match.startNextSet(makeMainDeck({9, 10}), Participant::One));
        }
    }
    EXPECT_EQ(MatchResult::ParticipantTwoWon, match.result());
    EXPECT_EQ(3, match.participant(Participant::Two).setWins());
}

TEST(PazaakValidation, InvalidActorStageIndexAndSignDoNotMutateState) {
    MatchState match = makeMatch(makeMainDeck({5}));
    Participant invalidParticipant = static_cast<Participant>(99);

    MatchState beforeSetStart(match);
    EXPECT_EQ(ActionError::SetInProgress, match.startNextSet(makeMainDeck({6}), Participant::One));
    EXPECT_EQ(beforeSetStart, match);
    EXPECT_EQ(ActionError::InvalidParticipant, match.startNextSet(makeMainDeck({6}), invalidParticipant));
    EXPECT_EQ(beforeSetStart, match);

    expectRejectedUnchanged(match, DrawCommand {invalidParticipant}, ActionError::InvalidParticipant);
    expectRejectedUnchanged(match, DrawCommand {Participant::Two}, ActionError::NotActiveParticipant);
    expectRejectedUnchanged(match, PlayHandCardCommand {Participant::Two, 0, std::nullopt}, ActionError::NotActiveParticipant);
    expectRejectedUnchanged(match, EndTurnCommand {Participant::Two}, ActionError::NotActiveParticipant);
    expectRejectedUnchanged(match, StandCommand {Participant::Two}, ActionError::NotActiveParticipant);
    expectRejectedUnchanged(match, PlayHandCardCommand {Participant::One, 9, std::nullopt}, ActionError::MustDrawFirst);

    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));
    expectRejectedUnchanged(match, DrawCommand {Participant::One}, ActionError::DrawAlreadyPerformed);
    expectRejectedUnchanged(match, PlayHandCardCommand {Participant::One, 9, std::nullopt}, ActionError::InvalidHandIndex);
    expectRejectedUnchanged(match, PlayHandCardCommand {Participant::One, 0, CardSign::Positive}, ActionError::SignNotAllowed);

    MatchState selectable = makeMatch(makeMainDeck({5}), makeParticipant(makeSelectableSideDeck(), {0, 1, 2, 3}), makeParticipant());
    ASSERT_EQ(ActionError::None, selectable.apply(DrawCommand {Participant::One}));
    expectRejectedUnchanged(selectable, PlayHandCardCommand {Participant::One, 0, std::nullopt}, ActionError::SignRequired);
    expectRejectedUnchanged(selectable, PlayHandCardCommand {Participant::One, 0, static_cast<CardSign>(99)}, ActionError::InvalidSign);

    EXPECT_THROW((void)MatchState(makeMainDeck({5}), makeParticipant(), makeParticipant(), invalidParticipant), std::invalid_argument);
}

TEST(PazaakNineCard, PriorityFinishesParticipantAndLetsOpponentContinue) {
    MatchState match = reachParticipantOneNineCardPriority({
        1, 1,
        1, 1,
        2, 2,
        2, 2,
        3, 3,
        3, 3,
        4, 4,
        4,
        4,
    });

    const ParticipantSetState &one = match.set().participant(Participant::One);
    ASSERT_EQ(kBoardSize, one.board().size());
    EXPECT_EQ(14, one.total());
    EXPECT_TRUE(one.hasNineCardPriority());
    EXPECT_TRUE(one.finished());
    EXPECT_FALSE(one.stood());
    EXPECT_FALSE(one.busted());
    EXPECT_EQ(SetResult::InProgress, match.set().result());
    EXPECT_EQ(Participant::Two, match.set().activeParticipant());
    EXPECT_EQ(TurnStage::AwaitingDraw, match.set().turnStage());

    expectRejectedUnchanged(match, DrawCommand {Participant::One}, ActionError::ParticipantFinished);
    expectRejectedUnchanged(match, PlayHandCardCommand {Participant::One, 1, std::nullopt}, ActionError::ParticipantFinished);
    expectRejectedUnchanged(match, EndTurnCommand {Participant::One}, ActionError::ParticipantFinished);
    expectRejectedUnchanged(match, StandCommand {Participant::One}, ActionError::ParticipantFinished);

    LegalActions priorityActions = match.legalActions(Participant::One);
    EXPECT_FALSE(priorityActions.canDraw);
    EXPECT_FALSE(priorityActions.canEndTurn);
    EXPECT_FALSE(priorityActions.canStand);
    EXPECT_TRUE(priorityActions.playableHandCards.empty());

    EXPECT_TRUE(match.legalActions(Participant::Two).canDraw);
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::Two}));
    EXPECT_EQ(8, match.set().participant(Participant::Two).board().size());
}

TEST(PazaakNineCard, NinthCardAtExactlyTwentyKeepsNineCardPriorityPrecedence) {
    MatchState match = reachParticipantOneNineCardPriority({
        2, 1,
        2, 1,
        3, 1,
        3, 1,
        4, 2,
        4, 2,
        4, 3,
        4,
    });

    const ParticipantSetState &player =
        match.set().participant(Participant::One);
    EXPECT_EQ(20, player.total());
    EXPECT_TRUE(player.hasNineCardPriority());
    EXPECT_TRUE(player.finished());
    EXPECT_FALSE(player.stood());
    EXPECT_EQ(Participant::Two, match.set().activeParticipant());
    EXPECT_EQ(TurnStage::AwaitingDraw, match.set().turnStage());
}

TEST(PazaakNineCard, PriorityDefeatsOpponentStandingOnHigherLegalTotal) {
    MatchState match = reachParticipantOneNineCardPriority({
        1, 1,
        1, 1,
        2, 2,
        2, 2,
        3, 3,
        3, 3,
        4, 4,
        4,
        4,
    });
    ASSERT_EQ(14, match.set().participant(Participant::One).total());

    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::Two}));
    ASSERT_EQ(20, match.set().participant(Participant::Two).total());
    EXPECT_TRUE(match.set().participant(Participant::Two).stood());

    EXPECT_EQ(SetResult::ParticipantOneWon, match.set().result());
    EXPECT_EQ(1, match.participant(Participant::One).setWins());
    EXPECT_EQ(0, match.participant(Participant::Two).setWins());
}

TEST(PazaakNineCard, PriorityDefeatsOpponentWhoLaterBusts) {
    MatchState match = reachParticipantOneNineCardPriority({
        1, 1,
        1, 1,
        2, 2,
        2, 2,
        3, 3,
        3, 3,
        4, 4,
        4,
        5,
    });

    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::Two}));
    ASSERT_EQ(21, match.set().participant(Participant::Two).total());
    ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::Two}));

    EXPECT_TRUE(match.set().participant(Participant::Two).busted());
    EXPECT_EQ(SetResult::ParticipantOneWon, match.set().result());
    EXPECT_EQ(1, match.participant(Participant::One).setWins());
}

TEST(PazaakNineCard, PriorityWinsWhenOpponentHadAlreadyFinished) {
    MatchState match = makeMatch(makeMainDeck({1, 10, 1, 1, 1, 2, 2, 2, 2, 3}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::Two}));
    ASSERT_EQ(ActionError::None, match.apply(StandCommand {Participant::Two}));

    for (size_t boardSize = 2; boardSize <= kBoardSize; ++boardSize) {
        ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));
        ASSERT_EQ(boardSize, match.set().participant(Participant::One).board().size());
        ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::One}));
    }

    EXPECT_TRUE(match.set().participant(Participant::One).hasNineCardPriority());
    EXPECT_EQ(SetResult::ParticipantOneWon, match.set().result());
    EXPECT_EQ(1, match.participant(Participant::One).setWins());
}

TEST(PazaakNineCard, NineCardsOverTwentyBustWithoutPriority) {
    MatchState match = makeMatch(makeMainDeck({1, 10, 1, 1, 1, 3, 3, 3, 3, 5}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::Two}));
    ASSERT_EQ(ActionError::None, match.apply(StandCommand {Participant::Two}));

    for (size_t boardSize = 2; boardSize <= kBoardSize; ++boardSize) {
        ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));
        ASSERT_EQ(boardSize, match.set().participant(Participant::One).board().size());
        if (boardSize < kBoardSize) {
            ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::One}));
        }
    }

    ASSERT_EQ(21, match.set().participant(Participant::One).total());
    ASSERT_EQ(ActionError::None, match.apply(StandCommand {Participant::One}));
    EXPECT_TRUE(match.set().participant(Participant::One).busted());
    EXPECT_FALSE(match.set().participant(Participant::One).hasNineCardPriority());
    EXPECT_EQ(SetResult::ParticipantTwoWon, match.set().result());
}

TEST(PazaakNineCard, BothNineIsExplicitlyUnresolvedAndCannotResolveTwice) {
    MatchState match = reachParticipantOneNineCardPriority({
        2, 1,
        2, 1,
        3, 1,
        3, 1,
        4, 2,
        4, 2,
        4, 3,
        4,
        3, 5,
    });
    ASSERT_TRUE(match.set().participant(Participant::One).hasNineCardPriority());
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::Two}));
    ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::Two}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::Two}));
    ASSERT_EQ(9, match.set().participant(Participant::Two).board().size());
    ASSERT_EQ(19, match.set().participant(Participant::Two).total());
    ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::Two}));

    ASSERT_EQ(SetResult::UnresolvedBothNine, match.set().result());
    EXPECT_EQ(TurnStage::Complete, match.set().turnStage());
    EXPECT_TRUE(match.set().participant(Participant::Two).hasNineCardPriority());
    EXPECT_EQ(0, match.participant(Participant::One).setWins());
    EXPECT_EQ(0, match.participant(Participant::Two).setWins());
    EXPECT_EQ(MatchResult::InProgress, match.result());

    LegalActions oneActions = match.legalActions(Participant::One);
    LegalActions twoActions = match.legalActions(Participant::Two);
    EXPECT_FALSE(oneActions.canDraw);
    EXPECT_FALSE(oneActions.canEndTurn);
    EXPECT_FALSE(oneActions.canStand);
    EXPECT_TRUE(oneActions.playableHandCards.empty());
    EXPECT_FALSE(twoActions.canDraw);
    EXPECT_FALSE(twoActions.canEndTurn);
    EXPECT_FALSE(twoActions.canStand);
    EXPECT_TRUE(twoActions.playableHandCards.empty());

    expectRejectedUnchanged(match, DrawCommand {Participant::One}, ActionError::SetComplete);
    expectRejectedUnchanged(match, PlayHandCardCommand {Participant::Two, 0, std::nullopt}, ActionError::SetComplete);
    expectRejectedUnchanged(match, EndTurnCommand {Participant::One}, ActionError::SetComplete);
    expectRejectedUnchanged(match, StandCommand {Participant::Two}, ActionError::SetComplete);

    MatchState unresolved(match);
    EXPECT_EQ(ActionError::SetUnresolved, match.startNextSet(makeMainDeck({10, 9}), Participant::One));
    EXPECT_EQ(unresolved, match);
    EXPECT_EQ(0, match.participant(Participant::One).setWins());
    EXPECT_EQ(0, match.participant(Participant::Two).setWins());
}

// ---------------------------------------------------------------------------
// KotOR II special side-deck cards.
// ---------------------------------------------------------------------------

// A ten-card side deck whose first card is the supplied special card, so a
// {0,1,2,3} hand selection always surfaces it as hand slot 0.
static SideDeck specialLeadDeck(CardDefinition lead) {
    return {
        lead,
        CardDefinition::fixedPositive(1),
        CardDefinition::fixedPositive(2),
        CardDefinition::fixedPositive(3),
        CardDefinition::fixedPositive(4),
        CardDefinition::fixedPositive(5),
        CardDefinition::fixedPositive(6),
        CardDefinition::fixedNegative(1),
        CardDefinition::fixedNegative(2),
        CardDefinition::fixedNegative(3),
    };
}

TEST(PazaakSpecialCards, DefinitionsValidateMagnitudeByBehavior) {
    EXPECT_EQ(CardBehavior::Double, CardDefinition::doubleCard().behavior());
    EXPECT_TRUE(CardDefinition::doubleCard().isSpecial());
    EXPECT_TRUE(CardDefinition::tiebreaker().isSignSelectable());
    EXPECT_TRUE(CardDefinition::valueChange().isValueSelectable());
    EXPECT_EQ(1, CardDefinition::tiebreaker().magnitude());
    // A special card must not declare a magnitude, and the Tiebreaker must be 1.
    EXPECT_THROW(CardDefinition(CardBehavior::Double, 3), std::invalid_argument);
    EXPECT_THROW(CardDefinition(CardBehavior::Tiebreaker, 2), std::invalid_argument);
    EXPECT_THROW(CardDefinition(CardBehavior::ValueChange, 1), std::invalid_argument);
}

TEST(PazaakSpecialCards, DoubleAddsTheValueOfThePrecedingBoardCard) {
    MatchState match = makeMatch(
        makeMainDeck({5}),
        makeParticipant(specialLeadDeck(CardDefinition::doubleCard()), {0, 1, 2, 3}),
        makeParticipant());
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));
    ASSERT_EQ(5, match.set().participant(Participant::One).total());
    ASSERT_EQ(ActionError::None, match.apply(PlayHandCardCommand {Participant::One, 0, std::nullopt}));
    // The Double is placed as a distinct board card that repeats the preceding
    // value, so the total updates once from 5 to 10.
    EXPECT_EQ(10, match.set().participant(Participant::One).total());
    ASSERT_EQ(2u, match.set().participant(Participant::One).board().size());
    EXPECT_EQ(5, match.set().participant(Participant::One).board()[1].value());
}

TEST(PazaakSpecialCards, DoubleCanPushAParticipantIntoABust) {
    MatchState match = makeMatch(
        makeMainDeck({6, 5, 8}),
        makeParticipant(specialLeadDeck(CardDefinition::doubleCard()), {0, 1, 2, 3}),
        makeParticipant());
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));   // 6
    ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::Two}));   // 5
    ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::Two}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));   // 6+8=14
    ASSERT_EQ(14, match.set().participant(Participant::One).total());
    ASSERT_EQ(ActionError::None, match.apply(PlayHandCardCommand {Participant::One, 0, std::nullopt}));
    EXPECT_EQ(22, match.set().participant(Participant::One).total());           // doubles the 8
    ASSERT_EQ(ActionError::None, match.apply(StandCommand {Participant::One}));
    EXPECT_EQ(SetResult::ParticipantTwoWon, match.set().result());
}

TEST(PazaakSpecialCards, DoubleRejectedBeforeTheMandatoryDraw) {
    MatchState match = makeMatch(
        makeMainDeck({5}),
        makeParticipant(specialLeadDeck(CardDefinition::doubleCard()), {0, 1, 2, 3}),
        makeParticipant());
    // No card has been drawn yet, so there is nothing to double.
    expectRejectedUnchanged(match, PlayHandCardCommand {Participant::One, 0, std::nullopt}, ActionError::MustDrawFirst);
}

TEST(PazaakSpecialCards, FlipTwoFourNegatesOwnPositiveTwosAndFours) {
    MatchState match = makeMatch(
        makeMainDeck({2, 1, 4}),
        makeParticipant(specialLeadDeck(CardDefinition::flipTwoFour()), {0, 1, 2, 3}),
        makeParticipant());
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));   // 2
    ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::Two}));   // 1
    ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::Two}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));   // 2+4=6
    ASSERT_EQ(6, match.set().participant(Participant::One).total());
    ASSERT_EQ(ActionError::None, match.apply(PlayHandCardCommand {Participant::One, 0, std::nullopt}));
    const auto &board = match.set().participant(Participant::One).board();
    EXPECT_EQ(-2, board[0].value());
    EXPECT_EQ(-4, board[1].value());
    EXPECT_EQ(0, board[2].value());   // the flip card contributes nothing itself
    EXPECT_EQ(-6, match.set().participant(Participant::One).total());
}

TEST(PazaakSpecialCards, FlipThreeSixNegatesOwnPositiveThreesAndSixes) {
    MatchState match = makeMatch(
        makeMainDeck({3, 1, 6}),
        makeParticipant(specialLeadDeck(CardDefinition::flipThreeSix()), {0, 1, 2, 3}),
        makeParticipant());
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));   // 3
    ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::Two}));   // 1
    ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::Two}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));   // 3+6=9
    ASSERT_EQ(ActionError::None, match.apply(PlayHandCardCommand {Participant::One, 0, std::nullopt}));
    const auto &board = match.set().participant(Participant::One).board();
    EXPECT_EQ(-3, board[0].value());
    EXPECT_EQ(-6, board[1].value());
    EXPECT_EQ(-9, match.set().participant(Participant::One).total());
}

TEST(PazaakSpecialCards, ValueChangeSelectsAmongTheFourSignedValues) {
    MatchState match = makeMatch(
        makeMainDeck({5}),
        makeParticipant(specialLeadDeck(CardDefinition::valueChange()), {0, 1, 2, 3}),
        makeParticipant());
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));
    // A value is required, a sign is not permitted, and only +/-1 and +/-2 are legal.
    EXPECT_EQ(ActionError::ValueRequired, match.validate(PlayHandCardCommand {Participant::One, 0, std::nullopt, std::nullopt}));
    EXPECT_EQ(ActionError::SignNotAllowed, match.validate(PlayHandCardCommand {Participant::One, 0, CardSign::Positive, 1}));
    EXPECT_EQ(ActionError::InvalidValue, match.validate(PlayHandCardCommand {Participant::One, 0, std::nullopt, 3}));
    ASSERT_EQ(ActionError::None, match.apply(PlayHandCardCommand {Participant::One, 0, std::nullopt, -2}));
    EXPECT_EQ(3, match.set().participant(Participant::One).total());
    EXPECT_EQ(-2, match.set().participant(Participant::One).board()[1].value());
}

TEST(PazaakSpecialCards, ValueChangeAtExactlyTwentyAutomaticallyStands) {
    MatchState match = makeMatch(
        makeMainDeck({10, 5, 8}),
        makeParticipant(specialLeadDeck(CardDefinition::valueChange()), {0, 1, 2, 3}),
        makeParticipant());
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));   // 10
    ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::Two}));   // 5
    ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::Two}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));   // 18
    ASSERT_EQ(ActionError::None, match.apply(PlayHandCardCommand {Participant::One, 0, std::nullopt, 2}));  // 20
    EXPECT_EQ(20, match.set().participant(Participant::One).total());
    EXPECT_TRUE(match.set().participant(Participant::One).stood());
}

TEST(PazaakSpecialCards, TiebreakerWinsAnOtherwiseTiedSet) {
    MatchState match = makeMatch(
        makeMainDeck({9, 10}),
        makeParticipant(specialLeadDeck(CardDefinition::tiebreaker()), {0, 1, 2, 3}),
        makeParticipant());
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));   // 9
    ASSERT_EQ(ActionError::None, match.apply(PlayHandCardCommand {Participant::One, 0, CardSign::Positive}));  // +1 -> 10
    EXPECT_TRUE(match.set().participant(Participant::One).hasTiebreaker());
    ASSERT_EQ(ActionError::None, match.apply(StandCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::Two}));   // 10
    ASSERT_EQ(ActionError::None, match.apply(StandCommand {Participant::Two}));
    // Totals are tied at 10, but the Tiebreaker holder takes the set.
    EXPECT_EQ(SetResult::ParticipantOneWon, match.set().result());
}

TEST(PazaakSpecialCards, TiebreakerDoesNotOverrideABust) {
    MatchState match = makeMatch(
        makeMainDeck({10, 5, 10}),
        makeParticipant(specialLeadDeck(CardDefinition::tiebreaker()), {0, 1, 2, 3}),
        makeParticipant());
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));   // 10
    ASSERT_EQ(ActionError::None, match.apply(PlayHandCardCommand {Participant::One, 0, CardSign::Positive}));  // 11
    ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::Two}));   // 5
    ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::Two}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));   // 21, busting
    ASSERT_EQ(21, match.set().participant(Participant::One).total());
    ASSERT_EQ(ActionError::None, match.apply(StandCommand {Participant::One}));
    // The bust is resolved before any tie logic, so the Tiebreaker holder loses.
    EXPECT_EQ(SetResult::ParticipantTwoWon, match.set().result());
}

TEST(PazaakSpecialCards, UsedSpecialCardCannotBePlayedAgain) {
    MatchState match = makeMatch(
        makeMainDeck({3, 5, 3}),
        makeParticipant(specialLeadDeck(CardDefinition::valueChange()), {0, 1, 2, 3}),
        makeParticipant());
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));   // 3
    ASSERT_EQ(ActionError::None, match.apply(PlayHandCardCommand {Participant::One, 0, std::nullopt, 1}));
    ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::One}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::Two}));
    ASSERT_EQ(ActionError::None, match.apply(EndTurnCommand {Participant::Two}));
    ASSERT_EQ(ActionError::None, match.apply(DrawCommand {Participant::One}));
    expectRejectedUnchanged(match, PlayHandCardCommand {Participant::One, 0, std::nullopt, 1}, ActionError::HandCardUsed);
}
