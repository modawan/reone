/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include "pazaak.h"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace reone {

namespace game {

enum class PazaakFlowScreen {
    Wager,
    Setup,
    Board,
};

enum class PazaakCompletedResult {
    PlayerWon,
    OpponentWon,
    PlayerForfeited,
};

enum class PazaakBoardState {
    PlayerTurn,
    AwaitingOpponentPolicy,
    SetComplete,
    MatchComplete,
    UnresolvedBothNine,
};

enum class PazaakOpponentEvent {
    None,
    Draw,
    PlayerDraw,
    PlayHandCard,
    EndTurn,
    Stand,
};

enum class PazaakPresentationEventType {
    TurnStarted,
    MainDeckDrawn,
    HandCardPlayed,
    PlayerSetWon,
    PlayerSetLost,
    SetTied,
    PlayerMatchWon,
    PlayerMatchLost,
};

struct PazaakPresentationEvent {
    PazaakPresentationEventType type;
    std::optional<pazaak::Participant> participant;
};

struct PazaakCollectionCard {
    pazaak::CardDefinition definition;
    size_t copies {0};
    int persistentId {-1};
};

struct PazaakBoardSlot {
    bool occupied {false};
    int value {0};
    pazaak::PlayedCardSource source {pazaak::PlayedCardSource::MainDeck};
    std::optional<pazaak::CardBehavior> behavior;
};

struct PazaakHandSlot {
    bool occupied {false};
    bool hidden {false};
    bool used {false};
    bool playable {false};
    std::optional<pazaak::CardDefinition> definition;
    pazaak::CardSign selectedSign {pazaak::CardSign::Positive};
    // Signed committed value for a ValueChange card: one of +1, +2, -1, -2.
    int selectedValue {1};
};

struct PazaakBoardProjection {
    std::array<PazaakBoardSlot, pazaak::kBoardSize> playerBoard;
    std::array<PazaakBoardSlot, pazaak::kBoardSize> opponentBoard;
    std::array<PazaakHandSlot, pazaak::kHandSize> playerHand;
    std::array<PazaakHandSlot, pazaak::kHandSize> opponentHand;
    std::string playerName;
    std::string opponentName;
    int playerTotal {0};
    int opponentTotal {0};
    uint8_t playerSetWins {0};
    uint8_t opponentSetWins {0};
    bool playerActive {false};
    bool opponentActive {false};
    bool playerStood {false};
    bool opponentStood {false};
    bool canEndTurn {false};
    bool canStand {false};
    bool forfeitRequested {false};
    PazaakBoardState state {PazaakBoardState::PlayerTurn};
    pazaak::SetResult setResult {pazaak::SetResult::InProgress};

    bool operator==(const PazaakBoardProjection &other) const;
};

struct PazaakSessionParams {
    int opponentDeck {0};
    std::string continuationScript;
    int maximumWager {0};
    bool tutorialRequested {false};
    uint32_t opponentId {0};
    std::string playerName;
    std::string opponentName;
    int availableCredits {0};
    std::vector<PazaakCollectionCard> collection;
    std::vector<size_t> initialChosenCards;
    std::optional<pazaak::SideDeck> opponentSideDeck;
    float resultPresentationDuration {1.5f};
    bool paceAutomaticDraws {false};
};

class PazaakSession {
public:
    using HandSelector = std::function<pazaak::HandSelection(const pazaak::SideDeck &)>;
    using MainDeckFactory = std::function<pazaak::MainDeck()>;
    using FirstParticipantSelector = std::function<pazaak::Participant(size_t setIndex)>;

    PazaakSession(
        PazaakSessionParams params,
        HandSelector playerHandSelector,
        HandSelector opponentHandSelector,
        MainDeckFactory mainDeckFactory,
        FirstParticipantSelector firstParticipantSelector = {});

    /// Development-only K1 card pool; it does not read or mutate inventory.
    static std::vector<PazaakCollectionCard> temporaryK1TestCollection();
    /// Development-only opponent deck used by the startpazaak console route.
    static pazaak::SideDeck temporaryK1OpponentSideDeck();
    /// KotOR II title-default card pool (all 23 card types). Used where the
    /// clean save does not yet persist owned KotOR II cards; never mutates a save.
    static std::vector<PazaakCollectionCard> k2DefaultCollection();
    /// Development-only KotOR II opponent deck (mixes numbered and special cards).
    static pazaak::SideDeck temporaryK2OpponentSideDeck();
    /// Development-only deterministic KotOR II showcase selection: indices into
    /// k2DefaultCollection covering every supported card family.
    static std::vector<size_t> k2ShowcaseChosenCards();

    PazaakFlowScreen screen() const { return _screen; }
    int opponentDeck() const { return _params.opponentDeck; }
    const std::string &continuationScript() const { return _params.continuationScript; }
    bool tutorialRequested() const { return _params.tutorialRequested; }
    uint32_t opponentId() const { return _params.opponentId; }
    const pazaak::SideDeck &opponentSideDeck() const { return *_params.opponentSideDeck; }

    int wager() const { return _wager; }
    int wagerLimit() const { return _wagerLimit; }
    void increaseWager();
    void decreaseWager();
    bool confirmWager();

    const std::vector<PazaakCollectionCard> &collection() const { return _params.collection; }
    const std::vector<size_t> &chosenCards() const { return _chosenCards; }
    size_t remainingCopies(size_t collectionIndex) const;
    bool selectCard(size_t collectionIndex);
    bool removeChosenCard(size_t chosenIndex);
    void clearChosenCards();
    bool canConfirmSetup() const;
    bool confirmSetup();

    const pazaak::MatchState *match() const { return _match.get(); }
    PazaakBoardProjection boardProjection() const;
    bool selectPlayerCardSign(size_t handIndex, pazaak::CardSign sign);
    /// Sets the committed value of a ValueChange card; value must be +/-1 or +/-2.
    bool selectPlayerCardValue(size_t handIndex, int value);
    /// The four legal ValueChange states, in change-control order (+1, +2, -1, -2).
    static const std::array<int, 4> &valueChangeStates();
    pazaak::ActionError playPlayerHandCard(size_t handIndex);
    pazaak::ActionError endPlayerTurn();
    pazaak::ActionError standPlayer();
    bool requestForfeit();
    bool cancelForfeitRequest();
    bool confirmForfeit();

    // Explicit development seam for deterministic integration tests.
    pazaak::ActionError applyOpponentCommand(const pazaak::Command &command);
    PazaakOpponentEvent advanceOpponentEvent();
    bool advanceResultPresentation(float dt);
    std::vector<PazaakPresentationEvent> takePresentationEvents();

    const std::optional<PazaakCompletedResult> &completedResult() const { return _completedResult; }
    bool terminal() const;
    bool presentationPending() const { return _resultPresentationPending; }

private:
    PazaakSessionParams _params;
    HandSelector _playerHandSelector;
    HandSelector _opponentHandSelector;
    MainDeckFactory _mainDeckFactory;
    FirstParticipantSelector _firstParticipantSelector;
    PazaakFlowScreen _screen {PazaakFlowScreen::Setup};
    int _wager {0};
    int _wagerLimit {0};
    std::vector<size_t> _chosenCards;
    std::unique_ptr<pazaak::MatchState> _match;
    std::array<pazaak::CardSign, pazaak::kHandSize> _selectedSigns;
    std::array<int, pazaak::kHandSize> _selectedValues;
    size_t _setIndex {0};
    bool _forfeitRequested {false};
    bool _resultPresentationPending {false};
    float _resultPresentationElapsed {0.0f};
    std::vector<PazaakPresentationEvent> _presentationEvents;
    std::optional<PazaakCompletedResult> _completedResult;

    static pazaak::HandSelection defaultHandSelection();

    pazaak::SideDeck chosenSideDeck() const;
    pazaak::ActionError applyPlayerCommand(const pazaak::Command &command);
    pazaak::ActionError applyAcceptedCommand(const pazaak::Command &command);
    void beginResultPresentation();
    void queueTurnStarted(pazaak::Participant participant);
    std::optional<pazaak::PlayHandCardCommand> chooseOpponentHandCard() const;
    pazaak::Command chooseOpponentAction() const;
};

} // namespace game

} // namespace reone
