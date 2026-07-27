/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "reone/game/pazaaksession.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace reone {

namespace game {

using namespace pazaak;

namespace {

bool boardSlotsEqual(
    const std::array<PazaakBoardSlot, kBoardSize> &left,
    const std::array<PazaakBoardSlot, kBoardSize> &right) {

    for (size_t i = 0; i < left.size(); ++i) {
        if (left[i].occupied != right[i].occupied ||
            left[i].value != right[i].value ||
            left[i].source != right[i].source ||
            left[i].behavior != right[i].behavior) {
            return false;
        }
    }
    return true;
}

bool handSlotsEqual(
    const std::array<PazaakHandSlot, kHandSize> &left,
    const std::array<PazaakHandSlot, kHandSize> &right) {

    for (size_t i = 0; i < left.size(); ++i) {
        if (left[i].occupied != right[i].occupied ||
            left[i].hidden != right[i].hidden ||
            left[i].used != right[i].used ||
            left[i].playable != right[i].playable ||
            !(left[i].definition == right[i].definition) ||
            left[i].selectedSign != right[i].selectedSign) {
            return false;
        }
    }
    return true;
}

} // namespace

bool PazaakBoardProjection::operator==(const PazaakBoardProjection &other) const {
    return boardSlotsEqual(playerBoard, other.playerBoard) &&
           boardSlotsEqual(opponentBoard, other.opponentBoard) &&
           handSlotsEqual(playerHand, other.playerHand) &&
           handSlotsEqual(opponentHand, other.opponentHand) &&
           playerName == other.playerName &&
           opponentName == other.opponentName &&
           playerTotal == other.playerTotal &&
           opponentTotal == other.opponentTotal &&
           playerSetWins == other.playerSetWins &&
           opponentSetWins == other.opponentSetWins &&
           playerActive == other.playerActive &&
           opponentActive == other.opponentActive &&
           playerStood == other.playerStood &&
           opponentStood == other.opponentStood &&
           canEndTurn == other.canEndTurn &&
           canStand == other.canStand &&
           forfeitRequested == other.forfeitRequested &&
           state == other.state &&
           setResult == other.setResult;
}

PazaakSession::PazaakSession(
    PazaakSessionParams params,
    HandSelector playerHandSelector,
    HandSelector opponentHandSelector,
    MainDeckFactory mainDeckFactory,
    FirstParticipantSelector firstParticipantSelector) :
    _params(std::move(params)),
    _playerHandSelector(std::move(playerHandSelector)),
    _opponentHandSelector(std::move(opponentHandSelector)),
    _mainDeckFactory(std::move(mainDeckFactory)),
    _firstParticipantSelector(std::move(firstParticipantSelector)) {

    if (_params.collection.empty()) {
        throw std::invalid_argument("Pazaak collection must not be empty");
    }
    if (!_params.opponentSideDeck) {
        throw std::invalid_argument("Pazaak opponent side deck must be supplied");
    }
    if (!_playerHandSelector) {
        _playerHandSelector = [](const SideDeck &) { return defaultHandSelection(); };
    }
    if (!_opponentHandSelector) {
        _opponentHandSelector = [](const SideDeck &) { return defaultHandSelection(); };
    }
    if (!_mainDeckFactory) {
        _mainDeckFactory = []() { return MainDeck::standardOrdered(); };
    }
    if (!_firstParticipantSelector) {
        _firstParticipantSelector = [](size_t) { return Participant::One; };
    }

    _wagerLimit = std::min(
        std::max(0, _params.maximumWager),
        std::max(0, _params.availableCredits));
    _params.resultPresentationDuration =
        std::max(0.0f, _params.resultPresentationDuration);
    _screen = _wagerLimit > 0 ? PazaakFlowScreen::Wager : PazaakFlowScreen::Setup;
    _wager = _wagerLimit;
    _chosenCards = _params.initialChosenCards;
    if (_chosenCards.size() > kSideDeckSize ||
        std::any_of(_chosenCards.begin(), _chosenCards.end(), [this](size_t index) {
            return index >= _params.collection.size();
        })) {
        throw std::invalid_argument("Invalid saved Pazaak side deck selection");
    }
    for (size_t index = 0; index < _params.collection.size(); ++index) {
        if (std::count(_chosenCards.begin(), _chosenCards.end(), index) >
            static_cast<ptrdiff_t>(_params.collection[index].copies)) {
            throw std::invalid_argument("Saved Pazaak side deck exceeds owned copies");
        }
    }
    _selectedSigns.fill(CardSign::Positive);
    _selectedValues.fill(1);
}

const std::array<int, 4> &PazaakSession::valueChangeStates() {
    static const std::array<int, 4> states {1, 2, -1, -2};
    return states;
}

std::vector<PazaakCollectionCard> PazaakSession::temporaryK1TestCollection() {
    std::vector<PazaakCollectionCard> cards;
    cards.reserve(18);
    for (int magnitude = 1; magnitude <= 6; ++magnitude) {
        cards.push_back({CardDefinition::fixedPositive(magnitude), 2, magnitude - 1});
    }
    for (int magnitude = 1; magnitude <= 6; ++magnitude) {
        cards.push_back({CardDefinition::fixedNegative(magnitude), 2, 6 + magnitude - 1});
    }
    for (int magnitude = 1; magnitude <= 6; ++magnitude) {
        cards.push_back({CardDefinition::signSelectable(magnitude), 2, 12 + magnitude - 1});
    }
    return cards;
}

void PazaakSession::increaseWager() {
    if (_screen == PazaakFlowScreen::Wager && _wager < _wagerLimit) {
        _wager = std::min(_wagerLimit, _wager + 5);
    }
}

void PazaakSession::decreaseWager() {
    if (_screen == PazaakFlowScreen::Wager && _wager > 1) {
        _wager = std::max(1, _wager - 5);
    }
}

bool PazaakSession::confirmWager() {
    if (_screen != PazaakFlowScreen::Wager || _wager < 1) {
        return false;
    }
    _screen = PazaakFlowScreen::Setup;
    return true;
}

size_t PazaakSession::remainingCopies(size_t collectionIndex) const {
    if (collectionIndex >= _params.collection.size()) {
        return 0;
    }
    size_t selected = static_cast<size_t>(std::count(
        _chosenCards.begin(), _chosenCards.end(), collectionIndex));
    size_t copies = _params.collection[collectionIndex].copies;
    return selected < copies ? copies - selected : 0;
}

bool PazaakSession::selectCard(size_t collectionIndex) {
    if (_screen != PazaakFlowScreen::Setup ||
        _chosenCards.size() >= kSideDeckSize ||
        remainingCopies(collectionIndex) == 0) {
        return false;
    }
    _chosenCards.push_back(collectionIndex);
    return true;
}

bool PazaakSession::removeChosenCard(size_t chosenIndex) {
    if (_screen != PazaakFlowScreen::Setup || chosenIndex >= _chosenCards.size()) {
        return false;
    }
    _chosenCards.erase(_chosenCards.begin() + static_cast<ptrdiff_t>(chosenIndex));
    return true;
}

void PazaakSession::clearChosenCards() {
    if (_screen == PazaakFlowScreen::Setup) {
        _chosenCards.clear();
    }
}

bool PazaakSession::canConfirmSetup() const {
    return _screen == PazaakFlowScreen::Setup && _chosenCards.size() == kSideDeckSize;
}

SideDeck PazaakSession::chosenSideDeck() const {
    if (_chosenCards.size() != kSideDeckSize) {
        throw std::logic_error("Pazaak setup does not contain ten cards");
    }
    return {
        _params.collection[_chosenCards[0]].definition,
        _params.collection[_chosenCards[1]].definition,
        _params.collection[_chosenCards[2]].definition,
        _params.collection[_chosenCards[3]].definition,
        _params.collection[_chosenCards[4]].definition,
        _params.collection[_chosenCards[5]].definition,
        _params.collection[_chosenCards[6]].definition,
        _params.collection[_chosenCards[7]].definition,
        _params.collection[_chosenCards[8]].definition,
        _params.collection[_chosenCards[9]].definition,
    };
}

SideDeck PazaakSession::temporaryK1OpponentSideDeck() {
    return {
        CardDefinition::fixedPositive(1),
        CardDefinition::fixedPositive(2),
        CardDefinition::fixedPositive(3),
        CardDefinition::fixedPositive(4),
        CardDefinition::fixedPositive(5),
        CardDefinition::fixedPositive(6),
        CardDefinition::fixedNegative(1),
        CardDefinition::fixedNegative(2),
        CardDefinition::signSelectable(3),
        CardDefinition::signSelectable(4),
    };
}

std::vector<PazaakCollectionCard> PazaakSession::k2DefaultCollection() {
    std::vector<PazaakCollectionCard> cards;
    cards.reserve(23);
    int id = 0;
    for (int magnitude = 1; magnitude <= 6; ++magnitude) {
        cards.push_back({CardDefinition::fixedPositive(magnitude), 2, id++});
    }
    for (int magnitude = 1; magnitude <= 6; ++magnitude) {
        cards.push_back({CardDefinition::fixedNegative(magnitude), 2, id++});
    }
    for (int magnitude = 1; magnitude <= 6; ++magnitude) {
        cards.push_back({CardDefinition::signSelectable(magnitude), 2, id++});
    }
    // Specials follow the authored side-deck screen order.
    cards.push_back({CardDefinition::tiebreaker(), 2, id++});
    cards.push_back({CardDefinition::doubleCard(), 2, id++});
    cards.push_back({CardDefinition::flipTwoFour(), 2, id++});
    cards.push_back({CardDefinition::flipThreeSix(), 2, id++});
    cards.push_back({CardDefinition::valueChange(), 2, id++});
    return cards;
}

SideDeck PazaakSession::temporaryK2OpponentSideDeck() {
    return {
        CardDefinition::fixedPositive(3),
        CardDefinition::fixedPositive(4),
        CardDefinition::fixedPositive(5),
        CardDefinition::signSelectable(1),
        CardDefinition::signSelectable(2),
        CardDefinition::fixedNegative(6),
        CardDefinition::doubleCard(),
        CardDefinition::valueChange(),
        CardDefinition::tiebreaker(),
        CardDefinition::flipTwoFour(),
    };
}

std::vector<size_t> PazaakSession::k2ShowcaseChosenCards() {
    // Indices into k2DefaultCollection, which is ordered +1..+6, -1..-6,
    // +/-1..+/-6, then Tiebreaker, Double, Flip 2&4, Flip 3&6, Value Change.
    // The first four become the opening hand and deliberately cover a Value
    // Change card, a sign-selectable card, a fixed card and a non-switchable
    // special; the remaining six expose the other specials in later sets.
    return {
        22, // Value Change
        12, // +/-1
        0,  // +1
        19, // Double
        20, // Flip 2&4
        21, // Flip 3&6
        18, // Tiebreaker
        6,  // -1
        4,  // +5
        13, // +/-2
    };
}

HandSelection PazaakSession::defaultHandSelection() {
    return {0, 1, 2, 3};
}

bool PazaakSession::confirmSetup() {
    if (!canConfirmSetup()) {
        return false;
    }

    try {
        SideDeck playerDeck(chosenSideDeck());
        SideDeck opponentDeck(*_params.opponentSideDeck);
        ParticipantMatchState player(playerDeck, _playerHandSelector(playerDeck));
        ParticipantMatchState opponent(opponentDeck, _opponentHandSelector(opponentDeck));
        _match = std::make_unique<MatchState>(
            _mainDeckFactory(),
            std::move(player),
            std::move(opponent),
            _firstParticipantSelector(0));
    } catch (const std::exception &) {
        _match.reset();
        return false;
    }

    _screen = PazaakFlowScreen::Board;
    _setIndex = 0;
    queueTurnStarted(_match->set().activeParticipant());
    if (!_params.paceAutomaticDraws &&
        _match->set().activeParticipant() == Participant::One) {
        applyAcceptedCommand(DrawCommand {Participant::One});
    }
    return true;
}

PazaakBoardProjection PazaakSession::boardProjection() const {
    PazaakBoardProjection projection;
    projection.playerName = _params.playerName;
    projection.opponentName = _params.opponentName;
    projection.forfeitRequested = _forfeitRequested;
    if (!_match) {
        return projection;
    }

    const SetState &set = _match->set();
    const ParticipantSetState &playerSet = set.participant(Participant::One);
    const ParticipantSetState &opponentSet = set.participant(Participant::Two);
    const ParticipantMatchState &playerMatch = _match->participant(Participant::One);
    const ParticipantMatchState &opponentMatch = _match->participant(Participant::Two);

    for (size_t i = 0; i < playerSet.board().size() && i < projection.playerBoard.size(); ++i) {
        const PlayedCard &card = playerSet.board()[i];
        std::optional<CardBehavior> behavior;
        if (card.source() == PlayedCardSource::Hand && card.handIndex()) {
            behavior = playerMatch.hand()[*card.handIndex()].definition.behavior();
        }
        projection.playerBoard[i] = {true, card.value(), card.source(), behavior};
    }
    for (size_t i = 0; i < opponentSet.board().size() && i < projection.opponentBoard.size(); ++i) {
        const PlayedCard &card = opponentSet.board()[i];
        std::optional<CardBehavior> behavior;
        if (card.source() == PlayedCardSource::Hand && card.handIndex()) {
            behavior = opponentMatch.hand()[*card.handIndex()].definition.behavior();
        }
        projection.opponentBoard[i] = {true, card.value(), card.source(), behavior};
    }

    LegalActions legal(_match->legalActions(Participant::One));
    for (size_t i = 0; i < playerMatch.hand().size() && i < projection.playerHand.size(); ++i) {
        const HandCard &card = playerMatch.hand()[i];
        bool playable = std::find(
                            legal.playableHandCards.begin(),
                            legal.playableHandCards.end(),
                            i) != legal.playableHandCards.end();
        projection.playerHand[i] = {
            true,
            false,
            card.used,
            playable,
            card.definition,
            _selectedSigns[i],
            _selectedValues[i],
        };
    }
    for (size_t i = 0; i < opponentMatch.hand().size() && i < projection.opponentHand.size(); ++i) {
        const HandCard &card = opponentMatch.hand()[i];
        projection.opponentHand[i] = {
            true,
            !card.used,
            card.used,
            false,
            card.used ? std::optional<CardDefinition>(card.definition) : std::nullopt,
            CardSign::Positive,
        };
    }

    projection.playerTotal = playerSet.total();
    projection.opponentTotal = opponentSet.total();
    projection.setResult = set.result();
    projection.playerSetWins = playerMatch.setWins();
    projection.opponentSetWins = opponentMatch.setWins();
    projection.playerActive = set.result() == SetResult::InProgress &&
                              set.activeParticipant() == Participant::One;
    projection.opponentActive = set.result() == SetResult::InProgress &&
                                set.activeParticipant() == Participant::Two;
    projection.playerStood = playerSet.stood();
    projection.opponentStood = opponentSet.stood();
    projection.canEndTurn = legal.canEndTurn;
    projection.canStand = legal.canStand;

    if (_match->result() != MatchResult::InProgress || _completedResult) {
        projection.state = PazaakBoardState::MatchComplete;
    } else if (set.result() == SetResult::UnresolvedBothNine) {
        projection.state = PazaakBoardState::UnresolvedBothNine;
    } else if (set.result() != SetResult::InProgress) {
        projection.state = PazaakBoardState::SetComplete;
    } else if (set.activeParticipant() == Participant::Two) {
        projection.state = PazaakBoardState::AwaitingOpponentPolicy;
    } else {
        projection.state = PazaakBoardState::PlayerTurn;
    }
    return projection;
}

bool PazaakSession::selectPlayerCardSign(size_t handIndex, CardSign sign) {
    if (_screen != PazaakFlowScreen::Board ||
        !_match ||
        terminal() ||
        handIndex >= kHandSize) {
        return false;
    }
    if (sign != CardSign::Positive && sign != CardSign::Negative) {
        return false;
    }
    const auto &hand = _match->participant(Participant::One).hand();
    LegalActions legal(_match->legalActions(Participant::One));
    bool playable = std::find(
                        legal.playableHandCards.begin(),
                        legal.playableHandCards.end(),
                        handIndex) != legal.playableHandCards.end();
    if (handIndex >= hand.size() ||
        !hand[handIndex].definition.isSignSelectable() ||
        hand[handIndex].used ||
        !playable) {
        return false;
    }
    _selectedSigns[handIndex] = sign;
    return true;
}

bool PazaakSession::selectPlayerCardValue(size_t handIndex, int value) {
    if (_screen != PazaakFlowScreen::Board ||
        !_match ||
        terminal() ||
        handIndex >= kHandSize) {
        return false;
    }
    const auto &states = valueChangeStates();
    if (std::find(states.begin(), states.end(), value) == states.end()) {
        return false;
    }
    const auto &hand = _match->participant(Participant::One).hand();
    LegalActions legal(_match->legalActions(Participant::One));
    bool playable = std::find(
                        legal.playableHandCards.begin(),
                        legal.playableHandCards.end(),
                        handIndex) != legal.playableHandCards.end();
    if (handIndex >= hand.size() ||
        !hand[handIndex].definition.isValueSelectable() ||
        hand[handIndex].used ||
        !playable) {
        return false;
    }
    _selectedValues[handIndex] = value;
    return true;
}

ActionError PazaakSession::playPlayerHandCard(size_t handIndex) {
    if (!_match || handIndex >= kHandSize) {
        return ActionError::InvalidHandIndex;
    }
    const auto &hand = _match->participant(Participant::One).hand();
    if (handIndex >= hand.size()) {
        return ActionError::InvalidHandIndex;
    }
    const CardDefinition &definition = hand[handIndex].definition;
    std::optional<CardSign> sign;
    std::optional<int> value;
    if (definition.isValueSelectable()) {
        value = _selectedValues[handIndex];
    } else if (definition.isSignSelectable()) {
        sign = _selectedSigns[handIndex];
    }
    return applyPlayerCommand(PlayHandCardCommand {Participant::One, handIndex, sign, value});
}

ActionError PazaakSession::endPlayerTurn() {
    return applyPlayerCommand(EndTurnCommand {Participant::One});
}

ActionError PazaakSession::standPlayer() {
    return applyPlayerCommand(StandCommand {Participant::One});
}

ActionError PazaakSession::applyPlayerCommand(const Command &command) {
    if (_screen != PazaakFlowScreen::Board || !_match || terminal()) {
        return ActionError::MatchComplete;
    }
    return applyAcceptedCommand(command);
}

ActionError PazaakSession::applyAcceptedCommand(const Command &command) {
    Participant actor = std::visit([](const auto &typed) {
        return typed.actor;
    },
                                   command);
    bool setWasInProgress = _match->set().result() == SetResult::InProgress;
    Participant previousActive = _match->set().activeParticipant();
    TurnStage previousStage = _match->set().turnStage();
    ActionError error = _match->apply(command);
    if (error != ActionError::None) {
        return error;
    }

    if (std::holds_alternative<DrawCommand>(command)) {
        _presentationEvents.push_back(
            {PazaakPresentationEventType::MainDeckDrawn, actor});
    } else if (std::holds_alternative<PlayHandCardCommand>(command)) {
        _presentationEvents.push_back(
            {PazaakPresentationEventType::HandCardPlayed, actor});
    }

    if (setWasInProgress && _match->set().result() != SetResult::InProgress) {
        beginResultPresentation();
    } else if (_match->set().result() == SetResult::InProgress &&
               _match->set().turnStage() == TurnStage::AwaitingDraw &&
               (_match->set().activeParticipant() != previousActive ||
                previousStage != TurnStage::AwaitingDraw)) {
        queueTurnStarted(_match->set().activeParticipant());
    }
    return ActionError::None;
}

ActionError PazaakSession::applyOpponentCommand(const Command &command) {
    bool opponentCommand = std::visit([](const auto &typed) {
        return typed.actor == Participant::Two;
    },
                                      command);
    if (!opponentCommand) {
        return ActionError::InvalidParticipant;
    }
    if (_screen != PazaakFlowScreen::Board || !_match || terminal()) {
        return ActionError::MatchComplete;
    }
    return applyAcceptedCommand(command);
}

void PazaakSession::beginResultPresentation() {
    if (_resultPresentationPending ||
        !_match ||
        _match->set().result() == SetResult::InProgress ||
        _match->set().result() == SetResult::UnresolvedBothNine) {
        return;
    }

    _resultPresentationPending = true;
    _resultPresentationElapsed = 0.0f;

    if (_match->result() == MatchResult::ParticipantOneWon) {
        _completedResult = PazaakCompletedResult::PlayerWon;
        _presentationEvents.push_back(
            {PazaakPresentationEventType::PlayerMatchWon, std::nullopt});
        return;
    }
    if (_match->result() == MatchResult::ParticipantTwoWon) {
        _completedResult = PazaakCompletedResult::OpponentWon;
        _presentationEvents.push_back(
            {PazaakPresentationEventType::PlayerMatchLost, std::nullopt});
        return;
    }

    switch (_match->set().result()) {
    case SetResult::ParticipantOneWon:
        _presentationEvents.push_back(
            {PazaakPresentationEventType::PlayerSetWon, std::nullopt});
        break;
    case SetResult::ParticipantTwoWon:
        _presentationEvents.push_back(
            {PazaakPresentationEventType::PlayerSetLost, std::nullopt});
        break;
    case SetResult::Tie:
        _presentationEvents.push_back(
            {PazaakPresentationEventType::SetTied, std::nullopt});
        break;
    default:
        break;
    }
}

void PazaakSession::queueTurnStarted(Participant participant) {
    _presentationEvents.push_back(
        {PazaakPresentationEventType::TurnStarted, participant});
}

bool PazaakSession::requestForfeit() {
    if (_screen != PazaakFlowScreen::Board ||
        !_match ||
        terminal() ||
        _match->set().result() != SetResult::InProgress ||
        _match->set().activeParticipant() != Participant::One ||
        _match->set().turnStage() != TurnStage::AwaitingAction) {
        return false;
    }
    _forfeitRequested = true;
    return true;
}

bool PazaakSession::cancelForfeitRequest() {
    if (!_forfeitRequested || terminal()) {
        return false;
    }
    _forfeitRequested = false;
    return true;
}

bool PazaakSession::confirmForfeit() {
    if (!_forfeitRequested || terminal()) {
        return false;
    }
    _completedResult = PazaakCompletedResult::PlayerForfeited;
    _forfeitRequested = false;
    _resultPresentationPending = true;
    _resultPresentationElapsed = 0.0f;
    _presentationEvents.push_back(
        {PazaakPresentationEventType::PlayerMatchLost, std::nullopt});
    return true;
}

bool PazaakSession::terminal() const {
    if (_completedResult) {
        return true;
    }
    if (!_match) {
        return false;
    }
    return _match->result() != MatchResult::InProgress ||
           _match->set().result() == SetResult::UnresolvedBothNine;
}

bool PazaakSession::advanceResultPresentation(float dt) {
    if (!_resultPresentationPending || !_match) {
        return false;
    }
    _resultPresentationElapsed += std::max(0.0f, dt);
    if (_resultPresentationElapsed < _params.resultPresentationDuration) {
        return false;
    }

    _resultPresentationPending = false;
    _resultPresentationElapsed = 0.0f;
    if (_completedResult) {
        return true;
    }

    Participant first = _firstParticipantSelector(_setIndex + 1);
    ActionError error = _match->startNextSet(_mainDeckFactory(), first);
    if (error != ActionError::None) {
        return false;
    }

    ++_setIndex;
    _forfeitRequested = false;
    queueTurnStarted(first);
    if (!_params.paceAutomaticDraws && first == Participant::One) {
        applyAcceptedCommand(DrawCommand {Participant::One});
    }
    return true;
}

std::vector<PazaakPresentationEvent> PazaakSession::takePresentationEvents() {
    std::vector<PazaakPresentationEvent> events(std::move(_presentationEvents));
    _presentationEvents.clear();
    return events;
}

std::optional<PlayHandCardCommand> PazaakSession::chooseOpponentHandCard() const {
    if (!_match ||
        _match->set().result() != SetResult::InProgress ||
        _match->set().activeParticipant() != Participant::Two ||
        _match->set().turnStage() != TurnStage::AwaitingAction) {
        return std::nullopt;
    }

    const ParticipantSetState &opponent = _match->set().participant(Participant::Two);
    const ParticipantSetState &player = _match->set().participant(Participant::One);
    const auto &hand = _match->participant(Participant::Two).hand();
    LegalActions legal(_match->legalActions(Participant::Two));
    int currentTotal = opponent.total();
    bool playerLocked = player.finished() && !player.hasNineCardPriority();

    // Every legal committed play, including each K2 selection, is enumerated and
    // scored by simulating the real rules on a copy of the match. This lets the
    // opponent use special cards (Double, the flips, Value Change, Tiebreaker)
    // coherently: their board effects are evaluated exactly as they will resolve,
    // and an option that would bust is never chosen.
    auto enumerate = [&](size_t handIndex) {
        std::vector<PlayHandCardCommand> commands;
        const CardDefinition &definition = hand[handIndex].definition;
        if (definition.isValueSelectable()) {
            for (int value : {1, 2, -1, -2}) {
                commands.push_back({Participant::Two, handIndex, std::nullopt, value});
            }
        } else if (definition.isSignSelectable()) {
            commands.push_back({Participant::Two, handIndex, CardSign::Positive});
            commands.push_back({Participant::Two, handIndex, CardSign::Negative});
        } else {
            commands.push_back({Participant::Two, handIndex, std::nullopt});
        }
        return commands;
    };

    struct Candidate {
        PlayHandCardCommand command;
        int total;
    };
    std::optional<Candidate> best;
    auto consider = [&](const PlayHandCardCommand &command) {
        MatchState sim(*_match);
        if (sim.apply(command) != ActionError::None) {
            return;
        }
        int total = sim.set().participant(Participant::Two).total();
        if (total > kTargetTotal) {
            return;
        }
        bool useful = currentTotal > kTargetTotal;
        if (!useful && playerLocked) {
            useful = total >= player.total() && total > currentTotal;
        }
        if (!useful) {
            return;
        }
        // Deterministic: keep the first play (in enumeration order) that reaches
        // the highest legal total.
        if (!best || total > best->total) {
            best = Candidate {command, total};
        }
    };

    for (size_t handIndex : legal.playableHandCards) {
        for (const PlayHandCardCommand &command : enumerate(handIndex)) {
            consider(command);
        }
    }

    if (!best) {
        return std::nullopt;
    }
    return best->command;
}

Command PazaakSession::chooseOpponentAction() const {
    const ParticipantSetState &opponent = _match->set().participant(Participant::Two);
    const ParticipantSetState &player = _match->set().participant(Participant::One);

    if (opponent.total() <= kTargetTotal &&
        !player.hasNineCardPriority() &&
        (opponent.total() == kTargetTotal ||
         (player.finished() && opponent.total() >= player.total()) ||
         opponent.board().size() + 1 >= kBoardSize)) {
        return StandCommand {Participant::Two};
    }
    return EndTurnCommand {Participant::Two};
}

PazaakOpponentEvent PazaakSession::advanceOpponentEvent() {
    if (_screen != PazaakFlowScreen::Board || !_match) {
        return PazaakOpponentEvent::None;
    }
    if (_resultPresentationPending) {
        return PazaakOpponentEvent::None;
    }
    if (terminal()) {
        return PazaakOpponentEvent::None;
    }

    const SetState &set = _match->set();
    if (set.result() != SetResult::InProgress) {
        return PazaakOpponentEvent::None;
    }
    if (set.activeParticipant() == Participant::One) {
        if (set.turnStage() != TurnStage::AwaitingDraw) {
            return PazaakOpponentEvent::None;
        }
        ActionError error = applyAcceptedCommand(DrawCommand {Participant::One});
        return error == ActionError::None
                   ? PazaakOpponentEvent::PlayerDraw
                   : PazaakOpponentEvent::None;
    }

    PazaakOpponentEvent event = PazaakOpponentEvent::None;
    ActionError error = ActionError::None;
    if (set.turnStage() == TurnStage::AwaitingDraw) {
        event = PazaakOpponentEvent::Draw;
        error = applyAcceptedCommand(DrawCommand {Participant::Two});
    } else if (set.turnStage() == TurnStage::AwaitingAction) {
        if (auto card = chooseOpponentHandCard()) {
            event = PazaakOpponentEvent::PlayHandCard;
            error = applyAcceptedCommand(*card);
        } else {
            Command command(chooseOpponentAction());
            event = std::holds_alternative<StandCommand>(command)
                        ? PazaakOpponentEvent::Stand
                        : PazaakOpponentEvent::EndTurn;
            error = applyAcceptedCommand(command);
        }
    }

    if (error != ActionError::None) {
        return PazaakOpponentEvent::None;
    }
    return event;
}

} // namespace game

} // namespace reone
